// Learned IR remote support for GPIO12.
// GPIO12 is initialized here only when Settings -> GPIO12 is explicitly IR.
// AUTO never tries to autodetect an IR receiver.

#include "nvs_storage.h"
#include <cstring>
#include "ir_remote.h"
#include "ir_decode.h"
#include "constants.h"
#include "gui.h"

extern void KeyUp(void);
extern void KeyDown(void);
extern void ButtonPress(void);
extern void SlideShowButtonPress(void);
extern void doStandby(void);
extern void RemoteModeAction(void);
extern void RemoteModeLongAction(void);
extern void RemoteVolumeStep(int8_t delta);
extern void RemoteTuneAction(int8_t direction, bool repeat);
extern void MarkEepromDirty(void);
extern bool FlushEeprom(void);

enum IrAction : uint8_t {
  IR_ACTION_TUNE_UP = 0,
  IR_ACTION_TUNE_DOWN,
  IR_ACTION_OK,
  IR_ACTION_VOL_UP,
  IR_ACTION_VOL_DOWN,
  IR_ACTION_MODE,
  IR_ACTION_SLIDESHOW,
  IR_ACTION_STANDBY,
  IR_ACTION_NONE = 0xFF
};

static const char* const kActionName[EE_IR_KEY_COUNT] = {
  "TUNE +", "TUNE -", "OK", "VOL +", "VOL -", "MODE", "SLIDESHOW", "STANDBY"
};

struct LearnedCode {
  uint8_t protocol;
  uint16_t address;
  uint16_t command;
  uint16_t extra;
  uint16_t bits;
  uint64_t raw;
};

// Standalone IR keeps both code tables in fixed storage. This deliberately
// makes the heap layout identical in AUTO/POLL and GPIO12=IR boots: enabling
// IR no longer inserts two early heap allocations before the TFT/radio setup.
// EE_IR_KEY_COUNT is only eight entries, so the fixed 384-byte cost is small
// and deterministic.
static LearnedCode learned[EE_IR_KEY_COUNT] = {};
static LearnedCode learnWork[EE_IR_KEY_COUNT] = {};
static bool prepareLogged = false;
static bool profileValid = false;
static bool profileLoaded = false;
static bool receiverStarted = false;
static IrAction lastRuntimeAction = IR_ACTION_NONE;
static uint32_t lastRuntimeFrameMs = 0;
static uint32_t runtimePressStartMs = 0;
static constexpr uint32_t IR_HELD_FRAME_GAP_MS = 220UL;
static constexpr uint32_t IR_REPEAT_INITIAL_DELAY_MS = 500UL;
static constexpr uint32_t IR_MODE_LONG_PRESS_MS = 1000UL;
static constexpr uint32_t IR_LEARN_RELEASE_MS = 220UL;

// MODE is delayed until release so one learned key can provide two mutually
// exclusive actions: a short MODE keeps the existing tune-mode action, while
// a continuous hold of at least one second switches DAB <-> FM exactly once.
static bool modePressPending = false;
static bool modeLongFired = false;
static uint32_t modePressStartMs = 0U;
static uint32_t modeLastFrameMs = 0U;
// GPIO12 capture is fully local to this project. No external IR receive
// library, sampling timer or receiver singleton is used. The ISR records only
// real input transitions; decoding is done later in normal loop() context.
static constexpr uint16_t IR_RAW_BUFFER_LENGTH = 100U;
static constexpr uint32_t IR_FRAME_GAP_US = 8000UL;
static constexpr uint32_t IR_EDGE_MIN_PULSE_US = 80UL;
static volatile bool edgeCapturePaused = true;
static volatile bool edgeFrameActive = false;
static volatile bool edgeOverflow = false;
static volatile uint16_t edgeDurationCount = 0;
static volatile uint32_t edgeInitialGapUs = 0;
static volatile uint32_t edgeLastEdgeUs = 0;
static volatile uint16_t edgeDurationsUs[IR_RAW_BUFFER_LENGTH] = {0};
static volatile uint32_t edgeTransitionCount = 0U;
static volatile uint32_t edgeGlitchCount = 0U;
static volatile uint32_t edgeOverflowCount = 0U;
static uint16_t decodeDurationsUs[IR_RAW_BUFFER_LENGTH] = {0};
static IrDecoderState decoderState{};
static uint32_t edgeFrameCount = 0U;
static uint32_t edgeDecodedCount = 0U;
static uint32_t edgeDiagTimerMs = 0U;
static uint32_t edgeDiagLastTransitions = 0U;
static uint32_t standbySuppressUntilMs = 0U;
static volatile bool edgeWakeSeedPending = false;
static volatile bool edgeWakeFirstMarkReady = false;
static volatile uint16_t edgeWakeFirstMarkUs = 0U;

// Test view keeps the last complete frame so a protocol-specific short repeat
// frame can be displayed as a repeat of that key instead of as IR_PROTO_UNKNOWN.
static IrFrame lastTestData{};
static bool lastTestDataValid = false;
static bool lastTestRepeatShown = false;
static uint32_t lastTestFrameMs = 0;

static constexpr uint8_t IR_PROFILE_VERSION = 1;
static constexpr uint8_t kMagic[4] = {'I', 'R', '0', '1'};

enum UiState : uint8_t {
  UI_NONE,
  UI_MENU,
  UI_LEARN,
  UI_CLEAR_CONFIRM,
  UI_TEST,
  UI_NOTICE
};

static UiState uiState = UI_NONE;
static uint8_t uiSelection = 0;
static uint8_t learnIndex = 0;
static bool learnWaitingRelease = false;
static uint32_t learnLastFrameMs = 0;
static bool clearYes = false;

static constexpr size_t IR_CODE_TABLE_BYTES =
    sizeof(LearnedCode) * EE_IR_KEY_COUNT;

