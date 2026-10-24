// SI4684 DAB chip driver. See si4684.h for the high-level summary.
//
// Implementation notes:
//   - The supplied Si468x library owns the common CTS/error state machine.
//     DAB parsers that still use SPIbuffer route all commands through that one
//     common transport.
//   - Commands and reply layouts mirror the Si468x programming guide (AN649).
//   - Slideshow segments and the assembled current image stay in RAM.

#include "si4684.h"
#include "constants.h"
#include "dab_scheduler_policy.h"
#include "dab_user_application.h"
#include "epg_runtime.h"
#include "epg_ingress.h"
#include "esp_heap_caps.h"

extern void SlideshowReceptionState(bool active);
extern uint8_t gpio12Mode;
#include "Si468xROM.h"
// Arduino defines interrupts() as a function-like macro; the standalone
// driver also has a reply field named `interrupts`.
#ifdef interrupts
#undef interrupts
#endif
#include "vendor/si468x/Si468x.h"
// DAB uses the supplied 6.0.9 application image.
#include "vendor/si468x/dab_6_0_9.h"
#include "vendor/si468x/fmhd_5_3_3.h"

unsigned char SPIbuffer[4096];      // shared SPI tx/rx buffer (commands and replies)
bool once = false;

static uint32_t slideshowFirstSegmentMs = 0;
static bool epgContinuous = false, epgManualPending = false, epgProductionPaused = false;

bool EnsembleInfoSet;
// FM PTY value 0 is a valid RDS value ("Unknown"), so validity must be
// tracked separately from fmPty itself. Cleared on every tune/seek and set
// only after FM_RDS_STATUS explicitly reports TP/PTY valid.
bool fmPtyValid = false;

// DAB labels carry independent charset identifiers. Keep one 4-bit charset
// value per *sorted* service row (32 bytes total). Si468x DLS service-data
// packets carry their own control/charset bytes before the text payload, so
// Dynamic Label decoding is independent from ServiceLabelCharset as well.
static uint8_t dabServiceCharsetValue[32] = {0};
static uint8_t dabDynamicLabelCharset = 0;
static uint8_t dabDynamicLabelLength = 0;

uint8_t DabServiceLabelCharset(uint8_t serviceIndex) {
  return serviceIndex < 32 ? dabServiceCharsetValue[serviceIndex] : 0;
}

// Read-only DLS metadata for GUI-side radiotext caching. These accessors do
// not alter the DAB receive/scheduler path; they only let ShowRT detect when
// the same byte payload needs decoding under a different DLS charset/length.
uint8_t DabDynamicLabelCharsetValue(void) {
  return dabDynamicLabelCharset;
}

uint8_t DabDynamicLabelLengthValue(void) {
  return dabDynamicLabelLength;
}

uint8_t slaveSelectPin;

// The TFT stays on TFT_eSPI's VSPI/SPI3 host. The radio owns HSPI/SPI2 for the
// whole ESP32 uptime. Calling global SPI.begin() with the radio pins would still
// select VSPI and collide with the TFT host/APB callback registration.
static constexpr int RADIO_SPI_SCK = 14;
static constexpr int RADIO_SPI_MISO = 16;
static constexpr int RADIO_SPI_MOSI = 13;
static constexpr int RADIO_SPI_CS = 15;
static constexpr uint32_t RADIO_SPI_HZ = 10000000UL;
static SPIClass RadioSPI(HSPI);
static bool radioSpiInitialized = false;
static bool radioMemoryPrepareAttempted = false;
static bool radioMemoryPrepared = false;

static si468x::Si468x chip;
// One 4 KiB workspace reduces the approximately 0.5 MB firmware image to
// 4092-byte HOST_LOAD payloads (4096 bytes including the three command args).
// Allocate it once from internal heap instead of static DRAM so it and the
// independent 50 KiB MOT slideshow buffer do not overcommit the ESP32 DRAM
// linker segment.
static constexpr size_t CHIP_WORKSPACE_BYTES = 4096U;
static constexpr size_t CHIP_HOST_LOAD_PAYLOAD =
    (CHIP_WORKSPACE_BYTES - 3U) & ~static_cast<size_t>(3U);
static uint8_t* chipWorkspace = nullptr;
static volatile uint8_t lastStatus0;
static volatile bool radioIntbPending = false;
// Diagnostic build: count every GPIO12 transition and its direction.
// radioIntbEdgeCount remains the total transition counter used by existing
// runtime telemetry.
static volatile uint32_t radioIntbEdgeCount = 0;
static volatile uint32_t radioIntbFallingCount = 0;
static volatile uint32_t radioIntbRisingCount = 0;
static RadioControlMode radioControlMode = RADIO_CTRL_DETECT;
enum class RadioIntbCapability : uint8_t {
  Unknown,
  Absent,
  Present
};
static RadioIntbCapability radioIntbCapability = RadioIntbCapability::Unknown;
static bool radioIntbInterruptAttached = false;
static int radioIntbInterruptMode = -1;
static uint32_t radioBootIrqEdgeCount = 0;
static bool commandAwaitingCts = false;
static bool commandStatusReadTriggeredByIrq = false;
static uint32_t commandStartedUs = 0;
static bool radioPowerUpCtsViaIrq = false;
static bool radioAppTestCtsViaIrq = false;
static bool radioDabRuntimeActive = false;
static bool dabStcPending = false;
static bool dabDsrvPending = false;
static bool dabDeviceEventPending = false;

static I2sAudioMode radioI2sAudioMode = I2S_AUDIO_48K;
static bool radioI2sOutputReady = false;

static uint16_t audioPinConfig(bool intbEnabled) {
  return i2sAudioPinConfig(radioI2sOutputReady ? radioI2sAudioMode : I2S_AUDIO_OFF,
                           intbEnabled);
}

void DAB::setI2sAudioMode(I2sAudioMode mode) {
  radioI2sAudioMode = sanitizeI2sAudioMode(mode);
}

// Format is writable only before the first tune (AN649). A reused image may
// already be tuned: verify its complete configuration without changing it.
static void configureI2sAudio(bool reuseRunningImage) {
  radioI2sOutputReady = false;
  const uint16_t rate = i2sAudioSampleRate(radioI2sAudioMode);
  DIAG_PRINTF("[I2S] mode=%s requested=%u rate=%u format=0x%04X reuse=%u\n",
              i2sAudioModeText(radioI2sAudioMode), rate != 0U, rate,
              I2S_AUDIO_FORMAT_STANDARD, reuseRunningImage);
  if (!rate) return;
  const si468x::Property properties[] = {
    si468x::Property::DIGITAL_IO_OUTPUT_SELECT,
    si468x::Property::DIGITAL_IO_OUTPUT_FORMAT,
    si468x::Property::DIGITAL_IO_OUTPUT_SAMPLE_RATE
  };
  const uint16_t values[] = {I2S_AUDIO_MASTER, I2S_AUDIO_FORMAT_STANDARD, rate};
  if (!reuseRunningImage) {
    for (uint8_t i = 0; i < 3; ++i) {
      const si468x::Result result = chip.setProperty(properties[i], values[i]);
      DIAG_PRINTF("[I2S] property=0x%04X value=0x%04X result=%d\n",
                  static_cast<unsigned>(properties[i]), values[i], static_cast<int>(result));
      if (result != si468x::Result::Ok) {
        DIAG_PRINTLN("[I2S] configuration failed; digital pins disabled, analog DAC retained");
        return;
      }
    }
  }
  // Check Result even if the output argument happens to contain the expected
  // value. All three properties must agree before PIN_CONFIG enables I2S.
  for (uint8_t i = 0; i < 3; ++i) {
    uint16_t actual = 0;
    const si468x::Result result = chip.getProperty(properties[i], actual, 250000UL);
    DIAG_PRINTF("[I2S] readback property=0x%04X result=%d expected=0x%04X actual=0x%04X\n",
                static_cast<unsigned>(properties[i]), static_cast<int>(result), values[i], actual);
    if (result != si468x::Result::Ok || actual != values[i]) {
      DIAG_PRINTLN("[I2S] verification failed; digital pins disabled, analog DAC retained");
      return;
    }
  }
  radioI2sOutputReady = true;
  DIAG_PRINTLN("[I2S] verified; digital output ready");
}

// Keep INTB as the fast path for real events and retain short, bounded safety
// polls so a disconnected or faulty application IRQ cannot stall the UI.
static constexpr uint32_t RADIO_INTB_CTS_SAFETY_US = 2000UL;
static constexpr uint32_t RADIO_FM_DIAG_INTERVAL_MS = 30000UL;
static constexpr uint32_t RADIO_DAB_DIAG_INTERVAL_MS = 30000UL;
static constexpr uint32_t RADIO_DAB_TUNE_TIMEOUT_MS = 5000UL;
static constexpr uint8_t RADIO_DAB_STALL_TIMEOUT_COUNT = 16U;
static constexpr uint16_t RADIO_DAB_EVENT_SERVICE_LIST = 0x0001U;
static constexpr uint16_t RADIO_DAB_EVENT_RECONFIGURATION = 0x0080U;

// Runtime-only counters. They are reset after each image boot/reuse so the
// rate-limited FM line compares main-screen and menu behaviour directly.
static bool radioRuntimeDiagnosticsActive = false;
static uint32_t diagFmBusySkipCount = 0;
static uint32_t diagFmRsqStatusCount = 0;
static uint32_t diagFmRdsStatusCount = 0;
static uint32_t diagCtsIrqCompletionCount = 0;
static uint32_t diagCtsPollCompletionCount = 0;
static uint32_t diagFmLastReportMs = 0;
static uint32_t diagDabStcCount = 0;
static uint32_t diagDabDsrvCount = 0;
static uint32_t diagDabDsrvOverflowCount = 0;
static uint32_t diagDabDeviceEventCount = 0;
static uint32_t diagDabCommandErrorCount = 0;
static uint32_t diagDabBusySkipCount = 0;
static uint32_t diagDabAudioStartCount = 0;
static uint32_t diagDabAudioStopCount = 0;
static uint32_t diagDabDataStartCount = 0;
static uint32_t diagDabDataStopCount = 0;
static uint32_t diagDabNotAvailableCount = 0;
static uint32_t diagDabCtsTimeoutCount = 0;
static uint32_t diagDabHostStarvationCount = 0;
static uint32_t diagDabHostStarvationMaxGapUs = 0;
static uint32_t diagDabGenuineCtsTimeoutCount = 0;
static uint32_t diagDabTuneBusyCount = 0;
static uint32_t diagDabDataRetryCount = 0;
static uint32_t diagDabAbandonedSwitchCount = 0;
static uint32_t diagDabStaleMotCount = 0;
static uint32_t diagDabRecoveryRequestCount = 0;
static uint32_t diagDabLastReportMs = 0;

// Raw SPI/bootstrap tracing is compiled in but silent by default. The bare
// serial command DEBUG enables it at runtime together with FM/DAB/RDS/SLS logs.

// Boot diagnostics counters are retained even when verbose transport logging
// is disabled because the compact boot summary reports total uploaded bytes.
static uint8_t diagLoadPhase = 0;
static uint32_t diagPhaseBytes[3] = {0, 0, 0};
static uint32_t diagNextLoadReport = 65536UL;
static volatile uint8_t diagLastCommand = 0;

static void IRAM_ATTR radioIntbIsr(void) {
  // ISR contract: record the transition only. SPI, logging and GUI work stay
  // in cooperative foreground code. CHANGE is deliberate in this diagnostic
  // build so the real Si4684 INTB polarity/pulse shape is observable.
  const uint8_t level = digitalRead(SI4684_INTB_PIN) == HIGH ? HIGH : LOW;
  radioIntbPending = true;
  ++radioIntbEdgeCount;
  if (level == LOW)
    ++radioIntbFallingCount;
  else
    ++radioIntbRisingCount;
}

static bool takeRadioIntb(void) {
  return __atomic_exchange_n(&radioIntbPending, false, __ATOMIC_ACQ_REL);
}

static uint32_t radioIntbEdges(void) {
  return __atomic_load_n(&radioIntbEdgeCount, __ATOMIC_ACQUIRE);
}

static uint32_t radioIntbFallingEdges(void) {
  return __atomic_load_n(&radioIntbFallingCount, __ATOMIC_ACQUIRE);
}

static uint32_t radioIntbRisingEdges(void) {
  return __atomic_load_n(&radioIntbRisingCount, __ATOMIC_ACQUIRE);
}

static uint32_t radioRuntimeIntbEdges(void) {
  return radioIntbEdges() - radioBootIrqEdgeCount;
}

static void setRadioIntbInterrupt(bool enabled, int mode = FALLING) {
  if (!enabled) {
    if (radioIntbInterruptAttached)
      detachInterrupt(digitalPinToInterrupt(SI4684_INTB_PIN));
    radioIntbInterruptAttached = false;
    radioIntbInterruptMode = -1;
    return;
  }

  // DETECT listens to both directions so the first physical transition is
  // impossible to miss. Once INTB is proven, switch to the documented
  // active-low FALLING interrupt and keep the LOW-level safeguard in
  // radioIntbActive() for coalesced/sticky sources.
  if (radioIntbInterruptAttached && radioIntbInterruptMode == mode) return;
  if (radioIntbInterruptAttached)
    detachInterrupt(digitalPinToInterrupt(SI4684_INTB_PIN));

  attachInterrupt(digitalPinToInterrupt(SI4684_INTB_PIN), radioIntbIsr, mode);
  radioIntbInterruptAttached = true;
  radioIntbInterruptMode = mode;
}

static bool radioIntbActive(void) {
  const bool edge = takeRadioIntb();
  // During one-shot HW detection a static LOW level must never masquerade as a
  // real IRQ. Only a transition latched by the ISR is accepted as evidence.
  if (radioControlMode == RADIO_CTRL_DETECT) return edge;

  // Once INTB is proven the ISR uses FALLING. A level check is retained because
  // several Si4684 interrupt sources can coalesce while INTB is already LOW.
  return edge || digitalRead(SI4684_INTB_PIN) == LOW;
}

static void promoteDetectedIntb(const char* stage) {
  if (radioControlMode != RADIO_CTRL_DETECT ||
      radioIntbCapability != RadioIntbCapability::Unknown)
    return;

  // This executes in foreground context, never in the ISR. The transition that
  // caused entry into hostIdle() is the physical proof. Promote immediately so
  // the *same* bootloader/application command and every following command use
  // the normal IRQ-first CTS path. Safety polling remains bounded as backup.
  radioIntbCapability = RadioIntbCapability::Present;
  radioControlMode = RADIO_CTRL_INTB;
  setRadioIntbInterrupt(true, FALLING);
  chip.setCtsPollIntervalUs(RADIO_INTB_CTS_SAFETY_US);
  chip.setIdleStatusPollIntervalUs(50000UL);

  DIAG_PRINTF(
      "[RADIO/IRQ] first INTB transition detected during %s cmd=0x%02X phase=%u; IRQ mode ACTIVE\n",
      stage ? stage : "command",
      static_cast<unsigned>(diagLastCommand),
      static_cast<unsigned>(diagLoadPhase));
}

static void usePollingFallback(const char* reason) {
  if (radioControlMode == RADIO_CTRL_POLL) return;
  setRadioIntbInterrupt(false);
  radioControlMode = RADIO_CTRL_POLL;
  chip.setCtsPollIntervalUs(1000);
  chip.setIdleStatusPollIntervalUs(20000);
  DIAG_PRINTF("[RADIO/IRQ] %s; polling fallback\n", reason ? reason : "INTB disabled");
}

// Runtime fallback also disables the physical INTB output. This helper is used
// only after BOOT, when application properties are available.
static void useRuntimePollingFallback(const char* reason) {
  usePollingFallback(reason);
  const uint16_t pinConfig = audioPinConfig(false);
  const si468x::Result pinConfigResult = chip.setProperty(
      si468x::Property::PIN_CONFIG_ENABLE, pinConfig);
  DIAG_PRINTF("[RADIO/IRQ] runtime PIN_CONFIG=0x%04X fallback result=%d\n",
                static_cast<unsigned>(pinConfig),
                static_cast<int>(pinConfigResult));
}