static void IRAM_ATTR irEdgeCaptureIsr(void) {
  if (edgeCapturePaused) return;
  ++edgeTransitionCount;

  const uint32_t now = micros();
  const bool levelHigh = digitalRead(SI4684_INTB_PIN) == HIGH;

  if (!edgeFrameActive) {
    // Demodulated IR receivers idle HIGH. A falling edge starts a frame.
    if (!levelHigh) {
      edgeInitialGapUs = now - edgeLastEdgeUs;
      edgeDurationCount = 0U;
      edgeOverflow = false;
      edgeFrameActive = true;
      edgeLastEdgeUs = now;
    } else {
      edgeLastEdgeUs = now;
    }
    return;
  }

  const uint32_t durationUs = now - edgeLastEdgeUs;
  edgeLastEdgeUs = now;

  // Ignore only unrealistically short glitches. A real demodulated IR symbol
  // in every supported decoder is substantially longer than this threshold.
  if (durationUs < IR_EDGE_MIN_PULSE_US) {
    ++edgeGlitchCount;
    edgeFrameActive = false;
    edgeOverflow = false;
    edgeDurationCount = 0U;
    edgeInitialGapUs = 0U;
    return;
  }

  if (edgeDurationCount < IR_RAW_BUFFER_LENGTH) {
    const uint16_t storedDuration =
        static_cast<uint16_t>(durationUs > 0xFFFFU ? 0xFFFFU : durationUs);
    edgeDurationsUs[edgeDurationCount++] = storedDuration;
    if (edgeWakeSeedPending) {
      // First rising edge after a light-sleep wake: this is the remaining
      // duration of the MARK that woke the CPU. Log later from foreground.
      edgeWakeFirstMarkUs = storedDuration;
      edgeWakeFirstMarkReady = true;
      edgeWakeSeedPending = false;
    }
  } else {
    if (!edgeOverflow) ++edgeOverflowCount;
    edgeOverflow = true;
  }
}

static void rearmEdgeCapture(void) {
  noInterrupts();
  edgeFrameActive = false;
  edgeOverflow = false;
  edgeDurationCount = 0U;
  edgeInitialGapUs = 0U;
  // Keep the timestamp of the previous frame's final rising edge. The next
  // falling edge then gives the real inter-frame gap used for repeat decoding.
  edgeCapturePaused = false;
  interrupts();
}

static bool takeEdgeFrame(uint16_t& durationCount,
                          uint32_t& initialGapUs,
                          bool& overflow) {
  if (edgeCapturePaused || !edgeFrameActive) return false;

  const uint32_t now = micros();
  if (digitalRead(SI4684_INTB_PIN) == LOW ||
      static_cast<uint32_t>(now - edgeLastEdgeUs) < IR_FRAME_GAP_US)
    return false;

  noInterrupts();
  const uint32_t lockedNow = micros();
  if (!edgeFrameActive || digitalRead(SI4684_INTB_PIN) == LOW ||
      static_cast<uint32_t>(lockedNow - edgeLastEdgeUs) < IR_FRAME_GAP_US) {
    interrupts();
    return false;
  }

  edgeCapturePaused = true;
  edgeFrameActive = false;
  durationCount = edgeDurationCount;
  initialGapUs = edgeInitialGapUs;
  overflow = edgeOverflow;
  interrupts();

  if (durationCount == 0U || durationCount > IR_RAW_BUFFER_LENGTH) {
    rearmEdgeCapture();
    return false;
  }

  for (uint16_t i = 0; i < durationCount; ++i)
    decodeDurationsUs[i] = edgeDurationsUs[i];
  return true;
}

static bool takeDecodedFrame(IrFrame& data) {
  uint16_t durationCount = 0U;
  uint32_t initialGapUs = 0U;
  bool overflow = false;
  if (!takeEdgeFrame(durationCount, initialGapUs, overflow)) return false;
  ++edgeFrameCount;

  const bool decoded = !overflow &&
      IrDecodeFrame(decodeDurationsUs, durationCount, initialGapUs,
                    decoderState, data);
  rearmEdgeCapture();
  if (!decoded) return false;
  ++edgeDecodedCount;

  if (data.flags & (IR_FLAG_OVERFLOW | IR_FLAG_PARITY_FAILED)) return false;
  return true;
}

bool IrRemotePrepare(void) {
  if (!prepareLogged) {
    prepareLogged = true;
    DIAG_PRINTF("[IR] static code tables ready: 2 x %u bytes\n",
                  static_cast<unsigned>(IR_CODE_TABLE_BYTES));
  }
  return true;
}

static uint16_t read16(int offset) {
  return static_cast<uint16_t>(Storage.readByte(offset)) |
         (static_cast<uint16_t>(Storage.readByte(offset + 1)) << 8);
}

static uint64_t read64(int offset) {
  uint64_t value = 0;
  for (uint8_t i = 0; i < 8; ++i)
    value |= static_cast<uint64_t>(Storage.readByte(offset + i)) << (8U * i);
  return value;
}

static void write16(int offset, uint16_t value) {
  Storage.writeByte(offset, static_cast<uint8_t>(value));
  Storage.writeByte(offset + 1, static_cast<uint8_t>(value >> 8));
}

static void write64(int offset, uint64_t value) {
  for (uint8_t i = 0; i < 8; ++i)
    Storage.writeByte(offset + i, static_cast<uint8_t>(value >> (8U * i)));
}

static uint16_t crc16Update(uint16_t crc, uint8_t data) {
  crc ^= static_cast<uint16_t>(data) << 8;
  for (uint8_t i = 0; i < 8; ++i)
    crc = (crc & 0x8000U) ? static_cast<uint16_t>((crc << 1) ^ 0x1021U)
                          : static_cast<uint16_t>(crc << 1);
  return crc;
}

static uint16_t storedProfileCrc(void) {
  uint16_t crc = 0xFFFFU;
  for (int i = 0; i < EE_IR_CONFIG_SIZE - 2; ++i)
    crc = crc16Update(crc, Storage.readByte(EE_IR_CONFIG_START + i));
  return crc;
}

// Validate the stored profile without requiring the learned-code heap tables.
// Settings can therefore display/clear the IR profile while GPIO12 is AUTO or
// INTB without introducing a late runtime allocation.
static bool storedProfileValid(void) {
  for (uint8_t i = 0; i < 4; ++i)
    if (Storage.readByte(EE_IR_CONFIG_START + i) != kMagic[i]) return false;
  if (Storage.readByte(EE_IR_CONFIG_START + 4) != IR_PROFILE_VERSION) return false;
  if (Storage.readByte(EE_IR_CONFIG_START + 5) != EE_IR_KEY_COUNT) return false;
  return storedProfileCrc() ==
         read16(EE_IR_CONFIG_START + EE_IR_CONFIG_SIZE - 2);
}

static bool codesEqual(const LearnedCode& a, const LearnedCode& b) {
  return a.protocol == b.protocol && a.address == b.address &&
         a.command == b.command && a.extra == b.extra &&
         a.bits == b.bits && a.raw == b.raw;
}

static bool profilesEqual(const LearnedCode* a, const LearnedCode* b) {
  for (uint8_t i = 0; i < EE_IR_KEY_COUNT; ++i)
    if (!codesEqual(a[i], b[i])) return false;
  return true;
}

static void loadProfile(void) {
  profileLoaded = false;
  profileValid = false;
  if (!IrRemotePrepare()) return;
  profileLoaded = true;
  if (!storedProfileValid()) {
    DIAG_PRINTLN("[IR] NVS profile empty or invalid");
    return;
  }

  int p = EE_IR_CONFIG_START + 6;
  for (uint8_t i = 0; i < EE_IR_KEY_COUNT; ++i) {
    learned[i].protocol = Storage.readByte(p++);
    learned[i].address = read16(p); p += 2;
    learned[i].command = read16(p); p += 2;
    learned[i].extra = read16(p); p += 2;
    learned[i].bits = read16(p); p += 2;
    learned[i].raw = read64(p); p += 8;
  }
  profileValid = true;
  DIAG_PRINTLN("[IR] learned profile loaded");
}

static void saveProfile(const LearnedCode* codes) {
  if (!IrRemotePrepare()) return;
  if (profileValid && profilesEqual(codes, learned)) {
    DIAG_PRINTLN("[IR] learned profile unchanged; NVS not written");
    return;
  }

  int p = EE_IR_CONFIG_START;
  for (uint8_t i = 0; i < 4; ++i) Storage.writeByte(p++, kMagic[i]);
  Storage.writeByte(p++, IR_PROFILE_VERSION);
  Storage.writeByte(p++, EE_IR_KEY_COUNT);

  for (uint8_t i = 0; i < EE_IR_KEY_COUNT; ++i) {
    Storage.writeByte(p++, codes[i].protocol);
    write16(p, codes[i].address); p += 2;
    write16(p, codes[i].command); p += 2;
    write16(p, codes[i].extra); p += 2;
    write16(p, codes[i].bits); p += 2;
    write64(p, codes[i].raw); p += 8;
  }

  // Two bytes at the end are CRC; bytes between records and CRC are reserved.
  while (p < EE_IR_CONFIG_START + EE_IR_CONFIG_SIZE - 2)
    Storage.writeByte(p++, 0);

  uint16_t crc = 0xFFFFU;
  for (int i = 0; i < EE_IR_CONFIG_SIZE - 2; ++i)
    crc = crc16Update(crc, Storage.readByte(EE_IR_CONFIG_START + i));
  write16(EE_IR_CONFIG_START + EE_IR_CONFIG_SIZE - 2, crc);

  memcpy(learned, codes, IR_CODE_TABLE_BYTES);
  profileValid = true;
  MarkEepromDirty();
  if (FlushEeprom())
    DIAG_PRINTLN("[IR] learned profile saved");
  else
    DIAG_PRINTLN("[IR] ERROR: learned profile commit failed");
}

static void clearProfile(void) {
  if (!profileValid) {
    DIAG_PRINTLN("[IR] clear requested but profile already empty");
    return;
  }
  for (int i = 0; i < EE_IR_CONFIG_SIZE; ++i)
    Storage.writeByte(EE_IR_CONFIG_START + i, 0);
  memset(learned, 0, IR_CODE_TABLE_BYTES);
  profileValid = false;
  lastRuntimeAction = IR_ACTION_NONE;
  lastRuntimeFrameMs = 0;
  runtimePressStartMs = 0;
  MarkEepromDirty();
  if (FlushEeprom())
    DIAG_PRINTLN("[IR] learned profile cleared");
  else
    DIAG_PRINTLN("[IR] ERROR: learned profile clear commit failed");
}

static LearnedCode fromFrame(const IrFrame& data) {
  LearnedCode result{};
  result.protocol = data.protocol;
  result.address = data.address;
  result.command = data.command;
  result.extra = data.extra;
  result.bits = data.bits;
  result.raw = data.raw;
  return result;
}

static bool frameMatches(const LearnedCode& code, const IrFrame& data) {
  if (code.protocol != data.protocol) return false;
  if (data.protocol == IR_PROTO_UNKNOWN)
    return code.raw == data.raw;
  return code.address == data.address && code.command == data.command &&
         code.extra == data.extra && code.bits == data.bits;
}