static bool hostWriteCommand(void*, uint8_t command, const uint8_t* args, uint16_t length) {
  diagLastCommand = command;
  commandAwaitingCts = true;
  commandStatusReadTriggeredByIrq = false;
  commandStartedUs = micros();
  if (command == 0x01) {
    if (diagnosticDebug) {
      DIAG_PRINTF("[RADIO/SPI] POWER_UP args=%u data=", length);
      for (uint16_t i = 0; i < length; ++i)
        DIAG_PRINTF("%02X%s", args[i], (i + 1U < length) ? " " : "");
      DIAG_PRINTLN();
    }
  } else if (command == 0x06) {
    if (diagLoadPhase < 2) ++diagLoadPhase;
    diagPhaseBytes[diagLoadPhase] = 0;
    diagNextLoadReport = 65536UL;
    if (diagnosticDebug)
      DIAG_PRINTF("[RADIO/SPI] LOAD_INIT phase=%u (%s)\n",
                    diagLoadPhase, diagLoadPhase == 1 ? "PATCH" : "FIRMWARE");
  } else if (command == 0x04) {
    if (diagLoadPhase <= 2) {
      const uint16_t payloadLength = length >= 3U ? length - 3U : 0U;
      diagPhaseBytes[diagLoadPhase] += payloadLength;
      if (diagPhaseBytes[diagLoadPhase] >= diagNextLoadReport) {
        if (diagnosticDebug)
          DIAG_PRINTF("[RADIO/SPI] HOST_LOAD phase=%u bytes=%u\n",
                        diagLoadPhase, diagPhaseBytes[diagLoadPhase]);
        while (diagNextLoadReport <= diagPhaseBytes[diagLoadPhase])
          diagNextLoadReport += 65536UL;
      }
    }
  } else if (command == 0x07 && diagnosticDebug) {
    DIAG_PRINTF("[RADIO/SPI] BOOT patchBytes=%u fwBytes=%u\n",
                  diagPhaseBytes[1], diagPhaseBytes[2]);
  }

  RadioSPI.beginTransaction(SPISettings(RADIO_SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(slaveSelectPin, LOW);
  RadioSPI.transfer(command);
  for (uint16_t i = 0; i < length; ++i) RadioSPI.transfer(args[i]);
  digitalWrite(slaveSelectPin, HIGH);
  RadioSPI.endTransaction();
  return true;
}

static bool hostReadReply(void*, uint8_t* destination, uint16_t length) {
  if (!destination || !length) return false;
  RadioSPI.beginTransaction(SPISettings(RADIO_SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(slaveSelectPin, LOW);
  RadioSPI.transfer(0);  // SPI framing byte; hidden from the common driver
  for (uint16_t i = 0; i < length; ++i) destination[i] = RadioSPI.transfer(0);
  digitalWrite(slaveSelectPin, HIGH);
  RadioSPI.endTransaction();

  // Raw SPI replies are useful only for transport debugging. Normal builds
  // report command failures in the higher-level DAB/FM diagnostics instead.
  if (diagnosticDebug &&
      (diagLastCommand == 0x01 || diagLastCommand == 0x09 ||
       (destination[0] & 0x40U))) {
    const uint16_t shown = length < 12 ? length : 12;
    DIAG_PRINTF("[RADIO/SPI] REPLY cmd=0x%02X len=%u data=",
                  static_cast<unsigned>(diagLastCommand), static_cast<unsigned>(length));
    for (uint16_t i = 0; i < shown; ++i)
      DIAG_PRINTF("%02X%s", destination[i], (i + 1U < shown) ? " " : "");
    if (length > shown) DIAG_PRINT(" ...");
    DIAG_PRINTLN();
    if ((destination[0] & 0x40U) && length >= 5)
      DIAG_PRINTF("[RADIO/SPI] ERR_CMD reason RESP4=0x%02X\n", destination[4]);
  }
  return true;
}

static uint32_t hostTimeUs(void*) { return micros(); }
static void hostIdle(void*) {
  const bool detecting = radioControlMode == RADIO_CTRL_DETECT;
  if ((radioControlMode == RADIO_CTRL_INTB || detecting) &&
      radioIntbActive()) {
    // The first real GPIO12 transition is enough to prove the connection.
    // Promote before servicing it, so this very first event is already used as
    // a normal IRQ completion and all remaining LOAD_INIT/HOST_LOAD/BOOT
    // commands run IRQ-first instead of staying in DETECT until BOOT finishes.
    if (detecting) promoteDetectedIntb("command");

    if (commandAwaitingCts) commandStatusReadTriggeredByIrq = true;
    chip.notifyInterrupt();
  }
  yield();
}

static void statusChanged(void*, const si468x::Status& status) {
  lastStatus0 = status.status0;
  const bool irqTriggered = commandStatusReadTriggeredByIrq;
  // One callback corresponds to the status read caused by the most recent
  // service decision. Do not carry its trigger into a later safety poll.
  commandStatusReadTriggeredByIrq = false;
  if (radioDabRuntimeActive) {
    if (status.stcInt() && !dabStcPending) {
      dabStcPending = true;
      ++diagDabStcCount;
    }
    if (status.dsrvInt() && !dabDsrvPending) {
      dabDsrvPending = true;
      ++diagDabDsrvCount;
    }
    if (status.deviceEventInt() && !dabDeviceEventPending) {
      dabDeviceEventPending = true;
      ++diagDabDeviceEventCount;
    }
  }

  if (!commandAwaitingCts || !status.cts()) return;

  if (irqTriggered) {
    // hostIdle() may promote DETECT -> INTB before this callback executes, so
    // completion-source diagnostics must not depend on the *current* mode.
    if (diagLastCommand == static_cast<uint8_t>(si468x::Command::POWER_UP))
      radioPowerUpCtsViaIrq = true;
    if (diagLastCommand == static_cast<uint8_t>(si468x::Command::GET_PART_INFO))
      radioAppTestCtsViaIrq = true;
  }

  if (radioRuntimeDiagnosticsActive) {
    if ((radioControlMode == RADIO_CTRL_INTB ||
         radioControlMode == RADIO_CTRL_DETECT) && irqTriggered)
      ++diagCtsIrqCompletionCount;
    else
      ++diagCtsPollCompletionCount;
  }

  if ((radioControlMode == RADIO_CTRL_INTB ||
       radioControlMode == RADIO_CTRL_DETECT) && !irqTriggered) {
    // Keep a diagnostic for a genuinely long wait, but do not flood the UART
    // for the normal 2 ms hybrid safety poll.
    if (static_cast<uint32_t>(micros() - commandStartedUs) >= 200000UL)
      DIAG_PRINTLN("[RADIO/IRQ] long CTS wait resolved by status poll");
  }
  commandAwaitingCts = false;
}

static void finishCommandDiagnostics(si468x::Result result) {
  // Pending/Busy still belong to a live operation. Every other result is
  // terminal, including timeout/transport failures which have no CTS callback.
  if (result == si468x::Result::Pending || result == si468x::Result::Busy)
    return;
  commandAwaitingCts = false;
  commandStatusReadTriggeredByIrq = false;
}

static size_t progmemImageReader(void* context, uint32_t offset, uint8_t* destination, size_t length) {
  const uint8_t* source = static_cast<const uint8_t*>(context);
  for (size_t i = 0; i < length; ++i) destination[i] = pgm_read_byte(source + offset + i);
  return length;
}

static void Set_Property(uint16_t property, uint16_t value);
static void charConverter(const char* input, wchar_t* output, size_t size);
static int compareCompID(const void* a, const void* b);

// Read back the chip identifier string (e.g. "Si4684").
char* DAB::getChipID(void) {
  return ChipType;
}

// Return the loaded firmware version string ("major.minor.build").
char* DAB::getFirmwareVersion(void) {
  return FirmwVersion;
}

// Sanity check: query the chip status. Returns true if it looks hung
// (caller responds by reinitialising the chip via doRecovery()).
bool DAB::panic(void) {
  si468x::SystemState state;
  const si468x::Result result = chip.getSystemState(state, 100000UL);
  finishCommandDiagnostics(result);
  if (result != si468x::Result::Ok) return true;
  return state.image != (isFm() ? si468x::Image::FMHD : si468x::Image::DAB);
}

int16_t DAB::getRSSI(void) {
  if (isFm()) return static_cast<int16_t>(fmRssi) * 10;
  return dabRssi10;
}

uint32_t DAB::getFreq(uint8_t freq) {
  if (isFm()) return static_cast<uint32_t>(fmFrequency10kHz) * 10UL;
  return DABfrequencyTable_DAB[freq].frequency;
}

const char* DAB::getChannel(uint8_t freq) {
  if (isFm()) return "FM";
  return DABfrequencyTable_DAB[freq].label;
}

// Set the chip's internal audio attenuator (the headphone amp does the
// fine volume; this is mostly a coarse control).
void DAB::vol(uint8_t vol) {
  Set_Property(0x0300, (vol & 0x3F));
}

// SET_PROPERTY (cmd 0x13): write one of the chip's internal properties such
// as sample rate, audio output config, FIC interrupt source. See AN649 §6.
static void Set_Property(uint16_t property, uint16_t value) {
  const si468x::Result result = chip.setProperty(property, value);
  finishCommandDiagnostics(result);
  if (result != si468x::Result::Ok)
    DIAG_PRINTF("[RADIO/PROP] set 0x%04X=0x%04X failed result=%d\n",
                  static_cast<unsigned>(property),
                  static_cast<unsigned>(value), static_cast<int>(result));
}

RadioControlMode DAB::controlMode(void) const {
  return radioControlMode;
}

const char* DAB::controlModeName(void) const {
  if (radioControlMode == RADIO_CTRL_DETECT) return "DETECT";
  return radioControlMode == RADIO_CTRL_INTB ? "INTB" : "POLL";
}

const char* DAB::intbHardwareName(void) const {
  if (radioIntbCapability == RadioIntbCapability::Present) return "INTB";
  if (radioIntbCapability == RadioIntbCapability::Absent) return "Polling";
  return "Unknown";
}

void DAB::applyFmRegionProperties(void) {
  const FmRegionProfile& profile = fmProfile();
  static const uint8_t seekRssi[] = {12, 18, 28};
  static const uint8_t seekSnr[] = {2, 4, 8};
  Set_Property(0x3100, profile.minFrequency10kHz);
  Set_Property(0x3101, profile.maxFrequency10kHz);
  Set_Property(0x3102, profile.seekSpacing10kHz);
  Set_Property(0x3900, profile.deEmphasis);
  Set_Property(0x3202, seekRssi[activeFmSeekSensitivity]);
  Set_Property(0x3204, seekSnr[activeFmSeekSensitivity]);
  DIAG_PRINTF("[FM/REGION] %s band=%u-%u spacing=%u de-emphasis=%u us data=%s\n",
                profile.menuName, profile.minFrequency10kHz,
                profile.maxFrequency10kHz, profile.seekSpacing10kHz,
                profile.deEmphasis == 0 ? 75U : 50U,
                profile.rbds ? "RBDS" : "RDS");
}

void DAB::setFmSeekSensitivity(uint8_t profile, bool applyNow) {
  activeFmSeekSensitivity = profile <= 2U ? profile : 1U;
  if (applyNow && isFm()) applyFmRegionProperties();
}

void DAB::setFmRegion(uint8_t region, bool applyNow) {
  activeFmRegion = sanitizeFmRegion(region);
  if (applyNow && isFm()) applyFmRegionProperties();
}

bool DAB::prepareRuntimeMemory(void) {
  // Never retry later in begin(): a failed early reservation is a startup
  // failure, not permission to fragment the heap and try again.
  if (radioMemoryPrepareAttempted) return radioMemoryPrepared;
  radioMemoryPrepareAttempted = true;

  if (!slideshowSegBuf) {
    slideshowSegBuf = static_cast<uint8_t*>(heap_caps_malloc(
        SLS_BUFFER_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (!slideshowSegBuf) {
    DIAG_PRINTF("[RAM/SLS] ERROR: early MOT allocation failed bytes=%u free=%u largest=%u\n",
                  static_cast<unsigned>(SLS_BUFFER_BYTES), ESP.getFreeHeap(),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return false;
  }
  DIAG_PRINTF("[RAM/SLS] early MOT buffer=%u address=%p free=%u largest=%u\n",
                static_cast<unsigned>(SLS_BUFFER_BYTES), slideshowSegBuf,
                ESP.getFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

  if (!chipWorkspace) {
    chipWorkspace = static_cast<uint8_t*>(heap_caps_malloc(
        CHIP_WORKSPACE_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (!chipWorkspace) {
    DIAG_PRINTF("[RAM/RADIO] ERROR: early HOST_LOAD workspace allocation failed bytes=%u free=%u largest=%u\n",
                  static_cast<unsigned>(CHIP_WORKSPACE_BYTES), ESP.getFreeHeap(),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return false;
  }
  DIAG_PRINTF("[RAM/RADIO] early HOST_LOAD workspace=%u address=%p free=%u largest=%u\n",
                static_cast<unsigned>(CHIP_WORKSPACE_BYTES), chipWorkspace,
                ESP.getFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

  radioMemoryPrepared = true;
  return true;
}

bool DAB::prepareSpiBus(void) {
  if (radioSpiInitialized) {
    if (diagnosticDebug)
      DIAG_PRINTLN("[RADIO/SPI] HSPI/SPI2 already initialised; begin skipped");
    return true;
  }

  pinMode(RADIO_SPI_CS, OUTPUT);
  digitalWrite(RADIO_SPI_CS, HIGH);
  RadioSPI.begin(RADIO_SPI_SCK, RADIO_SPI_MISO, RADIO_SPI_MOSI, RADIO_SPI_CS);
  radioSpiInitialized = true;
  DIAG_PRINTF("[RADIO/SPI] HSPI/SPI2 initialised once SCK=%d MISO=%d MOSI=%d CS=%d\n",
                RADIO_SPI_SCK, RADIO_SPI_MISO, RADIO_SPI_MOSI, RADIO_SPI_CS);
  return true;
}

// Cold-start sequence per AN649:
//   1. POWER_UP - configure clock + crystal
//   2. LOAD_INIT + HOST_LOAD - upload the patch + firmware blobs from flash
//   3. BOOT - jump to firmware
//   4. Configure DAB-specific properties (sample rate, audio output, FIC etc.)
// Returns true once the chip reports the requested application image is running.
bool DAB::begin(uint8_t SSpin, RadioMode requestedMode) {
  // Invalidate before any early boot failure or firmware/mode change.
  dabXpad = dab_diag::XpadReadback{};
  dabXpadLogPending = false;
  dabRawUaLog = dab_diag::RawLog{};
  radioI2sOutputReady = false;
  const uint32_t radioBeginMs = millis();
  if (!radioMemoryPrepared || !slideshowSegBuf || !chipWorkspace) {
    DIAG_PRINTLN("[RADIO/BOOT] ERROR: persistent radio RAM was not prepared during setup");
    return false;
  }
  if (!radioSpiInitialized) {
    DIAG_PRINTLN("[RADIO/BOOT] ERROR: HSPI/SPI2 was not prepared during setup");
    return false;
  }
  DIAG_PRINTF("[RAM/SLS] persistent MOT buffer=%u address=%p free=%u largest=%u\n",
                static_cast<unsigned>(SLS_BUFFER_BYTES), slideshowSegBuf,
                ESP.getFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

  DIAG_PRINTLN();
  DIAG_PRINTLN("[RADIO] ================================================");
  DIAG_PRINTF("[RADIO] begin SS=%u requestedMode=%s\n",
                SSpin, requestedMode == RADIO_MODE_FM ? "FM" : "DAB");
  DIAG_PRINTF("[RADIO] heap before begin=%u min=%u\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap());

  diagLoadPhase = 0;
  diagPhaseBytes[0] = diagPhaseBytes[1] = diagPhaseBytes[2] = 0;
  diagNextLoadReport = 65536UL;

  memset(SPIbuffer, 0, sizeof(SPIbuffer));
  slaveSelectPin = SSpin;
  tunePending = false;
  seekPending = false;
  ServiceStart = false;
  ServiceIndex = 0;
  numberofservices = 0;
  dabServiceListReady = false;
  dabServiceLabelValid = false;
  dabServiceListGeneration = 0;
  dabUaInventoryValid = false;
  dabUaListVersion = 0;
  dabUaServiceCount = 0;
  dabUaComponentCount = 0;
  dabUaScanIndex = 0;
  dabUaLogPending = false;
  dabUaDebugWasEnabled = false;
  dabUaNextQueryMs = millis();
  epgContinuous = epgManualPending = epgProductionPaused = false;
  epg::liveReset();
  dabSpiProbe = dab_probe::Probe{};
  resetDabSpiDiscovery();
  SlideShowAvailable = false;
  SlideShowUpdate = false;
  SlideShowUpdate2 = false;
  slideshowRamSize = 0;
  slideshowPublishedPending = false;
  lastCompletedTransportId = 0;
  lastCompletedTransportIdValid = false;
  resetSlideshowCollector();
  dabRssi10 = 0;
  dabSignalTimer = 0;
  SlideShowVerbose = false;
  dabCommand = DabCommand::None;
  dabCommandRequestId = 0;
  dabGeneration = dab_scheduler::nextGeneration(dabGeneration);
  dabCommandGeneration = dabGeneration;
  dabTuneRequestId = 0;
  dabWaitingTuneRequestId = 0;
  dabTuneRequestPending = false;
  dabWaitingForStc = false;
  dabTuneBusyRetries = 0;
  dabTuneRetryNotBeforeMs = 0;
  dabRequestedServiceId = 0;
  dabRequestedComponentId = 0;
  dabActiveServiceId = 0;
  dabActiveComponentId = 0;
  dabCommandServiceId = 0;
  dabCommandComponentId = 0;
  dabSwitch.reset();
  dabServiceRequestPending = false;
  dabActiveServiceValid = false;
  dabSignalRefreshPending = false;
  dabServiceListRefreshPending = false;
  dabEnsembleRefreshPending = false;
  dabTimeRefreshPending = false;
  dabAudioRefreshPending = false;
  dabAudioInfoValid = false;
  dabAudioInfoRetryCount = 0;
  dabAudioInfoNotBeforeMs = 0;
  dabCurrentSubchannelRefreshPending = false;
  dabCurrentServiceRefreshPending = false;
  dabServiceTypeScanIndex = 0;
  resetDabPeriodicDeadlines(millis());
  dabDataServiceId = 0;
  dabDataComponentId = 0;
  dabActiveDataServiceId = 0;
  dabActiveDataComponentId = 0;
  dabActiveDataServiceValid = false;
  dabDsrvBurstCount = 0;
  dabConsecutiveCtsTimeouts = 0;
  dabTransportStalled = false;
  lastStatus0 = 0;
  radioDabRuntimeActive = false;
  dabStcPending = false;
  dabDsrvPending = false;
  dabDeviceEventPending = false;

  // GPIO12 has an explicit boot-time role. INTB bypasses all detection and
  // attaches the proven FALLING IRQ path immediately; IR never lets the radio
  // driver own GPIO12. AUTO preserves the existing one-shot HW detection and
  // reuses its result across later FM/DAB switches in the same ESP32 run.
  setRadioIntbInterrupt(false);
  const uint8_t configuredGpio12Mode = sanitizeGpio12Mode(gpio12Mode);
  if (configuredGpio12Mode == GPIO12_INTB) {
    radioIntbCapability = RadioIntbCapability::Present;
    radioControlMode = RADIO_CTRL_INTB;
  } else if (configuredGpio12Mode == GPIO12_IR) {
    radioIntbCapability = RadioIntbCapability::Absent;
    radioControlMode = RADIO_CTRL_POLL;
  } else if (radioIntbCapability == RadioIntbCapability::Unknown) {
    radioControlMode = RADIO_CTRL_DETECT;
  } else if (radioIntbCapability == RadioIntbCapability::Present) {
    radioControlMode = RADIO_CTRL_INTB;
  } else {
    radioControlMode = RADIO_CTRL_POLL;
  }
  __atomic_store_n(&radioIntbPending, false, __ATOMIC_RELEASE);
  __atomic_store_n(&radioIntbEdgeCount, 0U, __ATOMIC_RELEASE);
  __atomic_store_n(&radioIntbFallingCount, 0U, __ATOMIC_RELEASE);
  __atomic_store_n(&radioIntbRisingCount, 0U, __ATOMIC_RELEASE);
  radioBootIrqEdgeCount = 0;
  radioPowerUpCtsViaIrq = false;
  radioAppTestCtsViaIrq = false;
  radioRuntimeDiagnosticsActive = false;
  diagFmBusySkipCount = 0;
  diagFmRsqStatusCount = 0;
  diagFmRdsStatusCount = 0;
  diagCtsIrqCompletionCount = 0;
  diagCtsPollCompletionCount = 0;
  diagFmLastReportMs = 0;
  diagDabStcCount = 0;
  diagDabDsrvCount = 0;
  diagDabDsrvOverflowCount = 0;
  diagDabDeviceEventCount = 0;
  diagDabCommandErrorCount = 0;
  diagDabBusySkipCount = 0;
  diagDabAudioStartCount = diagDabAudioStopCount = 0;
  diagDabDataStartCount = diagDabDataStopCount = 0;
  diagDabNotAvailableCount = diagDabCtsTimeoutCount = 0;
  diagDabHostStarvationCount = 0;
  diagDabHostStarvationMaxGapUs = 0;
  diagDabGenuineCtsTimeoutCount = 0;
  diagDabTuneBusyCount = 0;
  diagDabDataRetryCount = diagDabAbandonedSwitchCount = 0;
  diagDabStaleMotCount = 0;
  diagDabRecoveryRequestCount = 0;
  diagDabLastReportMs = 0;
  // GPIO12 is MTDI on classic ESP32. Hardware using it for INTB must have
  // VDD_SDIO fixed safely at 3.3 V; firmware never reads or writes eFuse.
  // In IR mode leave the pin completely to the standalone IR edge receiver.
  if (configuredGpio12Mode != GPIO12_IR) {
    pinMode(SI4684_INTB_PIN, INPUT_PULLUP);
    if (radioControlMode == RADIO_CTRL_DETECT)
      setRadioIntbInterrupt(true, CHANGE);
    else if (radioControlMode == RADIO_CTRL_INTB)
      setRadioIntbInterrupt(true, FALLING);
  }
  if (configuredGpio12Mode == GPIO12_INTB)
    DIAG_PRINTLN("[RADIO/IRQ] GPIO12 setting INTB; FALLING IRQ active before radio commands");
  else if (configuredGpio12Mode == GPIO12_IR)
    DIAG_PRINTLN("[RADIO/IRQ] GPIO12 setting IR; radio forced to polling");
  else if (radioControlMode == RADIO_CTRL_DETECT)
    DIAG_PRINTLN("[RADIO/IRQ] GPIO12 setting AUTO; detecting INTB");
  else
    DIAG_PRINTF("[RADIO/IRQ] AUTO reusing startup HW capability=%s\n",
                  radioIntbCapability == RadioIntbCapability::Present ? "INTB" : "POLL");

  pinMode(slaveSelectPin, OUTPUT);
  digitalWrite(slaveSelectPin, HIGH);
#ifdef TFT_CS
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif
  // HSPI was started exactly once during setup(). External SI4684/TFT resets
  // do not reset the ESP32 SPI hosts, so begin() only reuses the live bus.
  DIAG_PRINTLN("[RADIO] dedicated HSPI/SPI2 ready; no SPI reinitialisation");

  si468x::HostInterface host;
  host.writeCommand = hostWriteCommand;
  host.readReply = hostReadReply;
  host.timeUs = hostTimeUs;
  host.idle = hostIdle;
  chip.setHost(host);
  chip.setWorkspace(chipWorkspace, CHIP_WORKSPACE_BYTES);
  chip.setStatusCallback(statusChanged);
  chip.setCtsPollIntervalUs(radioControlMode == RADIO_CTRL_INTB
                                ? RADIO_INTB_CTS_SAFETY_US
                                : 1000UL);
  chip.setIdleStatusPollIntervalUs(radioControlMode == RADIO_CTRL_INTB
                                       ? 50000UL
                                       : 20000UL);
  DIAG_PRINTF("[RADIO/BOOT] workspace=%u chunk=%u CTS bootstrap=%s\n",
                static_cast<unsigned>(CHIP_WORKSPACE_BYTES),
                static_cast<unsigned>(CHIP_HOST_LOAD_PAYLOAD),
                controlModeName());

  // Check which image is already running before POWER_UP. This is important on
  // this PCB because resetting/reflashing the ESP32 does not necessarily reset
  // the Si4684. POWER_UP is a bootloader/startup command and is rejected by an
  // already running DAB/FM application image.
  delay(5);
  si468x::SystemState preState;
  DIAG_PRINTLN("[RADIO] PRECHECK GET_SYS_STATE before POWER_UP");
  si468x::Result preResult = chip.getSystemState(preState, 100000UL);
  DIAG_PRINTF("[RADIO] PRECHECK result=%d image=%u status0=0x%02X status3=0x%02X\n",
                static_cast<int>(preResult),
                preResult == si468x::Result::Ok ? static_cast<unsigned>(preState.image) : 255U,
                preResult == si468x::Result::Ok ? static_cast<unsigned>(preState.status.status0) : static_cast<unsigned>(lastStatus0),
                preResult == si468x::Result::Ok ? static_cast<unsigned>(preState.status.status3) : 0U);

  const si468x::Image expected = requestedMode == RADIO_MODE_FM ? si468x::Image::FMHD : si468x::Image::DAB;
  const bool reuseRunningImage =
      preResult == si468x::Result::Ok && preState.image == expected;

  // Do not touch shared GPIO17 when the requested application is already
  // running; this preserves the TFT controller state. A Si4684 STARTUP image
  // may continue through POWER_UP + HOST_LOAD + BOOT. If the opposite runtime
  // application is active, refuse here because mode switching also resets TFT.
  if (reuseRunningImage) {
    if (configuredGpio12Mode == GPIO12_AUTO &&
        radioIntbCapability == RadioIntbCapability::Unknown) {
      DIAG_PRINTLN("[RADIO/IRQ] running image without startup POWER_UP; INTB detection inconclusive");
      return false;
    }
    DIAG_PRINTF("[RADIO] active image already matches requested=%u - REUSE, no POWER_UP/upload\n",
                  static_cast<unsigned>(expected));
  } else if (preResult == si468x::Result::Ok &&
             (preState.image == si468x::Image::DAB || preState.image == si468x::Image::FMHD)) {
    DIAG_PRINTF("[RADIO] opposite active application image=%u requested=%u - refusing shared reset\n",
                  static_cast<unsigned>(preState.image), static_cast<unsigned>(expected));
    return false;
  } else {
    DIAG_PRINTF("[RADIO] startup/bootloader state image=%u - boot sequence required\n",
                  preResult == si468x::Result::Ok ? static_cast<unsigned>(preState.image) : 255U);
  }

  si468x::PowerUpConfig power;
  // During the first ESP32 boot DETECT is a real hybrid IRQ+poll mode. The
  // FALLING edge both proves the physical connection and wakes the same
  // generic command engine that completes POWER_UP.
  power.ctsInterruptEnable = true;
  power.clockMode = 1;
  power.trSize = 7;
  power.iBias = 0x48;
  power.crystalFrequencyHz = 19200000UL;
  power.cTune = 0x1F;
  power.iBiasRun = 0x18;

  // Select the firmware image for the requested radio mode.
  // Reset handling, TFT recovery, patch and SPI timing are mode-independent.
  const uint8_t* image =
      requestedMode == RADIO_MODE_FM ? si468x_fmhd_5_3_3 : si468x_dab_6_0_9;
  const uint32_t imageSize =
      requestedMode == RADIO_MODE_FM ? si468x_fmhd_5_3_3_size : si468x_dab_6_0_9_size;
  si468x::Result result = si468x::Result::Ok;
  uint32_t patchUploadMs = 0;
  uint32_t firmwareUploadMs = 0;
  if (!reuseRunningImage) {
    DIAG_PRINTF("[RADIO] bootHostImage start: patch=%u B image=%s %u B\n",
                  static_cast<unsigned>(sizeof(rom_patch_016)),
                  requestedMode == RADIO_MODE_FM ? "FMHD 5.3.3" : "DAB 6.0.9",
                  static_cast<unsigned>(imageSize));
    const uint32_t bootStartMs = millis();

    int intbBeforePowerUp = digitalRead(SI4684_INTB_PIN);
    uint32_t powerUpStartChanges = radioIntbEdges();
    uint32_t powerUpStartFall = radioIntbFallingEdges();
    uint32_t powerUpStartRise = radioIntbRisingEdges();
    if (configuredGpio12Mode == GPIO12_AUTO &&
        radioIntbCapability == RadioIntbCapability::Unknown) {
      // The proof must belong to this POWER_UP, not to the pre-flight status
      // query or to a stale level left by an earlier device state.
      __atomic_store_n(&radioIntbPending, false, __ATOMIC_RELEASE);
      __atomic_store_n(&radioIntbEdgeCount, 0U, __ATOMIC_RELEASE);
      __atomic_store_n(&radioIntbFallingCount, 0U, __ATOMIC_RELEASE);
      __atomic_store_n(&radioIntbRisingCount, 0U, __ATOMIC_RELEASE);
      radioPowerUpCtsViaIrq = false;
      intbBeforePowerUp = digitalRead(SI4684_INTB_PIN);
      powerUpStartChanges = 0;
      powerUpStartFall = 0;
      powerUpStartRise = 0;
      DIAG_PRINTF("[RADIO/IRQTEST] before POWER_UP INTB=%d changes=0 fall=0 rise=0\n",
                    intbBeforePowerUp);
    }
    result = chip.powerUp(power, 1000000UL);
    if (configuredGpio12Mode == GPIO12_AUTO &&
        radioIntbCapability == RadioIntbCapability::Unknown) {
      const int intbAfterPowerUp = digitalRead(SI4684_INTB_PIN);
      const uint32_t powerUpChanges = radioIntbEdges() - powerUpStartChanges;
      const uint32_t powerUpFall = radioIntbFallingEdges() - powerUpStartFall;
      const uint32_t powerUpRise = radioIntbRisingEdges() - powerUpStartRise;
      DIAG_PRINTF("[RADIO/IRQTEST] after POWER_UP result=%d status0=0x%02X INTB=%d changes=%u fall=%u rise=%u viaIrq=%u\n",
                    static_cast<int>(result), static_cast<unsigned>(lastStatus0),
                    intbAfterPowerUp, static_cast<unsigned>(powerUpChanges),
                    static_cast<unsigned>(powerUpFall),
                    static_cast<unsigned>(powerUpRise),
                    radioPowerUpCtsViaIrq ? 1U : 0U);

      if (result != si468x::Result::Ok) {
        DIAG_PRINTLN("[RADIO/IRQ] POWER_UP failed; INTB detection inconclusive");
      } else if (powerUpChanges > 0U) {
        radioIntbCapability = RadioIntbCapability::Present;
        radioControlMode = RADIO_CTRL_INTB;
        chip.setCtsPollIntervalUs(RADIO_INTB_CTS_SAFETY_US);
        chip.setIdleStatusPollIntervalUs(50000UL);
        DIAG_PRINTF("[RADIO/IRQTEST] POWER_UP transition observed before=%d after=%d changes=%u fall=%u rise=%u\n",
                      intbBeforePowerUp, intbAfterPowerUp,
                      static_cast<unsigned>(powerUpChanges),
                      static_cast<unsigned>(powerUpFall),
                      static_cast<unsigned>(powerUpRise));
        DIAG_PRINTF("[RADIO/IRQ] POWER_UP CTS via %s\n",
                      radioPowerUpCtsViaIrq ? "INTB" : "safety poll");
        DIAG_PRINTLN("[RADIO/IRQ] INTB connected; IRQ mode");
      } else {
        // Do NOT classify Absent here. Keep DETECT active into LOAD_INIT /
        // HOST_LOAD / BOOT. The first later transition promotes immediately to
        // INTB mode inside hostIdle(); only if the whole boot produces no
        // transition do we need the explicit application probe after BOOT.
        DIAG_PRINTLN("[RADIO/IRQTEST] POWER_UP produced no GPIO12 transition");
        DIAG_PRINTLN("[RADIO/IRQTEST] continuing DETECT into LOAD_INIT/HOST_LOAD/BOOT; first toggle will activate IRQ immediately");
      }
    }

    const uint32_t patchStartMs = millis();
    if (result == si468x::Result::Ok) result = chip.loadInit(1000000UL);
    if (result == si468x::Result::Ok)
      result = chip.hostLoadImage(
          progmemImageReader, const_cast<uint8_t*>(rom_patch_016),
          sizeof(rom_patch_016), 1000000UL);
    if (result == si468x::Result::Ok) delay(4);
    patchUploadMs = millis() - patchStartMs;

    const uint32_t firmwareStartMs = millis();
    if (result == si468x::Result::Ok) result = chip.loadInit(1000000UL);
    if (result == si468x::Result::Ok)
      result = chip.hostLoadImage(
          progmemImageReader, const_cast<uint8_t*>(image), imageSize,
          1000000UL);
    firmwareUploadMs = millis() - firmwareStartMs;
    if (result == si468x::Result::Ok) result = chip.boot(1000000UL);

    DIAG_PRINTF("[RADIO] bootHostImage result=%d elapsed=%u ms status0=0x%02X patchBytes=%u fwBytes=%u\n",
                  static_cast<int>(result),
                  static_cast<unsigned>(millis() - bootStartMs),
                  static_cast<unsigned>(lastStatus0),
                  static_cast<unsigned>(diagPhaseBytes[1]),
                  static_cast<unsigned>(diagPhaseBytes[2]));
    if (result != si468x::Result::Ok) {
      DIAG_PRINTLN("[RADIO] ERROR: bootHostImage failed");
      return false;
    }

  } else {
    DIAG_PRINTLN("[RADIO] bootHostImage skipped; preserving running image and TFT state");
  }

  // If POWER_UP itself was silent, DETECT stayed armed through the complete
  // bootloader transfer. hostIdle() promotes to INTB on the first transition,
  // so by this point capability is already Present whenever LOAD_INIT,
  // HOST_LOAD or BOOT generated a usable INTB event. Only a completely silent
  // boot reaches the deterministic application-firmware fallback probe below.
  radioBootIrqEdgeCount = radioIntbEdges();
  DIAG_PRINTF("[RADIO/IRQ] boot INTB edges=%u\n",
                static_cast<unsigned>(radioBootIrqEdgeCount));

  const bool applicationProbeNeeded =
      configuredGpio12Mode == GPIO12_AUTO &&
      radioIntbCapability == RadioIntbCapability::Unknown;
  const bool runtimeIntbRequested =
      configuredGpio12Mode != GPIO12_IR &&
      radioIntbCapability != RadioIntbCapability::Absent;
  const bool irPollingBootstrap = configuredGpio12Mode == GPIO12_IR;

  // GPIO12=IR must leave the Si4684 in exactly the same application-side
  // state as AUTO after a confirmed no-INTB fallback: audio outputs enabled,
  // physical INTB output disabled, CTS/status bootstrap performed by polling.
  // The only intended difference is ownership of ESP32 GPIO12 itself.
  const bool intbOutputEnabled = runtimeIntbRequested;

  // This block is reached in DETECT only if POWER_UP *and the entire
  // LOAD_INIT/HOST_LOAD/BOOT sequence* produced no transition. It is the final
  // fallback probe. The first application-side transition is again promoted
  // immediately by hostIdle(); old hardware with GPIO12 NC still cannot stall
  // because the 1 ms safety poll remains enabled.
  uint32_t appStartChanges = 0;
  uint32_t appStartFall = 0;
  uint32_t appStartRise = 0;
  int appIntbBefore = digitalRead(SI4684_INTB_PIN);
  if (applicationProbeNeeded) {
    radioControlMode = RADIO_CTRL_DETECT;
    setRadioIntbInterrupt(true, CHANGE);
    takeRadioIntb();
    appStartChanges = radioIntbEdges();
    appStartFall = radioIntbFallingEdges();
    appStartRise = radioIntbRisingEdges();
    appIntbBefore = digitalRead(SI4684_INTB_PIN);
    DIAG_PRINTF("[RADIO/IRQTEST] before APP bootstrap INTB=%d changes=%u fall=%u rise=%u\n",
                  appIntbBefore,
                  static_cast<unsigned>(appStartChanges),
                  static_cast<unsigned>(appStartFall),
                  static_cast<unsigned>(appStartRise));
  } else if (runtimeIntbRequested) {
    radioControlMode = RADIO_CTRL_POLL;
  }

  // PIN_CONFIG_ENABLE and INT_CTL_ENABLE are always protected by bounded
  // polling. In DETECT mode an actual IRQ can still win and complete the same
  // command through hostIdle()/notifyInterrupt().
  chip.setCtsPollIntervalUs(1000);
  chip.setIdleStatusPollIntervalUs(20000);

  // Cold DAB/FM boot: master -> format -> rate -> readback -> pins.
  // Reused image: readback only; never rewrite format after a possible tune.
  configureI2sAudio(reuseRunningImage);
  const uint16_t pinConfig = audioPinConfig(intbOutputEnabled);
  si468x::Result pinConfigResult = chip.setProperty(
      si468x::Property::PIN_CONFIG_ENABLE, pinConfig);
  DIAG_PRINTF("[RADIO/IRQ] runtime PIN_CONFIG=0x%04X result=%d\n",
                static_cast<unsigned>(pinConfig),
                static_cast<int>(pinConfigResult));

  si468x::Result runtimeCtsResult = si468x::Result::Ok;
  si468x::Result pendingStatusResult = si468x::Result::Ok;
  si468x::Status pendingStatus;
  if ((runtimeIntbRequested || irPollingBootstrap) &&
      pinConfigResult == si468x::Result::Ok) {
    runtimeCtsResult = chip.setInterruptEnable(si468x::INTERRUPT_CTS);
    DIAG_PRINTF("[RADIO/IRQ] runtime sources=0x%04X stage=%s result=%d\n",
                  static_cast<unsigned>(si468x::INTERRUPT_CTS),
                  irPollingBootstrap ? "IR-poll-bootstrap" : "bootstrap",
                  static_cast<int>(runtimeCtsResult));
    if (runtimeCtsResult == si468x::Result::Ok) {
      pendingStatusResult = chip.readStatus(pendingStatus);
      DIAG_PRINTF("[RADIO/IRQ] runtime pending status=0x%02X readResult=%d\n",
                    static_cast<unsigned>(pendingStatus.status0),
                    static_cast<int>(pendingStatusResult));
    }
  }

  if (pinConfigResult != si468x::Result::Ok) {
    DIAG_PRINTLN("[RADIO] ERROR: PIN_CONFIG_ENABLE failed");
    return false;
  }

  si468x::PartInfo part;
  si468x::SystemState state;
  bool partInfoAlreadyRead = false;

  if (applicationProbeNeeded) {
    if (runtimeCtsResult != si468x::Result::Ok ||
        pendingStatusResult != si468x::Result::Ok) {
      DIAG_PRINTLN("[RADIO/IRQTEST] application INTB bootstrap failed; detection inconclusive");
      return false;
    }

    // The bootstrap itself is part of the physical test: enabling INTBOUTEN or
    // CTSIEN may already cause a transition, and that is valid proof that the
    // Si4684 output reaches GPIO12. Clear only the pending host latch here; keep
    // the counters so all application-side transitions remain in the delta.
    takeRadioIntb();
    radioAppTestCtsViaIrq = false;
    const int appBeforeGetPart = digitalRead(SI4684_INTB_PIN);
    DIAG_PRINTF("[RADIO/IRQTEST] before APP GET_PART_INFO INTB=%d appChanges=%u fall=%u rise=%u\n",
                  appBeforeGetPart,
                  static_cast<unsigned>(radioIntbEdges() - appStartChanges),
                  static_cast<unsigned>(radioIntbFallingEdges() - appStartFall),
                  static_cast<unsigned>(radioIntbRisingEdges() - appStartRise));

    const si468x::Result appProbeResult = chip.getPartInfo(part);
    partInfoAlreadyRead = appProbeResult == si468x::Result::Ok;
    const int appIntbAfter = digitalRead(SI4684_INTB_PIN);
    const uint32_t appChanges = radioIntbEdges() - appStartChanges;
    const uint32_t appFall = radioIntbFallingEdges() - appStartFall;
    const uint32_t appRise = radioIntbRisingEdges() - appStartRise;

    DIAG_PRINTF("[RADIO/IRQTEST] after APP GET_PART_INFO result=%d status0=0x%02X INTB=%d changes=%u fall=%u rise=%u viaIrq=%u part=%u\n",
                  static_cast<int>(appProbeResult),
                  static_cast<unsigned>(lastStatus0), appIntbAfter,
                  static_cast<unsigned>(appChanges),
                  static_cast<unsigned>(appFall),
                  static_cast<unsigned>(appRise),
                  radioAppTestCtsViaIrq ? 1U : 0U,
                  partInfoAlreadyRead ? static_cast<unsigned>(part.partNumber) : 0U);

    if (appProbeResult != si468x::Result::Ok) {
      DIAG_PRINTLN("[RADIO/IRQTEST] application probe command failed; INTB detection inconclusive");
      return false;
    }

    if (appChanges > 0U) {
      radioIntbCapability = RadioIntbCapability::Present;
      radioControlMode = RADIO_CTRL_INTB;
      chip.setCtsPollIntervalUs(RADIO_INTB_CTS_SAFETY_US);
      chip.setIdleStatusPollIntervalUs(50000UL);
      DIAG_PRINTF("[RADIO/IRQTEST] application INTB transition observed before=%d after=%d changes=%u fall=%u rise=%u\n",
                    appIntbBefore, appIntbAfter,
                    static_cast<unsigned>(appChanges),
                    static_cast<unsigned>(appFall),
                    static_cast<unsigned>(appRise));
      DIAG_PRINTF("[RADIO/IRQTEST] APP GET_PART_INFO CTS serviced via %s\n",
                    radioAppTestCtsViaIrq ? "INTB" : "safety polling");
      DIAG_PRINTLN("[RADIO/IRQ] INTB connected; IRQ mode confirmed by application firmware");
    } else {
      radioIntbCapability = RadioIntbCapability::Absent;
      DIAG_PRINTF("[RADIO/IRQTEST] application firmware produced no GPIO12 transition INTB=%d->%d\n",
                    appIntbBefore, appIntbAfter);
      usePollingFallback("no application INTB transition");
      const uint16_t fallbackPins = audioPinConfig(false);
      pinConfigResult = chip.setProperty(
          si468x::Property::PIN_CONFIG_ENABLE, fallbackPins);
      DIAG_PRINTF("[RADIO/IRQ] runtime PIN_CONFIG=0x%04X after failed probe result=%d\n",
                    static_cast<unsigned>(fallbackPins),
                    static_cast<int>(pinConfigResult));
      if (pinConfigResult != si468x::Result::Ok) return false;
    }
  } else if (runtimeIntbRequested) {
    radioControlMode = RADIO_CTRL_INTB;
    if (runtimeCtsResult == si468x::Result::Ok &&
        pendingStatusResult == si468x::Result::Ok) {
      takeRadioIntb();
      chip.setCtsPollIntervalUs(RADIO_INTB_CTS_SAFETY_US);
      chip.setIdleStatusPollIntervalUs(50000UL);
      DIAG_PRINTLN("[RADIO/IRQ] runtime INTB armed");
    } else {
      useRuntimePollingFallback("runtime INTB bootstrap failed");
    }
  } else if (irPollingBootstrap) {
    if (runtimeCtsResult != si468x::Result::Ok ||
        pendingStatusResult != si468x::Result::Ok) {
      DIAG_PRINTLN("[RADIO/IRQ] ERROR: IR polling bootstrap failed");
      return false;
    }
    // Keep the same bounded polling values used by AUTO after INTB absence.
    radioControlMode = RADIO_CTRL_POLL;
    radioIntbCapability = RadioIntbCapability::Absent;
    chip.setCtsPollIntervalUs(1000UL);
    chip.setIdleStatusPollIntervalUs(20000UL);
    DIAG_PRINTLN("[RADIO/IRQ] IR polling bootstrap complete; AUTO fallback state matched");
  }

  // Start runtime edge telemetry after the one-shot application probe so its
  // diagnostic transitions are not counted as ordinary FM/DAB runtime IRQs.
  // Discard any diagnostic release-edge still latched in the host-side flag.
  takeRadioIntb();
  radioBootIrqEdgeCount = radioIntbEdges();

  if (!partInfoAlreadyRead) {
    DIAG_PRINTLN("[RADIO] GET_PART_INFO");
    result = chip.getPartInfo(part);
    DIAG_PRINTF("[RADIO] GET_PART_INFO result=%d part=%u status0=0x%02X\n",
                  static_cast<int>(result),
                  result == si468x::Result::Ok ? static_cast<unsigned>(part.partNumber) : 0U,
                  static_cast<unsigned>(lastStatus0));
  } else {
    result = si468x::Result::Ok;
    DIAG_PRINTF("[RADIO] GET_PART_INFO reused application probe part=%u status0=0x%02X\n",
                  static_cast<unsigned>(part.partNumber),
                  static_cast<unsigned>(lastStatus0));
  }
  if (result != si468x::Result::Ok || part.partNumber != 4684) {
    DIAG_PRINTLN("[RADIO] ERROR: GET_PART_INFO failed or part != 4684");
    return false;
  }
  snprintf(ChipType, sizeof(ChipType), "SI%u", part.partNumber);

  DIAG_PRINTLN("[RADIO] GET_SYS_STATE");
  result = chip.getSystemState(state);
  DIAG_PRINTF("[RADIO] GET_SYS_STATE result=%d image=%u status0=0x%02X\n",
                static_cast<int>(result),
                result == si468x::Result::Ok ? static_cast<unsigned>(state.image) : 0xFFU,
                static_cast<unsigned>(lastStatus0));
  if (result != si468x::Result::Ok) {
    DIAG_PRINTLN("[RADIO] ERROR: GET_SYS_STATE failed");
    return false;
  }
  if (state.image != expected) {
    DIAG_PRINTF("[RADIO] ERROR: wrong active image expected=%u actual=%u\n",
                  static_cast<unsigned>(expected), static_cast<unsigned>(state.image));
    return false;
  }
  activeMode = requestedMode;

  si468x::FunctionInfo functionInfo;
  DIAG_PRINTLN("[RADIO] GET_FUNC_INFO");
  result = chip.getFunctionInfo(functionInfo);
  DIAG_PRINTF("[RADIO] GET_FUNC_INFO result=%d status0=0x%02X\n",
                static_cast<int>(result), static_cast<unsigned>(lastStatus0));
  if (result != si468x::Result::Ok) {
    DIAG_PRINTLN("[RADIO] ERROR: GET_FUNC_INFO failed");
    return false;
  }
  snprintf(FirmwVersion, sizeof(FirmwVersion), "%u.%u.%u",
           functionInfo.major, functionInfo.minor, functionInfo.build);
  DIAG_PRINTF("[RADIO] Si%u image=%s firmware=%s\n", part.partNumber,
                requestedMode == RADIO_MODE_FM ? "FMHD" : "DAB", FirmwVersion);

  DIAG_PRINTLN("[RADIO] shared properties begin");
  // Shared audio/front-end setup retained from the proven DAB configuration.
  Set_Property(0x1710, 0xFC4A);
  Set_Property(0x1711, 0x00F8);
  DIAG_PRINTLN("[RADIO] shared properties done");

  if (requestedMode == RADIO_MODE_DAB) {
    DIAG_PRINTLN("[RADIO] DAB frequency list + generic properties");
    uint32_t frequencies[38];
    for (uint8_t i = 0; i < 38; ++i)
      frequencies[i] = DABfrequencyTable_DAB[i].frequency;
    const si468x::Result frequencyListResult =
        chip.dabSetFrequencyList(frequencies, 38);

    const si468x::Result dsrvSourceResult =
        chip.setDigitalServiceInterruptSource(
            si468x::DSRV_INTERRUPT_PACKET_READY |
            si468x::DSRV_INTERRUPT_OVERFLOW);
    const si468x::Result dsrvRepeatResult =
        chip.setInterruptRepeat(si468x::INTERRUPT_DSRV);
    const si468x::Result dabEventSourceResult = chip.setProperty(
        si468x::Property::DAB_EVENT_INTERRUPT_SOURCE,
        RADIO_DAB_EVENT_SERVICE_LIST |
            RADIO_DAB_EVENT_RECONFIGURATION);
    Set_Property(static_cast<uint16_t>(si468x::Property::DIGITAL_SERVICE_RESTART_DELAY), 0x0064);
    Set_Property(static_cast<uint16_t>(si468x::Property::DAB_VALID_RSSI_TIME), 0x0000);
    Set_Property(static_cast<uint16_t>(si468x::Property::DAB_VALID_RSSI_THRESHOLD), 0x0080);
    Set_Property(static_cast<uint16_t>(si468x::Property::DAB_EVENT_MIN_SVRLIST_PERIOD), 0x0000);
    Set_Property(static_cast<uint16_t>(si468x::Property::DAB_EVENT_MIN_SVRLIST_PERIOD_RECONFIG), 0x0000);
    Set_Property(static_cast<uint16_t>(si468x::Property::DAB_EVENT_MIN_FREQINFO_PERIOD), 0x0000);
    configureDabXpad();
    Set_Property(static_cast<uint16_t>(si468x::Property::DAB_DRC_OPTION), 0x0002);
    Set_Property(static_cast<uint16_t>(si468x::Property::DAB_CTRL_DAB_MUTE_ENABLE), 0x0000);
    const uint16_t interruptSources =
        si468x::INTERRUPT_CTS | si468x::INTERRUPT_STC |
        si468x::INTERRUPT_DSRV | si468x::INTERRUPT_DEVICE_EVENT;
    const si468x::Result irqResult = chip.setInterruptEnable(interruptSources);
    si468x::Status irqStatus;
    const si468x::Result irqStatusResult =
        irqResult == si468x::Result::Ok
            ? chip.readStatus(irqStatus)
            : irqResult;
    takeRadioIntb();
    DIAG_PRINTF("[RADIO/IRQ] DAB freq=%d sources=0x%04X repeat=0x%04X dsrv=0x%04X event=0x%04X irq=%d read=%d\n",
                  static_cast<int>(frequencyListResult),
                  static_cast<unsigned>(interruptSources),
                  static_cast<unsigned>(si468x::INTERRUPT_DSRV),
                  static_cast<unsigned>(si468x::DSRV_INTERRUPT_PACKET_READY |
                                        si468x::DSRV_INTERRUPT_OVERFLOW),
                  static_cast<unsigned>(RADIO_DAB_EVENT_SERVICE_LIST |
                                        RADIO_DAB_EVENT_RECONFIGURATION),
                  static_cast<int>(irqResult),
                  static_cast<int>(irqStatusResult));
    if (frequencyListResult != si468x::Result::Ok ||
        dsrvSourceResult != si468x::Result::Ok ||
        dsrvRepeatResult != si468x::Result::Ok ||
        dabEventSourceResult != si468x::Result::Ok ||
        irqResult != si468x::Result::Ok ||
        irqStatusResult != si468x::Result::Ok)
      useRuntimePollingFallback("DAB interrupt-source configuration failed");
  } else {
    DIAG_PRINTLN("[RADIO] FM band/RDS properties");
    applyFmRegionProperties();
    Set_Property(0x3200, 20);    // max tune error
    Set_Property(0x3C00, 0x001B);
    Set_Property(0x3C01, 1);
    Set_Property(0x3C02, 0x0051); // RDS enabled, conservative BLE thresholds
    const uint16_t interruptSources =
        si468x::INTERRUPT_CTS | si468x::INTERRUPT_STC |
        si468x::INTERRUPT_ACF | si468x::INTERRUPT_RDS |
        si468x::INTERRUPT_RSQ | si468x::INTERRUPT_DEVICE_EVENT;
    const si468x::Result irqResult = chip.setInterruptEnable(interruptSources);
    si468x::Status irqStatus;
    const si468x::Result irqStatusResult =
        irqResult == si468x::Result::Ok
            ? chip.readStatus(irqStatus)
            : irqResult;
    takeRadioIntb();
    DIAG_PRINTF("[RADIO/IRQ] runtime sources=0x%04X mode=FM result=%d read=%d\n",
                  static_cast<unsigned>(interruptSources),
                  static_cast<int>(irqResult),
                  static_cast<int>(irqStatusResult));
    if (irqResult != si468x::Result::Ok ||
        irqStatusResult != si468x::Result::Ok)
      useRuntimePollingFallback("FM interrupt-source configuration failed");
    clearFmData();
  }

  clearData();
  // No boot/configuration command may leak diagnostic state into FM runtime.
  commandAwaitingCts = false;
  commandStatusReadTriggeredByIrq = false;
  radioRuntimeDiagnosticsActive = true;
  diagFmLastReportMs = millis();
  diagDabLastReportMs = millis();
  radioDabRuntimeActive = requestedMode == RADIO_MODE_DAB;
  DIAG_PRINTF("[RADIO/BOOT] patch=%u ms firmware=%u ms total=%u ms mode=%s irqEdges=%u workspace=%u chunk=%u\n",
                static_cast<unsigned>(patchUploadMs),
                static_cast<unsigned>(firmwareUploadMs),
                static_cast<unsigned>(millis() - radioBeginMs),
                controlModeName(), static_cast<unsigned>(radioIntbEdges()),
                static_cast<unsigned>(CHIP_WORKSPACE_BYTES),
                static_cast<unsigned>(CHIP_HOST_LOAD_PAYLOAD));
  DIAG_PRINTF("[RADIO] begin SUCCESS mode=%s firmware=%s heap=%u min=%u\n",
                requestedMode == RADIO_MODE_FM ? "FM" : "DAB",
                FirmwVersion, ESP.getFreeHeap(), ESP.getMinFreeHeap());
  return true;
}

// Boot-only, after DAB image verification. The existing bounded executeCommand
// API owns CTS/IRQ and finishes each transaction before the next is issued.
// Never call this from Update()/the live command scheduler.
void DAB::configureDabXpad(void) {
  dabXpad = dab_diag::XpadReadback{};
  const si468x::Result setResult = chip.setProperty(
      si468x::Property::DAB_XPAD_ENABLE, dab_diag::XPAD_REQUESTED);
  finishCommandDiagnostics(setResult);
  dabXpad.setResult = static_cast<int8_t>(setResult);
  if (setResult == si468x::Result::Ok) {
    uint16_t value = 0;
    const si468x::Result getResult = chip.getProperty(
        si468x::Property::DAB_XPAD_ENABLE, value, 250000UL);
    finishCommandDiagnostics(getResult);
    dabXpad.getResult = static_cast<int8_t>(getResult);
    if (getResult == si468x::Result::Ok) {
      dabXpad.value = value;
      dabXpad.valid = true;
    }
  }
  dabXpadLogPending = true;
}

void DAB::onHardwareResetComplete(void) {
  dabXpad = dab_diag::XpadReadback{};
  dabXpadLogPending = false;
  dabRawUaLog = dab_diag::RawLog{};
  dabServiceLabelValid = false;
  epgContinuous = epgManualPending = epgProductionPaused = false;
  epg::liveReset();
  dabSpiProbe = dab_probe::Probe{};
  const si468x::Result aborted = chip.abortCommand();
  dabGeneration = dab_scheduler::nextGeneration(dabGeneration);
  dabCommandGeneration = dabGeneration;
  dabCommand = DabCommand::None;
  dabSwitch.reset();
  dabTuneRequestPending = false;
  dabServiceRequestPending = false;
  dabActiveServiceValid = false;
  dabActiveDataServiceValid = false;
  dabUaLogPending = false;
  dabUaInventoryValid = false;
  dabUaListVersion = 0;
  dabUaServiceCount = 0;
  dabUaComponentCount = 0;
  dabUaScanIndex = 0;
  resetDabSpiDiscovery();
  dabWaitingForStc = false;
  dabTuneBusyRetries = 0;
  dabTuneRetryNotBeforeMs = 0;
  dabConsecutiveCtsTimeouts = 0;
  dabTransportStalled = false;
  invalidateDabAudioInfo();
  dabStcPending = false;
  dabDsrvPending = false;
  dabDeviceEventPending = false;
  radioDabRuntimeActive = false;
  commandAwaitingCts = false;
  commandStatusReadTriggeredByIrq = false;
  __atomic_store_n(&radioIntbPending, false, __ATOMIC_RELEASE);
  DIAG_PRINTF("[RADIO/RESET] physical reset complete hostAbort=%d generation=%u\n",
                static_cast<int>(aborted),
                static_cast<unsigned>(dabGeneration));
}

void DAB::resetDabPeriodicDeadlines(uint32_t now) {
  dabSignalNextDueMs = now + dab_scheduler::DAB_SIGNAL_INTERVAL_MS;
  dabEnsembleNextDueMs = now + dab_scheduler::DAB_ENSEMBLE_INITIAL_PHASE_MS;
  dabTimeNextDueMs = now + dab_scheduler::DAB_TIME_INITIAL_PHASE_MS;
  resetDabServiceDeadlines(now);
  dabLastLowPriorityCommandMs = now - dab_scheduler::DAB_LOW_PRIORITY_GAP_MS;
}

void DAB::resetDabServiceDeadlines(uint32_t now) {
  dabAudioNextDueMs = now + dab_scheduler::DAB_AUDIO_INITIAL_PHASE_MS;
  dabCurrentServiceNextDueMs = now + dab_scheduler::DAB_SERVICE_INITIAL_PHASE_MS;
  dabCurrentSubchannelNextDueMs =
      now + dab_scheduler::DAB_SUBCHANNEL_INITIAL_PHASE_MS;
}

void DAB::invalidateDabAudioInfo(uint32_t notBeforeMs) {
  dabAudioInfoValid = false;
  dabAudioInfoRetryCount = 0;
  dabAudioInfoNotBeforeMs = notBeforeMs;
  dabAudioRefreshPending = false;
  bitrate = 0;
  samplerate = 0;
  audiomode = 4;
  servicetype = 9;
  pty = 36;
  protectionlevel = 0;
}

dab_scheduler::CtsTimeoutClass DAB::recordDabCtsResult(
    bool timedOut) {
  const dab_scheduler::CtsTimeoutClass classification =
      dab_scheduler::classifyCtsTimeout(timedOut, chip.lastServiceGapUs());
  if (classification == dab_scheduler::CtsTimeoutClass::HostStarved) {
    ++diagDabHostStarvationCount;
    if (chip.lastServiceGapUs() > diagDabHostStarvationMaxGapUs)
      diagDabHostStarvationMaxGapUs = chip.lastServiceGapUs();
  } else if (classification == dab_scheduler::CtsTimeoutClass::Genuine) {
    ++diagDabCtsTimeoutCount;
    ++diagDabGenuineCtsTimeoutCount;
  }
  dabConsecutiveCtsTimeouts = dab_scheduler::nextConsecutiveCtsTimeouts(
      classification, dabConsecutiveCtsTimeouts);
  if (dab_scheduler::ctsRecoveryRequired(
          dabConsecutiveCtsTimeouts, RADIO_DAB_STALL_TIMEOUT_COUNT) &&
      !dabTransportStalled) {
    dabTransportStalled = true;
    ++diagDabRecoveryRequestCount;
    DIAG_PRINTF("[DAB/STALL] %u consecutive genuine CTS timeouts; recovery requested\n",
                static_cast<unsigned>(dabConsecutiveCtsTimeouts));
  }
  return classification;
}

void DAB::updateDabPeriodicRequests(uint32_t now) {
  if (dab_scheduler::takePeriodicDeadline(
          now, dabSignalNextDueMs,
          dab_scheduler::DAB_SIGNAL_INTERVAL_MS))
    dabSignalRefreshPending = true;
  if (signallock && dab_scheduler::takePeriodicDeadline(
                        now, dabEnsembleNextDueMs,
                        dab_scheduler::DAB_ENSEMBLE_INTERVAL_MS))
    dabEnsembleRefreshPending = true;
  if (signallock && dab_scheduler::takePeriodicDeadline(
                        now, dabTimeNextDueMs,
                        dab_scheduler::DAB_TIME_INTERVAL_MS))
    dabTimeRefreshPending = true;
  if (dabActiveServiceValid && dab_scheduler::takePeriodicDeadline(
                                   now, dabAudioNextDueMs,
                                   dab_scheduler::DAB_AUDIO_INTERVAL_MS))
    dabAudioRefreshPending = true;
  if (dabActiveServiceValid && dab_scheduler::takePeriodicDeadline(
                                   now, dabCurrentServiceNextDueMs,
                                   dab_scheduler::DAB_SERVICE_INTERVAL_MS))
    dabCurrentServiceRefreshPending = true;
  if (dabActiveServiceValid && dab_scheduler::takePeriodicDeadline(
                                   now, dabCurrentSubchannelNextDueMs,
                                   dab_scheduler::DAB_SUBCHANNEL_INTERVAL_MS))
    dabCurrentSubchannelRefreshPending = true;
}

// Queue a cooperative ensemble refresh. The scheduler starts the actual
// command after higher-priority transport and RF work.
void DAB::EnsembleInfo(void) {
  dabEnsembleRefreshPending = true;
}

void DAB::parseDabServiceListReply(uint16_t replyLength) {
  // SPIbuffer[0] is the legacy command prefix; reply/status starts at [1].
  // The list SIZE is therefore [5], with the first service at [13].
  dab_inventory::Header inventory;
  if (replyLength < 4U || !dab_inventory::validate(
          SPIbuffer + 5, replyLength - 4U, sizeof(service) / sizeof(service[0]),
          sizeof(dabUaComponents) / sizeof(dabUaComponents[0]), inventory)) {
    invalidateDabInventory();
    return;
  }
  const uint8_t parsedServices = inventory.services;

  // Parse transactionally. A malformed/provisional reply must not erase the
  // last structurally valid list that the UI or full scan can still use.
  DABService parsed[32] = {};
  uint32_t parsedServiceId[32] = {0};
  uint8_t parsedServiceCharset[32] = {0};
  uint16_t offset = 13U;
  for (uint8_t i = 0; i < parsedServices; ++i) {
    if (static_cast<uint32_t>(offset) + 24U > replyLength + 1U) return;

    serviceID = static_cast<uint32_t>(SPIbuffer[offset]) |
                (static_cast<uint32_t>(SPIbuffer[offset + 1]) << 8) |
                (static_cast<uint32_t>(SPIbuffer[offset + 2]) << 16) |
                (static_cast<uint32_t>(SPIbuffer[offset + 3]) << 24);
    const uint8_t numberOfComponents = SPIbuffer[offset + 5] & 0x0FU;
    if (numberOfComponents >= 15U) return;  // reserved by AN649
    const uint8_t serviceCharset = SPIbuffer[offset + 6] & 0x0FU;
    memcpy(parsed[i].Label, &SPIbuffer[offset + 8], 16);
    parsed[i].Label[16] = '\0';
    for (int8_t j = 15; j >= 0 && parsed[i].Label[j] == ' '; --j)
      parsed[i].Label[j] = '\0';
    offset = static_cast<uint16_t>(offset + 24U);

    componentID = 0;
    for (uint8_t j = 0; j < numberOfComponents; ++j) {
      if (static_cast<uint32_t>(offset) + 4U > replyLength + 1U) return;
      if (j == 0) {
        componentID = static_cast<uint32_t>(SPIbuffer[offset]) |
                      (static_cast<uint32_t>(SPIbuffer[offset + 1]) << 8) |
                      (static_cast<uint32_t>(SPIbuffer[offset + 2]) << 16) |
                      (static_cast<uint32_t>(SPIbuffer[offset + 3]) << 24);
      }
      offset = static_cast<uint16_t>(offset + 4U);
    }
    parsed[i].ServiceID = serviceID;
    parsed[i].CompID = componentID;
    // Unknown until the cooperative DAB_GET_SUBCHAN_INFO pass classifies it.
    // Zero is a valid audio mode and must never be used as an "unknown" value.
    parsed[i].ServiceType = 8;
    // Service Info 3 low nibble is SlCharset for this specific service.
    // Remember the pre-sort ServiceID so the charset can be realigned after
    // qsort() without growing the DABService structure or static DRAM.
    parsedServiceId[i] = serviceID;
    parsedServiceCharset[i] = serviceCharset;

    // Some ensembles briefly publish zero-filled labels while rebuilding an
    // otherwise identical list. Preserve the last good label for the exact
    // SID/CID pair; a real newer label still wins immediately.
    if (!dab_scheduler::labelHasContent(parsed[i].Label)) {
      for (uint8_t old = 0; old < numberofservices; ++old) {
        if (service[old].ServiceID == serviceID &&
            service[old].CompID == componentID &&
            dab_scheduler::labelHasContent(service[old].Label)) {
          memcpy(parsed[i].Label, service[old].Label,
                 sizeof(parsed[i].Label));
          parsedServiceCharset[i] = dabServiceCharsetValue[old];
          break;
        }
      }
    }
  }

  // Reaching this point means the complete list reply was structurally valid.
  // Capture every component only after validation, without changing the audio
  // station table or its ordering. The loops above proved all read bounds.
  // Avoid restarting a dump when a periodic list reply has identical topology.
  uint16_t uaOffset = 13U;
  uint16_t uaComponent = 0U;
  bool uaChanged = !dabServiceListReady || !dabUaScan.enumerated ||
                   !dabUaInventoryValid || dabUaListVersion != inventory.version ||
                   dabUaServiceCount != parsedServices;
  for (uint8_t i = 0; i < parsedServices; ++i) {
    const uint32_t sid = si468x::readLe32(SPIbuffer + uaOffset);
    const uint8_t count = SPIbuffer[uaOffset + 5U] & 0x0FU;
    uaChanged = uaChanged || dabUaServices[i].sid != sid ||
                dabUaServices[i].count != count ||
                dabUaServices[i].info1 != SPIbuffer[uaOffset + 4U];
    dabUaServices[i].sid = sid;
    dabUaServices[i].first = uaComponent;
    dabUaServices[i].count = count;
    dabUaServices[i].info1 = SPIbuffer[uaOffset + 4U];
    uaOffset += 24U;
    if (!dab_spi::retainComponents(dabUaComponents,
            sizeof(dabUaComponents) / sizeof(dabUaComponents[0]), uaComponent,
            SPIbuffer + uaOffset, replyLength + 1U - uaOffset, count, uaChanged)) {
      invalidateDabInventory();
      return;
    }
    uaComponent += count;
    uaOffset += 4U * count;
  }
  uaChanged = uaChanged || dabUaComponentCount != uaComponent;
  dabUaListVersion = inventory.version;
  dabUaInventoryValid = true;
  dabUaServiceCount = parsedServices;
  dabUaComponentCount = uaComponent;
  dabServiceListReady = true;
  if (uaChanged) {
    dabSpiProbe.stop(millis());
    dabUaScanIndex = 0U;
    dabUaLogPending = false;
    resetDabSpiDiscovery();
  }
  // A valid list may legitimately contain zero services; that still resolves
  // a pending startup/MEM restore attempt deterministically.
  numberofservices = parsedServices;
  memset(service, 0, sizeof(service));
  memset(dabServiceCharsetValue, 0, sizeof(dabServiceCharsetValue));
  if (numberofservices != 0U)
    memcpy(service, parsed, numberofservices * sizeof(DABService));
  dabServiceListReady = true;
  dabServiceListGeneration =
      dab_scheduler::nextGeneration(dabServiceListGeneration);
  if (numberofservices == 0) return;
  qsort(service, numberofservices, sizeof(DABService), compareCompID);
  for (uint8_t sorted = 0; sorted < numberofservices; ++sorted) {
    for (uint8_t original = 0; original < numberofservices; ++original) {
      if (service[sorted].ServiceID == parsedServiceId[original] &&
          service[sorted].CompID == parsed[original].CompID) {
        dabServiceCharsetValue[sorted] = parsedServiceCharset[original];
        break;
      }
    }
  }
  if (ServiceIndex >= numberofservices) ServiceIndex = 0;
  const bool componentKnown = dabServiceRequestPending || dabActiveServiceValid;
  const uint32_t currentComponent = dabServiceRequestPending
      ? dabRequestedComponentId : dabActiveComponentId;
  if (CurrentServiceID != service[ServiceIndex].ServiceID ||
      (componentKnown && currentComponent != service[ServiceIndex].CompID)) {
    for (uint8_t i = 0; i < numberofservices; ++i) {
      if (CurrentServiceID == service[i].ServiceID &&
          (!componentKnown || currentComponent == service[i].CompID)) {
        ServiceIndex = i;
        break;
      }
    }
  }
}

// Pull one chunk of service-data from the chip. Each chunk is either:
//   - Dynamic Label / Radiotext (group flag 0x02) → copy into ServiceData
//   - MOT slideshow header     (0x80 0x00 0x12 ...) → record total length, TID
//   - MOT slideshow segment    (0x00/0x80 ...   ) → memcpy into slideshowSegBuf
// Called on every Update() while the receiver has signal lock.
void DAB::getServiceData(void) {
  uint32_t byte_count = 0;
  const auto discardStaleMot = []() {
    ++diagDabStaleMotCount;
    static uint32_t lastLogMs = 0U;
    const uint32_t now = millis();
    if (diagnosticDebug &&
        (lastLogMs == 0U || static_cast<uint32_t>(now - lastLogMs) >= 5000U)) {
      lastLogMs = now;
      DIAG_PRINTF("[SLS] stale service/TID discarded total=%u\n",
                  static_cast<unsigned>(diagDabStaleMotCount));
    }
  };
  static dab_scheduler::RepeatedEventThrottle repeatedCompletedMotLog;
  const auto logRepeatedCompletedMot = [this](uint16_t transportId) {
    if (!SlideShowDebug || diagSerial.availableForWrite() < 80) return;
    uint16_t suppressed = 0;
    if (!dab_scheduler::shouldLogRepeatedEvent(
            repeatedCompletedMotLog, transportId, millis(), SlideShowVerbose,
            5000U, suppressed))
      return;
    if (suppressed != 0U)
      DIAG_PRINTF("[SLS] Ignore repeated completed TID=%u suppressed=%u\n",
                  transportId, static_cast<unsigned>(suppressed));
    else
      DIAG_PRINTF("[SLS] Ignore repeated completed TID=%u\n", transportId);
  };

  // finishDabCommand() has already fetched and parsed the fixed 24-byte DSRV
  // header into SPIbuffer[1..24]. Re-read the preserved reply at full length
  // only when its payload fits the shared 4 KiB buffer.
  byte_count = SPIbuffer[19] + (static_cast<uint32_t>(SPIbuffer[20]) << 8);
  if (byte_count + 24U < sizeof(SPIbuffer)) {
    if (byte_count > 0) {
      const si468x::Result readResult = chip.readCurrentReply(
          SPIbuffer + 1, static_cast<uint16_t>(byte_count + 24U));
      finishCommandDiagnostics(readResult);
      if (readResult == si468x::Result::Ok) {
        byte_count = SPIbuffer[19] + (SPIbuffer[20] << 8);

        // Read DAB Dynamic Label (DLS). Si468x exposes the two DLS prefix bytes
        // at PAYLOAD[0..1] and the character field from PAYLOAD+2. BYTE_COUNT
        // covers the complete DSRV payload, so only BYTE_COUNT-2 bytes belong to
        // the displayed text; X-PAD reassembly is not performed on this payload.
        if (((SPIbuffer[8] >> 6) & 0x03) == 0x02 && byte_count >= 2U) {
          const uint8_t dlsPrefix0 = SPIbuffer[25];

          if ((dlsPrefix0 & 0x10U) == 0U) {
            // For a normal Dynamic Label, the high nibble of prefix byte 1 is
            // the character-set identifier. Some Si468x software reports 0x04
            // for UCS-2; normalize it to the registered 0x06 decoder path.
            uint8_t charset = static_cast<uint8_t>((SPIbuffer[26] >> 4) & 0x0FU);
            if (charset == 0x04U) charset = 0x06U;
            dabDynamicLabelCharset = charset;

            size_t copyLength = static_cast<size_t>(byte_count - 2U);
            if (copyLength > sizeof(ServiceData)) copyLength = sizeof(ServiceData);
            memcpy(ServiceData, SPIbuffer + 27, copyLength);

            // Keep an explicit byte length for UCS-2/UTF-16BE and discard only
            // trailing NUL padding. Copy exactly BYTE_COUNT-2 bytes so data beyond
            // the DLS payload cannot appear as residual glyphs.
            while (copyLength > 0U && ServiceData[copyLength - 1U] == '\0')
              --copyLength;
            dabDynamicLabelLength = static_cast<uint8_t>(copyLength);
            if (copyLength < sizeof(ServiceData)) ServiceData[copyLength] = '\0';
          } else if ((dlsPrefix0 & 0x0FU) == 0x01U) {
            // DLS command 1 removes the currently displayed Dynamic Label.
            memset(ServiceData, 0, sizeof(ServiceData));
            dabDynamicLabelLength = 0;
          }

          // MOT header. The collector identity is TransportId; BodySize is
          // only a completeness/integrity constraint.
        } else if (((SPIbuffer[8] >> 6) & 0x03) == 0x01 &&
                   byte_count >= 13U && byte_count < 200U &&
                   SPIbuffer[27] == 0x80 && SPIbuffer[28] == 0x00 &&
                   SPIbuffer[29] == 0x12) {
          const uint16_t transportID =
              (static_cast<uint16_t>(SPIbuffer[30]) << 8) | SPIbuffer[31];
          const uint32_t newLength =
              (static_cast<uint32_t>(SPIbuffer[34]) << 20) |
              (static_cast<uint32_t>(SPIbuffer[35]) << 12) |
               (static_cast<uint32_t>(SPIbuffer[36]) << 4) |
               (static_cast<uint32_t>(SPIbuffer[37]) >> 4);

          if (!dabSwitch.slsContextValid) {
            discardStaleMot();
          } else if (newLength == 0U || newLength > SLS_BUFFER_BYTES) {
            if (SlideShowDebug)
              DIAG_PRINTF("[SLS] Reject header TID=%u length=%u\n",
                            transportID, newLength);
          } else if (lastCompletedTransportIdValid &&
                     transportID == lastCompletedTransportId) {
            logRepeatedCompletedMot(transportID);
          } else if (slideshowPublishedPending || SlideShowUpdate) {
            // The only image-sized buffer is still owned by the UI. DSRV is
            // deliberately drained, but no new object may claim the buffer.
            if (SlideShowDebug)
              DIAG_PRINTF("[SLS] Hold published image; ignore header TID=%u\n",
                            transportID);
          } else {
            if (SlideShowTransportIDValid &&
                transportID != SlideShowTransportID) {
              if (SlideShowDebug)
                DIAG_PRINTF("[SLS] Partial TID=%u abandoned; new header TID=%u\n",
                              SlideShowTransportID, transportID);
              lockSlideshowTransport(transportID);
            } else if (!SlideShowTransportIDValid) {
              lockSlideshowTransport(transportID);
            }

            bool metadataChanged = SlideShowLength == 0U;
            if (SlideShowLength != 0U && SlideShowLength != newLength) {
              // Conflicting metadata for the same TID is corruption. Start the
              // same TID afresh using the newest complete header.
              if (SlideShowDebug)
                DIAG_PRINTF("[SLS] TID=%u BodySize changed %u -> %u; reset\n",
                              transportID, SlideShowLength, newLength);
              lockSlideshowTransport(transportID);
              metadataChanged = true;
            }

            SlideShowLength = newLength;
            SlideShowInit = true;
            if (metadataChanged || SlideShowLastActivity == 0U)
              SlideShowLastActivity = millis();
            if (SlideShowDebug)
              DIAG_PRINTF("[SLS] Header TID=%u length=%u bytes=%u\n",
                            transportID, SlideShowLength,
                            SlideShowByteCounter);

            if (SlideShowByteCounter == SlideShowLength &&
                allSegmentsReceived()) {
              if (SlideShowTotalSegments == 0U)
                SlideShowTotalSegments =
                    static_cast<uint16_t>(SlideShowHighestSegment) + 1U;
              assembleSlideshow();
            } else if (SlideShowByteCounter > SlideShowLength) {
              if (SlideShowDebug)
                DIAG_PRINTLN("[SLS] Buffered bytes exceed BodySize; reset");
              resetSlideshowCollector();
            }
          }

          // MOT body segment. SPIbuffer[27] bit 7 is the last-segment flag in
          // the DSRV data-group prefix already used by this parser.
        } else if (((SPIbuffer[8] >> 6) & 0x03) == 0x01 &&
                   byte_count > 11U &&
                   (SPIbuffer[27] == 0x00 || SPIbuffer[27] == 0x80) &&
                   SPIbuffer[29] == 0x12) {
          const uint16_t transportID =
              (static_cast<uint16_t>(SPIbuffer[30]) << 8) | SPIbuffer[31];
          const uint8_t segmentNumber = SPIbuffer[28];
          const bool lastSegment = (SPIbuffer[27] & 0x80U) != 0U;
          const uint16_t dataLen = static_cast<uint16_t>(byte_count - 11U);

          if (!dabSwitch.slsContextValid) {
            discardStaleMot();
          } else if (lastCompletedTransportIdValid &&
              transportID == lastCompletedTransportId) {
            if (segmentNumber == 0U) logRepeatedCompletedMot(transportID);
          } else if (slideshowPublishedPending || SlideShowUpdate) {
            // Preserve the complete image until acknowledgeSlideshow().
            if (SlideShowDebug && segmentNumber == 0U)
              DIAG_PRINTF("[SLS] Hold published image; ignore seg=0 TID=%u\n",
                            transportID);
          } else {
            if (SlideShowTransportIDValid &&
                transportID != SlideShowTransportID && segmentNumber == 0U) {
              if (SlideShowDebug)
                DIAG_PRINTF("[SLS] Partial TID=%u abandoned; new seg=0 TID=%u\n",
                              SlideShowTransportID, transportID);
              lockSlideshowTransport(transportID);
            } else if (!SlideShowTransportIDValid) {
              lockSlideshowTransport(transportID);
              if (SlideShowDebug)
                DIAG_PRINTF("[SLS] Collector locked to TID=%u\n", transportID);
            }

            if (transportID != SlideShowTransportID) {
              if (SlideShowDebug)
                DIAG_PRINTF("[SLS] Ignore seg=%u TID=%u (collecting TID=%u)\n",
                              segmentNumber, transportID,
                              SlideShowTransportID);
            } else {
              const uint8_t byteIndex = segmentNumber / 8U;
              const uint8_t bitIndex = segmentNumber % 8U;
              const uint8_t bitMask = static_cast<uint8_t>(1U << bitIndex);

              if ((SlideShowSegmentBitmap[byteIndex] & bitMask) == 0U) {
                const bool lastConflict =
                    lastSegment && SlideShowLastSegmentValid &&
                    segmentNumber != SlideShowLastSegment;
                const bool pastKnownEnd =
                    SlideShowLastSegmentValid &&
                    segmentNumber > SlideShowLastSegment;
                const bool lastBeforeHighest =
                    lastSegment && SlideShowByteCounter != 0U &&
                    segmentNumber < SlideShowHighestSegment;

                if (lastConflict || pastKnownEnd || lastBeforeHighest ||
                    dataLen > SLS_MAX_SEG_SIZE ||
                    SlideShowByteCounter > SLS_BUFFER_BYTES - dataLen) {
                  if (SlideShowDebug)
                    DIAG_PRINTF("[SLS] Invalid seg=%u len=%u last=%u; reset TID=%u\n",
                                  segmentNumber, dataLen,
                                  lastSegment ? 1U : 0U, transportID);
                  resetSlideshowCollector();
                } else {
                  // Invalidate the previous published view before the first
                  // write, not afterwards. slideshowPublishedPending above
                  // guarantees the UI has already finished using its bytes.
                  if (SlideShowByteCounter == 0U) {
                    beginSlideshowReception();
                    slideshowFirstSegmentMs = millis();
                  }

                  if (!storeSlideshowSegment(segmentNumber,
                                              SPIbuffer + 34, dataLen)) {
                    if (SlideShowDebug)
                      DIAG_PRINTF("[SLS] Seg=%u len=%u does not fit packed buffer\n",
                                    segmentNumber, dataLen);
                    resetSlideshowCollector();
                  } else {
                    SlideShowSegmentBitmap[byteIndex] |= bitMask;
                    SlideShowByteCounter += dataLen;
                    if (segmentNumber > SlideShowHighestSegment)
                      SlideShowHighestSegment = segmentNumber;

                    if (lastSegment) {
                      SlideShowLastSegmentValid = true;
                      SlideShowLastSegment = segmentNumber;
                      SlideShowTotalSegments =
                          static_cast<uint16_t>(segmentNumber) + 1U;
                    }

                    SlideShowInit = true;
                    SlideShowLastActivity = millis();
                    static uint32_t lastSegmentLogMs = 0;
                    static uint16_t droppedSegmentLogs = 0;
                    const uint32_t segmentLogNow = millis();
                    if (SlideShowDebug &&
                        dab_scheduler::shouldLogMotSegment(
                            SlideShowVerbose, segmentNumber, lastSegment,
                            segmentLogNow, lastSegmentLogMs)) {
                      if (diagSerial.availableForWrite() >= 96) {
                        if (droppedSegmentLogs != 0U) {
                          DIAG_PRINTF("[SLS] UART suppressed=%u segment logs\n",
                                      static_cast<unsigned>(droppedSegmentLogs));
                          droppedSegmentLogs = 0;
                        }
                        DIAG_PRINTF("[SLS] Saved TID=%u seg=%u len=%u bytes=%u/%u%s\n",
                                      transportID, segmentNumber, dataLen,
                                      SlideShowByteCounter, SlideShowLength,
                                      lastSegment ? " LAST" : "");
                      } else if (droppedSegmentLogs != 0xFFFFU) {
                        ++droppedSegmentLogs;
                      }
                    }

                    if (SlideShowLength != 0U &&
                        SlideShowByteCounter > SlideShowLength) {
                      if (SlideShowDebug)
                        DIAG_PRINTLN("[SLS] Segment data exceed BodySize; reset");
                      resetSlideshowCollector();
                    } else if (allSegmentsReceived() &&
                               ((SlideShowLength != 0U &&
                                 SlideShowByteCounter == SlideShowLength) ||
                                (SlideShowLength == 0U &&
                                 SlideShowLastSegmentValid))) {
                      if (SlideShowTotalSegments == 0U)
                        SlideShowTotalSegments =
                            static_cast<uint16_t>(SlideShowHighestSegment) + 1U;
                      assembleSlideshow();
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

// The only MOT buffer stops representing a complete image as soon as a new
// object starts. The previous picture, if currently visible, remains untouched
// in the TFT controller's GRAM until a new complete image is decoded.
void DAB::beginSlideshowReception(void) {
  SlideShowAvailable = false;
  SlideShowUpdate = false;
  SlideShowUpdate2 = false;
  slideshowRamSize = 0;
  SlideshowReceptionState(true);
}

void DAB::acknowledgeSlideshow(void) {
  SlideShowUpdate = false;
  slideshowPublishedPending = false;
}

// Insert one new segment into the single buffer while keeping already received
// data packed in SegmentNumber order. Later arrival of a lower segment shifts
// only the collected tail and therefore needs no second image-sized buffer.
bool DAB::storeSlideshowSegment(uint8_t segmentNumber,
                                const uint8_t* data,
                                uint16_t dataLength) {
  if (!slideshowSegBuf || !data || dataLength == 0U ||
      dataLength > SLS_MAX_SEG_SIZE ||
      SlideShowByteCounter > SLS_BUFFER_BYTES - dataLength) return false;

  size_t insertOffset = 0U;
  for (uint16_t i = 0; i < segmentNumber; ++i) {
    insertOffset += slideshowSegLen[i];
  }
  if (insertOffset > SlideShowByteCounter) return false;

  const size_t tailLength = SlideShowByteCounter - insertOffset;
  memmove(slideshowSegBuf + insertOffset + dataLength,
          slideshowSegBuf + insertOffset, tailLength);
  memcpy(slideshowSegBuf + insertOffset, data, dataLength);
  slideshowSegLen[segmentNumber] = dataLength;
  return true;
}

// Forget every buffered slideshow segment. Clearing the metadata is enough;
// packed bytes in slideshowSegBuf are overwritten by the next received object.
void DAB::clearSegmentBuffer(void) {
  SlideshowReceptionState(false);
  memset(slideshowSegLen, 0, sizeof(slideshowSegLen));
  memset(SlideShowSegmentBitmap, 0, sizeof(SlideShowSegmentBitmap));
  SlideShowLastSegmentValid = false;
  SlideShowLastSegment = 0;
}

void DAB::resetSlideshowCollector(void) {
  clearSegmentBuffer();
  SlideShowTransportIDValid = false;
  SlideShowTransportID = 0;
  SlideShowByteCounter = 0;
  SlideShowHighestSegment = 0;
  SlideShowTotalSegments = 0;
  SlideShowLength = 0;
  SlideShowInit = false;
  SlideShowLastActivity = 0;
  slideshowFirstSegmentMs = 0;
}

void DAB::lockSlideshowTransport(uint16_t transportId) {
  resetSlideshowCollector();
  SlideShowTransportID = transportId;
  SlideShowTransportIDValid = true;
}

// Check if we have every segment from 0..N where N is either:
//   - SlideShowTotalSegments (set from an explicit last-segment flag), or
//   - SlideShowHighestSegment + 1 (used with an exact BodySize match).
bool DAB::allSegmentsReceived(void) {
  uint16_t segmentsToCheck = SlideShowTotalSegments;
  if (segmentsToCheck == 0U && SlideShowByteCounter != 0U)
    segmentsToCheck = static_cast<uint16_t>(SlideShowHighestSegment) + 1U;
  if (segmentsToCheck == 0) return false;

  for (uint16_t i = 0; i < segmentsToCheck; ++i) {
    const uint8_t byteIndex = static_cast<uint8_t>(i / 8U);
    const uint8_t bitIndex = static_cast<uint8_t>(i % 8U);
    if (!(SlideShowSegmentBitmap[byteIndex] & (1 << bitIndex))) {
      return false;
    }
  }
  return true;
}

// Validate the already packed segments and publish the contiguous RAM buffer.
void DAB::assembleSlideshow(void) {
  if (SlideShowDebug) {
    DIAG_PRINTF("[SLS] Assembling: %u segments, %u bytes received, %u bytes expected\n",
                  SlideShowTotalSegments, SlideShowByteCounter, SlideShowLength);
  }

  uint32_t actualSize = 0;
  for (uint16_t i = 0;
       i < SlideShowTotalSegments && i < SLS_MAX_SEGMENTS; ++i) {
    if (slideshowSegLen[i] > 0) {
      actualSize += slideshowSegLen[i];
    } else if (SlideShowDebug) {
      DIAG_PRINTF("[SLS] WARNING: segment %u missing!\n", i);
    }
  }

  const bool sizeValid =
      actualSize > 8 &&
      actualSize <= SLS_BUFFER_BYTES &&
      actualSize == SlideShowByteCounter &&
      (SlideShowLength == 0 || actualSize == SlideShowLength);

  const bool validJPEG =
      sizeValid &&
      slideshowSegBuf[0] == 0xFF &&
      slideshowSegBuf[1] == 0xD8 &&
      slideshowSegBuf[2] == 0xFF &&
      slideshowSegBuf[actualSize - 2] == 0xFF &&
      slideshowSegBuf[actualSize - 1] == 0xD9;

  const bool validPNG =
      sizeValid &&
      slideshowSegBuf[0] == 0x89 &&
      slideshowSegBuf[1] == 0x50 &&
      slideshowSegBuf[2] == 0x4E &&
      slideshowSegBuf[3] == 0x47 &&
      slideshowSegBuf[4] == 0x0D &&
      slideshowSegBuf[5] == 0x0A &&
      slideshowSegBuf[6] == 0x1A &&
      slideshowSegBuf[7] == 0x0A;

  if (!validJPEG && !validPNG) {
    if (SlideShowDebug) {
      DIAG_PRINTF("[SLS] REJECTED new image: size=%u expected=%u hdr=%02X %02X %02X %02X\n",
                    actualSize, SlideShowLength,
                    actualSize > 0 ? slideshowSegBuf[0] : 0,
                    actualSize > 1 ? slideshowSegBuf[1] : 0,
                    actualSize > 2 ? slideshowSegBuf[2] : 0,
                    actualSize > 3 ? slideshowSegBuf[3] : 0);
    }
  } else {
    slideshowRamSize = actualSize;
    SlideShowUpdate = true;
    SlideShowUpdate2 = true;
    SlideShowAvailable = true;
    slideshowPublishedPending = true;
    lastCompletedTransportId = SlideShowTransportID;
    lastCompletedTransportIdValid = SlideShowTransportIDValid;

    if (SlideShowDebug) {
      DIAG_PRINTF("[SLS] RAM image ready: %s, %u bytes\n",
                    validJPEG ? "JPEG" : "PNG", actualSize);
    }
    DIAG_PRINTF("[SLS/UI] READY: icon/button enabled, %u bytes\n", actualSize);
    SlideshowReceptionState(false);
    DIAG_PRINTF("[SLS/TIME] first-segment-to-ready=%u ms\n", slideshowFirstSegmentMs ? (millis() - slideshowFirstSegmentMs) : 0U);
    DIAG_PRINTF("[RAM/SLS] single MOT buffer=%u image=%u\n",
                  (unsigned)SLS_BUFFER_BYTES, actualSize);
  }

  // Reset only collector metadata. The compacted image bytes remain in this
  // single buffer and are published through slideshowRamSize on success.
  resetSlideshowCollector();
}

// Populate per-service metadata (PTY, ECC, bitrate, audio mode, sample rate,
// time/date, protection level) for the currently selected service. The
// service list itself is filled in EnsembleInfo().
void DAB::ServiceInfo(void) {
  if (!dabActiveServiceValid) return;
  invalidateDabAudioInfo(
      millis() + dab_scheduler::DAB_AUDIO_INFO_INITIAL_DELAY_MS);
  dabAudioRefreshPending = true;
  dabCurrentSubchannelRefreshPending = true;
  dabCurrentServiceRefreshPending = true;
}

// Wipe every cached metadata field. Called when re-tuning so stale labels,
// PTY etc. from the previous channel don't briefly show up on the display.
void DAB::clearData(void) {
  dabServiceLabelValid = false;
  dabServiceListReady = false;
  dabUaInventoryValid = false;
  dabUaListVersion = 0;
  dabUaServiceCount = 0;
  dabUaComponentCount = 0;
  dabUaScanIndex = 0;
  dabUaLogPending = false;
  resetDabSpiDiscovery();
  for (byte x = 0; x < 32; x++) {
    service[x].ServiceID = 0;
    service[x].CompID = 0;
    service[x].ServiceType = 8;
    dabServiceCharsetValue[x] = 0;
    for (byte y = 0; y < 16; y++) service[x].Label[y] = '\0';
  }
  dabDynamicLabelCharset = 0;
  dabDynamicLabelLength = 0;
  for (byte x = 0; x < 128; x++) ServiceData[x] = '\0';
  ServiceLabelCharset = 0;
  EnsembleLabelCharset = 0;
  invalidateDabAudioInfo();
}

bool DAB::isDabServiceMetadataReady(void) const {
  return !isFm() && dabServiceListReady &&
         dabServiceTypeScanIndex >= numberofservices &&
         dabCommand != DabCommand::ServiceType;
}

bool DAB::isDabServiceStartPending(void) const {
  return !isFm() && dabServiceRequestPending &&
         dabSwitch.state != dab_switch::State::Failed;
}

// Start a DAB tune. Completion is handled cooperatively by Update(); there is
// no multi-second polling loop here.
void DAB::setFreq(uint8_t freq) {
  if (isFm()) return;
  dabSpiProbe.stop(millis());
  numberofservices = 0;
  clearData();

  for (byte x = 0; x < 16; x++) {
    EnsembleLabel[x] = '\0';
    PStext[x] = '\0';
  }

  EID[0] = '\0';
  SID[0] = '\0';
  pty = 36;
  ecc = 0;
  ensembleEcc = 0;
  serviceHasOwnEcc = false;
  protectionlevel = 0;
  bitrate = 0;
  samplerate = 0;
  servicetype = 9;
  audiomode = 4;
  dataServiceCheck = 0;
  ServiceStart = false;
  SlideShowAvailable = false;
  SlideShowUpdate = false;
  SlideShowUpdate2 = false;
  slideshowRamSize = 0;
  slideshowPublishedPending = false;
  lastCompletedTransportIdValid = false;
  resetSlideshowCollector();

  signallock = false;
  fic = 0;
  cnr = 0;
  lastStatus0 = 0;

  dabGeneration = dab_scheduler::nextGeneration(dabGeneration);
  if (dab_switch::transitionActive(dabSwitch.state))
    ++diagDabAbandonedSwitchCount;
  dabSwitch.requestSwitch();
  epg::liveReset();
  epgProductionPaused = false;
  ++dabTuneRequestId;
  if (dabTuneRequestId == 0) ++dabTuneRequestId;
  dabRequestedFrequency = freq;
  dabTuneRequestPending = true;
  dabWaitingForStc = false;
  dabTuneBusyRetries = 0;
  dabTuneRetryNotBeforeMs = 0;
  dabStcPending = false;
  dabServiceRequestPending = false;
  dabSignalRefreshPending = false;
  dabServiceListRefreshPending = false;
  dabEnsembleRefreshPending = false;
  dabTimeRefreshPending = false;
  dabAudioRefreshPending = false;
  dabCurrentSubchannelRefreshPending = false;
  dabCurrentServiceRefreshPending = false;
  dabServiceTypeScanIndex = 0;
  dabDsrvPending = false;
  dabDeviceEventPending = false;
  if (!dabActiveDataServiceValid) dataServiceCheck = 0;
  tunePending = true;
  resetDabPeriodicDeadlines(millis());
  if (diagnosticDebug)
    DIAG_PRINTF("[DAB/ASYNC] queued tune index=%u request=%u\n",
                  freq, static_cast<unsigned>(dabTuneRequestId));
}

void DAB::clearFmData(void) {
  fmValid = false;
  fmAfcRail = false;
  fmPilot = false;
  fmStereoBlend = 0;
  fmRssi = -100;
  fmSnr = 0;
  fmMultipath = 0;
  fmPi = 0;
  fmPty = 0;
  fmTp = false;
  fmTa = false;
  fmPtyValid = false;
  fmAf.clear();
  fmCtValid = false;
  fmCt = fm_features::ClockTime{};
  fmCtValidator.reset();
  fmPsSeenMask = 0;
  fmPsConfirmedMask = 0;
  fmRtMask = 0;
  fmRtSeenMask = 0;
  fmRtAb = false;
  fmRtVersionB = false;
  fmRtVersionKnown = false;
  memset(fmPs, 0, sizeof(fmPs));
  memset(fmRadioText, 0, sizeof(fmRadioText));
  memset(fmPsWork, ' ', 8); fmPsWork[8] = '\0';
  memset(fmRtWork, ' ', 64); fmRtWork[64] = '\0';
  memset(PStext, 0, sizeof(PStext));
  memset(ServiceData, 0, sizeof(ServiceData));
  dabDynamicLabelCharset = 0;
  dabDynamicLabelLength = 0;
  ServiceLabelCharset = 0;
  EnsembleLabelCharset = 0;
  SlideShowAvailable = false;
  SlideShowUpdate = false;
  SlideShowUpdate2 = false;
  slideshowRamSize = 0;
  slideshowPublishedPending = false;
  lastCompletedTransportIdValid = false;
  resetSlideshowCollector();
  signallock = false;
}

void DAB::setFmFrequency(uint16_t frequency10kHz) {
  if (!isFm()) return;
  cancelFmAf(true);
  frequency10kHz = normalizeFmFrequency(frequency10kHz, activeFmRegion);
  clearFmData();
  fmFrequency10kHz = frequency10kHz;
  lastStatus0 = 0;
  const si468x::Result result = chip.startFmTune(frequency10kHz);
  finishCommandDiagnostics(result);
  tunePending = result == si468x::Result::Pending || result == si468x::Result::Ok;
  seekPending = false;
  tuneDeadline = millis() + 3000UL;
  fmRsqTimer = fmAcfTimer = fmRdsTimer = 0;
}

bool DAB::startFmSeek(bool up) {
  if (!isFm() || tunePending || chip.busy()) return false;
  cancelFmAf(true);
  clearFmData();
  lastStatus0 = 0;
  const si468x::Result result = chip.startFmSeek(up, true);
  finishCommandDiagnostics(result);
  tunePending = result == si468x::Result::Pending || result == si468x::Result::Ok;
  seekPending = tunePending;
  tuneDeadline = millis() + 12000UL;
  fmRsqTimer = fmAcfTimer = fmRdsTimer = 0;
  return tunePending;
}

void DAB::setFmAfEnabled(bool enabled) {
  fmAfEnabled = enabled;
  if (enabled && fmAfStationSince == 0U) fmAfStationSince = millis();
  DIAG_PRINTF("[FM/AF] enabled=%u\n", enabled ? 1U : 0U);
}

void DAB::setFmAfAllowed(bool allowed) {
  fmAfAllowed = allowed;
}

bool DAB::isFmAfProbing(void) const {
  return fmAfState == FmAfState::CandidateTune ||
         fmAfState == FmAfState::CandidatePi ||
         fmAfState == FmAfState::RestoreTune;
}

bool DAB::takeFmAfSwitch(uint16_t& frequency10kHz) {
  if (!fmAfSwitchPending) return false;
  fmAfSwitchPending = false;
  frequency10kHz = fmFrequency10kHz;
  return true;
}

void DAB::cancelFmAf(bool userRetune) {
  fmAfState = FmAfState::Idle;
  fmAfCandidateIndex = 0U;
  fmAfCandidatePi = 0U;
  fmAfCandidateValid = false;
  fmAfCandidateSamples = 0U;
  fmAfSwitchPending = false;
  fmAfStationSince = millis();
  if (userRetune) fmAfCooldownUntil = 0U;
}

bool DAB::startFmAfTune(uint16_t frequency10kHz, FmAfState nextState,
                        uint32_t now) {
  if (chip.busy() || tunePending) return false;
  lastStatus0 = 0U;
  const si468x::Result result = chip.startFmTune(frequency10kHz);
  finishCommandDiagnostics(result);
  if (result != si468x::Result::Pending && result != si468x::Result::Ok)
    return false;
  tunePending = true;
  seekPending = false;
  tuneDeadline = now + 3000UL;
  fmRsqTimer = fmRdsTimer = 0U;
  fmAfState = nextState;
  fmAfStateSince = now;
  fmAfCandidatePi = 0U;
  fmAfCandidateValid = false;
  fmAfCandidateSamples = 0U;
  DIAG_PRINTF("[FM/AF] probe tune=%u.%02u state=%u\n",
              static_cast<unsigned>(frequency10kHz / 100U),
              static_cast<unsigned>(frequency10kHz % 100U),
              static_cast<unsigned>(nextState));
  return true;
}

bool DAB::processFmAf(uint32_t now) {
  auto startNextCandidate = [this, now]() -> bool {
    while (fmAfCandidateIndex < fmAf.count) {
      const uint16_t candidate = fmAf.frequency10kHz[fmAfCandidateIndex++];
      if (candidate == fmAfOriginalFrequency ||
          !isFmFrequencyValid(candidate, activeFmRegion))
        continue;
      fmAfCandidateFrequency = candidate;
      return startFmAfTune(candidate, FmAfState::CandidateTune, now);
    }
    return startFmAfTune(fmAfOriginalFrequency, FmAfState::RestoreTune, now);
  };

  const bool usable = isFm() && fmAfEnabled && fmAfAllowed &&
                      fmPi != 0U && fmAf.count != 0U;
  if (!usable) {
    if (isFmAfProbing() && fmAfState != FmAfState::RestoreTune &&
        !tunePending)
      return startFmAfTune(fmAfOriginalFrequency, FmAfState::RestoreTune, now);
    if (!isFmAfProbing()) {
      fmAfState = FmAfState::Idle;
      fmAfStationSince = now;
    }
    return false;
  }

  if (fmAfState == FmAfState::Cooldown) {
    if (static_cast<int32_t>(now - fmAfCooldownUntil) < 0) return false;
    fmAfState = FmAfState::Idle;
    fmAfStationSince = now;
  }

  if (fmAfState == FmAfState::CandidatePi) {
    if (fm_af::sweepExpired(now, fmAfSweepDeadline)) {
      fmAfCandidateIndex = fmAf.count;
      DIAG_PRINTLN("[FM/AF] sweep timeout; restoring original");
      return startNextCandidate();
    }
    if (fmAfCandidatePi != 0U && fmAfCandidateSamples >= 2U) {
      if (fm_af::candidateIsBetter(
              fmAfCandidateValid, fmAfExpectedPi, fmAfCandidatePi,
              fmAfOriginalRssi, fmAfOriginalSnr,
              fmAfCandidateRssi, fmAfCandidateSnr)) {
        fmFrequency10kHz = fmAfCandidateFrequency;
        fmRssi = fmAfCandidateRssi;
        fmSnr = fmAfCandidateSnr;
        fmValid = true;
        signallock = true;
        fmAfSwitchPending = true;
        fmAfCooldownUntil = now + fm_af::SUCCESS_COOLDOWN_MS;
        fmAfState = FmAfState::Cooldown;
        fmAfStationSince = now;
        rfSampleGeneration = dab_scheduler::nextGeneration(rfSampleGeneration);
        DIAG_PRINTF("[FM/AF] accepted=%u.%02u PI=%04X RSSI=%d->%d SNR=%d->%d\n",
                    static_cast<unsigned>(fmFrequency10kHz / 100U),
                    static_cast<unsigned>(fmFrequency10kHz % 100U),
                    static_cast<unsigned>(fmAfCandidatePi),
                    static_cast<int>(fmAfOriginalRssi),
                    static_cast<int>(fmAfCandidateRssi),
                    static_cast<int>(fmAfOriginalSnr),
                    static_cast<int>(fmAfCandidateSnr));
        return false;
      }
      DIAG_PRINTF("[FM/AF] rejected=%u PI=%04X RSSI=%d SNR=%d\n",
                  static_cast<unsigned>(fmAfCandidateFrequency),
                  static_cast<unsigned>(fmAfCandidatePi),
                  static_cast<int>(fmAfCandidateRssi),
                  static_cast<int>(fmAfCandidateSnr));
      return startNextCandidate();
    }
    if (now - fmAfStateSince >= fm_af::PI_TIMEOUT_MS)
      return startNextCandidate();
    return false;
  }

  if (isFmAfProbing()) return false;

  if (fmAfStationSince == 0U) fmAfStationSince = now;
  if (now - fmAfStationSince < fm_af::STATION_STABLE_MS) return false;
  static const uint8_t rssiThreshold[] = {12U, 18U, 28U};
  static const uint8_t snrThreshold[] = {2U, 4U, 8U};
  const bool weak = fm_af::weakSignal(
      fmValid, fmRssi, fmSnr, rssiThreshold[activeFmSeekSensitivity],
      snrThreshold[activeFmSeekSensitivity]);
  if (!weak) {
    fmAfState = FmAfState::Idle;
    return false;
  }
  if (fmAfState != FmAfState::WeakHold) {
    fmAfState = FmAfState::WeakHold;
    fmAfStateSince = now;
    DIAG_PRINTF("[FM/AF] weak hold start RSSI=%d SNR=%d\n",
                static_cast<int>(fmRssi), static_cast<int>(fmSnr));
    return false;
  }
  if (now - fmAfStateSince < fm_af::WEAK_HOLD_MS) return false;

  fmAfOriginalFrequency = fmFrequency10kHz;
  fmAfOriginalRssi = fmRssi;
  fmAfOriginalSnr = fmSnr;
  fmAfExpectedPi = fmPi;
  fmAfCandidateIndex = 0U;
  fmAfSweepDeadline = now + fm_af::SWEEP_TIMEOUT_MS;
  DIAG_PRINTF("[FM/AF] weak hold met PI=%04X freq=%u RSSI=%d SNR=%d count=%u\n",
              static_cast<unsigned>(fmAfExpectedPi),
              static_cast<unsigned>(fmAfOriginalFrequency),
              static_cast<int>(fmAfOriginalRssi),
              static_cast<int>(fmAfOriginalSnr),
              static_cast<unsigned>(fmAf.count));
  return startNextCandidate();
}

void DAB::processFmRds(void) {
  // Drain only a bounded number of groups per scheduler pass. AN649 defines
  // RDSFIFOUSED as including the group returned in the current reply; when it
  // is >1 another oldest group can be fetched immediately without waiting for
  // the next 80 ms poll. Limiting the drain keeps GUI/radio scheduling fair.
  static constexpr uint8_t kMaxRdsGroupsPerPass = 3;

  auto processGroup = [this](const si468x::FmRdsGroup& group) {
    // BLE 0 and 1 are clean or corrected by at most two bits. Do not use BLE 2
    // for text: a 3-5 bit correction can otherwise become a visible character.
    if (group.ble[1] > 1U) return;

    const uint16_t blockB = group.block[1];
    const uint8_t groupType = static_cast<uint8_t>((blockB >> 12) & 0x0F);
    const bool versionB = (blockB & 0x0800U) != 0;

    if (groupType == 0) {
      const bool newTa = (blockB & 0x0010U) != 0;
      if (fmTa != newTa) {
        fmTa = newTa;
        DIAG_PRINTF("[FM/TA] TP=%u TA=%u\n", fmTp ? 1U : 0U,
                    fmTa ? 1U : 0U);
      }

      if (!versionB && group.ble[2] <= 1U && fmPi != 0U) {
        const uint8_t af1 = static_cast<uint8_t>(group.block[2] >> 8);
        const uint8_t af2 = static_cast<uint8_t>(group.block[2] & 0xFFU);
        const bool changed = fmAf.addCode(af1) | fmAf.addCode(af2);
        if (changed) {
          DIAG_PRINTF("[FM/AF] PI=%04X count=%u last=%u.%02u\n",
                      static_cast<unsigned>(fmPi),
                      static_cast<unsigned>(fmAf.count),
                      static_cast<unsigned>(
                          fmAf.frequency10kHz[fmAf.count - 1U] / 100U),
                      static_cast<unsigned>(
                          fmAf.frequency10kHz[fmAf.count - 1U] % 100U));
        }
      }

      if (group.ble[3] > 1U) return;

      const uint8_t segment = blockB & 0x03U;
      const uint8_t segmentBit = static_cast<uint8_t>(1U << segment);
      const uint8_t pos = static_cast<uint8_t>(segment * 2U);
      const char incoming[2] = {
          static_cast<char>(group.block[3] >> 8),
          static_cast<char>(group.block[3] & 0xFF)};

      if ((fmPsSeenMask & segmentBit) == 0U) {
        memcpy(fmPsWork + pos, incoming, 2);
        fmPsSeenMask |= segmentBit;
        fmPsConfirmedMask &= static_cast<uint8_t>(~segmentBit);
      } else if (memcmp(fmPsWork + pos, incoming, 2) == 0) {
        fmPsConfirmedMask |= segmentBit;
      } else {
        // Keep confirmation local to the changed 2-character segment. Other
        // already-confirmed segments remain valid, so one missing/repeated
        // group can no longer prevent PS acquisition indefinitely.
        memcpy(fmPsWork + pos, incoming, 2);
        fmPsConfirmedMask &= static_cast<uint8_t>(~segmentBit);
      }
      fmPsWork[8] = '\0';

      if (fmPsSeenMask == 0x0F) {
        bool validPs = false;
        for (uint8_t i = 0; i < 8; ++i) {
          const uint8_t c = static_cast<uint8_t>(fmPsWork[i]);
          if (c == 0x00U || c == 0x0AU || c == 0x0BU || c == 0x0DU) {
            validPs = false;
            break;
          }
          if (c != ' ') validPs = true;
        }
        if (!validPs) {
          fmPsSeenMask = 0;
          fmPsConfirmedMask = 0;
          memset(fmPsWork, ' ', 8); fmPsWork[8] = '\0';
          return;
        }

        const bool firstAcquisition = fmPs[0] == '\0';
        const bool stableReplacement = fmPsConfirmedMask == 0x0F;
        if ((firstAcquisition || stableReplacement) &&
            memcmp(fmPs, fmPsWork, 8) != 0) {
          memcpy(fmPs, fmPsWork, 8); fmPs[8] = '\0';
          memset(PStext, 0, sizeof(PStext));
          memcpy(PStext, fmPs, 8);
          DIAG_PRINTF("[FM/RDS] PS %s='%s' confirmedMask=0x%02X\n",
                        firstAcquisition ? "acquired" : "updated",
                        fmPs, fmPsConfirmedMask);
        }
      }
      return;
    }

    if (groupType == 4U && !versionB && group.ble[2] <= 1U &&
        group.ble[3] <= 1U) {
      fm_features::ClockTime decoded;
      if (!fm_features::decodeClockTime(blockB, group.block[2],
                                        group.block[3], decoded))
        return;
      fm_features::ClockTime confirmed;
      const fm_features::ClockSampleResult result =
          fmCtValidator.ingest(decoded, fmPi, millis(), confirmed);
      if (result == fm_features::ClockSampleResult::Confirmed) {
        const bool changed = !fmCtValid || decoded.year != fmCt.year ||
                             decoded.month != fmCt.month ||
                             decoded.day != fmCt.day ||
                             decoded.hour != fmCt.hour ||
                             decoded.minute != fmCt.minute ||
                             decoded.localOffsetHalfHours !=
                                 fmCt.localOffsetHalfHours;
        if (changed) {
          fmCt = decoded;
          fmCtValid = true;
          DIAG_PRINTF("[FM/CT] confirmed %04u-%02u-%02u %02u:%02u UTC offset=%d/2h\n",
                      fmCt.year, fmCt.month, fmCt.day, fmCt.hour, fmCt.minute,
                      static_cast<int>(fmCt.localOffsetHalfHours));
        }
      } else if (result == fm_features::ClockSampleResult::Candidate) {
        DIAG_PRINTF("[FM/CT] candidate %04u-%02u-%02u %02u:%02u offset=%d/2h\n",
                    decoded.year, decoded.month, decoded.day, decoded.hour,
                    decoded.minute,
                    static_cast<int>(decoded.localOffsetHalfHours));
      } else if (result == fm_features::ClockSampleResult::Rejected) {
        DIAG_PRINTF("[FM/CT] rejected inconsistent %04u-%02u-%02u %02u:%02u offset=%d/2h\n",
                    decoded.year, decoded.month, decoded.day, decoded.hour,
                    decoded.minute,
                    static_cast<int>(decoded.localOffsetHalfHours));
      }
      return;
    }

    if (groupType != 2) return;

    const bool ab = (blockB & 0x0010U) != 0;
    if (!fmRtVersionKnown || versionB != fmRtVersionB || ab != fmRtAb) {
      fmRtAb = ab;
      fmRtVersionB = versionB;
      fmRtVersionKnown = true;
      fmRtMask = 0;
      fmRtSeenMask = 0;
      memset(fmRtWork, ' ', 64); fmRtWork[64] = '\0';
    }

    const uint8_t segment = blockB & 0x0FU;
    const uint8_t charsPerSegment = versionB ? 2U : 4U;
    const uint8_t pos = segment * charsPerSegment;
    char segmentData[4];

    if (!versionB && group.ble[2] <= 1U && group.ble[3] <= 1U) {
      segmentData[0] = static_cast<char>(group.block[2] >> 8);
      segmentData[1] = static_cast<char>(group.block[2] & 0xFF);
      segmentData[2] = static_cast<char>(group.block[3] >> 8);
      segmentData[3] = static_cast<char>(group.block[3] & 0xFF);
    } else if (versionB && group.ble[3] <= 1U) {
      segmentData[0] = static_cast<char>(group.block[3] >> 8);
      segmentData[1] = static_cast<char>(group.block[3] & 0xFF);
    } else {
      return;
    }

    for (uint8_t i = 0; i < charsPerSegment; ++i) {
      const uint8_t c = static_cast<uint8_t>(segmentData[i]);
      if (c == 0x00U || c == 0x0BU) return;
    }

    const uint16_t segmentBit = static_cast<uint16_t>(1U << segment);
    if ((fmRtSeenMask & segmentBit) == 0) {
      memcpy(fmRtWork + pos, segmentData, charsPerSegment);
      fmRtSeenMask |= segmentBit;
      fmRtMask &= static_cast<uint16_t>(~segmentBit);
    } else if (memcmp(fmRtWork + pos, segmentData, charsPerSegment) == 0) {
      fmRtMask |= segmentBit;
    } else {
      memset(fmRtWork, ' ', 64); fmRtWork[64] = '\0';
      memcpy(fmRtWork + pos, segmentData, charsPerSegment);
      fmRtSeenMask = segmentBit;
      fmRtMask = 0;
    }

    const uint8_t maxChars = versionB ? 32U : 64U;
    int16_t endPos = -1;
    for (uint8_t i = 0; i < maxChars; ++i) {
      const uint8_t c = static_cast<uint8_t>(fmRtWork[i]);
      if (c == 0x0D || c == '\n') { endPos = i; break; }
    }

    uint8_t requiredLastSegment = 15U;
    if (endPos >= 0) requiredLastSegment = static_cast<uint8_t>(endPos / charsPerSegment);
    const uint16_t requiredMask = requiredLastSegment == 15U
        ? 0xFFFFU
        : static_cast<uint16_t>((1UL << (requiredLastSegment + 1U)) - 1UL);

    if ((fmRtMask & requiredMask) == requiredMask) {
      memcpy(fmRadioText, fmRtWork, maxChars);
      fmRadioText[maxChars] = '\0';
      if (endPos >= 0 && endPos < maxChars) fmRadioText[endPos] = '\0';
      for (int16_t i = static_cast<int16_t>(maxChars) - 1;
           i >= 0 && fmRadioText[i] == ' '; --i) fmRadioText[i] = '\0';
      memset(ServiceData, 0, sizeof(ServiceData));
      strncpy(ServiceData, fmRadioText, sizeof(ServiceData) - 1);
    }
  };

  for (uint8_t drained = 0; drained < kMaxRdsGroupsPerPass; ++drained) {
    si468x::FmRdsGroup group;
    ++diagFmRdsStatusCount;
    const si468x::Result statusResult =
        chip.fmRdsStatus(group, false, false, drained == 0U, 100000UL);
    finishCommandDiagnostics(statusResult);
    if (statusResult != si468x::Result::Ok) return;

    if (isFmAfProbing()) {
      if (group.piValid && group.ble[0] <= 1U) fmAfCandidatePi = group.pi;
      if (group.fifoUsed <= 1U) return;
      continue;
    }

    if (group.fifoLost) {
      fmPsSeenMask = 0;
      fmPsConfirmedMask = 0;
      fmRtMask = 0;
      fmRtSeenMask = 0;
      fmRtVersionKnown = false;
      memset(fmPsWork, ' ', 8); fmPsWork[8] = '\0';
      memset(fmRtWork, ' ', 64); fmRtWork[64] = '\0';
      ++diagFmRdsStatusCount;
      const si468x::Result clearResult =
          chip.fmRdsStatus(group, true, true, true, 100000UL);
      finishCommandDiagnostics(clearResult);
      return;
    }

    // PI and TP/PTY are status fields and are useful even with an empty FIFO.
    if (group.piValid && group.pi != fmPi) {
      fmPi = group.pi;
      fmAf.clear(fmPi);
      fmCtValidator.reset();
      fmCtValid = false;
      fmCt = fm_features::ClockTime{};
      fmTa = false;
      // PS/RT segments collected before a PI transition belong to a different
      // programme. Reset both working and published text so a scan can never
      // combine an old name with the newly tuned station.
      fmPsSeenMask = 0;
      fmPsConfirmedMask = 0;
      memset(fmPsWork, ' ', 8); fmPsWork[8] = '\0';
      memset(fmPs, 0, sizeof(fmPs));
      memset(PStext, 0, sizeof(PStext));
      fmRtMask = 0;
      fmRtSeenMask = 0;
      memset(fmRtWork, ' ', 64); fmRtWork[64] = '\0';
      memset(fmRadioText, 0, sizeof(fmRadioText));
      memset(ServiceData, 0, sizeof(ServiceData));
      DIAG_PRINTF("[FM/AF] PI changed=%04X; list cleared\n",
                  static_cast<unsigned>(fmPi));
    }
    if (group.tpPtyValid) {
      if (fmTp != group.tp) {
        fmTp = group.tp;
        DIAG_PRINTF("[FM/TA] TP=%u TA=%u\n", fmTp ? 1U : 0U,
                    fmTa ? 1U : 0U);
      }
      fmPty = group.pty;
      fmPtyValid = true;
      pty = fmPty;
    }

    if (!group.sync || group.fifoUsed == 0U) return;
    processGroup(group);

    // fifoUsed includes the group just returned. 1 therefore means that this
    // was the last available group; >1 means another group is already queued.
    if (group.fifoUsed <= 1U) return;
  }
}

void DAB::updateFm(void) {
  const uint32_t now = millis();
  const bool reportDiagnostics =
      static_cast<uint32_t>(now - diagFmLastReportMs) >= RADIO_FM_DIAG_INTERVAL_MS;
  if (chip.busy()) {
    ++diagFmBusySkipCount;
    if (reportDiagnostics) {
      diagFmLastReportMs = now;
      DIAG_PRINTF("[FM/IRQ] edges=%u busySkip=%u rsq=%u rds=%u ctsIrq=%u ctsPoll=%u\n",
                    static_cast<unsigned>(radioRuntimeIntbEdges()),
                    static_cast<unsigned>(diagFmBusySkipCount),
                    static_cast<unsigned>(diagFmRsqStatusCount),
                    static_cast<unsigned>(diagFmRdsStatusCount),
                    static_cast<unsigned>(diagCtsIrqCompletionCount),
                    static_cast<unsigned>(diagCtsPollCompletionCount));
    }
    return;
  }

  if (processFmAf(now)) return;

  const bool stc = (lastStatus0 & si468x::INTERRUPT_STC) != 0;
  const bool timedOut = tunePending && static_cast<int32_t>(now - tuneDeadline) >= 0;
  const bool tuneRsqDue = tunePending && (stc || now - fmRsqTimer >= 100U);
  const bool idleRsqDue = !tunePending &&
      now - fmRsqTimer >= dab_scheduler::FM_RSQ_INTERVAL_MS;
  const bool rdsDue = !tunePending &&
      (lastStatus0 & si468x::INTERRUPT_RDS ||
       now - fmRdsTimer >= dab_scheduler::FM_RDS_POLL_INTERVAL_MS);
  const bool acfDue = !tunePending &&
      now - fmAcfTimer >= dab_scheduler::FM_ACF_INTERVAL_MS;
  const dab_scheduler::FmWork work = dab_scheduler::chooseFmWork(
      tunePending, tuneRsqDue, rdsDue, idleRsqDue, acfDue);

  // Tune/seek status stays on the fast 100 ms path. In idle mode RDS retains
  // priority over periodic metrics so its FIFO is not slowed to UI cadence.
  if (work == dab_scheduler::FmWork::TuneRsq) {
    si468x::FmRsqStatus rsq;
    ++diagFmRsqStatusCount;
    const si468x::Result rsqResult =
        chip.fmRsqStatus(rsq, true, false, timedOut, stc || timedOut, 100000UL);
    finishCommandDiagnostics(rsqResult);
    if (rsqResult == si468x::Result::Ok) {
      const bool afTune = fmAfState == FmAfState::CandidateTune ||
                          fmAfState == FmAfState::RestoreTune;
      if (!afTune && isFmFrequencyValid(rsq.frequency10kHz, activeFmRegion))
        fmFrequency10kHz = rsq.frequency10kHz;
      // During tune acquisition the command can return a syntactically valid
      // reply whose RSQ VALID bit is still clear. Do not publish its transient
      // metrics (multipath commonly reads 100 and flashes the bar full-scale).
      if (afTune && fmAfState == FmAfState::CandidateTune && rsq.valid) {
        fmAfCandidateValid = true;
        fmAfCandidateRssi = rsq.rssi;
        fmAfCandidateSnr = rsq.snr;
        if (fmAfCandidateSamples != 0xFFU) ++fmAfCandidateSamples;
      } else if (!afTune && rsq.valid) {
        fmRssi = rsq.rssi;
        fmSnr = rsq.snr;
        const uint8_t rawMultipath =
            rsq.multipath > 100U ? 100U : rsq.multipath;
        // FM_RSQ occasionally returns a single full-scale multipath sample.
        // Filter at the RSQ sample rate so one raw reading cannot flash the UI
        // to 100%; a sustained change still converges normally.
        if (!fmValid)
          fmMultipath = rawMultipath;
        else
          fmMultipath = static_cast<uint8_t>(
              (static_cast<uint16_t>(fmMultipath) * 3U + rawMultipath + 2U) / 4U);
        cnr = rsq.snr < 0 ? 0 : static_cast<uint8_t>(rsq.snr);
        fic = fmMultipath;
      }
      if (!afTune) {
        fmValid = rsq.valid;
        fmAfcRail = rsq.afcRail;
        signallock = rsq.valid;
        rfSampleGeneration = dab_scheduler::nextGeneration(rfSampleGeneration);
      }
      if (stc || timedOut) {
        tunePending = false;
        seekPending = false;
        if (fmAfState == FmAfState::CandidateTune) {
          fmAfState = FmAfState::CandidatePi;
          fmAfStateSince = now;
        } else if (fmAfState == FmAfState::RestoreTune) {
          fmAfState = FmAfState::Cooldown;
          fmAfCooldownUntil = now + fm_af::FAILURE_COOLDOWN_MS;
          fmAfStationSince = now;
          DIAG_PRINTLN("[FM/AF] original frequency restored");
        }
      }
      lastStatus0 &= static_cast<uint8_t>(~si468x::INTERRUPT_STC);
    }
    fmRsqTimer = now;
  }

  if (work == dab_scheduler::FmWork::Rds) {
    processFmRds();
    lastStatus0 &= static_cast<uint8_t>(~si468x::INTERRUPT_RDS);
    fmRdsTimer = now;
  }

  if (work == dab_scheduler::FmWork::IdleRsq) {
    si468x::FmRsqStatus rsq;
    ++diagFmRsqStatusCount;
    const si468x::Result rsqResult =
        chip.fmRsqStatus(rsq, true, false, false, false, 100000UL);
    finishCommandDiagnostics(rsqResult);
    if (rsqResult == si468x::Result::Ok) {
      const bool afProbe = fmAfState == FmAfState::CandidatePi;
      if (!afProbe && isFmFrequencyValid(rsq.frequency10kHz, activeFmRegion))
        fmFrequency10kHz = rsq.frequency10kHz;
      if (afProbe && rsq.valid) {
        fmAfCandidateValid = true;
        fmAfCandidateRssi = rsq.rssi;
        fmAfCandidateSnr = rsq.snr;
        if (fmAfCandidateSamples != 0xFFU) ++fmAfCandidateSamples;
      } else if (!afProbe && rsq.valid) {
        fmRssi = rsq.rssi;
        fmSnr = rsq.snr;
        const uint8_t rawMultipath = rsq.multipath > 100U ? 100U : rsq.multipath;
        if (!fmValid)
          fmMultipath = rawMultipath;
        else
          fmMultipath = static_cast<uint8_t>(
              (static_cast<uint16_t>(fmMultipath) * 3U + rawMultipath + 2U) / 4U);
        cnr = rsq.snr < 0 ? 0 : static_cast<uint8_t>(rsq.snr);
        fic = fmMultipath;
      }
      if (!afProbe) {
        fmValid = rsq.valid;
        fmAfcRail = rsq.afcRail;
        signallock = rsq.valid;
        rfSampleGeneration = dab_scheduler::nextGeneration(rfSampleGeneration);
      }
    }
    fmRsqTimer = now;
  }

  if (work == dab_scheduler::FmWork::Acf) {
    si468x::FmAcfStatus acf;
    const si468x::Result acfResult = chip.fmAcfStatus(acf, true, 100000UL);
    finishCommandDiagnostics(acfResult);
    if (acfResult == si468x::Result::Ok) {
      fmPilot = acf.pilot;
      fmStereoBlend = acf.stereoBlendPercent;
      audiomode = acf.pilot ? 2 : 1;
    }
    fmAcfTimer = now;
  }

  if (reportDiagnostics) {
    diagFmLastReportMs = now;
    DIAG_PRINTF("[FM/IRQ] edges=%u busySkip=%u rsq=%u rds=%u ctsIrq=%u ctsPoll=%u\n",
                  static_cast<unsigned>(radioRuntimeIntbEdges()),
                  static_cast<unsigned>(diagFmBusySkipCount),
                  static_cast<unsigned>(diagFmRsqStatusCount),
                  static_cast<unsigned>(diagFmRdsStatusCount),
                  static_cast<unsigned>(diagCtsIrqCompletionCount),
                  static_cast<unsigned>(diagCtsPollCompletionCount));
  }
}

// Start audio for service[_index] in the current ensemble. Resets the
// slideshow segment buffer because the new service has its own MOT stream.
void DAB::setService(uint8_t _index) {
  union {
    uint32_t combine;
    uint8_t monoctet[4];
  } u;

  if (isFm() || _index >= numberofservices) return;
  const uint32_t requestedSid = service[_index].ServiceID;
  const uint32_t requestedCid = service[_index].CompID;
  if ((dabServiceRequestPending && dabRequestedServiceId == requestedSid &&
       dabRequestedComponentId == requestedCid) ||
      (!dabServiceRequestPending && dabActiveServiceValid && ServiceStart &&
       dabActiveServiceId == requestedSid &&
       dabActiveComponentId == requestedCid)) {
    return;
  }

  // Invalidate in-flight UA ownership as well as endpoint selection. Keep the
  // last known ensemble presence while the new audio context becomes stable.
  epg::livePadEndpoint(0,0);
  resetDabSpiDiscovery(true, false);
  dabSpiProbe.stop(millis());
  pty = 36;
  invalidateDabAudioInfo();
  protectionlevel = 0;
  for (byte x = 0; x < 128; x++) ServiceData[x] = '\0';
  memset(PStext, 0, sizeof(PStext));
  dabServiceLabelValid = false;
  slideshowRamSize = 0;
  SlideShowAvailable = false;
  SlideShowUpdate = false;
  SlideShowUpdate2 = false;
  slideshowPublishedPending = false;
  lastCompletedTransportIdValid = false;
  ServiceStart = false;
  ServiceIndex = _index;
  ecc = 0;  // Reset so ServiceInfo() picks up the new service's ECC
  serviceHasOwnEcc = false;
  // Reset segment tracking (RAM-only).
  resetSlideshowCollector();

  u.combine = service[ServiceIndex].ServiceID;

  SID[3] = u.monoctet[0] & 0xF;
  SID[2] = (u.monoctet[0] & 0xF0) >> 4;
  SID[1] = u.monoctet[1] & 0xF;
  SID[0] = (u.monoctet[1] & 0xF0) >> 4;

  for (int i = 0; i < 4; i++) {
    if (SID[i] < 10) {
      SID[i] += '0';
    } else {
      SID[i] += 'A' - 10;
    }
  }
  SID[4] = '\0';
  CurrentServiceID = service[ServiceIndex].ServiceID;
  dabRequestedServiceId = requestedSid;
  dabRequestedComponentId = requestedCid;
  dabServiceRequestPending = true;
  if (dab_switch::transitionActive(dabSwitch.state))
    ++diagDabAbandonedSwitchCount;
  dabSwitch.requestSwitch();
  if (diagnosticDebug)
    DIAG_PRINTF("[DAB/SWITCH] state=%s requested SID=%08X CID=%08X request=%u\n",
                  dab_switch::stateName(dabSwitch.state),
                  static_cast<unsigned>(dabRequestedServiceId),
                  static_cast<unsigned>(dabRequestedComponentId),
                  static_cast<unsigned>(dabSwitch.requestId));
}

bool DAB::startDabCommand(DabCommand operation, uint8_t command,
                          const uint8_t* args, uint16_t argLength,
                          uint16_t replyLength, uint32_t timeoutUs) {
  if (chip.busy() || dabCommand != DabCommand::None) return false;
  if (replyLength > sizeof(SPIbuffer) - 1U) {
    ++diagDabCommandErrorCount;
    return false;
  }
  if (replyLength > 0) memset(SPIbuffer + 1, 0, replyLength);
  const si468x::Result result = chip.startCommand(
      static_cast<si468x::Command>(command), args, argLength,
      replyLength > 0 ? SPIbuffer + 1 : nullptr, replyLength, timeoutUs);
  if (result == si468x::Result::Pending) {
    dabCommand = operation;
    dabCommandGeneration = dabGeneration;
    if (operation == DabCommand::StopDataService ||
        operation == DabCommand::StopService ||
        operation == DabCommand::StartService ||
        operation == DabCommand::StartDataService) {
      dabSwitch.bindCommand();
    }
    return true;
  }
  finishCommandDiagnostics(result);
  ++diagDabCommandErrorCount;
  DIAG_PRINTF("[DAB/ASYNC] command 0x%02X start failed result=%d\n",
                command, static_cast<int>(result));
  const uint32_t now = millis();
  const dab_scheduler::CtsTimeoutClass ctsClass =
      recordDabCtsResult(result == si468x::Result::Timeout);

  if (operation == DabCommand::StopDataService) {
    if (!dabSwitch.backoff(dab_switch::Resume::StopData,
                           dabSwitch.stopRetries,
                           dab_switch::MAX_STOP_RETRIES, now)) {
      if (ctsClass == dab_scheduler::CtsTimeoutClass::Genuine) {
        dabTransportStalled = true;
        ++diagDabRecoveryRequestCount;
      } else {
        setDabSwitchState(dab_switch::State::Failed);
      }
    }
  } else if (operation == DabCommand::StopService) {
    if (!dabSwitch.backoff(dab_switch::Resume::StopAudio,
                           dabSwitch.stopRetries,
                           dab_switch::MAX_STOP_RETRIES, now)) {
      if (ctsClass == dab_scheduler::CtsTimeoutClass::Genuine) {
        dabTransportStalled = true;
        ++diagDabRecoveryRequestCount;
      } else {
        setDabSwitchState(dab_switch::State::Failed);
      }
    }
  } else if (operation == DabCommand::StartService) {
    if (!dabSwitch.backoff(dab_switch::Resume::StartAudio,
                           dabSwitch.audioRetries,
                           dab_switch::MAX_AUDIO_RETRIES, now)) {
      dabSwitch.audioRetries = 0U;
      dabSwitch.resume = dab_switch::Resume::StartAudio;
      dabSwitch.state = dab_switch::State::Backoff;
      dabSwitch.notBeforeMs = now + dab_switch::DATA_DEFER_MS;
    }
  } else if (operation == DabCommand::StartDataService) {
    if (dabSwitch.backoff(dab_switch::Resume::ResolveData,
                          dabSwitch.dataRetries,
                          dab_switch::MAX_DATA_RETRIES, now)) {
      ++diagDabDataRetryCount;
    } else {
      dabSwitch.deferData(now);
    }
  }
  return false;
}

void DAB::finishDabCommand(void) {
  const DabCommand completed = dabCommand;
  const si468x::Result result = chip.lastResult();
  const uint32_t completedGeneration = dabCommandGeneration;
  dabCommand = DabCommand::None;
  finishCommandDiagnostics(result);

  // Device errors prove that the tuner answered. A timeout after a long host
  // service gap is diagnostic host starvation, not evidence that CTS is lost.
  const dab_scheduler::CtsTimeoutClass ctsClass =
      recordDabCtsResult(result == si468x::Result::Timeout);

  // Reconcile physical probe ownership even if a retune superseded its START.
  // It is independent of the audio/SLS switch command and never updates their
  // active SID/CID fields. An unanswered START is conservatively followed by STOP.
  if (completed == DabCommand::SpiProbeStart || completed == DabCommand::SpiProbeStop) {
    const uint32_t now = millis();
    const bool stopAbsent = completed == DabCommand::SpiProbeStop &&
        result == si468x::Result::DeviceError && chip.lastDeviceError() == 0x03U;
    if (result != si468x::Result::Ok && !stopAbsent) ++diagDabCommandErrorCount;
    if (completed == DabCommand::SpiProbeStart) {
      dabSpiProbe.started(now, result == si468x::Result::Ok,
                          result == si468x::Result::Timeout);
      if (result == si468x::Result::Ok)
        epg::liveEndpoint(dabSpiProbe.spi.sid, dabSpiProbe.spi.cid);
      DIAG_PRINTF("[DAB/SPI/TEST] START result=%d reason=%02X; no SLS STOP issued\n",
                  static_cast<int>(result), static_cast<unsigned>(chip.lastDeviceError()));
    } else {
      dabSpiProbe.stopped(now, result == si468x::Result::Ok || stopAbsent);
      DIAG_PRINTF("[DAB/SPI/TEST] STOP result=%d reason=%02X attempt=%u\n",
                  static_cast<int>(result), static_cast<unsigned>(chip.lastDeviceError()),
                  static_cast<unsigned>(dabSpiProbe.stopAttempts));
    }
    if (!dabSpiProbe.running()) logSpiProbeSummary(
        dabSpiProbe.state == dab_probe::State::CleanupFailed ? "cleanup uncertain" : "finished");
    return;
  }

  if (!dab_scheduler::generationMatches(completedGeneration, dabGeneration)) {
    // A superseded service command may still have changed tuner state before a
    // newer tune request invalidated its metadata generation. Reconcile only
    // the physical audio/data ownership; the new switch request remains the
    // sole authority for what happens next.
    const bool staleNotAvailable = result == si468x::Result::DeviceError &&
                                   chip.lastDeviceError() == 0x03U;
    if (result == si468x::Result::Ok) {
      if (completed == DabCommand::StartService) {
        dabActiveServiceId = dabCommandServiceId;
        dabActiveComponentId = dabCommandComponentId;
        dabActiveServiceValid = true;
      } else if (completed == DabCommand::StartDataService) {
        dabActiveDataServiceId = dabCommandServiceId;
        dabActiveDataComponentId = dabCommandComponentId;
        dabActiveDataServiceValid = true;
      } else if (completed == DabCommand::StopService &&
                 dabCommandServiceId == dabActiveServiceId &&
                 dabCommandComponentId == dabActiveComponentId) {
        dabActiveServiceValid = false;
      } else if (completed == DabCommand::StopDataService &&
                 dabCommandServiceId == dabActiveDataServiceId &&
                 dabCommandComponentId == dabActiveDataComponentId) {
        dabActiveDataServiceValid = false;
      }
    } else if (staleNotAvailable) {
      if (completed == DabCommand::StopService) dabActiveServiceValid = false;
      if (completed == DabCommand::StopDataService)
        dabActiveDataServiceValid = false;
    }
    DIAG_PRINTF("[DAB/ASYNC] stale completion ignored op=%u generation=%u current=%u result=%d\n",
                  static_cast<unsigned>(completed),
                  static_cast<unsigned>(completedGeneration),
                  static_cast<unsigned>(dabGeneration),
                  static_cast<int>(result));
    // Status bits sampled while completing the old command belong to the old
    // multiplex as well. A fresh tune will repopulate all three event flags.
    dabStcPending = false;
    dabDsrvPending = false;
    dabDeviceEventPending = false;
    return;
  }

  if (result != si468x::Result::Ok) {
    ++diagDabCommandErrorCount;
    const uint8_t deviceReason = chip.lastDeviceError();
    if (result == si468x::Result::DeviceError && deviceReason == 0x03U)
      ++diagDabNotAvailableCount;
    // AN649 reason 0x03 (NOT_AVAILABLE) is common for metadata queried before
    // the newly started service has published it. Keep it observable without
    // flooding UART; all other command failures are reported immediately.
    static uint32_t lastNotAvailableLogMs = 0;
    static uint16_t suppressedNotAvailable = 0;
    static uint8_t lastNotAvailableOperation = 0xFFU;
    const uint32_t errorNow = millis();
    const bool notAvailable = result == si468x::Result::DeviceError &&
                              deviceReason == 0x03U;

    if (completed == DabCommand::ServiceListHeader) invalidateDabInventory();
    if (completed == DabCommand::ComponentInfo) {
      if (dabUaCommandListGeneration == dabUaScan.generation &&
          dabUaCommandIndex < dabUaComponentCount) {
        dabSpiIdentities[dabUaCommandIndex] = dab_spi::Identity::Failed;
        dabUaScan.commit(dabUaCommandListGeneration, false);
        dabSpiDiscoveryDirty = true;
      }
      DIAG_PRINTF("[DAB/UA] SID=%08X CID=%08X query failed result=%d reason=0x%02X\n",
                  static_cast<unsigned>(dabCommandServiceId),
                  static_cast<unsigned>(dabCommandComponentId),
                  static_cast<int>(result), static_cast<unsigned>(deviceReason));
      return;  // each inventory entry is attempted once; no EPG recovery
    }

    if (completed == DabCommand::Tune &&
        dabCommandRequestId == dabTuneRequestId &&
        result == si468x::Result::DeviceError && deviceReason == 0x18U) {
      ++diagDabTuneBusyCount;
      dabWaitingForStc = false;
      if (dabTuneBusyRetries < dab_scheduler::DAB_TUNE_BUSY_MAX_RETRIES) {
        ++dabTuneBusyRetries;
        dabTuneRetryNotBeforeMs = errorNow +
            dab_scheduler::DAB_TUNE_BUSY_BACKOFF_MS * dabTuneBusyRetries;
        dabTuneRequestPending = true;
        tunePending = true;
        setDabSwitchState(dab_switch::State::Idle);
        DIAG_PRINTF("[DAB/TUNE] busy index=%u retry=%u/%u\n",
                    static_cast<unsigned>(dabRequestedFrequency),
                    static_cast<unsigned>(dabTuneBusyRetries),
                    static_cast<unsigned>(
                        dab_scheduler::DAB_TUNE_BUSY_MAX_RETRIES));
        return;
      }
      DIAG_PRINTF("[DAB/TUNE] busy retries exhausted index=%u\n",
                  static_cast<unsigned>(dabRequestedFrequency));
    }

    if (completed == DabCommand::AudioInfo && notAvailable) {
      dabAudioInfoValid = false;
      if (dabAudioInfoRetryCount <
          dab_scheduler::DAB_AUDIO_INFO_FAST_RETRIES) {
        ++dabAudioInfoRetryCount;
        const uint32_t retryDelay =
            dab_scheduler::audioInfoRetryDelayMs(dabAudioInfoRetryCount);
        dabAudioInfoNotBeforeMs = errorNow + retryDelay;
        dabAudioRefreshPending = true;
        DIAG_PRINTF("[DAB/AUDIO] NOT_AVAILABLE retry=%u next=%ums\n",
                    static_cast<unsigned>(dabAudioInfoRetryCount),
                    static_cast<unsigned>(retryDelay));
      } else {
        dabAudioInfoRetryCount = 0;
        dabAudioRefreshPending = false;
        dabAudioInfoNotBeforeMs =
            errorNow + dab_scheduler::DAB_AUDIO_INFO_SLOW_RETRY_MS;
        dabAudioNextDueMs = dabAudioInfoNotBeforeMs;
        DIAG_PRINTLN("[DAB/AUDIO] NOT_AVAILABLE deferred to normal interval");
      }
      return;
    }

    const uint8_t completedOperation = static_cast<uint8_t>(completed);
    const bool newNotAvailableGroup =
        lastNotAvailableOperation != completedOperation;
    const bool printNotAvailable = deviceReason == 0x03U &&
        (newNotAvailableGroup || lastNotAvailableLogMs == 0U ||
         static_cast<uint32_t>(errorNow - lastNotAvailableLogMs) >= 5000UL);
    if (deviceReason != 0x03U || printNotAvailable) {
      if (suppressedNotAvailable != 0U) {
        DIAG_PRINTF("[DAB/ASYNC] NOT_AVAILABLE op=%u suppressed=%u\n",
                      static_cast<unsigned>(lastNotAvailableOperation),
                      static_cast<unsigned>(suppressedNotAvailable));
        suppressedNotAvailable = 0;
      }
      DIAG_PRINTF("[DAB/ASYNC] command failed op=%u result=%d reason=0x%02X\n",
                    static_cast<unsigned>(completed), static_cast<int>(result),
                    static_cast<unsigned>(deviceReason));
      if (deviceReason == 0x03U) {
        lastNotAvailableLogMs = errorNow;
        lastNotAvailableOperation = completedOperation;
      }
    } else if (suppressedNotAvailable != 0xFFFFU) {
      ++suppressedNotAvailable;
    }
    const bool currentSwitchCommand = dabSwitch.commandIsCurrent();
    if (completed == DabCommand::StopDataService) {
      if (notAvailable) {
        if (dabCommandServiceId == dabActiveDataServiceId &&
            dabCommandComponentId == dabActiveDataComponentId) {
          dabActiveDataServiceValid = false;
          dataServiceCheck = 0;
        }
        if (currentSwitchCommand) {
          dabSwitch.stopRetries = 0U;
          setDabSwitchState(dab_switch::State::StopAudio);
        }
      } else if (currentSwitchCommand &&
                 dabSwitch.backoff(dab_switch::Resume::StopData,
                                   dabSwitch.stopRetries,
                                   dab_switch::MAX_STOP_RETRIES, errorNow)) {
        DIAG_PRINTF("[DAB/SWITCH] data STOP result=%d retry=%u until=%u\n",
                    static_cast<int>(result),
                    static_cast<unsigned>(dabSwitch.stopRetries),
                    static_cast<unsigned>(dabSwitch.notBeforeMs));
      } else if (currentSwitchCommand &&
                 ctsClass == dab_scheduler::CtsTimeoutClass::Genuine) {
        dabTransportStalled = true;
        ++diagDabRecoveryRequestCount;
        DIAG_PRINTLN("[DAB/SWITCH] data STOP CTS loss; recovery requested");
      } else if (currentSwitchCommand) {
        setDabSwitchState(dab_switch::State::Failed);
      }
    } else if (completed == DabCommand::StopService) {
      if (notAvailable) {
        if (dabCommandServiceId == dabActiveServiceId &&
            dabCommandComponentId == dabActiveComponentId) {
          dabActiveServiceValid = false;
          ServiceStart = false;
        }
        if (currentSwitchCommand) {
          dabSwitch.stopRetries = 0U;
          dabSwitch.notBeforeMs = errorNow + dab_switch::STOP_SETTLE_MS;
          setDabSwitchState(dab_switch::State::ServiceSettle);
        }
      } else if (currentSwitchCommand &&
                 dabSwitch.backoff(dab_switch::Resume::StopAudio,
                                   dabSwitch.stopRetries,
                                   dab_switch::MAX_STOP_RETRIES, errorNow)) {
        DIAG_PRINTF("[DAB/SWITCH] audio STOP result=%d retry=%u until=%u\n",
                    static_cast<int>(result),
                    static_cast<unsigned>(dabSwitch.stopRetries),
                    static_cast<unsigned>(dabSwitch.notBeforeMs));
      } else if (currentSwitchCommand &&
                 ctsClass == dab_scheduler::CtsTimeoutClass::Genuine) {
        dabTransportStalled = true;
        ++diagDabRecoveryRequestCount;
        DIAG_PRINTLN("[DAB/SWITCH] audio STOP CTS loss; recovery requested");
      } else if (currentSwitchCommand) {
        setDabSwitchState(dab_switch::State::Failed);
      }
    }
    if ((completed == DabCommand::Tune || completed == DabCommand::TuneStatus) &&
        dabCommandRequestId == dabTuneRequestId) {
      // Retry an unanswered tune so a wedged transport reaches the bounded
      // stall threshold. A CTS device error is terminal for this request.
      dabTuneRequestPending = result == si468x::Result::Timeout;
      dabWaitingForStc = false;
      tunePending = dabTuneRequestPending;
      setDabSwitchState(dabTuneRequestPending
                            ? dab_switch::State::RequestSwitch
                            : dab_switch::State::Failed);
    }
    if (completed == DabCommand::StartService) {
      ServiceStart = false;
      if (currentSwitchCommand) {
        if (!dabSwitch.backoff(dab_switch::Resume::StartAudio,
                               dabSwitch.audioRetries,
                               dab_switch::MAX_AUDIO_RETRIES, errorNow)) {
          // Keep converging on the newest audio target. Refresh its context and
          // retry later; an explicit 0x03 proves the transport is responsive.
          dabServiceListRefreshPending = signallock;
          dabSwitch.audioRetries = 0U;
          dabSwitch.resume = dab_switch::Resume::StartAudio;
          dabSwitch.state = dab_switch::State::Backoff;
          dabSwitch.notBeforeMs = errorNow + dab_switch::DATA_DEFER_MS;
          DIAG_PRINTF("[DAB/SWITCH] audio start deferred until=%u result=%d reason=0x%02X\n",
                      static_cast<unsigned>(dabSwitch.notBeforeMs),
                      static_cast<int>(result),
                      static_cast<unsigned>(deviceReason));
        }
      }
    }
    if (completed == DabCommand::StartDataService) {
      if (currentSwitchCommand) {
        if (dabSwitch.backoff(dab_switch::Resume::ResolveData,
                              dabSwitch.dataRetries,
                              dab_switch::MAX_DATA_RETRIES, errorNow)) {
          ++diagDabDataRetryCount;
          DIAG_PRINTF("[DAB/DATA] start rejected retry=%u/%u until=%u result=%d reason=0x%02X\n",
                      static_cast<unsigned>(dabSwitch.dataRetries),
                      static_cast<unsigned>(dab_switch::MAX_DATA_RETRIES),
                      static_cast<unsigned>(dabSwitch.notBeforeMs),
                      static_cast<int>(result),
                      static_cast<unsigned>(deviceReason));
        } else {
          dabSwitch.deferData(errorNow);
          DIAG_PRINTF("[DAB/DATA] audio kept; SLS deferred until=%u\n",
                      static_cast<unsigned>(dabSwitch.notBeforeMs));
        }
      }
    }
    return;
  }

  const auto parseOk = [](si468x::Result parseResult) {
    return parseResult == si468x::Result::Ok;
  };

  switch (completed) {
    case DabCommand::ComponentInfo: {
      if (!dabServiceListReady || dabUaCommandIndex >= dabUaComponentCount ||
          dabUaCommandListGeneration != dabUaScan.generation) break;
      // Read LENUA first, then fetch exactly the retained reply. Reuse the
      // vendor parser for all fixed fields and for the raw UA block view.
      const uint16_t length = 28U + SPIbuffer[28];
      const si468x::Result readResult = chip.readCurrentReply(SPIbuffer + 1, length);
      si468x::DabComponentInfo info;
      dabSpiIdentities[dabUaCommandIndex] = dab_spi::Identity::Failed;
      dabSpiDiscoveryDirty = true;
      if (readResult != si468x::Result::Ok ||
          si468x::Si468x::parseDabComponentInfo(SPIbuffer + 1, length, info) !=
              si468x::Result::Ok ||
          !dab_ua::valid(info.userApplicationRaw, info.userApplicationBytes,
                         info.numberOfUserApplications)) {
        dabUaScan.commit(dabUaCommandListGeneration, false);
        DIAG_PRINTF("[DAB/UA] SID=%08X CID=%08X malformed/unreadable list result=%d\n",
                    static_cast<unsigned>(dabCommandServiceId),
                    static_cast<unsigned>(dabCommandComponentId),
                    static_cast<int>(readResult));
        break;  // local diagnostic failure; no application or SLS reset
      }
      const dab_spi::Identity identity = dab_spi::identify(
          info.userApplicationRaw, info.userApplicationBytes,
          info.numberOfUserApplications);
      dabSpiIdentities[dabUaCommandIndex] = identity;
      dabUaScan.commit(dabUaCommandListGeneration, true);
      // Discovery runs quietly without DEBUG. Detailed UA output is optional.
      // Best effort single-slot dump: skip a newer diagnostic, never a query.
      if (!diagnosticDebug || dabUaLogPending) break;
      if (info.userApplicationBytes)
        memcpy(dabUaRaw, info.userApplicationRaw, info.userApplicationBytes);
      memcpy(dabUaLabel, info.label, sizeof(dabUaLabel));
      // Prevent a broadcast label from injecting diagnostic lines/control bytes.
      for (uint8_t i = 0; i < 16U; ++i)
        if (static_cast<uint8_t>(dabUaLabel[i]) < 0x20U && dabUaLabel[i] != '\0')
          dabUaLabel[i] = '?';
      dabUaLanguage = info.language;
      dabUaCount = info.numberOfUserApplications;
      dabUaBytes = info.userApplicationBytes;
      dabUaRemaining = dabUaCount;
      dabUaOffset = 0;
      dabUaDataOffset = 0;
      dabUaReplySid = dabCommandServiceId;
      dabUaReplyCid = dabCommandComponentId;
      dabUaHeaderPending = true;
      dabUaEntryPending = true;
      dabUaLogPending = true;
      dabUaNextLogMs = millis();
      break;
    }

    case DabCommand::Tune:
      if (dabCommandRequestId == dabTuneRequestId && !dabTuneRequestPending) {
        dabTuneBusyRetries = 0;
        dabTuneRetryNotBeforeMs = 0;
        dabWaitingTuneRequestId = dabCommandRequestId;
        dabWaitingForStc = true;
        dabTuneDeadlineMs = millis() + RADIO_DAB_TUNE_TIMEOUT_MS;
        if (diagnosticDebug)
          DIAG_PRINTF("[DAB/ASYNC] tune command accepted request=%u\n",
                        static_cast<unsigned>(dabCommandRequestId));
      }
      break;

    case DabCommand::TuneStatus:
    case DabCommand::SignalStatus: {
      si468x::DabDigradStatus status;
      const si468x::Result parseResult = si468x::Si468x::parseDabDigradStatus(
          SPIbuffer + 1, 23, status);
      if (!parseOk(parseResult)) {
        ++diagDabCommandErrorCount;
        break;
      }
      const bool wasLocked = signallock;
      dabRssi10 = static_cast<int16_t>(status.rssi) * 10;
      fic = status.ficQuality > 100U ? 100U : status.ficQuality;
      cnr = status.cnr;
      signallock = status.valid && status.acquired;
      rfSampleGeneration = dab_scheduler::nextGeneration(rfSampleGeneration);

      if (completed == DabCommand::TuneStatus &&
          dabCommandRequestId == dabTuneRequestId) {
        dabWaitingForStc = false;
        tunePending = false;
        epgContinuous = epgManualPending = epgProductionPaused = false;
        epg::liveReset();
        dabSpiProbe = dab_probe::Probe{};  // new tuned ensemble, no old probe context
        setDabSwitchState(dab_switch::State::Idle);
        if (signallock) {
          dabServiceListRefreshPending = true;
          dabEnsembleRefreshPending = true;
          dabTimeRefreshPending = true;
          resetDabPeriodicDeadlines(millis());
        }
        DIAG_PRINTF("[DAB/ASYNC] tune complete request=%u lock=%u index=%u\n",
                      static_cast<unsigned>(dabCommandRequestId), signallock,
                      status.tuneIndex);
      } else if (completed == DabCommand::SignalStatus &&
                 signallock != wasLocked) {
        // SignalStatus only updates RF state. Metadata is event/period driven;
        // a lock transition is the one allowed trigger for immediate refresh.
        if (signallock) {
          dabServiceListRefreshPending = true;
          dabEnsembleRefreshPending = true;
          dabTimeRefreshPending = true;
          resetDabPeriodicDeadlines(millis());
        }
      }
      break;
    }

    case DabCommand::DsrvHeader: {
      si468x::DsrvHeader header;
      const si468x::Result parseResult = si468x::Si468x::parseDsrvHeader(
          SPIbuffer + 1, 24, header);
      if (!parseOk(parseResult)) {
        ++diagDabCommandErrorCount;
        dabDsrvPending = false;
        break;
      }
      if (header.interruptSource & 0x02U) ++diagDabDsrvOverflowCount;
      const dab_probe::Endpoint audio(dabActiveServiceId, dabActiveComponentId,
                                      dabActiveServiceValid);
      const dab_probe::Endpoint sls(dabActiveDataServiceId, dabActiveDataComponentId,
                                    dabActiveDataServiceValid);
      const dab_probe::Route target = dab_probe::route(
          header.serviceId, header.componentId, header.dataSource,
          audio, sls, dabSpiProbe.spi);
      dabSpiProbe.observe(target, header.dataSource, header.serviceStopped(),
                          header.byteCount > 0U);
      const uint32_t routeNow = millis();
      const uint8_t routeIndex = static_cast<uint8_t>(target);
      const bool feedLegacy = dab_probe::feedLegacyDecoder(
          target, dabSpiProbe.spi.valid,
          dabActiveServiceValid && dab_switch::audioContextUsable(dabSwitch.state));
      // Only diagnostic interpretation: raw UA never enters route() or the
      // legacy decoder gate. Old generation samples cannot supply confirmation.
      if (dabRawUaLog.ready(routeNow, dabUaScan.generation, diagnosticDebug,
                            diagSerial.availableForWrite())) {
        const bool usable = dabActiveServiceValid &&
            dab_switch::audioContextUsable(dabSwitch.state);
        const char* context = !usable ? "inactive" :
            target == dab_probe::Route::Audio ? "audio" :
            target == dab_probe::Route::Sls ? "sls" :
            target == dab_probe::Route::Spi ? "spi" : "unmatched";
        DIAG_PRINTF("[DSRV/UA] raw16_17=0x%04X meaning=%s src=%u source=%s sid=%08X cid=%08X context=%s gen=%u rev=unverified suppressed=%u\n",
                    header.rawUatypeOrRfu,
                    dab_diag::rawMeaning(header.dataSource, header.rawUatypeOrRfu),
                    header.dataSource, dab_diag::sourceName(header.dataSource),
                    static_cast<unsigned>(header.serviceId),
                    static_cast<unsigned>(header.componentId), context,
                    static_cast<unsigned>(dabUaScan.generation),
                    static_cast<unsigned>(dabRawUaLog.suppressed));
        dabRawUaLog.emitted();
      }
      // Capture identity mismatches even when SPI is absent and the probe
      // cannot run. Never guess a CID mask or disable collector switch guards.
      static uint32_t lastIdentityLogMs = 0;
      static bool identityWasLogged = false;
      static uint32_t suppressedIdentities = 0;
      if (diagnosticDebug && target == dab_probe::Route::Ignored && header.byteCount > 0U) {
        if ((!identityWasLogged ||
             static_cast<uint32_t>(routeNow - lastIdentityLogMs) >= 5000U) &&
            diagSerial.availableForWrite() >= 256) {
          identityWasLogged = true;
          lastIdentityLogMs = routeNow;
          DIAG_PRINTF("[DSRV/IDENTITY] src=%u sid=%08X cid=%08X audio=%08X/%08X valid=%u sls=%08X/%08X valid=%u legacy=%u suppressed=%u\n",
                      static_cast<unsigned>(header.dataSource),
                      static_cast<unsigned>(header.serviceId),
                      static_cast<unsigned>(header.componentId),
                      static_cast<unsigned>(audio.sid), static_cast<unsigned>(audio.cid),
                      static_cast<unsigned>(audio.valid),
                      static_cast<unsigned>(sls.sid), static_cast<unsigned>(sls.cid),
                      static_cast<unsigned>(sls.valid), static_cast<unsigned>(feedLegacy),
                      static_cast<unsigned>(suppressedIdentities));
          suppressedIdentities = 0;
        } else dab_probe::increment(suppressedIdentities);
      }
      if (!epgContinuous && dabSpiProbe.running() && diagnosticDebug) {
        if (static_cast<uint32_t>(routeNow - dabSpiProbeLastRouteMs[routeIndex]) >= 500U &&
            diagSerial.availableForWrite() >= 256) {
          dabSpiProbeLastRouteMs[routeIndex] = routeNow;
          DIAG_PRINTF("[DSRV/ROUTE] route=%u src=%u sid=%08X cid=%08X dscty=%u bytes=%u remain=%u state=%u suppressed=%u\n",
                      static_cast<unsigned>(routeIndex),
                      static_cast<unsigned>(header.dataSource),
                      static_cast<unsigned>(header.serviceId),
                      static_cast<unsigned>(header.componentId),
                      static_cast<unsigned>(header.dscType),
                      static_cast<unsigned>(header.byteCount),
                      static_cast<unsigned>(header.buffersRemaining),
                      static_cast<unsigned>(header.serviceState),
                      static_cast<unsigned>(dabSpiProbeSuppressedRoutes));
          dabSpiProbeSuppressedRoutes = 0;
        } else dab_probe::increment(dabSpiProbeSuppressedRoutes);
      }
      if (header.byteCount > 0U) {
        const epg_ingress::PadRoute padRoute = epg_ingress::pad(
            header.dataSource, header.dscType, header.rawUatypeOrRfu,
            header.serviceId, header.componentId,
            dab_probe::Endpoint(dabActiveServiceId,dabActiveComponentId,dabActiveServiceValid),
            xpadSpiPresent(),
            dabActiveServiceValid && dab_switch::audioContextUsable(dabSwitch.state) &&
                !dabServiceRequestPending && !dabTuneRequestPending,
            (header.serviceState==0U || header.serviceState==3U) &&
                !(header.interruptSource & 0x04U));
        if (padRoute == epg_ingress::PadRoute::Epg) {
          if (header.byteCount <= sizeof(SPIbuffer)-25U) {
            epg::livePadEndpoint(dabActiveServiceId,dabActiveComponentId);
            if (epg::liveEnabled()) {
              const si468x::Result readResult = chip.readCurrentReply(
                  SPIbuffer+1,static_cast<uint16_t>(24U+header.byteCount));
              finishCommandDiagnostics(readResult);
              if (readResult==si468x::Result::Ok)
                epg::livePadPayload(SPIbuffer+25,header.byteCount);
              else ++diagDabCommandErrorCount;
            }
          }
        } else if (padRoute == epg_ingress::PadRoute::Discard) {
          ++diagDabStaleMotCount;
        } else if (target == dab_probe::Route::Spi) {
          if (header.dataSource == 0U && header.byteCount <= sizeof(SPIbuffer) - 25U &&
              epg::liveEnabled() && dabSpiProbe.state == dab_probe::State::Observing &&
              dabActiveServiceValid && !dabServiceRequestPending && !dabTuneRequestPending) {
            const si468x::Result readResult = chip.readCurrentReply(
                SPIbuffer + 1, static_cast<uint16_t>(24U + header.byteCount));
            finishCommandDiagnostics(readResult);
            if (readResult == si468x::Result::Ok)
              epg::livePayload(SPIbuffer + 25, header.byteCount);
            else ++diagDabCommandErrorCount;
          }
        } else if (feedLegacy) {
          getServiceData();
        } else if (target != dab_probe::Route::Spi) {
          ++diagDabStaleMotCount;
        }
      }
      dabDsrvPending = header.buffersRemaining > 0;
      break;
    }

    case DabCommand::EventStatus: {
      si468x::DabEventStatus event;
      const si468x::Result parseResult = si468x::Si468x::parseDabEventStatus(
          SPIbuffer + 1, 8, event);
      if (!parseOk(parseResult)) {
        ++diagDabCommandErrorCount;
        break;
      }
      dabDeviceEventPending = false;
      if (event.reconfiguration || event.serviceListInterrupt ||
          event.serviceListAvailable) {
        dabServiceListRefreshPending = true;
        dabEnsembleRefreshPending = true;
      }
      break;
    }

    case DabCommand::ServiceListHeader: {
      const uint16_t listSize = static_cast<uint16_t>(SPIbuffer[5]) |
                                (static_cast<uint16_t>(SPIbuffer[6]) << 8);
      const uint32_t fullLength = static_cast<uint32_t>(listSize) + 6U;
      if (fullLength < 12U || fullLength > sizeof(SPIbuffer) - 1U) {
        invalidateDabInventory();
        ++diagDabCommandErrorCount;
        break;
      }
      const si468x::Result readResult = chip.readCurrentReply(
          SPIbuffer + 1, static_cast<uint16_t>(fullLength));
      finishCommandDiagnostics(readResult);
      if (readResult != si468x::Result::Ok) {
        ++diagDabCommandErrorCount;
        invalidateDabInventory();
        break;
      }
      parseDabServiceListReply(static_cast<uint16_t>(fullLength));
      dabServiceTypeScanIndex = 0;
      dabEnsembleRefreshPending = true;
      requestDabDataServiceEvaluation();
      break;
    }

    case DabCommand::EnsembleInfo: {
      si468x::DabEnsembleInfo info;
      const si468x::Result parseResult = si468x::Si468x::parseDabEnsembleInfo(
          SPIbuffer + 1, 26, info);
      if (!parseOk(parseResult)) {
        ++diagDabCommandErrorCount;
        break;
      }
      if (info.ensembleId == 0 || info.ensembleId == 0xFFFFU) {
        EnsembleInfoSet = false;
        break;
      }
      EnsembleInfoSet = true;
      if (EID[0] == '\0' || EnsembleLabel[0] == '\0') {
        static const char hex[] = "0123456789ABCDEF";
        EID[0] = hex[(info.ensembleId >> 12) & 0x0F];
        EID[1] = hex[(info.ensembleId >> 8) & 0x0F];
        EID[2] = hex[(info.ensembleId >> 4) & 0x0F];
        EID[3] = hex[info.ensembleId & 0x0F];
        EID[4] = '\0';
        memcpy(EnsembleLabel, info.label, 16);
        EnsembleLabel[16] = '\0';
        for (int8_t i = 15; i >= 0 && EnsembleLabel[i] == ' '; --i)
          EnsembleLabel[i] = '\0';
        // AN649 keeps the ensemble label charset in the byte between ECC and
        // the abbreviation mask; the typed helper intentionally leaves it raw.
        EnsembleLabelCharset = SPIbuffer[24];
      }
      if (ensembleEcc == 0 && info.ecc != 0) ensembleEcc = info.ecc;
      break;
    }

    case DabCommand::Time: {
      si468x::DabTimeInfo time;
      const si468x::Result parseResult = si468x::Si468x::parseDabTime(
          SPIbuffer + 1, 11, time);
      if (!parseOk(parseResult)) {
        ++diagDabCommandErrorCount;
        break;
      }
      Year = time.year;
      Months = time.month;
      Days = time.day;
      Hours = time.hour;
      Minutes = time.minute;
      Seconds = time.second;
      dabTimeGeneration = dab_scheduler::nextGeneration(dabTimeGeneration);
      break;
    }

    case DabCommand::ServiceType: {
      const uint8_t index = static_cast<uint8_t>(dabCommandRequestId);
      si468x::DabSubchannelInfo info;
      const si468x::Result parseResult = si468x::Si468x::parseDabSubchannelInfo(
          SPIbuffer + 1, 12, info);
      if (!parseOk(parseResult)) {
        ++diagDabCommandErrorCount;
      } else if (index < numberofservices) {
        service[index].ServiceType = info.serviceMode;
      }
      if (dabServiceTypeScanIndex == index)
        dabServiceTypeScanIndex = static_cast<uint8_t>(index + 1U);
      requestDabDataServiceEvaluation();
      break;
    }

    case DabCommand::AudioInfo: {
      si468x::DabAudioInfo info;
      const si468x::Result parseResult = si468x::Si468x::parseDabAudioInfo(
          SPIbuffer + 1, 10, info);
      if (!parseOk(parseResult)) {
        ++diagDabCommandErrorCount;
      } else if (dabActiveServiceValid &&
                 dabCommandServiceId == dabActiveServiceId &&
                 dabCommandComponentId == dabActiveComponentId) {
        bitrate = info.bitRateKbps;
        samplerate = info.sampleRateHz;
        audiomode = info.audioMode;
        dabAudioInfoValid = true;
        dabAudioInfoRetryCount = 0;
        dabAudioInfoNotBeforeMs = 0;
        dabAudioNextDueMs =
            millis() + dab_scheduler::DAB_AUDIO_INFO_SLOW_RETRY_MS;
        DIAG_PRINTF("[DAB/AUDIO] info ready codec=%s bitrate=%u sample=%u mode=%u\n",
                    servicetype == 4U ? "HE-AAC" :
                    (fm_features::isAudioServiceType(servicetype)
                         ? "MP2" : "loading"),
                    static_cast<unsigned>(bitrate),
                    static_cast<unsigned>(samplerate),
                    static_cast<unsigned>(audiomode));
      }
      break;
    }

    case DabCommand::CurrentSubchannelInfo: {
      si468x::DabSubchannelInfo info;
      const si468x::Result parseResult = si468x::Si468x::parseDabSubchannelInfo(
          SPIbuffer + 1, 12, info);
      if (!parseOk(parseResult)) {
        ++diagDabCommandErrorCount;
      } else if (dabActiveServiceValid &&
                 dabCommandServiceId == dabActiveServiceId &&
                 dabCommandComponentId == dabActiveComponentId) {
        servicetype = info.serviceMode;
        protectionlevel = info.protectionInfo;
        if (dabAudioInfoValid)
          DIAG_PRINTF("[DAB/AUDIO] codec ready=%s bitrate=%u sample=%u mode=%u\n",
                      servicetype == 4U ? "HE-AAC" :
                      (fm_features::isAudioServiceType(servicetype)
                           ? "MP2" : "unknown"),
                      static_cast<unsigned>(bitrate),
                      static_cast<unsigned>(samplerate),
                      static_cast<unsigned>(audiomode));
      }
      break;
    }

    case DabCommand::CurrentServiceInfo: {
      si468x::DabServiceInfo info;
      const si468x::Result parseResult = si468x::Si468x::parseDabServiceInfo(
          SPIbuffer + 1, 26, info);
      if (!parseOk(parseResult)) {
        ++diagDabCommandErrorCount;
      } else if (dab_switch::serviceMetadataCurrent(
                     dabActiveServiceValid, dabServiceRequestPending,
                     dabCommandServiceId, dabCommandComponentId,
                     dabActiveServiceId, dabActiveComponentId,
                     dabRequestedServiceId, dabRequestedComponentId)) {
        memcpy(PStext, info.label, 16);
        PStext[16] = '\0';
        for (int8_t i = 15; i >= 0 && PStext[i] == ' '; --i) PStext[i] = '\0';
        pty = info.pty;
        ServiceLabelCharset = info.charset;
        dabServiceLabelValid = true;
        if (ecc == 0) {
          serviceHasOwnEcc = info.ecc != 0;
          ecc = serviceHasOwnEcc ? info.ecc : ensembleEcc;
        }
      }
      break;
    }

    case DabCommand::StopDataService:
      ++diagDabDataStopCount;
      if (dabActiveDataServiceValid &&
          dabCommandServiceId == dabActiveDataServiceId &&
          dabCommandComponentId == dabActiveDataComponentId) {
        dabActiveDataServiceValid = false;
        dataServiceCheck = 0;
      }
      if (dabSwitch.commandIsCurrent()) {
        dabSwitch.stopRetries = 0U;
        setDabSwitchState(dab_switch::State::StopAudio);
      }
      break;

    case DabCommand::StopService:
      ++diagDabAudioStopCount;
      if (dabActiveServiceValid && dabCommandServiceId == dabActiveServiceId &&
          dabCommandComponentId == dabActiveComponentId) {
        dabActiveServiceValid = false;
        ServiceStart = false;
      }
      if (dabSwitch.commandIsCurrent()) {
        dabSwitch.stopRetries = 0U;
        dabSwitch.notBeforeMs = millis() + dab_switch::STOP_SETTLE_MS;
        setDabSwitchState(dab_switch::State::ServiceSettle);
      }
      break;

    case DabCommand::StartService:
      ++diagDabAudioStartCount;
      dabActiveServiceId = dabCommandServiceId;
      dabActiveComponentId = dabCommandComponentId;
      dabActiveServiceValid = true;
      if (dabSwitch.commandIsCurrent() &&
          dabCommandServiceId == dabRequestedServiceId &&
          dabCommandComponentId == dabRequestedComponentId) {
        dabServiceRequestPending = false;
        ServiceStart = true;
        ServiceInfo();
        resetDabServiceDeadlines(millis());
        dabSwitch.audioRetries = 0U;
        dabSwitch.notBeforeMs = millis() + dab_switch::AUDIO_TO_DATA_SETTLE_MS;
        setDabSwitchState(dab_switch::State::AudioSettle);
      }
      break;

    case DabCommand::StartDataService:
      ++diagDabDataStartCount;
      dataServiceCheck = dabCommandComponentId;
      dabActiveDataServiceId = dabCommandServiceId;
      dabActiveDataComponentId = dabCommandComponentId;
      dabActiveDataServiceValid = true;
      if (dabSwitch.commandIsCurrent() &&
          dabCommandServiceId == dabDataServiceId &&
          dabCommandComponentId == dabDataComponentId) {
        dabSwitch.dataRetries = 0U;
        dabSwitch.dataEvaluationRequested = false;
        dabSwitch.slsContextValid = true;
        setDabSwitchState(dab_switch::State::Ready);
      }
      break;

    case DabCommand::None:
    case DabCommand::SpiProbeStart:  // handled before generation reconciliation
    case DabCommand::SpiProbeStop:
      break;
  }
}

void DAB::setDabSwitchState(dab_switch::State state) {
  if (dabSwitch.state == state) return;
  if (diagnosticDebug)
    DIAG_PRINTF("[DAB/SWITCH] state=%s -> %s request=%u\n",
                dab_switch::stateName(dabSwitch.state),
                dab_switch::stateName(state),
                static_cast<unsigned>(dabSwitch.requestId));
  dabSwitch.state = state;
}

void DAB::requestDabDataServiceEvaluation(void) {
  dabSwitch.requestDataEvaluation();
}

bool DAB::resolveDabDataService(void) {
  if (!dabActiveServiceValid || !dabUaInventoryValid ||
      !dabUaScan.functionalComplete(dabCommand == DabCommand::ComponentInfo)) return false;
  dab_spi::SlideshowSelector selector(dabActiveServiceId, dabActiveComponentId);
  for (uint8_t i = 0; i < dabUaServiceCount; ++i) {
    const UaService& entry = dabUaServices[i];
    for (uint16_t j = entry.first; j < entry.first + entry.count; ++j)
      selector.consider(entry.sid, dabUaComponents[j], dabSpiIdentities[j]);
  }
  if (!selector.selected()) return false;
  dabDataServiceId = dabActiveServiceId;
  dabDataComponentId = selector.componentId();
  return true;
}

void DAB::processDabSwitchState(uint32_t now) {
  dabSwitch.serviceBackoff(now);
  if (dabSwitch.state == dab_switch::State::AudioOnly && dabSwitch.ready(now))
    setDabSwitchState(dab_switch::State::ResolveData);
  if (dabSwitch.state == dab_switch::State::Ready &&
      dabSwitch.dataEvaluationRequested && !dabActiveDataServiceValid)
    setDabSwitchState(dab_switch::State::ResolveData);

  uint8_t args[11] = {0};  // DAB SERTYPE is deliberately zero (AN649).
  switch (dabSwitch.state) {
    case dab_switch::State::RequestSwitch:
      setDabSwitchState(dab_switch::dispatchRequest(
          dabActiveDataServiceValid, dabActiveServiceValid,
          dabTuneRequestPending, dabServiceRequestPending));
      return;

    case dab_switch::State::StopData:
      if (!dabActiveDataServiceValid) {
        setDabSwitchState(dab_switch::State::StopAudio);
        return;
      }
      si468x::writeLe32(args + 3, dabActiveDataServiceId);
      si468x::writeLe32(args + 7, dabActiveDataComponentId);
      dabCommandServiceId = dabActiveDataServiceId;
      dabCommandComponentId = dabActiveDataComponentId;
      if (startDabCommand(DabCommand::StopDataService,
                          static_cast<uint8_t>(si468x::Command::STOP_DIGITAL_SERVICE),
                          args, sizeof(args))) {
        setDabSwitchState(dab_switch::State::WaitDataStop);
        if (diagnosticDebug)
          DIAG_PRINTF("[DAB/DATA] stop SID=%08X CID=%08X\n",
                      static_cast<unsigned>(dabCommandServiceId),
                      static_cast<unsigned>(dabCommandComponentId));
      }
      return;

    case dab_switch::State::StopAudio:
      if (!dabActiveServiceValid) {
        dabSwitch.notBeforeMs = now + dab_switch::STOP_SETTLE_MS;
        setDabSwitchState(dab_switch::State::ServiceSettle);
        return;
      }
      si468x::writeLe32(args + 3, dabActiveServiceId);
      si468x::writeLe32(args + 7, dabActiveComponentId);
      dabCommandServiceId = dabActiveServiceId;
      dabCommandComponentId = dabActiveComponentId;
      if (startDabCommand(DabCommand::StopService,
                          static_cast<uint8_t>(si468x::Command::STOP_DIGITAL_SERVICE),
                          args, sizeof(args))) {
        setDabSwitchState(dab_switch::State::WaitAudioStop);
      }
      return;

    case dab_switch::State::ServiceSettle:
      if (!dabSwitch.ready(now)) return;
      if (dabTuneRequestPending)
        setDabSwitchState(dab_switch::State::Idle);
      else if (dabServiceRequestPending)
        setDabSwitchState(dab_switch::State::StartAudio);
      else
        setDabSwitchState(dab_switch::State::Idle);
      return;

    case dab_switch::State::StartAudio:
      if (!dabServiceRequestPending) {
        setDabSwitchState(dab_switch::State::Idle);
        return;
      }
      si468x::writeLe32(args + 3, dabRequestedServiceId);
      si468x::writeLe32(args + 7, dabRequestedComponentId);
      dabCommandServiceId = dabRequestedServiceId;
      dabCommandComponentId = dabRequestedComponentId;
      if (startDabCommand(DabCommand::StartService,
                          static_cast<uint8_t>(si468x::Command::START_DIGITAL_SERVICE),
                          args, sizeof(args))) {
        setDabSwitchState(dab_switch::State::WaitAudioStart);
      }
      return;

    case dab_switch::State::AudioSettle:
      if (!dabSwitch.ready(now)) return;
      setDabSwitchState(dab_switch::State::ResolveData);
      return;

    case dab_switch::State::ResolveData:
      if (!dabActiveServiceValid) {
        setDabSwitchState(dab_switch::State::Failed);
        return;
      }
      if (dabServiceTypeScanIndex < numberofservices) return;
      if (dabActiveDataServiceValid) {
        dabSwitch.slsContextValid = true;
        setDabSwitchState(dab_switch::State::Ready);
      } else if (resolveDabDataService()) {
        setDabSwitchState(dab_switch::State::StartData);
      } else {
        // Many ensembles carry MOT slideshow in the audio service's PAD and
        // expose no standalone type-3 data service. Once the exact audio
        // START has completed and its settle interval has elapsed, DSRV data
        // belongs to that confirmed audio context and must not be discarded
        // merely because there is no separate START_DATA target.
        dabSwitch.slsContextValid = dab_switch::audioPadSlsContextValid(
            dabActiveServiceValid, false);
        dabSwitch.dataEvaluationRequested = false;
        setDabSwitchState(dab_switch::State::Ready);
      }
      return;

    case dab_switch::State::StartData:
      si468x::writeLe32(args + 3, dabDataServiceId);
      si468x::writeLe32(args + 7, dabDataComponentId);
      dabCommandServiceId = dabDataServiceId;
      dabCommandComponentId = dabDataComponentId;
      if (startDabCommand(DabCommand::StartDataService,
                          static_cast<uint8_t>(si468x::Command::START_DIGITAL_SERVICE),
                          args, sizeof(args))) {
        setDabSwitchState(dab_switch::State::WaitDataStart);
        if (diagnosticDebug)
          DIAG_PRINTF("[DAB/DATA] start SID=%08X CID=%08X\n",
                      static_cast<unsigned>(dabCommandServiceId),
                      static_cast<unsigned>(dabCommandComponentId));
      }
      return;

    case dab_switch::State::Idle:
    case dab_switch::State::WaitDataStop:
    case dab_switch::State::WaitAudioStop:
    case dab_switch::State::WaitTune:
    case dab_switch::State::WaitAudioStart:
    case dab_switch::State::WaitDataStart:
    case dab_switch::State::Backoff:
    case dab_switch::State::Ready:
    case dab_switch::State::AudioOnly:
    case dab_switch::State::Failed:
      return;
  }
}

void DAB::scheduleNextDabCommand(void) {
  if (chip.busy() || dabCommand != DabCommand::None || dabTransportStalled)
    return;

  const uint32_t now = millis();
  // Production packet reception shares the existing serialized START/STOP
  // cleanup owner. Explicit DEBUG SPI still runs its original finite window.
  if (dabSpiProbe.state == dab_probe::State::Idle) {
    epgContinuous = false;
    if (epgManualPending) { epgManualPending = false; requestSpiProbe(); }
    else if (!epgProductionPaused && epg::liveEnabled() && !epg::replayActive() &&
             dabSpiDiscovery.available() && requestSpiProbe()) epgContinuous = true;
  }
  const bool sameProbeContext = (epgContinuous || diagnosticDebug) &&
      (!epgContinuous || (dabSpiPresent && dabSpiDiscovery.status != dab_spi::Status::Ambiguous)) &&
      !epg::replayActive() && signallock && !dabServiceRequestPending && !dabTuneRequestPending &&
      dabSpiProbe.requestId == dabSwitch.requestId && dabActiveServiceValid &&
      dabSpiProbe.audio.matches(dabActiveServiceId, dabActiveComponentId) &&
      dabSpiProbe.sls.valid == dabActiveDataServiceValid &&
      (!dabActiveDataServiceValid ||
       dabSpiProbe.sls.matches(dabActiveDataServiceId, dabActiveDataComponentId));
  // Continuous reception has no observation deadline. Context changes still
  // take the same bounded STOP/cleanup path, before any tune/service teardown.
  if (!(epgContinuous && sameProbeContext && dabSpiProbe.state == dab_probe::State::Observing))
    dabSpiProbe.advance(now, sameProbeContext);
  const bool cleanupPending = dabSpiProbe.state == dab_probe::State::StopRequested;
  if (cleanupPending && dab_probe::due(now, dabSpiProbe.nextActionMs)) {
    uint8_t args[11] = {0};
    si468x::writeLe32(args + 3, dabSpiProbe.spi.sid);
    si468x::writeLe32(args + 7, dabSpiProbe.spi.cid);
    dabSpiProbe.state = dab_probe::State::Stopping;
    ++dabSpiProbe.stopAttempts;
    if (!startDabCommand(DabCommand::SpiProbeStop,
                         static_cast<uint8_t>(si468x::Command::STOP_DIGITAL_SERVICE),
                         args, sizeof(args))) {
      dabSpiProbe.stopped(now, false);
      if (!dabSpiProbe.running()) logSpiProbeSummary("cleanup uncertain");
    }
    return;
  }
  // During bounded STOP backoff keep draining DSRV, but do not issue a tune or
  // another service START/STOP before probe cleanup has had its three attempts.
  if (!cleanupPending) processDabSwitchState(now);
  if (chip.busy() || dabCommand != DabCommand::None) return;

  const uint8_t zero = 0;

  if (!cleanupPending && dabSwitch.state == dab_switch::State::Idle && dabTuneRequestPending) {
      if (!dab_scheduler::retryReady(now, dabTuneRetryNotBeforeMs)) return;
      const uint32_t requestId = dabTuneRequestId;
      dabStcPending = false;
      const si468x::Result result = chip.startDabTune(dabRequestedFrequency);
      if (result == si468x::Result::Pending) {
        dabTuneRequestPending = false;
        dabCommandRequestId = requestId;
        dabCommand = DabCommand::Tune;
        dabCommandGeneration = dabGeneration;
        setDabSwitchState(dab_switch::State::WaitTune);
      } else {
        finishCommandDiagnostics(result);
        ++diagDabCommandErrorCount;
        tunePending = false;
        dabTuneRequestPending = false;
        DIAG_PRINTF("[DAB/ASYNC] tune start failed result=%d\n",
                      static_cast<int>(result));
      }
      return;
  }

  if (dabWaitingForStc &&
      (dabStcPending || static_cast<int32_t>(millis() - dabTuneDeadlineMs) >= 0)) {
    if (!dabStcPending) DIAG_PRINTLN("[DAB/ASYNC] STC timeout; reading final status");
    const uint8_t args[1] = {0x01};
    dabStcPending = false;
    dabCommandRequestId = dabWaitingTuneRequestId;
    startDabCommand(DabCommand::TuneStatus,
                    static_cast<uint8_t>(si468x::Command::DAB_DIGRAD_STATUS),
                    args, sizeof(args), 23);
    return;
  }

  const bool lowPriorityReady =
      static_cast<uint32_t>(now - dabLastLowPriorityCommandMs) >=
      dab_scheduler::DAB_LOW_PRIORITY_GAP_MS;
  const bool uaQueryPending = !cleanupPending && dabServiceListReady &&
      dabActiveServiceValid && signallock && dabUaScan.enumerated &&
      (dabSwitch.state == dab_switch::State::Ready ||
       dabSwitch.state == dab_switch::State::AudioOnly) &&
      dabUaScanIndex < dabUaComponentCount &&
      dab_scheduler::retryReady(now, dabUaNextQueryMs);
  const bool lowPriorityPending = lowPriorityReady &&
      (dabDeviceEventPending ||
       (dabServiceListRefreshPending && signallock) ||
       dabServiceTypeScanIndex < numberofservices ||
       (dabEnsembleRefreshPending && signallock) ||
       (dabTimeRefreshPending && signallock) ||
       (dabAudioRefreshPending && dabActiveServiceValid &&
        dab_scheduler::retryReady(now, dabAudioInfoNotBeforeMs)) ||
       (dabCurrentServiceRefreshPending && dabActiveServiceValid) ||
       (dabCurrentSubchannelRefreshPending && dabActiveServiceValid) ||
       uaQueryPending || dabSpiProbe.state == dab_probe::State::StartRequested);
  const dab_scheduler::BackgroundWork work =
      dab_scheduler::chooseBackgroundWork(
          dabDsrvPending, dabDsrvBurstCount, dabSignalRefreshPending,
          lowPriorityPending);

  if (work == dab_scheduler::BackgroundWork::Dsrv) {
    if (dabDsrvBurstCount >= dab_scheduler::DAB_MAX_DSRV_BURST)
      dabDsrvBurstCount = 0;
    const uint8_t args[1] = {0x01};
    if (startDabCommand(DabCommand::DsrvHeader,
                        static_cast<uint8_t>(si468x::Command::GET_DIGITAL_SERVICE_DATA),
                        args, sizeof(args), 24)) {
      ++dabDsrvBurstCount;
      dabDsrvPending = false;
    }
    return;
  }

  if (work == dab_scheduler::BackgroundWork::Signal) {
    const uint8_t args[1] = {0x08};
    if (startDabCommand(DabCommand::SignalStatus,
                        static_cast<uint8_t>(si468x::Command::DAB_DIGRAD_STATUS),
                        args, sizeof(args), 23))
      dabSignalRefreshPending = false;
    dabDsrvBurstCount = 0;
    return;
  }

  if (work != dab_scheduler::BackgroundWork::LowPriority) return;
  dabDsrvBurstCount = 0;

  if (dabDeviceEventPending) {
    const uint8_t args[1] = {0x01};
    if (startDabCommand(DabCommand::EventStatus,
                        static_cast<uint8_t>(si468x::Command::DAB_GET_EVENT_STATUS),
                        args, sizeof(args), 8)) {
      dabDeviceEventPending = false;
      dabLastLowPriorityCommandMs = now;
    }
    return;
  }

  if (dabServiceListRefreshPending && signallock) {
    if (startDabCommand(DabCommand::ServiceListHeader,
                        static_cast<uint8_t>(si468x::Command::GET_DIGITAL_SERVICE_LIST),
                        &zero, 1, 8)) {
      dabServiceListRefreshPending = false;
      dabLastLowPriorityCommandMs = now;
    } else {
      invalidateDabInventory();
    }
    return;
  }

  if (dabServiceTypeScanIndex < numberofservices) {
    const uint8_t index = dabServiceTypeScanIndex;
    uint8_t args[11] = {0};
    si468x::writeLe32(args + 3, service[index].ServiceID);
    si468x::writeLe32(args + 7, service[index].CompID);
    dabCommandRequestId = index;
    if (startDabCommand(DabCommand::ServiceType,
                        static_cast<uint8_t>(si468x::Command::DAB_GET_SUBCHAN_INFO),
                        args, sizeof(args), 12))
      dabLastLowPriorityCommandMs = now;
    return;
  }

  if (dabEnsembleRefreshPending && signallock) {
    if (startDabCommand(DabCommand::EnsembleInfo,
                        static_cast<uint8_t>(si468x::Command::DAB_GET_ENSEMBLE_INFO),
                        &zero, 1, 26)) {
      dabEnsembleRefreshPending = false;
      dabLastLowPriorityCommandMs = now;
    }
    return;
  }

  if (dabTimeRefreshPending && signallock) {
    if (startDabCommand(DabCommand::Time,
                        static_cast<uint8_t>(si468x::Command::DAB_GET_TIME),
                        &zero, 1, 11)) {
      dabTimeRefreshPending = false;
      dabLastLowPriorityCommandMs = now;
    }
    return;
  }

  if (dabAudioRefreshPending && dabActiveServiceValid) {
    if (!dab_scheduler::retryReady(now, dabAudioInfoNotBeforeMs)) return;
    dabCommandServiceId = dabActiveServiceId;
    dabCommandComponentId = dabActiveComponentId;
    if (startDabCommand(DabCommand::AudioInfo,
                        static_cast<uint8_t>(si468x::Command::DAB_GET_AUDIO_INFO),
                        &zero, 1, 10)) {
      dabAudioRefreshPending = false;
      dabLastLowPriorityCommandMs = now;
    }
    return;
  }

  if (dabCurrentServiceRefreshPending && dabActiveServiceValid) {
    uint8_t args[7] = {0};
    si468x::writeLe32(args + 3, dabActiveServiceId);
    dabCommandServiceId = dabActiveServiceId;
    dabCommandComponentId = dabActiveComponentId;
    if (startDabCommand(DabCommand::CurrentServiceInfo,
                        static_cast<uint8_t>(si468x::Command::DAB_GET_SERVICE_INFO),
                        args, sizeof(args), 26)) {
      dabCurrentServiceRefreshPending = false;
      dabLastLowPriorityCommandMs = now;
    }
    return;
  }

  if (dabCurrentSubchannelRefreshPending && dabActiveServiceValid) {
    uint8_t args[11] = {0};
    si468x::writeLe32(args + 3, dabActiveServiceId);
    si468x::writeLe32(args + 7, dabActiveComponentId);
    dabCommandServiceId = dabActiveServiceId;
    dabCommandComponentId = dabActiveComponentId;
    if (startDabCommand(DabCommand::CurrentSubchannelInfo,
                        static_cast<uint8_t>(si468x::Command::DAB_GET_SUBCHAN_INFO),
                        args, sizeof(args), 12)) {
      dabCurrentSubchannelRefreshPending = false;
      dabLastLowPriorityCommandMs = now;
    }
    return;
  }

  if (dabSpiProbe.state == dab_probe::State::StartRequested) {
    logSpiProbeSummary("baseline complete");
    uint8_t args[11] = {0};
    si468x::writeLe32(args + 3, dabSpiProbe.spi.sid);
    si468x::writeLe32(args + 7, dabSpiProbe.spi.cid);
    dabSpiProbe.state = dab_probe::State::Starting;
    if (!startDabCommand(DabCommand::SpiProbeStart,
                         static_cast<uint8_t>(si468x::Command::START_DIGITAL_SERVICE),
                         args, sizeof(args))) {
      dabSpiProbe.started(now, false, true);
      DIAG_PRINTLN("[DAB/SPI/TEST] START submission failed; conservative STOP scheduled");
    } else dabLastLowPriorityCommandMs = now;
    return;
  }

  // UA discovery, last in the existing low-priority queue. One query per
  // component per inventory/debug activation; failures do not retry forever.
  if (uaQueryPending) {
    const uint16_t index = dabUaScanIndex++;
    for (uint8_t i = 0; i < dabUaServiceCount; ++i) {
      const UaService& entry = dabUaServices[i];
      if (index < entry.first || index >= entry.first + entry.count) continue;
      uint8_t args[11] = {0};
      dabCommandServiceId = entry.sid;
      dabCommandComponentId = dabUaComponents[index];
      dabUaCommandListGeneration = dabUaScan.generation;
      dabUaCommandIndex = index;
      if (!dabUaScan.issue()) return;
      si468x::writeLe32(args + 3, dabCommandServiceId);
      si468x::writeLe32(args + 7, dabCommandComponentId);
      dabUaNextQueryMs = now + 500U;
      if (startDabCommand(DabCommand::ComponentInfo,
                          static_cast<uint8_t>(si468x::Command::DAB_GET_COMPONENT_INFO),
                          args, sizeof(args), 28)) {
        dabLastLowPriorityCommandMs = now;
      } else {
        dabSpiIdentities[index] = dab_spi::Identity::Failed;
        dabUaScan.commit(dabUaCommandListGeneration, false);
        dabSpiDiscoveryDirty = true;
        DIAG_PRINTF("[DAB/UA] SID=%08X CID=%08X query not started\n",
                    static_cast<unsigned>(dabCommandServiceId),
                    static_cast<unsigned>(dabCommandComponentId));
      }
      return;
    }
  }
}

bool DAB::requestSpiProbe(void) {
  if (epg::replayActive() || dabSpiProbe.state == dab_probe::State::CleanupFailed) return false;
  if (epgContinuous && dabSpiProbe.spi.valid) {
    epgManualPending = true;
    dabSpiProbe.stop(millis());
    return true;
  }
  if (isFm() || !dabSpiDiscovery.available() || !dabActiveServiceValid ||
      !signallock || dabServiceRequestPending || dabTuneRequestPending ||
      (dabSwitch.state != dab_switch::State::Ready &&
       dabSwitch.state != dab_switch::State::AudioOnly)) return false;
  const uint32_t now = millis();
  if (!dabSpiProbe.request(
          dab_probe::Endpoint(dabActiveServiceId, dabActiveComponentId),
          dab_probe::Endpoint(dabActiveDataServiceId, dabActiveDataComponentId,
                              dabActiveDataServiceValid),
          dab_probe::Endpoint(dabSpiDiscovery.serviceId, dabSpiDiscovery.componentId),
          dabSwitch.requestId, now)) return false;
  dabSpiProbeLastSummaryMs = now;
  dabSpiProbeSuppressedRoutes = 0;
  for (auto& last : dabSpiProbeLastRouteMs) last = now - 500U;
  dabSpiProbeCtsStart = diagDabCtsTimeoutCount;
  dabSpiProbeOverflowStart = diagDabDsrvOverflowCount;
  dabSpiProbeUartDropStart = diagSerial.droppedBytes();
  DIAG_PRINTF("[DAB/SPI/TEST] baseline 5s then START SID=%08X CID=%08X; observe 30s; audio/SLS kept\n",
              static_cast<unsigned>(dabSpiProbe.spi.sid),
              static_cast<unsigned>(dabSpiProbe.spi.cid));
  return true;
}

void DAB::stopSpiProbe(void) {
  epgManualPending = false; epgProductionPaused = true;
  dabSpiProbe.stop(millis());
  DIAG_PRINTLN("[DAB/SPI/TEST] stop requested");
}

void DAB::logSpiProbeSummary(const char* phase) {
  if (!diagnosticDebug || diagSerial.availableForWrite() < 512) return;
  const dab_probe::Counts& before = dabSpiProbe.before;
  const dab_probe::Counts& after = dabSpiProbe.after;
  DIAG_PRINTF("[DAB/SPI/TEST] %s before audio/pad/sls/spi=%u/%u/%u/%u after=%u/%u/%u/%u ignored=%u SLS_STOP=%u CTS+=%u overflow+=%u UARTdrop+=%u\n",
              phase, static_cast<unsigned>(before.audio), static_cast<unsigned>(before.pad),
              static_cast<unsigned>(before.sls), static_cast<unsigned>(before.spi),
              static_cast<unsigned>(after.audio), static_cast<unsigned>(after.pad),
              static_cast<unsigned>(after.sls), static_cast<unsigned>(after.spi),
              static_cast<unsigned>(after.ignored), static_cast<unsigned>(after.slsStopped),
              static_cast<unsigned>(diagDabCtsTimeoutCount - dabSpiProbeCtsStart),
              static_cast<unsigned>(diagDabDsrvOverflowCount - dabSpiProbeOverflowStart),
              static_cast<unsigned>(diagSerial.droppedBytes() - dabSpiProbeUartDropStart));
  if (after.slsStopped)
    DIAG_PRINTLN("[DAB/SPI/TEST] tuner reported SLS service stopped; verify on HW, no fallback enabled");
  else if (after.spi && (dabSpiProbe.sls.valid ? after.sls : after.pad))
    DIAG_PRINTLN("[DAB/SPI/TEST] distinct SPI and SLS/PAD traffic observed; verify audio and slideshow on HW");
  else
    DIAG_PRINTLN("[DAB/SPI/TEST] coexistence inconclusive; missing packets can be carousel timing");
}

bool DAB::xpadSpiPresent(void) const {
  if(isFm() || !ServiceStart || !dabUaInventoryValid || !dabActiveServiceValid ||
     dabServiceRequestPending || dabTuneRequestPending ||
     !dab_switch::audioContextUsable(dabSwitch.state)) return false;
  for(uint8_t i=0;i<dabUaServiceCount;++i) {
    const UaService& entry=dabUaServices[i];
    if(entry.sid!=dabActiveServiceId || entry.first>dabUaComponentCount ||
       entry.count>dabUaComponentCount-entry.first) continue;
    for(uint16_t j=entry.first;j<entry.first+entry.count;++j)
      if(dabUaComponents[j]==dabActiveComponentId && dab_spi::isSpi(dabSpiIdentities[j]))
        return true;
  }
  return false;
}

void DAB::resetDabSpiDiscovery(bool preservePresence, bool logInventory) {
  const uint16_t previousConfirmed = dabSpiDiscovery.confirmed;
  for (auto& identity : dabSpiIdentities) identity = dab_spi::Identity::Unqueried;
  dabUaRefresh.cancel();
  if (!preservePresence) dabSpiPresent = false;
  dabUaScan.begin(dabUaComponentCount, dabServiceListReady && dabUaInventoryValid);
  dabUaScanIndex = 0;
  dabUaLogPending = false;
  dabUaSummaryStage = 3;
  dabSpiDiscoveryGeneration = 0;
  dabUaListLogService = 0;
  dabUaListLogComponent = 0;
  dabUaListHeaderPending = true;
  dabUaPacketLogPending = false;
  dabUaListLogPending = logInventory && diagnosticDebug && dabUaServiceCount != 0U;
  dabSpiDiscovery = dab_spi::Result{};
  if (preservePresence) dabSpiDiscovery.confirmed = previousConfirmed;
  if (!dabUaInventoryValid && preservePresence) {
    dabSpiDiscovery.status = dab_spi::Status::Incomplete;
  }
  dabSpiDiscoveryDirty = true;
}

void DAB::invalidateDabInventory(void) {
  dabUaInventoryValid = false;
  // Keep the last valid station/component inventory and last known presence,
  // but invalidate all in-flight UA answers and any selected probe endpoint.
  resetDabSpiDiscovery(true, false);
  dabSpiDiscovery.status = dab_spi::Status::Incomplete;
}

void DAB::refreshDabSpiDiscovery(void) {
  const bool invalidInventory = !dabUaInventoryValid &&
      dabSpiDiscovery.status == dab_spi::Status::Incomplete;
  if (!dabSpiDiscoveryDirty) return;
  if (!invalidInventory && (!dabServiceListReady || !dabActiveServiceValid ||
      dabServiceRequestPending ||
      (dabSwitch.state != dab_switch::State::Ready &&
       dabSwitch.state != dab_switch::State::AudioOnly))) return;
  if (dabUaInventoryValid &&
      !dabUaScan.functionalComplete(dabCommand == DabCommand::ComponentInfo)) return;
  dab_spi::Selector selector(dabActiveServiceId);
  for (uint8_t i = 0; i < dabUaServiceCount; ++i) {
    const UaService& entry = dabUaServices[i];
    for (uint16_t j = entry.first; j < entry.first + entry.count; ++j) {
      const bool mot = dab_inventory::Component{dabUaComponents[j]}.packetMot();
      selector.consider(entry.sid, dabUaComponents[j], dabSpiIdentities[j], mot);
    }
  }
  dab_spi::Result result = selector.finish();
  if (!dabUaInventoryValid) {
    result.status = dab_spi::Status::Incomplete;
    result.confirmed = dabSpiDiscovery.confirmed; // last valid evidence, no endpoint
  }
  dabSpiDiscovery = result;
  dabSpiDiscoveryGeneration = dabUaScan.generation;
  if (dabUaInventoryValid) dabSpiPresent = result.confirmedPresent();
  // Only a completed validated inventory can withdraw the selected PAD owner.
  // During routine UA refresh a temporarily unqueried answer merely pauses PAD.
  if(dabUaInventoryValid) {
    const bool pad=xpadSpiPresent();
    if(pad) epg::livePadEndpoint(dabActiveServiceId,dabActiveComponentId);
    else if(dabUaScan.failed==0) epg::livePadEndpoint(0,0);
  }
  dabSpiDiscoveryDirty = false;
  // Invalid inventories require a new valid service list, never a UA-only pass.
  if (dabUaInventoryValid) {
    dabUaRefresh.completed(millis());
    if (!dabActiveDataServiceValid && resolveDabDataService())
      requestDabDataServiceEvaluation();
  }
  dabUaSummaryStage = diagnosticDebug ? 0U : 3U;
}

void DAB::logDabUserApplications(uint32_t now) {
  if (!diagnosticDebug) {
    dabUaDebugWasEnabled = false;
    dabUaLogPending = false;
    dabUaListLogPending = false;
    dabUaSummaryStage = 3U;
    dabXpadLogPending = false;
    return;
  }
  if (!dabUaDebugWasEnabled) {
    dabUaDebugWasEnabled = true;
    // DEBUG affects only diagnostics: never restart an operational scan.
    dabUaListLogService = dabUaListLogComponent = 0;
    dabUaListHeaderPending = true;
    dabUaPacketLogPending = false;
    dabUaListLogPending = dabUaServiceCount != 0U;
    dabUaSummaryStage = dabSpiDiscoveryDirty ? 3U : 0U;
    dabXpadLogPending = true;
  }
  if (dabXpadLogPending) {
    if (!dab_scheduler::retryReady(now, dabUaNextLogMs) ||
        diagSerial.availableForWrite() < 256) return;
    dabUaNextLogMs = now + 100U;
    dabXpadLogPending = false;
    if (dabXpad.valid) {
      DIAG_PRINTF("[DAB/XPAD] requested=0x%04X setResult=%d readback=0x%04X getResult=%d match=%u fw=%s rev=unverified\n",
                  dab_diag::XPAD_REQUESTED, static_cast<int>(dabXpad.setResult),
                  dabXpad.value, static_cast<int>(dabXpad.getResult),
                  dabXpad.matches() ? 1U : 0U, FirmwVersion);
    } else if (dabXpad.getResult == dab_diag::NOT_ATTEMPTED) {
      DIAG_PRINTF("[DAB/XPAD] requested=0x%04X setResult=%d readback=unavailable getResult=not-attempted fw=%s rev=unverified\n",
                  dab_diag::XPAD_REQUESTED, static_cast<int>(dabXpad.setResult), FirmwVersion);
    } else {
      DIAG_PRINTF("[DAB/XPAD] requested=0x%04X setResult=%d readback=unavailable getResult=%d fw=%s rev=unverified\n",
                  dab_diag::XPAD_REQUESTED, static_cast<int>(dabXpad.setResult),
                  static_cast<int>(dabXpad.getResult), FirmwVersion);
    }
    return;
  }
  if (dabUaListLogPending) {
    if (!dab_scheduler::retryReady(now, dabUaNextLogMs) ||
        diagSerial.availableForWrite() < 256) return;
    dabUaNextLogMs = now + 100U;
    const UaService& entry = dabUaServices[dabUaListLogService];
    const dab_inventory::Component component{
        entry.count ? dabUaComponents[entry.first + dabUaListLogComponent] : 0U};
    if (dabUaListHeaderPending) {
      DIAG_PRINTF("[DAB/SVC] SID=%08X PD=%u components=%u version=%u\n",
                  static_cast<unsigned>(entry.sid), entry.info1 & 1U,
                  entry.count, dabUaListVersion);
      dabUaListHeaderPending = false;
    } else if (dabUaPacketLogPending) {
      DIAG_PRINTF("[DAB/SPI] packet-MOT candidate SID=%08X CID=%08X identity=unverified\n",
                  static_cast<unsigned>(entry.sid), static_cast<unsigned>(component.cid));
      dabUaPacketLogPending = false;
      ++dabUaListLogComponent;
    } else {
      DIAG_PRINTF("[DAB/COMP] SID=%08X i=%u CID=%08X TMID=%u %s=%u DG=%u PS=%s CA=%u UAvalid=%u\n",
                  static_cast<unsigned>(entry.sid), dabUaListLogComponent,
                  static_cast<unsigned>(component.cid), component.tmid(),
                  component.tmid() == 0U ? "ASCTy" : "DSCTy", component.type(),
                  component.dataGroups() ? 1U : 0U,
                  component.primary() ? "primary" : "secondary",
                  component.ca() ? 1U : 0U, component.uaValid() ? 1U : 0U);
      if (component.packetMot()) dabUaPacketLogPending = true;
      else ++dabUaListLogComponent;
    }
    if (!dabUaListHeaderPending && dabUaListLogComponent == entry.count) {
      dabUaListLogComponent = 0;
      dabUaListHeaderPending = true;
      if (++dabUaListLogService == dabUaServiceCount) dabUaListLogPending = false;
    }
    return;
  }
  // The published result and inventory are immutable within this generation.
  // Reset/tune discards the summary before any new inventory can be logged.
  if (dabUaSummaryStage < 3U &&
      (dabSpiDiscoveryDirty || dabSpiDiscoveryGeneration != dabUaScan.generation))
    dabUaSummaryStage = 3U;
  if (dabUaSummaryStage < 3U) {
    if (!dab_scheduler::retryReady(now, dabUaNextLogMs) ||
        diagSerial.availableForWrite() < 256) return;
    dabUaNextLogMs = now + 100U;
    if (dabUaSummaryStage == 0U) {
      uint16_t packetMot = 0;
      uint8_t dataServices = 0;
      for (uint8_t i = 0; i < dabUaServiceCount; ++i)
        if (dabUaServices[i].info1 & 1U) ++dataServices;
      for (uint16_t i = 0; i < dabUaComponentCount; ++i)
        if (dab_inventory::Component{dabUaComponents[i]}.packetMot()) ++packetMot;
      DIAG_PRINTF("[DAB/INV] services=%u components=%u audio=%u data=%u packetMOT=%u invalid=%u version=%u\n",
                  dabUaServiceCount, dabUaComponentCount,
                  dabUaServiceCount - dataServices, dataServices, packetMot,
                  dabUaInventoryValid ? 0U : 1U, dabUaListVersion);
    } else if (dabUaSummaryStage == 1U) {
      DIAG_PRINTF("[DAB/UA] scan %s services=%u components=%u processed=%u failed=%u\n",
                  dabUaInventoryValid ? "complete" : "incomplete",
                  dabUaServiceCount, dabUaScan.total, dabUaScan.processed, dabUaScan.failed);
    } else {
      const dab_spi::Result& result = dabSpiDiscovery;
      DIAG_PRINTF("[DAB/SPI] confirmed=%u packetMOTUnverified=%u status=%s selected=%u SID=%08X CID=%08X profile=unknown\n",
                  result.confirmed, result.packetMotUnverified,
                  dab_spi::statusName(result.status), result.available() ? 1U : 0U,
                  static_cast<unsigned>(result.serviceId),
                  static_cast<unsigned>(result.componentId));
    }
    ++dabUaSummaryStage;
    return;
  }
  if (!dabUaLogPending || !dab_scheduler::retryReady(now, dabUaNextLogMs)) return;
  // Leave room for radio/protocol diagnostics. Do not drop half a UA line or
  // wait for UART space. At most one short queued line every 100 ms.
  if (diagSerial.availableForWrite() < 256) return;
  dabUaNextLogMs = now + 100U;
  if (dabUaHeaderPending) {
    DIAG_PRINTF("[DAB/UA] SID=%08X CID=%08X label=\"%s\" lang=%u count=%u bytes=%u\n",
                static_cast<unsigned>(dabUaReplySid),
                static_cast<unsigned>(dabUaReplyCid), dabUaLabel,
                static_cast<unsigned>(dabUaLanguage),
                static_cast<unsigned>(dabUaCount),
                static_cast<unsigned>(dabUaBytes));
    dabUaHeaderPending = false;
    if (dabUaRemaining == 0U) dabUaLogPending = false;
    return;
  }
  // The entire copied block was validated before publication. Reader still
  // checks the remaining slice so future parser changes cannot bypass bounds.
  dab_ua::Reader reader(dabUaRaw + dabUaOffset, dabUaBytes - dabUaOffset,
                        static_cast<uint8_t>(dabUaRemaining));
  dab_ua::Application application;
  if (!reader.next(application)) {
    dabUaLogPending = false;
    return;
  }
  if (dabUaEntryPending) {
    DIAG_PRINTF("[DAB/UA]   type=0x%03X (%s) len=%u\n",
                static_cast<unsigned>(application.type), dab_ua::name(application.type),
                static_cast<unsigned>(application.length));
    dabUaEntryPending = false;
    if (application.length != 0U) return;
  } else {
    char hex[16U * 3U + 1U];
    static const char digits[] = "0123456789ABCDEF";
    const uint16_t left = application.length - dabUaDataOffset;
    const uint16_t count = left > 16U ? 16U : left;
    for (uint16_t i = 0; i < count; ++i) {
      const uint8_t byte = application.data[dabUaDataOffset + i];
      hex[i * 3U] = digits[byte >> 4];
      hex[i * 3U + 1U] = digits[byte & 15U];
      hex[i * 3U + 2U] = ' ';
    }
    hex[count * 3U] = '\0';
    DIAG_PRINTF("[DAB/UA]     data[%u]=%s\n",
                static_cast<unsigned>(dabUaDataOffset), hex);
    dabUaDataOffset += count;
    if (dabUaDataOffset < application.length) return;
  }
  dabUaOffset += 3U + application.length + ((application.length & 1U) ? 0U : 1U);
  dabUaDataOffset = 0;
  dabUaEntryPending = true;
  if (--dabUaRemaining == 0U) dabUaLogPending = false;
}

// Periodic cooperative driver pump. No DAB command waits for CTS or STC here;
// every call advances at most one in-flight command and starts at most one more.
void DAB::Update(bool allowNewWork) {
  if (isFm()) {
    if (radioControlMode == RADIO_CTRL_INTB && radioIntbActive()) {
      // Non-blocking tune/seek commands are completed here rather than through
      // hostIdle(), so record that this CTS service was IRQ-triggered as well.
      if (commandAwaitingCts) commandStatusReadTriggeredByIrq = true;
      chip.notifyInterrupt();
    }
    const bool commandWasBusy = chip.busy();
    const si468x::Result serviceResult = chip.service();
    finishCommandDiagnostics(serviceResult);
    if (commandWasBusy && !chip.busy() && chip.lastDeadlineLatenessUs() > 0U)
      DIAG_PRINTF("[RADIO/CTS] late host service gap=%u us afterDeadline=%u us result=%d (CTS-ready time unknown)\n",
                    static_cast<unsigned>(chip.lastServiceGapUs()),
                    static_cast<unsigned>(chip.lastDeadlineLatenessUs()),
                    static_cast<int>(serviceResult));
    if (allowNewWork) updateFm();
    return;
  }

  // Slideshow collection and rendering use RAM only; no filesystem path is
  // involved in the runtime collector.
  const uint32_t now = millis();
  if (SlideShowInit && SlideShowLastActivity > 0 && now - SlideShowLastActivity > 30000) {
    if (SlideShowDebug) DIAG_PRINTLN("[SLS] Collection timeout, resetting");
    // The buffer contains an incomplete object and therefore remains
    // unavailable. A picture already decoded to TFT GRAM is not erased here.
    resetSlideshowCollector();
  }

  if (radioControlMode == RADIO_CTRL_INTB && radioIntbActive()) {
    if (commandAwaitingCts) commandStatusReadTriggeredByIrq = true;
    chip.notifyInterrupt();
  }
  const bool commandWasBusy = chip.busy();
  const si468x::Result serviceResult = chip.service();
  finishCommandDiagnostics(serviceResult);
  if (commandWasBusy && !chip.busy() && chip.lastDeadlineLatenessUs() > 0U)
    DIAG_PRINTF("[RADIO/CTS] late host service gap=%u us afterDeadline=%u us result=%d (CTS-ready time unknown)\n",
                  static_cast<unsigned>(chip.lastServiceGapUs()),
                  static_cast<unsigned>(chip.lastDeadlineLatenessUs()),
                  static_cast<int>(serviceResult));
  if (dabCommand != DabCommand::None && !chip.busy()) finishDabCommand();

  if (allowNewWork && !chip.busy()) {
    const bool uaRefreshEligible = !isFm() && dabServiceListReady &&
        dabActiveServiceValid && signallock && !dabServiceRequestPending &&
        !dabTuneRequestPending && (!dabSpiProbe.running() || epgContinuous) &&
        (!dabSpiProbe.spi.valid || epgContinuous) &&
        (dabSwitch.state == dab_switch::State::Ready ||
         dabSwitch.state == dab_switch::State::AudioOnly);
    if (dabUaRefresh.ready(now, uaRefreshEligible)) {
      resetDabSpiDiscovery(true, false);
      dabUaNextQueryMs = now;
    }
    refreshDabSpiDiscovery();
    logDabUserApplications(now);
    if (!epgContinuous && dabSpiProbe.running() &&
        static_cast<uint32_t>(now - dabSpiProbeLastSummaryMs) >= 5000U) {
      dabSpiProbeLastSummaryMs = now;
      logSpiProbeSummary("progress");
    }
  }

  if (allowNewWork) updateDabPeriodicRequests(now);

  if (chip.busy()) {
    ++diagDabBusySkipCount;
  } else if (allowNewWork) {
    scheduleNextDabCommand();
  }

  if (now - diagDabLastReportMs >= RADIO_DAB_DIAG_INTERVAL_MS) {
    diagDabLastReportMs = now;
    DIAG_PRINTF("[DAB/IRQ] hw=%s mode=%s edges=%u ctsIrq=%u ctsPoll=%u STC=%u DSRV=%u overflow=%u DEVNT=%u busy=%u errors=%u pending=%02X\n",
                  intbHardwareName(), controlModeName(),
                  static_cast<unsigned>(radioRuntimeIntbEdges()),
                  static_cast<unsigned>(diagCtsIrqCompletionCount),
                  static_cast<unsigned>(diagCtsPollCompletionCount),
                  static_cast<unsigned>(diagDabStcCount),
                  static_cast<unsigned>(diagDabDsrvCount),
                  static_cast<unsigned>(diagDabDsrvOverflowCount),
                  static_cast<unsigned>(diagDabDeviceEventCount),
                  static_cast<unsigned>(diagDabBusySkipCount),
                  static_cast<unsigned>(diagDabCommandErrorCount),
                  static_cast<unsigned>((dabStcPending ? 0x01U : 0U) |
                                        (dabDsrvPending ? 0x10U : 0U) |
                                        (dabDeviceEventPending ? 0x80U : 0U)));
    DIAG_PRINTF("[DAB/SWITCH] state=%s req=%u audio=%u/%u data=%u/%u NA=%u CTS=%u dataRetry=%u abandoned=%u staleMOT=%u recovery=%u current=%08X/%08X requested=%08X/%08X data=%08X/%08X\n",
                dab_switch::stateName(dabSwitch.state),
                static_cast<unsigned>(dabSwitch.requestId),
                static_cast<unsigned>(diagDabAudioStartCount),
                static_cast<unsigned>(diagDabAudioStopCount),
                static_cast<unsigned>(diagDabDataStartCount),
                static_cast<unsigned>(diagDabDataStopCount),
                static_cast<unsigned>(diagDabNotAvailableCount),
                static_cast<unsigned>(diagDabCtsTimeoutCount),
                static_cast<unsigned>(diagDabDataRetryCount),
                static_cast<unsigned>(diagDabAbandonedSwitchCount),
                static_cast<unsigned>(diagDabStaleMotCount),
                static_cast<unsigned>(diagDabRecoveryRequestCount),
                static_cast<unsigned>(dabActiveServiceId),
                static_cast<unsigned>(dabActiveComponentId),
                static_cast<unsigned>(dabRequestedServiceId),
                static_cast<unsigned>(dabRequestedComponentId),
                static_cast<unsigned>(dabActiveDataServiceId),
                static_cast<unsigned>(dabActiveDataComponentId));
    DIAG_PRINTF("[DAB/CTS] host=%u maxGap=%u genuine=%u consecutive=%u tuneBusy=%u\n",
                static_cast<unsigned>(diagDabHostStarvationCount),
                static_cast<unsigned>(diagDabHostStarvationMaxGapUs),
                static_cast<unsigned>(diagDabGenuineCtsTimeoutCount),
                static_cast<unsigned>(dabConsecutiveCtsTimeouts),
                static_cast<unsigned>(diagDabTuneBusyCount));
  }
}

// Encode wchar_t code points into a caller-owned UTF-8 buffer.  This is the
// fixed-allocation counterpart of convertToUTF8() for hot GUI paths.
static void convertToUTF8Buffer(const wchar_t* input, char* output, size_t outputSize) {
  if (!output || outputSize == 0) return;
  output[0] = '\0';
  if (!input) return;

  size_t out = 0;
  while (*input && out + 1 < outputSize) {
    const uint32_t unicode = static_cast<uint32_t>(*input++);
    uint8_t bytes[4];
    size_t count = 0;

    if (unicode < 0x80U) {
      bytes[0] = static_cast<uint8_t>(unicode);
      count = 1;
    } else if (unicode < 0x800U) {
      bytes[0] = static_cast<uint8_t>(0xC0U | (unicode >> 6));
      bytes[1] = static_cast<uint8_t>(0x80U | (unicode & 0x3FU));
      count = 2;
    } else if (unicode < 0x10000U) {
      bytes[0] = static_cast<uint8_t>(0xE0U | (unicode >> 12));
      bytes[1] = static_cast<uint8_t>(0x80U | ((unicode >> 6) & 0x3FU));
      bytes[2] = static_cast<uint8_t>(0x80U | (unicode & 0x3FU));
      count = 3;
    } else {
      bytes[0] = static_cast<uint8_t>(0xF0U | (unicode >> 18));
      bytes[1] = static_cast<uint8_t>(0x80U | ((unicode >> 12) & 0x3FU));
      bytes[2] = static_cast<uint8_t>(0x80U | ((unicode >> 6) & 0x3FU));
      bytes[3] = static_cast<uint8_t>(0x80U | (unicode & 0x3FU));
      count = 4;
    }

    if (out + count >= outputSize) break;
    for (size_t i = 0; i < count; ++i) output[out++] = static_cast<char>(bytes[i]);
  }
  output[out] = '\0';
}

// Convert a fixed DAB/RDS label directly into caller-owned storage.  This keeps
// frequently refreshed station/ensemble-name paths off the heap while preserving
// exactly the same charset handling as ASCII().
void DAB::ASCIIToBuffer(const char* input, uint8_t charset, char* output, size_t outputSize) {
  if (!output || outputSize == 0) return;
  output[0] = '\0';
  if (!input) return;

  if (charset == 0x0F) {
    snprintf(output, outputSize, "%s", input);
    return;
  }

  if (charset == 0x06) {
    wchar_t temp[9];
    size_t out = 0;
    for (size_t i = 0; i + 1 < 16 && out < 8; i += 2) {
      const uint16_t code =
          (static_cast<uint16_t>(static_cast<uint8_t>(input[i])) << 8) |
          static_cast<uint8_t>(input[i + 1]);
      if (code == 0) break;
      temp[out++] = static_cast<wchar_t>(code);
    }
    temp[out] = L'\0';
    convertToUTF8Buffer(temp, output, outputSize);
    return;
  }

  if (charset != 0x00) {
    snprintf(output, outputSize, "%s", input);
    return;
  }

  wchar_t temp[128];
  charConverter(input, temp, sizeof(temp) / sizeof(wchar_t));
  convertToUTF8Buffer(temp, output, outputSize);
}

// qsort() comparator: order services by component ID low byte ascending.
static int compareCompID(const void* a, const void* b) {
  uint32_t compID_a = (*((DABService*)a)).CompID & 0xFF;
  uint32_t compID_b = (*((DABService*)b)).CompID & 0xFF;

  if (compID_a < compID_b) return -1;
  if (compID_a > compID_b) return 1;
  return 0;
}

// ETSI TS 101 756 V1.7.1, charset 0000 (Complete EBU Latin repertoire).
// This is also the Basic RDS character set. Numeric Unicode code points are
// used deliberately so source-file encoding cannot alter the mapping.
static const uint16_t EBU_LATIN_TO_UNICODE[256] = {
  0x0000, 0x0118, 0x012E, 0x0172, 0x0102, 0x0116, 0x010E, 0x0218, 0x021A, 0x010A, 0x0000, 0x0000, 0x0120, 0x0000, 0x017B, 0x0143, // 0x00..0x0F
  0x0105, 0x0119, 0x012F, 0x0173, 0x0103, 0x0117, 0x010F, 0x0219, 0x021B, 0x010B, 0x0147, 0x011A, 0x0121, 0x0139, 0x017C, 0x002D, // 0x10..0x1F
  0x0020, 0x0021, 0x0022, 0x0023, 0x013A, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F, // 0x20..0x2F
  0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F, // 0x30..0x3F
  0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F, // 0x40..0x4F
  0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005A, 0x005B, 0x016E, 0x005D, 0x0141, 0x0142, // 0x50..0x5F
  0x0104, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F, // 0x60..0x6F
  0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007A, 0x00AB, 0x016F, 0x00BB, 0x013D, 0x0126, // 0x70..0x7F
  0x00E1, 0x00E0, 0x00E9, 0x00E8, 0x00ED, 0x00EC, 0x00F3, 0x00F2, 0x00FA, 0x00F9, 0x00D1, 0x00C7, 0x015E, 0x00DF, 0x00A1, 0x0178, // 0x80..0x8F
  0x00E2, 0x00E4, 0x00EA, 0x00EB, 0x00EE, 0x00EF, 0x00F4, 0x00F6, 0x00FB, 0x00FC, 0x00F1, 0x00E7, 0x015F, 0x011F, 0x0131, 0x00FF, // 0x90..0x9F
  0x0136, 0x0145, 0x00A9, 0x0122, 0x011E, 0x011B, 0x0148, 0x0151, 0x0150, 0x20AC, 0x00A3, 0x0024, 0x0100, 0x0112, 0x012A, 0x016A, // 0xA0..0xAF
  0x0137, 0x0146, 0x013B, 0x0123, 0x013C, 0x0130, 0x0144, 0x0171, 0x0170, 0x00BF, 0x013E, 0x00B0, 0x0101, 0x0113, 0x012B, 0x016B, // 0xB0..0xBF
  0x00C1, 0x00C0, 0x00C9, 0x00C8, 0x00CD, 0x00CC, 0x00D3, 0x00D2, 0x00DA, 0x00D9, 0x0158, 0x010C, 0x0160, 0x017D, 0x00D0, 0x013F, // 0xC0..0xCF
  0x00C2, 0x00C4, 0x00CA, 0x00CB, 0x00CE, 0x00CF, 0x00D4, 0x00D6, 0x00DB, 0x00DC, 0x0159, 0x010D, 0x0161, 0x017E, 0x0111, 0x0140, // 0xD0..0xDF
  0x00C3, 0x00C5, 0x00C6, 0x0152, 0x0177, 0x00DD, 0x00D5, 0x00D8, 0x00DE, 0x014A, 0x0154, 0x0106, 0x015A, 0x0179, 0x0164, 0x00F0, // 0xE0..0xEF
  0x00E3, 0x00E5, 0x00E6, 0x0153, 0x0175, 0x00FD, 0x00F5, 0x00F8, 0x00FE, 0x014B, 0x0155, 0x0107, 0x015B, 0x017A, 0x0165, 0x0127, // 0xF0..0xFF
};

// Translate one charset-0000 EBU/RDS byte string into Unicode code points.
// Output is a wchar_t buffer that convertToUTF8() later serialises as UTF-8.
static void charConverter(const char* input, wchar_t* output, size_t outSize) {
  if (!input || !output || outSize == 0) return;

  size_t outIndex = 0;
  for (size_t inIndex = 0;
       input[inIndex] != '\0' && outIndex < outSize - 1;
       ++inIndex) {
    const uint8_t code = static_cast<uint8_t>(input[inIndex]);
    const uint16_t unicode = EBU_LATIN_TO_UNICODE[code];

    // 0x0A/0x0B/0x0D are explicitly non-displayable in the EBU table.
    if (unicode != 0)
      output[outIndex++] = static_cast<wchar_t>(unicode);
  }
  output[outIndex] = L'\0';
}

// Decode the currently assembled DAB Dynamic Label directly into caller-owned
// UTF-8 storage. The explicit source length is essential for UTF-16BE because
// ordinary ASCII-range UTF-16 characters contain zero bytes. This is the
// caller-owned fixed storage used directly by ShowRT().
void DabDynamicLabelTextToBuffer(const char* input, char* output, size_t outputSize) {
  if (!output || outputSize == 0) return;
  output[0] = '\0';
  if (!input || dabDynamicLabelLength == 0) return;

  if (dabDynamicLabelCharset == 0x06) {
    wchar_t temp[65];
    size_t out = 0;
    for (size_t i = 0;
         i + 1 < dabDynamicLabelLength && out < (sizeof(temp) / sizeof(temp[0])) - 1;
         i += 2) {
      const uint16_t code =
          (static_cast<uint16_t>(static_cast<uint8_t>(input[i])) << 8) |
          static_cast<uint8_t>(input[i + 1]);
      if (code == 0) continue;
      // DLS control codes: flatten preferred line/headline breaks for the
      // one-line TFT strip; retain the standard preferred word break as '-'.
      if (code == 0x000AU || code == 0x000BU)
        temp[out++] = L' ';
      else if (code == 0x001FU)
        temp[out++] = L'-';
      else
        temp[out++] = static_cast<wchar_t>(code);
    }
    temp[out] = L'\0';
    convertToUTF8Buffer(temp, output, outputSize);
    return;
  }

  char temp[129];
  const size_t len = min(static_cast<size_t>(dabDynamicLabelLength),
                         sizeof(temp) - 1U);
  memcpy(temp, input, len);
  temp[len] = '\0';

  if (dabDynamicLabelCharset == 0x0F) {
    // Preserve the UTF-8 bytes exactly; only DLS layout hints are flattened.
    for (size_t i = 0; i < len && temp[i] != '\0'; ++i) {
      const uint8_t code = static_cast<uint8_t>(temp[i]);
      if (code == 0x0AU || code == 0x0BU) temp[i] = ' ';
      else if (code == 0x1FU) temp[i] = '-';
    }
    snprintf(output, outputSize, "%s", temp);
    return;
  }

  if (dabDynamicLabelCharset == 0x00) {
    // DLS defines these three bytes as layout hints independently of the
    // selected charset. Flatten them for our one-line ticker before EBU decode.
    for (size_t i = 0; i < len; ++i) {
      const uint8_t code = static_cast<uint8_t>(temp[i]);
      if (code == 0x0AU || code == 0x0BU) temp[i] = ' ';
      else if (code == 0x1FU) temp[i] = '-';
    }
    wchar_t wide[129];
    charConverter(temp, wide, sizeof(wide) / sizeof(wide[0]));
    convertToUTF8Buffer(wide, output, outputSize);
    return;
  }

  snprintf(output, outputSize, "%s", temp);
}