static IrAction findAction(const IrFrame& data) {
  if (!profileValid) return IR_ACTION_NONE;
  for (uint8_t i = 0; i < EE_IR_KEY_COUNT; ++i)
    if (frameMatches(learned[i], data)) return static_cast<IrAction>(i);
  return IR_ACTION_NONE;
}

static int8_t findLearningDuplicate(const IrFrame& data) {
  for (uint8_t i = 0; i < learnIndex; ++i)
    if (frameMatches(learnWork[i], data)) return static_cast<int8_t>(i);
  return -1;
}

static void clearModePressState(void) {
  modePressPending = false;
  modeLongFired = false;
  modePressStartMs = 0U;
  modeLastFrameMs = 0U;
}

static void servicePendingModePress(uint32_t now) {
  if (!modePressPending) return;

  const uint32_t sinceLastFrame =
      static_cast<uint32_t>(now - modeLastFrameMs);

  // The threshold may fall between two IR repeat frames. A recently received
  // MODE frame is enough evidence that the key is still held.
  if (!modeLongFired &&
      static_cast<uint32_t>(now - modePressStartMs) >= IR_MODE_LONG_PRESS_MS &&
      sinceLastFrame < IR_HELD_FRAME_GAP_MS) {
    modeLongFired = true;
    DIAG_PRINTF("[IR/MODE] long press %lu ms -> DAB/FM switch\n",
                  static_cast<unsigned long>(now - modePressStartMs));
    RemoteModeLongAction();
    return;
  }

  if (modeLongFired) {
    // SwitchRadioMode() is deliberately blocking while the SI4684 image is
    // reloaded. During that time MODE repeats can still hit the edge ISR even
    // though foreground decoding is paused. Do not mistake that foreground
    // pause for a key release: require the GPIO edge stream itself to have been
    // quiet for the full held-frame gap before rearming MODE.
    const uint32_t edgeQuietUs = static_cast<uint32_t>(micros() - edgeLastEdgeUs);
    if (edgeQuietUs < IR_HELD_FRAME_GAP_MS * 1000UL) return;
  } else if (sinceLastFrame < IR_HELD_FRAME_GAP_MS) {
    return;
  }

  // Silence longer than the held-frame gap is the release event. If the long
  // action has not fired, this is the one and only place the short action runs.
  if (!modeLongFired) {
    DIAG_PRINTF("[IR/MODE] short press released after %lu ms\n",
                  static_cast<unsigned long>(modeLastFrameMs - modePressStartMs));
    RemoteModeAction();
  }

  clearModePressState();
  if (lastRuntimeAction == IR_ACTION_MODE) {
    lastRuntimeAction = IR_ACTION_NONE;
    lastRuntimeFrameMs = now;
    runtimePressStartMs = 0U;
  }
}

static bool actionRepeats(IrAction action) {
  return action == IR_ACTION_TUNE_UP || action == IR_ACTION_TUNE_DOWN ||
         action == IR_ACTION_VOL_UP || action == IR_ACTION_VOL_DOWN;
}

static void dispatch(IrAction action, bool repeat) {
  switch (action) {
    case IR_ACTION_TUNE_UP:    RemoteTuneAction(+1, repeat); break;
    case IR_ACTION_TUNE_DOWN:  RemoteTuneAction(-1, repeat); break;
    case IR_ACTION_OK:         ButtonPress(); break;
    case IR_ACTION_VOL_UP:     if (!menu) RemoteVolumeStep(+2); break;
    case IR_ACTION_VOL_DOWN:   if (!menu) RemoteVolumeStep(-2); break;
    case IR_ACTION_MODE:       RemoteModeAction(); break;
    case IR_ACTION_SLIDESHOW:  if (!menu) SlideShowButtonPress(); break;
    case IR_ACTION_STANDBY:
      if (!menu) {
        const uint32_t now = millis();
        if (static_cast<int32_t>(now - standbySuppressUntilMs) < 0) {
          // An IR frame that woke the CPU must not immediately put the radio
          // back to sleep. Non-repeat STANDBY is ignored only during this short
          // post-wake window; normal runtime operation is unchanged afterwards.
          DIAG_PRINTLN("[IR/SLEEP] wake STANDBY frame suppressed");
        } else {
          doStandby();
        }
      }
      break;
    default: break;
  }
}

static void drawUiBase(const char* title) {
  tft.pushImage(0, 0, 320, 240, configurationbackground);
  tftPrint(0, title, 155, 5, PrimaryColor, PrimaryColorSmooth, 28);
}

static void restoreUiBand(int16_t y, int16_t height) {
  if (y < 0) {
    height += y;
    y = 0;
  }
  if (height <= 0 || y >= 240) return;
  if (y + height > 240) height = 240 - y;
  tft.pushImage(0, y, 320, height,
                configurationbackground + static_cast<uint32_t>(y) * 320U);
}

static const char* menuItemText(uint8_t index) {
  switch (index) {
    case 0: return irLearnText[language];
    case 1: return irClearText[language];
    case 2: return irTestText[language];
    default: return irBackText[language];
  }
}

static void drawMenuRow(uint8_t index, bool restoreBackground) {
  if (index >= 4U) return;
  if (restoreBackground) restoreUiBand(48 + index * 32, 32);
  const bool selected = index == uiSelection;
  char text[96];
  snprintf(text, sizeof(text), "%s%s", selected ? "> " : "  ", menuItemText(index));
  tftPrintFixed(-1, text, 70, 55 + index * 32,
                selected ? ActiveColor : PrimaryColor,
                selected ? ActiveColorSmooth : PrimaryColorSmooth, 28);
}

static void drawMenu(void) {
  drawUiBase(irRemoteText[language]);
  for (uint8_t i = 0; i < 4; ++i) drawMenuRow(i, false);
  // Keep learned/empty state inside the same bottom information row used by
  // the parent Settings screen. y=204 placed the text visibly above that band.
  tftPrint(0,
           profileValid ? irProfileLearnedText[language]
                        : irProfileEmptyText[language],
           155, 222, SecondaryColor, SecondaryColorSmooth, 16);
}

static void drawNeedIr(void) {
  drawUiBase(irRemoteText[language]);
  tftPrint(0, irSetGpio12Text[language], 155, 102,
           ActiveColor, ActiveColorSmooth, 28);
  tftPrint(0, irPressOkText[language], 155, 145,
           SecondaryColor, SecondaryColorSmooth, 16);
}

static void drawLearn(void) {
  drawUiBase(irLearningText[language]);
  tftPrint(0, irPressText[language], 155, 72,
           SecondaryColor, SecondaryColorSmooth, 16);
  tftPrint(0, kActionName[learnIndex], 155, 101,
           ActiveColor, ActiveColorSmooth, 28);
  char progress[16];
  snprintf(progress, sizeof(progress), "%u/%u",
           static_cast<unsigned>(learnIndex + 1U),
           static_cast<unsigned>(EE_IR_KEY_COUNT));
  tftPrintFixed(0, progress, 155, 145, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(0, irPhysicalOkCancelsText[language], 155, 199,
           SecondaryColor, SecondaryColorSmooth, 16);
}

static void drawLearnRelease(void) {
  drawUiBase(irLearningText[language]);
  tftPrint(0, irReleaseKeyText[language], 155, 96,
           ActiveColor, ActiveColorSmooth, 28);
  char progress[16];
  snprintf(progress, sizeof(progress), "%u/%u",
           static_cast<unsigned>(learnIndex),
           static_cast<unsigned>(EE_IR_KEY_COUNT));
  tftPrintFixed(0, progress, 155, 145, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(0, irPhysicalOkCancelsText[language], 155, 199,
           SecondaryColor, SecondaryColorSmooth, 16);
}

static void drawClearChoices(bool restoreBackground) {
  if (restoreBackground) restoreUiBand(122, 44);
  char choices[128];
  if (clearYes)
    snprintf(choices, sizeof(choices), "%s     > %s",
             irNoText[language], irYesText[language]);
  else
    snprintf(choices, sizeof(choices), "> %s     %s",
             irNoText[language], irYesText[language]);
  tftPrintFixed(0, choices, 155, 132,
                PrimaryColor, PrimaryColorSmooth, 28);
}

static void drawClear(void) {
  drawUiBase(irRemoteText[language]);
  tftPrint(0, irClearLearnedRemoteText[language], 155, 82,
           ActiveColor, ActiveColorSmooth, 28);
  drawClearChoices(false);
}



static void drawTestData(const IrFrame* data, bool restoreBackground) {
  if (!data) return;
  if (restoreBackground) restoreUiBand(45, 136);

  const IrAction action = findAction(*data);
  char text[160];

  snprintf(text, sizeof(text), "%s: %s",
           irProtocolText[language], IrProtocolName(data->protocol));
  tftPrintFixed(-1, text, 28, 55, PrimaryColor, PrimaryColorSmooth, 16);

  snprintf(text, sizeof(text), "%s:  0x%04X",
           irAddressText[language], static_cast<unsigned>(data->address));
  tftPrintFixed(-1, text, 28, 80, PrimaryColor, PrimaryColorSmooth, 16);

  snprintf(text, sizeof(text), "%s:  0x%04X",
           irCommandText[language], static_cast<unsigned>(data->command));
  tftPrintFixed(-1, text, 28, 105, PrimaryColor, PrimaryColorSmooth, 16);

  snprintf(text, sizeof(text), "%s:   %s",
           irActionText[language],
           action == IR_ACTION_NONE ? irNotAssignedText[language] : kActionName[action]);
  tftPrintFixed(-1, text, 28, 130, ActiveColor, ActiveColorSmooth, 16);

  snprintf(text, sizeof(text), "%s:   %s",
           irRepeatText[language],
           (data->flags & (IR_FLAG_REPEAT | IR_FLAG_AUTO_REPEAT))
               ? irYesText[language] : irNoText[language]);
  tftPrintFixed(-1, text, 28, 155, SecondaryColor, SecondaryColorSmooth, 16);
}

static void drawTestRepeatRow(bool repeat, bool restoreBackground) {
  if (restoreBackground) restoreUiBand(147, 28);
  char text[128];
  snprintf(text, sizeof(text), "%s:   %s",
           irRepeatText[language], repeat ? irYesText[language] : irNoText[language]);
  tftPrintFixed(-1, text, 28, 155, SecondaryColor, SecondaryColorSmooth, 16);
}

static void drawTest(const IrFrame* data = nullptr) {
  drawUiBase(irTestTitleText[language]);
  if (!data) {
    tftPrint(0, irPressRemoteKeyText[language], 155, 90,
             ActiveColor, ActiveColorSmooth, 28);
  } else {
    drawTestData(data, false);
  }
  tftPrint(0, irPhysicalOkExitsText[language], 155, 200,
           SecondaryColor, SecondaryColorSmooth, 16);
}

void IrRemoteBegin(void) {
  if (receiverStarted) return;
  if (!IrRemotePrepare()) {
    DIAG_PRINTLN("[IR] receiver not started: persistent buffers unavailable");
    return;
  }
  if (!profileLoaded) loadProfile();

  // The IR receive path is project-local: GPIO CHANGE ISR + ir_decode.cpp.
  // No Arduino-IRremote begin/start/timer/global state exists anymore.
  pinMode(SI4684_INTB_PIN, INPUT);
  IrDecoderReset(decoderState);

  noInterrupts();
  edgeTransitionCount = 0U;
  edgeGlitchCount = 0U;
  edgeOverflowCount = 0U;
  edgeCapturePaused = true;
  edgeFrameActive = false;
  edgeOverflow = false;
  edgeDurationCount = 0U;
  edgeInitialGapUs = 0U;
  edgeLastEdgeUs = micros() - 100000UL;
  interrupts();

  attachInterrupt(digitalPinToInterrupt(SI4684_INTB_PIN), irEdgeCaptureIsr, CHANGE);
  rearmEdgeCapture();

  receiverStarted = true;
  lastRuntimeAction = IR_ACTION_NONE;
  lastRuntimeFrameMs = 0;
  runtimePressStartMs = 0;
  clearModePressState();
  lastTestDataValid = false;
  lastTestRepeatShown = false;
  lastTestFrameMs = 0;
  edgeFrameCount = 0U;
  edgeDecodedCount = 0U;
  edgeDiagTimerMs = millis();
  edgeDiagLastTransitions = 0U;
  DIAG_PRINTF("[IR] standalone edge receiver started GPIO%u profile=%s gap=%u us\n",
                SI4684_INTB_PIN, profileValid ? "learned" : "empty",
                static_cast<unsigned>(IR_FRAME_GAP_US));
}

void IrRemoteStop(void) {
  if (!receiverStarted) return;
  detachInterrupt(digitalPinToInterrupt(SI4684_INTB_PIN));
  noInterrupts();
  edgeCapturePaused = true;
  edgeFrameActive = false;
  edgeWakeSeedPending = false;
  edgeWakeFirstMarkReady = false;
  interrupts();
  // No external IR receive timer exists in standalone edge mode.
  receiverStarted = false;
  lastRuntimeAction = IR_ACTION_NONE;
  lastRuntimeFrameMs = 0;
  runtimePressStartMs = 0;
  clearModePressState();
  lastTestDataValid = false;
  lastTestRepeatShown = false;
  lastTestFrameMs = 0;
  DIAG_PRINTLN("[IR] edge receiver stopped");
}

void IrRemoteResumeAfterLightSleep(bool seedActiveLowPulse) {
  // Reinstall the project's normal CHANGE edge receiver after GPIO12 was used
  // temporarily as a LOW-level light-sleep wake source.
  IrRemoteBegin();
  standbySuppressUntilMs = millis() + 1000UL;

  if (!receiverStarted || !seedActiveLowPulse ||
      digitalRead(SI4684_INTB_PIN) != LOW) {
    DIAG_PRINTF("[IR/SLEEP] edge receiver resumed seed=%u level=%c\n",
                  seedActiveLowPulse ? 1U : 0U,
                  digitalRead(SI4684_INTB_PIN) == HIGH ? 'H' : 'L');
    return;
  }

  // The wake-causing falling edge occurred while the CPU was asleep and could
  // not run irEdgeCaptureIsr(). Start an in-progress frame now so the following
  // rising edge measures the REMAINING part of that first MARK. For long-leader
  // protocols this is often still within decoder tolerance. A deliberately
  // long initial gap prevents the first full wake frame being tagged as repeat.
  noInterrupts();
  edgeCapturePaused = false;
  edgeFrameActive = true;
  edgeOverflow = false;
  edgeDurationCount = 0U;
  edgeInitialGapUs = 100000UL;
  edgeLastEdgeUs = micros();
  edgeWakeSeedPending = true;
  edgeWakeFirstMarkReady = false;
  edgeWakeFirstMarkUs = 0U;
  interrupts();

  DIAG_PRINTLN("[IR/SLEEP] wake LOW seeded as partial first MARK");
}

bool IrRemoteQualifyStandbyWake(uint32_t timeoutMs) {
  if (!receiverStarted || !profileValid) return false;

  const uint32_t started = millis();
  while (static_cast<uint32_t>(millis() - started) < timeoutMs) {
    IrFrame data{};
    if (!takeDecodedFrame(data)) {
      delay(1);
      continue;
    }

    const bool repeat =
        (data.flags & (IR_FLAG_REPEAT | IR_FLAG_AUTO_REPEAT)) != 0U;
    const IrAction action = findAction(data);
    DIAG_PRINTF("[IR/SLEEP] qualify protocol=%s action=%s repeat=%u elapsed=%lu ms\n",
                  IrProtocolName(data.protocol),
                  action == IR_ACTION_NONE ? "NONE" : kActionName[action],
                  repeat ? 1U : 0U,
                  static_cast<unsigned long>(millis() - started));

    // Accept only the learned STANDBY action. The decoder state was reset when
    // the edge receiver resumed, so a protocol repeat can map to STANDBY only
    // after a real STANDBY frame in this same qualification window established
    // its address/command. A stale pre-sleep repeat can therefore not qualify.
    if (action == IR_ACTION_STANDBY) {
      lastRuntimeAction = IR_ACTION_NONE;
      lastRuntimeFrameMs = millis();
      runtimePressStartMs = 0U;
      standbySuppressUntilMs = millis() + 1000UL;
      DIAG_PRINTLN("[IR/SLEEP] learned STANDBY wake accepted");
      return true;
    }
  }

  // Do not let a rejected wake candidate influence normal runtime repeat
  // association if/when a later wake is accepted.
  lastRuntimeAction = IR_ACTION_NONE;
  lastRuntimeFrameMs = millis();
  runtimePressStartMs = 0U;
  DIAG_PRINTLN("[IR/SLEEP] wake rejected: learned STANDBY not confirmed");
  return false;
}

bool IrRemoteHasProfile(void) {
  return profileLoaded ? profileValid : storedProfileValid();
}
bool IrRemoteUiActive(void) { return uiState != UI_NONE; }

void IrRemoteUiOpen(void) {
  if (receiverStarted) {
    if (!profileLoaded) loadProfile();
  } else {
    // In AUTO/INTB mode the submenu remains discoverable, but simply reading
    // its status must not allocate IR buffers after boot.
    profileValid = storedProfileValid();
  }
  uiState = UI_MENU;
  uiSelection = 0;
  drawMenu();
}

void IrRemoteUiAbort(void) {
  uiState = UI_NONE;
  learnIndex = 0;
  learnWaitingRelease = false;
  learnLastFrameMs = 0;
  clearYes = false;
  lastTestDataValid = false;
  lastTestRepeatShown = false;
  lastTestFrameMs = 0;
}

void IrRemoteUiRotate(int8_t direction) {
  if (uiState == UI_MENU) {
    const uint8_t previous = uiSelection;
    if (direction > 0) uiSelection = (uiSelection + 1U) % 4U;
    else uiSelection = uiSelection == 0 ? 3 : uiSelection - 1;
    // Only the two affected rows are restored/redrawn. Repainting the complete
    // 320x240 background on every encoder detent caused the visible flashing.
    drawMenuRow(previous, true);
    if (uiSelection != previous) drawMenuRow(uiSelection, true);
  } else if (uiState == UI_CLEAR_CONFIRM) {
    clearYes = !clearYes;
    drawClearChoices(true);
  }
}

bool IrRemoteUiPress(void) {
  if (uiState == UI_LEARN || uiState == UI_TEST || uiState == UI_NOTICE) {
    uiState = UI_MENU;
    lastTestDataValid = false;
    lastTestRepeatShown = false;
    lastTestFrameMs = 0;
    drawMenu();
    return false;
  }
  if (uiState == UI_CLEAR_CONFIRM) {
    if (clearYes) clearProfile();
    uiState = UI_MENU;
    clearYes = false;
    drawMenu();
    return false;
  }
  if (uiState != UI_MENU) return true;

  switch (uiSelection) {
    case 0: // Learn
      if (!receiverStarted) {
        uiState = UI_NOTICE;
        drawNeedIr();
        return false;
      }
      if (!IrRemotePrepare()) {
        uiState = UI_NOTICE;
        drawUiBase(irRemoteText[language]);
        tftPrint(0, irMemoryErrorText[language], 155, 102,
                 ActiveColor, ActiveColorSmooth, 28);
        tftPrint(0, irPressOkText[language], 155, 145,
                 SecondaryColor, SecondaryColorSmooth, 16);
        return false;
      }
      memcpy(learnWork, learned, IR_CODE_TABLE_BYTES);
      learnIndex = 0;
      learnWaitingRelease = false;
      learnLastFrameMs = 0;
      uiState = UI_LEARN;
      drawLearn();
      return false;

    case 1: // Clear
      if (!profileValid) {
        drawMenu();
        return false;
      }
      clearYes = false;
      uiState = UI_CLEAR_CONFIRM;
      drawClear();
      return false;

    case 2: // Test
      if (!receiverStarted) {
        uiState = UI_NOTICE;
        drawNeedIr();
        return false;
      }
      uiState = UI_TEST;
      lastTestDataValid = false;
      lastTestRepeatShown = false;
      lastTestFrameMs = 0;
      drawTest();
      return false;

    default: // Back
      uiState = UI_NONE;
      return true;
  }
}

void IrRemoteProcess(void) {
  if (!receiverStarted) return;

  if (__atomic_exchange_n(&edgeWakeFirstMarkReady, false, __ATOMIC_ACQ_REL)) {
    const uint16_t firstMark =
        __atomic_load_n(&edgeWakeFirstMarkUs, __ATOMIC_ACQUIRE);
    DIAG_PRINTF("[IR/SLEEP] residual first MARK=%u us (wake+resume time was already elapsed)\n",
                  static_cast<unsigned>(firstMark));
  }

  // Diagnostics are deliberately silent while GPIO12 is idle. If unexpected
  // edge activity is starving the cooperative radio scheduler, the monitor
  // will expose it without adding periodic UART traffic in the normal case.
  const uint32_t diagNow = millis();
  if (static_cast<uint32_t>(diagNow - edgeDiagTimerMs) >= 5000UL) {
    edgeDiagTimerMs = diagNow;
    const uint32_t transitions =
        __atomic_load_n(&edgeTransitionCount, __ATOMIC_ACQUIRE);
    if (transitions != edgeDiagLastTransitions) {
      const uint32_t glitches =
          __atomic_load_n(&edgeGlitchCount, __ATOMIC_ACQUIRE);
      const uint32_t overflows =
          __atomic_load_n(&edgeOverflowCount, __ATOMIC_ACQUIRE);
      DIAG_PRINTF("[IR/EDGE] edges=%u frames=%u decoded=%u glitches=%u overflow=%u\n",
                    static_cast<unsigned>(transitions),
                    static_cast<unsigned>(edgeFrameCount),
                    static_cast<unsigned>(edgeDecodedCount),
                    static_cast<unsigned>(glitches),
                    static_cast<unsigned>(overflows));
      edgeDiagLastTransitions = transitions;
    }
  }

  // Between learning steps require a real release (a short quiet gap). This
  // prevents remotes that resend complete frames, not only explicit repeat
  // frames, from teaching one held key into multiple actions.
  if (uiState == UI_LEARN && learnWaitingRelease &&
      millis() - learnLastFrameMs >= IR_LEARN_RELEASE_MS) {
    learnWaitingRelease = false;
    drawLearn();
  }

  // MODE short/long recognition must run even when no new frame arrives:
  // release is represented by silence, and 1000 ms can fall between repeats.
  // Learn/Test remain isolated because only the normal UI state reaches this.
  if (uiState == UI_NONE) servicePendingModePress(millis());

  IrFrame data{};
  if (!takeDecodedFrame(data)) return;

  const bool repeat = data.flags &
      (IR_FLAG_REPEAT | IR_FLAG_AUTO_REPEAT);
  const uint32_t now = millis();

  if (uiState == UI_LEARN) {
    learnLastFrameMs = now;
    if (learnWaitingRelease || repeat) return;

    const int8_t duplicate = findLearningDuplicate(data);
    if (duplicate >= 0) {
      DIAG_PRINTF("[IR/LEARN] duplicate of %s ignored while waiting for %s\n",
                    kActionName[duplicate], kActionName[learnIndex]);
      learnWaitingRelease = true;
      drawLearnRelease();
      return;
    }

    learnWork[learnIndex] = fromFrame(data);
    DIAG_PRINTF("[IR/LEARN] %s protocol=%s address=0x%04X command=0x%04X bits=%u\n",
                  kActionName[learnIndex], IrProtocolName(data.protocol),
                  data.address, data.command, data.bits);
    ++learnIndex;
    if (learnIndex >= EE_IR_KEY_COUNT) {
      saveProfile(learnWork);
      uiState = UI_MENU;
      learnIndex = 0;
      learnWaitingRelease = false;
      drawMenu();
    } else {
      learnWaitingRelease = true;
      drawLearnRelease();
    }
    return;
  }

  if (uiState == UI_TEST) {
    IrFrame displayData = data;
    const bool shortUnknownFollowup =
        data.protocol == IR_PROTO_UNKNOWN && lastTestDataValid &&
        static_cast<uint32_t>(now - lastTestFrameMs) < IR_HELD_FRAME_GAP_MS;
    if ((repeat || shortUnknownFollowup) && lastTestDataValid) {
      // Protocol-specific repeat frames may not carry address/command and can
      // fall through to HASH/IR_PROTO_UNKNOWN. Keep the previous key on screen. Only
      // change the Repeat row once; repainting the whole data block for every
      // held-key frame caused the Test view to flash continuously.
      if (!lastTestRepeatShown) {
        drawTestRepeatRow(true, true);
        lastTestRepeatShown = true;
      }
      lastTestFrameMs = now;
      return;
    }

    displayData.flags &= static_cast<uint8_t>(
        ~(IR_FLAG_REPEAT | IR_FLAG_AUTO_REPEAT));
    lastTestData = displayData;
    lastTestDataValid = true;
    lastTestRepeatShown = false;
    lastTestFrameMs = now;
    drawTestData(&displayData, true);
    return;
  }

  if (uiState != UI_NONE) return;

  IrAction action = IR_ACTION_NONE;
  bool heldFrame = false;
  if (repeat) {
    // A repeat is valid only while it remains temporally attached to the last
    // decoded key. A late/noisy repeat can therefore never resurrect an old
    // action.
    if (lastRuntimeAction != IR_ACTION_NONE &&
        static_cast<uint32_t>(now - lastRuntimeFrameMs) < IR_HELD_FRAME_GAP_MS) {
      action = lastRuntimeAction;
      heldFrame = true;
    }
  } else {
    action = findAction(data);
    heldFrame = action != IR_ACTION_NONE && action == lastRuntimeAction &&
                static_cast<uint32_t>(now - lastRuntimeFrameMs) <
                    IR_HELD_FRAME_GAP_MS;
  }

  if (action == IR_ACTION_NONE) {
    // Before the long action fires, an unrelated/unassigned frame is an
    // unambiguous MODE release. After a long action, malformed/overflowed
    // frames can appear while SwitchRadioMode() was busy; keep the MODE release
    // guard alive until the physical edge stream is really quiet.
    if (modePressPending && !modeLongFired) {
      modeLastFrameMs = now - IR_HELD_FRAME_GAP_MS;
      servicePendingModePress(now);
    }
    if (modePressPending && modeLongFired) return;
    lastRuntimeAction = IR_ACTION_NONE;
    lastRuntimeFrameMs = now;
    runtimePressStartMs = 0;
    return;
  }

  if (action != IR_ACTION_MODE && modePressPending) {
    if (modeLongFired) {
      // After the long switch, ignore every other IR action until MODE has
      // physically gone quiet. This guarantees that the tail of a held key can
      // never become a second short MODE or another accidental command.
      return;
    }
    // A different valid key ends a still-pending short MODE immediately.
    modeLastFrameMs = now - IR_HELD_FRAME_GAP_MS;
    servicePendingModePress(now);
    heldFrame = false;
  }

  if (action == IR_ACTION_MODE) {
    if (!modePressPending) {
      modePressPending = true;
      modeLongFired = false;
      modePressStartMs = now;
      modeLastFrameMs = now;
      lastRuntimeAction = IR_ACTION_MODE;
      lastRuntimeFrameMs = now;
      runtimePressStartMs = now;
      DIAG_PRINTLN("[IR/MODE] press pending; long threshold=1000 ms");
      return;
    }

    modeLastFrameMs = now;
    lastRuntimeAction = IR_ACTION_MODE;
    lastRuntimeFrameMs = now;
    servicePendingModePress(now);
    return;
  }

  if (!heldFrame) {
    // First frame: execute once immediately, then require a deliberate hold
    // before TUNE/VOL autorepeat is allowed to start. MODE is handled above
    // because its short action must wait until release.
    lastRuntimeAction = action;
    lastRuntimeFrameMs = now;
    runtimePressStartMs = now;
    dispatch(action, false);
    return;
  }

  // Keep the held-key association alive even while repeats are deliberately
  // suppressed during the initial delay.
  lastRuntimeAction = action;
  lastRuntimeFrameMs = now;
  if (!actionRepeats(action)) return;
  if (static_cast<uint32_t>(now - runtimePressStartMs) <
      IR_REPEAT_INITIAL_DELAY_MS) return;
  dispatch(action, true);
}
