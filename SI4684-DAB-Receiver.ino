#include "src/epg_runtime.h"
#include "src/epg_ui.h"
// SI4684 DAB/FM Receiver - main Arduino sketch.
//
// HARDWARE WIRING (authoritative for this PCB)
// ---------------------------------------------
// TFT ILI9341 / TFT_eSPI - dedicated VSPI:
//   SCLK  = GPIO18
//   MISO  = GPIO19
//   MOSI  = GPIO23
//   CS    = GPIO5
//   DC    = GPIO4
//   RESET = shared hardware RST net on GPIO17
//
// SI4684 - separate SPI bus:
//   SCLK  = GPIO14
//   MISO  = GPIO16
//   MOSI  = GPIO13
//   CS    = GPIO15
//   RSTB  = shared hardware RST net on GPIO17
//   INTB  = GPIO12 when fitted; one firmware auto-detects it and otherwise polls.
//
// IMPORTANT RESET RULE:
//   GPIO17 resets BOTH the ILI9341 and SI4684. TFT_eSPI is therefore compiled
//   with TFT_RST=-1. GPIO17 is pulsed manually once, then the TFT is
//   reinitialised without letting TFT_eSPI pulse RESET again.
//
// Other subsystems:
//   * TPA6130A2 headphone amp (I2C)
//   * Dual rotary encoders + four buttons
//   * RAM-only MOT slideshow buffer
//   * direct NVS settings, presets, IR profile and FM scan list
//
// setup() initialises everything in a hardware-safe order. loop() is the
// cooperative scheduler for radio, UI, communication and controls.

#include <TFT_eSPI.h>
#include <SPI.h>
#include <TimeLib.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "src/nvs_storage.h"
#include <cstdlib>
#include <Wire.h>
#include "src/font.h"
#include "src/constants.h"
#include "src/graphics.h"
#include "src/language.h"
#include "src/gui.h"
#include "src/comms.h"
#include "src/slideshow.h"
#include "src/si4684.h"
#include "src/TPA6130A2.h"
#include "src/ir_remote.h"
#include "src/fm_features.h"
#include "src/fm_station_list.h"
#include "src/dab_station_list.h"

TPA6130A2 Headphones;
DAB radio;
bool diagnosticDebug = false;
NonBlockingSerialMonitor diagSerial(Serial);

TFT_eSPI tft = TFT_eSPI(240, 320);

bool autoslideshow;
bool ChannelListView;
bool DabGlobalListView;
bool fmScanCompleted;
bool dabScanCompleted;
bool direction;
bool displayreset;
bool memorystore;
bool menu;
bool menuopen;
bool resetFontOnNextCall;
bool seek;
bool setvolume;
bool ShowServiceInformation;
bool SlideShowAvailableOld;
bool SlideShowView;
bool store;
bool trysetservice;
uint8_t trysetserviceFreq = 0xFF;
uint32_t trysetserviceComponentId;
bool trysetserviceComponentValid;
bool tuned;
bool tuning;
bool eepromDirty;
bool dabSeekStarted;
bool fmSeekStarted;
bool radioSwitchMuted;
bool fmAfProbeMuted;
bool dabServiceSelectionPending;
enum class DabAutoState : uint8_t {
  Idle, WaitCurrentServiceTypes, TuneMux, WaitTune, WaitServiceList,
  WaitServiceTypes
};
static DabAutoState dabAutoState = DabAutoState::Idle;
static bool dabAutoForward = true;
static uint8_t dabAutoChannelsTried = 0;
static uint32_t dabAutoDeadlineMs = 0;
enum class FmScanState : uint8_t {
  Idle, WaitBandStart, StartSeek, WaitSeek, RdsDwell
};
static FmScanState fmScanState = FmScanState::Idle;
static uint16_t fmScanFirstFrequency = 0;
static uint16_t fmScanCandidateFrequency = 0;
static uint16_t fmScanSeekCount = 0;
static uint32_t fmScanDeadlineMs = 0;
static constexpr uint32_t FM_SCAN_PS_STABILITY_MS = 4000UL;
static char fmScanPsCandidate[9] = {};
static bool fmScanPsCandidateValid = false;
static bool fmScanPsDynamic = false;
static uint32_t fmScanOverlayUntilMs = 0;
static bool fmScanNeedsMainRedraw = false;
enum class DabFullScanState : uint8_t {
  Idle, TuneChannel, WaitTune, WaitServiceList, WaitServiceTypes,
  WaitListRefresh
};
static DabFullScanState dabFullScanState = DabFullScanState::Idle;
static uint8_t dabScanChannel = 0;
static uint8_t dabScanChannelsTried = 0;
static uint8_t dabScanRestoreChannel = 0;
static uint32_t dabScanRestoreServiceId = 0;
static uint32_t dabScanDeadlineMs = 0;
static uint32_t dabScanTuneListGeneration = 0;
static uint32_t dabScanObservedListGeneration = 0;
static uint32_t dabScanHardDeadlineMs = 0;
static uint32_t dabScanSettleDeadlineMs = 0;
static uint8_t dabScanListRequests = 0;
static bool dabScanStoredAnyList = false;
static uint32_t dabScanOverlayUntilMs = 0;
static bool dabScanNeedsMainRedraw = false;
byte audiomodeold;
byte ContrastSet;
byte CurrentTheme;
byte dabfreq;
byte dabfreqold;
byte displayflip;
byte eccold;
byte ficold;
byte language;
byte memorypos;
byte memoryposold;
byte memoryposstatus;
byte ptyold;
byte rotarymode;
byte servicetypeold;
byte subnetclient;
byte tot;
byte tunemode;
byte unit;
byte volume;
RadioMode radioMode = RADIO_MODE_DAB;
RadioMode requestedRadioMode = RADIO_MODE_DAB;
uint8_t fmRegion = static_cast<uint8_t>(FmRegion::Europe);
uint8_t requestedFmRegion = static_cast<uint8_t>(FmRegion::Europe);
uint8_t fmSeekSensitivity = 1;
bool fmAfEnabled = false;
uint8_t gpio12Mode = GPIO12_AUTO;
uint8_t requestedGpio12Mode = GPIO12_AUTO;
I2sAudioMode i2sAudioMode = I2S_AUDIO_48K;
I2sAudioMode requestedI2sAudioMode = I2S_AUDIO_48K;
uint16_t fmfreq = 8750;
uint16_t fmPendingStationFrequency = 0;
char fmPendingStationPs[9] = "";
char _serviceName[17];
uint8_t _serviceNameCharset;
const uint8_t* currentFont = nullptr;
int ActiveColor;
int ActiveColorSmooth;
int BackgroundColor;
int BackgroundColor2;
int BackgroundColor3;
int BackgroundColor4;
int BarInsignificantColor;
int BarSignificantColor;
int Bitrateupdatetimer;
int GreyoutColor;
int InsignificantColor;
int InsignificantColorSmooth;
int menuoption = ITEM1;
int PrimaryColor;
int PrimaryColorSmooth;
volatile int rotary;
volatile int rotary2;
int rssi;
int rssiold = 200;
int RTlengthold;
int RTWidth;
int SecondaryColor;
int SecondaryColorSmooth;
int SignalLevelold;
int SignificantColor;
int SignificantColorSmooth;
int SNRupdatetimer;
int xPos;
int16_t SAvg;
int16_t SAvg2;
int16_t SignalLevel;
int8_t CNR;
int8_t CNRold;
char clockstringOld[6] = "";
char datestringOld[11] = "";
char EIDold[5] = "";
char EnsembleNameOld[65] = "";
char dabfreqStringOld[12] = "";
char ITUold[4] = "";
char PLold[9] = "";
char PSold[65] = "";
char RTold[RT_TEXT_BUFFER_SIZE] = "";
char SIDold[5] = "";
uint16_t BitrateOld;
uint32_t _serviceID;
uint8_t freq = 0;
uint8_t service = 0;
unsigned long tottimer;
unsigned long rssiTimer;
unsigned long rtticker;
unsigned long rttickerhold;
unsigned long TuningTimer;
unsigned long VolumeTimer;
unsigned long EepromDirtyTimer;
unsigned long RadioSwitchMuteTimer;
unsigned long DabServiceSelectionTimer;

static constexpr uint32_t DAB_SERVICE_SELECTION_DEBOUNCE_MS = 300UL;
static constexpr uint32_t RADIO_STALL_RECOVERY_MIN_INTERVAL_MS = 60000UL;
static constexpr uint32_t LCD_POWER_ON_SETTLE_MS = 1500UL;
static constexpr uint32_t LCD_QUALIFIED_RESET_LOW_MS = 50UL;
static constexpr uint32_t LCD_RESET_RELEASE_SETTLE_MS = 200UL;
static constexpr uint32_t LCD_POST_INIT_RETRY_DELAY_MS = 1000UL;
static uint32_t LastRadioStallRecoveryMs = 0;
static bool RadioStallRecoveryPerformed = false;

struct SettingsSnapshot {
  uint8_t language;
  uint8_t contrast;
  bool autoslideshow;
  uint8_t unit;
  uint8_t tot;
  uint8_t theme;
  RadioMode radioMode;
  uint8_t fmRegion;
  uint8_t fmSeekSensitivity;
  bool fmAfEnabled;
  uint8_t gpio12Mode;
  I2sAudioMode i2sAudioMode;
};

static SettingsSnapshot settingsOriginal{};
static bool settingsSnapshotValid = false;

static const int8_t enc_states[]  = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

typedef struct _Memory {
  byte      Channel;
  uint32_t  ServiceID;
  char      Label[17];
} DABMemory;

typedef struct _FmMemory {
  uint16_t Frequency10kHz;
  uint16_t ProgramId;
  char Label[EE_FM_PRESET_NAME_LENGTH];
} FmMemory;

TFT_eSprite FullLineSprite = TFT_eSprite(&tft);
TFT_eSprite OneBigLineSprite = TFT_eSprite(&tft);
TFT_eSprite ModeSprite = TFT_eSprite(&tft);
TFT_eSprite QualityBarSprite = TFT_eSprite(&tft);

DABMemory memory[EE_PRESETS_CNT];
FmMemory fmMemory[EE_PRESETS_CNT];

// Forward declarations
bool DefaultSettings(void);
void read_encoder(void);
void read_encoder2(void);
void DoMemoryPosTune(void);
void ProcessDAB(void);
void Seek(bool mode);
void StartDabAuto(bool forward);
void ProcessDabAuto(void);
void CancelDabAuto(void);
void StartFmFullScan(void);
void ProcessFmFullScan(void);
void CancelFmFullScan(void);
void StartDabFullScan(void);
void ProcessDabFullScan(void);
void CancelDabFullScan(void);
void Rotary1ButtonProcess(void);
void deepSleep(void);
bool StoreFrequency(void);
void closeVolume(void);
void KeyUp(void);
void KeyDown(void);
void KeyUp2(void);
void KeyDown2(void);
void ShowTuneModeCurrent(void);
void ModeButtonPress(void);
void SlideShowButtonPress(void);
void StandbyButtonPress(void);
void ButtonPress(void);
void Button2Press(void);
void doRecovery(void);
void doStandby(void);
bool ActivateCurrentService(void);
uint8_t ChannelListCount(void);
uint8_t ChannelListIndex(void);
void MoveChannelList(bool forward);
void DABSelectService(bool dir);
bool IsStationEmpty(void);
void LoadPresets(void);
bool SwitchRadioMode(RadioMode newMode, bool force = false);
static void RestoreTftAfterSharedReset(const char* tag);
static void RestoreTftControllerNoReset(const char* tag);
static void WakeTftControllerFromSleep(const char* tag);
void MarkEepromDirty(void);
bool FlushEeprom(void);
void LogRamUsage(const char* tag);
void LogBootRamUsage(const char* tag);
void LogMemoryIntegrity(const char* tag);
void SlideshowReceptionState(bool active);
void CaptureSettingsSnapshot(void);
void ExitSettingsMenu(void);
void CycleTuneMode(void);
void RemoteModeAction(void);
void RemoteModeLongAction(void);
void RemoteVolumeStep(int8_t delta);
void RemoteTuneAction(int8_t direction, bool repeat);
static void RedrawVolumeOverlay(void);
static bool WaitPinHighStable(uint8_t pin, uint32_t stableMs, uint32_t timeoutMs);
static void EnterLightSleep(bool showStandbyScreen);


static bool slsReceiving = false;
bool slsWaitingView = false;
bool slsWaitingOverlayVisible = false;
static bool slsDisplayedFingerprintValid = false;
static uint32_t slsDisplayedHash = 0;
static uint32_t slsDisplayedSize = 0;
static bool standbyWakeReleaseGuard = false;
static bool slsReceiveIndicatorDrawn = false;

// MOT receive progress is intentionally heap-free. The UI keeps only a few
// scalar cache values and paints the 30x4 bar directly to the TFT.
static bool slsProgressDrawn = false;
static uint8_t slsProgressModeOld = 0xFF;
static uint8_t slsProgressPixelsOld = 0xFF;
static uint8_t slsProgressAnimPosOld = 0xFF;

static constexpr int16_t SLS_PROGRESS_X = 10;
static constexpr int16_t SLS_PROGRESS_Y = 211;
static constexpr int16_t SLS_PROGRESS_W = 30;
static constexpr int16_t SLS_PROGRESS_H = 4;
static constexpr uint8_t SLS_PROGRESS_RUNNER_W = 7;
static constexpr uint32_t SLS_PROGRESS_ANIM_MS = 120U;

void SlideshowReceptionState(bool active) {
  if (slsReceiving == active) return;
  slsReceiving = active;

  if (!active) {
    // The red loading bitmap is drawn directly over the normal icon. Force
    // ShowSlideShowIcon() to repaint that region after a tune/service change,
    // timeout, or completed MOT even when availability stayed false.
    SlideShowAvailableOld = !radio.SlideShowAvailable;
  }
  slsReceiveIndicatorDrawn = false;
  slsProgressModeOld = 0xFF;
  slsProgressPixelsOld = 0xFF;
  slsProgressAnimPosOld = 0xFF;

  DIAG_PRINTF("[SLS/UI] reception=%s\n", active ? "IN PROGRESS" : "IDLE");
}

static uint16_t tint565PreserveShade(uint16_t src, uint16_t tint,
                                     uint8_t maxLum) {
  uint8_t sr = (src >> 11) & 0x1F;
  uint8_t sg = (src >> 5) & 0x3F;
  uint8_t sb = src & 0x1F;
  uint8_t lum = static_cast<uint8_t>((sr * 2 + sg + sb * 2) / 5);
  if (maxLum == 0) maxLum = 1;

  uint8_t tr = (tint >> 11) & 0x1F;
  uint8_t tg = (tint >> 5) & 0x3F;
  uint8_t tb = tint & 0x1F;
  uint8_t r = static_cast<uint8_t>(static_cast<uint16_t>(tr) * lum / maxLum);
  uint8_t g = static_cast<uint8_t>(static_cast<uint16_t>(tg) * lum / maxLum);
  uint8_t b = static_cast<uint8_t>(static_cast<uint16_t>(tb) * lum / maxLum);
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

static void DrawNeutralStatusPanel(void) {
  // The volume bitmap contains a loudspeaker icon, so it must not be reused
  // for unrelated status messages. Keep the same compact rounded-panel idiom
  // but draw a neutral, theme-aware surface.
  tft.fillRoundRect(25, 46, 270, 50, 8, BackgroundColor3);
  tft.drawRoundRect(25, 46, 270, 50, 8, ActiveColor);
}

static void DrawSlideshowLoadingIcon(int16_t x, int16_t y, bool flatBackground) {
  // Preserve the slideshow icon artwork and tint only its active
  // pixels red. The neutral panel separately removes the volume loudspeaker.
  uint16_t loadingIcon[30 * 22];
  uint8_t maxLum = 1;
  for (uint16_t i = 0; i < 30U * 22U; ++i) {
    if (slideshowon[i] != slideshowoff[i]) {
      uint16_t px = slideshowon[i];
      uint8_t r = (px >> 11) & 0x1F;
      uint8_t g = (px >> 5) & 0x3F;
      uint8_t b = px & 0x1F;
      uint8_t lum = static_cast<uint8_t>((r * 2 + g + b * 2) / 5);
      if (lum > maxLum) maxLum = lum;
    }
  }
  for (uint16_t i = 0; i < 30U * 22U; ++i) {
    if (slideshowon[i] != slideshowoff[i])
      loadingIcon[i] = tint565PreserveShade(slideshowon[i], TFT_RED, maxLum);
    else
      loadingIcon[i] = flatBackground ? BackgroundColor3 : slideshowoff[i];
  }
  tft.pushImage(x, y, 30, 22, loadingIcon);
}

enum SlsProgressMode : uint8_t {
  SLS_PROGRESS_INDETERMINATE = 0,
  SLS_PROGRESS_EXACT = 1
};

static void RestoreSlideshowProgressBackground(void) {
  // Background[] has a 320-pixel stride, therefore restore one scanline at a
  // time. This avoids a temporary pixel buffer and restores the exact artwork.
  for (int16_t row = 0; row < SLS_PROGRESS_H; ++row) {
    const uint32_t offset =
        static_cast<uint32_t>(SLS_PROGRESS_Y + row) * 320U +
        static_cast<uint32_t>(SLS_PROGRESS_X);
    tft.pushImage(SLS_PROGRESS_X, SLS_PROGRESS_Y + row,
                  SLS_PROGRESS_W, 1, Background + offset);
  }
}

static void DrawSlideshowProgressTrack(uint8_t pixels, uint16_t color) {
  if (pixels > SLS_PROGRESS_W) pixels = SLS_PROGRESS_W;
  tft.fillRect(SLS_PROGRESS_X, SLS_PROGRESS_Y,
               SLS_PROGRESS_W, SLS_PROGRESS_H, GreyoutColor);
  if (pixels != 0U) {
    tft.fillRect(SLS_PROGRESS_X, SLS_PROGRESS_Y,
                 pixels, SLS_PROGRESS_H, color);
  }
  slsProgressDrawn = true;
}

static void DrawSlideshowProgressRunner(uint8_t pos) {
  const uint8_t maxPos = SLS_PROGRESS_W - SLS_PROGRESS_RUNNER_W;
  if (pos > maxPos) pos = maxPos;
  tft.fillRect(SLS_PROGRESS_X, SLS_PROGRESS_Y,
               SLS_PROGRESS_W, SLS_PROGRESS_H, GreyoutColor);
  tft.fillRect(SLS_PROGRESS_X + pos, SLS_PROGRESS_Y,
               SLS_PROGRESS_RUNNER_W, SLS_PROGRESS_H, TFT_RED);
  slsProgressDrawn = true;
}

static void ShowSlideshowReceiveIndicator(void) {
  if (radioMode != RADIO_MODE_DAB || radio.SlideShowAvailable || !slsReceiving) {
    slsReceiveIndicatorDrawn = false;
    if (slsProgressDrawn) {
      RestoreSlideshowProgressBackground();
      slsProgressDrawn = false;
    }
    slsProgressModeOld = 0xFF;
    slsProgressPixelsOld = 0xFF;
    slsProgressAnimPosOld = 0xFF;
    return;
  }

  if (!slsReceiveIndicatorDrawn || displayreset) {
    DrawSlideshowLoadingIcon(10, 187, false);
    slsReceiveIndicatorDrawn = true;
  }

  const uint32_t receivedBytes = radio.slideshowReceivedBytes();
  const uint32_t expectedBytes = radio.slideshowExpectedBytes();

  if (expectedBytes != 0U) {
    // MOT BodySize is authoritative. From this point the bar is exact and uses
    // the theme ActiveColor. Before BodySize is known we deliberately do not
    // estimate progress from segment numbers because that state is not useful
    // in the receiver's actual MOT delivery sequence.
    uint32_t clampedBytes = receivedBytes;
    if (clampedBytes > expectedBytes) clampedBytes = expectedBytes;
    const uint8_t pixels = static_cast<uint8_t>(
        (clampedBytes * static_cast<uint32_t>(SLS_PROGRESS_W)) /
        expectedBytes);

    if (displayreset || slsProgressModeOld != SLS_PROGRESS_EXACT ||
        slsProgressPixelsOld != pixels) {
      DrawSlideshowProgressTrack(pixels, static_cast<uint16_t>(ActiveColor));
      slsProgressModeOld = SLS_PROGRESS_EXACT;
      slsProgressPixelsOld = pixels;
      slsProgressAnimPosOld = 0xFF;
    }
    return;
  }

  // BodySize is not known yet: show only a low-cost red runner. Derive the
  // position directly from millis(), so no timer object, String, sprite, or
  // heap allocation is needed. The triangular phase makes it bounce at both
  // ends.
  const uint8_t travel = SLS_PROGRESS_W - SLS_PROGRESS_RUNNER_W;
  const uint32_t tick = millis() / SLS_PROGRESS_ANIM_MS;
  const uint8_t phase = static_cast<uint8_t>(tick % (2U * travel));
  const uint8_t pos = phase <= travel ? phase : static_cast<uint8_t>(2U * travel - phase);

  if (displayreset || slsProgressModeOld != SLS_PROGRESS_INDETERMINATE ||
      slsProgressAnimPosOld != pos) {
    DrawSlideshowProgressRunner(pos);
    slsProgressModeOld = SLS_PROGRESS_INDETERMINATE;
    slsProgressPixelsOld = 0xFF;
    slsProgressAnimPosOld = pos;
  }
}

static void ShowSlideshowWaitingOverlay(void) {
  // Manual waiting remains a compact overlay while the normal DAB fields keep
  // refreshing underneath. The existing MOT state machine is the sole source.
  DrawNeutralStatusPanel();
  DrawSlideshowLoadingIcon(39, 60, true);
  tftPrint(0, slideshowLoadingText[language], 180, 62,
           PrimaryColor, BackgroundColor3, 16);
  slsWaitingOverlayVisible = true;
}

// EPG shares the slideshow status panel, without slideshow-specific artwork.
void DrawEpgWaitingPanel(const char* text) {
  DrawNeutralStatusPanel();
  // A compact programme/list symbol uses the current theme colours.
  tft.drawRoundRect(38, 59, 27, 24, 3, ActiveColor);
  for (int row = 0; row < 3; ++row)
    tft.drawLine(43, 65 + row * 6, 60, 65 + row * 6, PrimaryColor);
  tftPrintFixed(-1, text, 76, 62, PrimaryColor, BackgroundColor3, 16);
}

static uint32_t SlideshowFingerprint(const uint8_t* data, uint32_t size) {
  uint32_t hash = 2166136261u;
  for (uint32_t i = 0; data && i < size; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

static void RestoreMainDisplayAfterSlideshow(void) {
  BuildDisplay();

  // BuildDisplay draws the static background. When it is called from inside
  // ProcessDAB, loop() clears displayreset before the next ProcessDAB pass, so
  // redraw every dynamic field now instead of leaving cached text invisible.
  ShowSignalLevel();
  ShowRT();
  ShowBitrate();
  ShowEID();
  ShowSID();
  ShowPTY();
  ShowProtectionlevel();
  ShowPS();
  ShowEN();
  ShowAudioMode();
  ShowClock();
  ShowSlideShowIcon();
  ShowSlideshowReceiveIndicator();
  ShowECC();
}

void LogRamUsage(const char* tag) {
  const uint32_t heapTotal = ESP.getHeapSize();
  const uint32_t heapFree = ESP.getFreeHeap();
  const uint32_t heapMin = ESP.getMinFreeHeap();
  const uint32_t heapMax = ESP.getMaxAllocHeap();

  DIAG_PRINTF("[RAM] %s heap total=%u free=%u used=%u minfree=%u maxblock=%u\n",
                tag ? tag : "-",
                heapTotal,
                heapFree,
                heapTotal - heapFree,
                heapMin,
                heapMax);

#if CONFIG_SPIRAM
  DIAG_PRINTF("[RAM] %s psram total=%u free=%u used=%u\n",
                tag ? tag : "-",
                ESP.getPsramSize(),
                ESP.getFreePsram(),
                ESP.getPsramSize() - ESP.getFreePsram());
#endif

  DIAG_PRINTF("[RAM] %s slideshow single MOT buffer=%u bytes\n",
                tag ? tag : "-", (unsigned)radio.slideshowCapacity());
}

// Boot diagnostics must remain visible even when verbose runtime diagnostics
// are disabled: an allocation failure during setup cannot be investigated by
// enabling DEBUG later from the serial console.
void LogBootRamUsage(const char* tag) {
  constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  Serial.printf("[RAM/BOOT] %s free=%u largest=%u minfree=%u\n",
                tag ? tag : "-",
                static_cast<unsigned>(heap_caps_get_free_size(caps)),
                static_cast<unsigned>(heap_caps_get_largest_free_block(caps)),
                static_cast<unsigned>(ESP.getMinFreeHeap()));
}

void LogMemoryIntegrity(const char* tag) {
  // heap_caps_check_integrity_all() validates every heap region and prints
  // allocator diagnostics itself if corruption is detected. Stack HWM is
  // logged in the native FreeRTOS units used by this ESP32 core.
  const bool heapOk = heap_caps_check_integrity_all(true);
  const UBaseType_t stackHwm = uxTaskGetStackHighWaterMark(nullptr);
  DIAG_PRINTF("[MEMCHK] %s heap=%s free=%u min=%u maxblock=%u stackHWM=%u\n",
                tag ? tag : "-", heapOk ? "OK" : "CORRUPT",
                ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
                static_cast<unsigned>(stackHwm));
}

void MarkEepromDirty(void) {
  eepromDirty = true;
  EepromDirtyTimer = millis();
}

static bool CommitEepromNow(const char* reason) {
  if (Storage.commit()) {
    eepromDirty = false;
    return true;
  }

  // Keep the dirty flag set so the periodic 3-second path can retry. Critical
  // callers (restart/standby/IR learn) also receive the failure immediately.
  eepromDirty = true;
  EepromDirtyTimer = millis();
  DIAG_PRINTF("[NVS] ERROR: commit failed (%s); dirty retained\n",
                reason ? reason : "unspecified");
  return false;
}

bool FlushEeprom(void) {
  if (!eepromDirty) return true;
  return CommitEepromNow("flush");
}

static void ClearIrProfileStorage(void) {
  for (int i = EE_IR_CONFIG_START; i < EE_IR_CONFIG_END; ++i)
    Storage.writeByte(i, 0);
}

static void WriteEmptyFmPresetsV4(void) {
  for (int i = 0; i < EE_PRESETS_CNT; ++i) {
    Storage.put(EE_FM_PRESETS_FREQ_START + i * 2,
               static_cast<uint16_t>(EE_FM_PRESET_EMPTY_FREQUENCY));
    Storage.put(EE_FM_PRESETS_PI_START + i * 2, static_cast<uint16_t>(0));
    for (int y = 0; y < EE_FM_PRESET_NAME_LENGTH; ++y)
      Storage.writeByte(EE_FM_PRESETS_NAME_START +
                           i * EE_FM_PRESET_NAME_LENGTH + y,
                       '\0');
  }
}

// DAB records keep their byte-for-byte schema. FM records are loaded
// separately because schema 4+ stores an absolute uint16_t frequency and PI.
void LoadPresets(void) {
  for (int i = 0; i < EE_PRESETS_CNT; ++i) {    if (radioMode == RADIO_MODE_FM) {
      Storage.get(EE_FM_PRESETS_FREQ_START + i * 2,
                 fmMemory[i].Frequency10kHz);
      Storage.get(EE_FM_PRESETS_PI_START + i * 2, fmMemory[i].ProgramId);
      for (int y = 0; y < EE_FM_PRESET_NAME_LENGTH; ++y)
        fmMemory[i].Label[y] = Storage.readByte(
            EE_FM_PRESETS_NAME_START + i * EE_FM_PRESET_NAME_LENGTH + y);
      fmMemory[i].Label[EE_FM_PRESET_NAME_LENGTH - 1] = '\0';
    } else {
      memset(memory[i].Label, 0, sizeof(memory[i].Label));
      memory[i].Channel = Storage.readByte(EE_PRESETS_FREQ_START + i);
      Storage.get(EE_PRESETS_SERVICEID_START + i * 8, memory[i].ServiceID);
      for (int y = 0; y < 16; ++y) memory[i].Label[y] = Storage.readByte(EE_PRESETS_NAME_START + i * 17 + y);
    }
  }

  if (radioMode == RADIO_MODE_FM) {
    radio.numberofservices = 0;
    radio.ServiceIndex = 0;
    for (uint8_t n = 0; n < 32; ++n) {
      radio.service[n].CompID = 0;
      radio.service[n].ServiceID = 0;
      radio.service[n].ServiceType = 8;
      memset(radio.service[n].Label, 0, sizeof(radio.service[n].Label));
    }
    for (int i = 0; i < EE_PRESETS_CNT && radio.numberofservices < 32; ++i) {
      const uint16_t stationFrequency = fmMemory[i].Frequency10kHz;
      if (!isFmFrequencyValid(stationFrequency, fmRegion)) continue;
      bool duplicate = false;
      for (uint8_t n = 0; n < radio.numberofservices; ++n) {
        if (radio.service[n].CompID == stationFrequency) { duplicate = true; break; }
      }
      if (duplicate) continue;
      DABService& station = radio.service[radio.numberofservices++];
      station.CompID = stationFrequency;
      station.ServiceID = fmMemory[i].ProgramId;
      station.ServiceType = 8;
      strncpy(station.Label, fmMemory[i].Label, sizeof(station.Label) - 1);
      if (station.Label[0] == '\0') snprintf(station.Label, sizeof(station.Label), "%.1f MHz", station.CompID / 100.0f);
    }
  }
}

static void RestoreTftControllerNoReset(const char* tag) {
  // GPIO17 is shared with the SI4684, so runtime TFT recovery must never pulse
  // the physical reset line. Re-program only the ILI9341 controller registers.
  // This deliberately avoids another tft.init(): on ESP32 that would register
  // the same SPI/APB callback again and produce addApbChangeCallback() errors.
  analogWrite(CONTRASTPIN, 0);
  pinMode(17, OUTPUT);
  digitalWrite(17, HIGH);
  digitalWrite(15, HIGH);  // keep SI4684 CS inactive during TFT traffic

  DIAG_PRINTF("[%s/TFT] controller restore begin\n",
                tag ? tag : "TFT");

  // Software reset followed by the standard TFT_eSPI ILI9341 initialisation
  // sequence. ESP32 SPI host/sprite/font state remains in RAM; only the panel
  // controller state is rebuilt.
  tft.writecommand(0x01);  // SWRESET
  // Match the conservative reset settle time used by TFT_eSPI. This restore
  // path is used after a real shared reset and on wake, so do not send the
  // controller setup table only 10 ms after SWRESET.
  delay(150);

  tft.writecommand(0xEF); tft.writedata(0x03); tft.writedata(0x80); tft.writedata(0x02);
  tft.writecommand(0xCF); tft.writedata(0x00); tft.writedata(0xC1); tft.writedata(0x30);
  tft.writecommand(0xED); tft.writedata(0x64); tft.writedata(0x03); tft.writedata(0x12); tft.writedata(0x81);
  tft.writecommand(0xE8); tft.writedata(0x85); tft.writedata(0x00); tft.writedata(0x78);
  tft.writecommand(0xCB); tft.writedata(0x39); tft.writedata(0x2C); tft.writedata(0x00); tft.writedata(0x34); tft.writedata(0x02);
  tft.writecommand(0xF7); tft.writedata(0x20);
  tft.writecommand(0xEA); tft.writedata(0x00); tft.writedata(0x00);
  tft.writecommand(0xC0); tft.writedata(0x23);                    // Power control 1
  tft.writecommand(0xC1); tft.writedata(0x10);                    // Power control 2
  tft.writecommand(0xC5); tft.writedata(0x3E); tft.writedata(0x28); // VCOM control 1
  tft.writecommand(0xC7); tft.writedata(0x86);                    // VCOM control 2
  tft.writecommand(0x3A); tft.writedata(0x55);                    // RGB565
  tft.writecommand(0xB1); tft.writedata(0x00); tft.writedata(0x13);
  tft.writecommand(0xB6); tft.writedata(0x08); tft.writedata(0x82); tft.writedata(0x27);
  tft.writecommand(0xF2); tft.writedata(0x00);
  tft.writecommand(0x26); tft.writedata(0x01);

  tft.writecommand(0xE0);
  const uint8_t gammaPos[] = {0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,0x37,0x07,0x10,0x03,0x0E,0x09,0x00};
  for (uint8_t v : gammaPos) tft.writedata(v);

  tft.writecommand(0xE1);
  const uint8_t gammaNeg[] = {0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F};
  for (uint8_t v : gammaNeg) tft.writedata(v);

  tft.writecommand(0x11);  // SLPOUT
  delay(120);
  tft.writecommand(0x29);  // DISPON
  delay(20);

  tft.setRotation(displayflip == 0 ? 3 : 1);
  tft.setSwapBytes(true);
  doTheme();
  tft.fillScreen(BackgroundColor);

  DIAG_PRINTF("[%s/TFT] controller restore complete; backlight OFF\n",
                tag ? tag : "TFT");
}

static void WakeTftControllerFromSleep(const char* tag) {
  // Light sleep never pulses the shared GPIO17 reset. The ILI9341 was put
  // into SLPIN deliberately, so wake it with the matching SLPOUT sequence
  // instead of issuing SWRESET and rebuilding the whole controller. This
  // keeps the panel state transition deterministic and avoids the occasional
  // white-screen wake observed after a software reset.
  analogWrite(CONTRASTPIN, 0);
  pinMode(17, OUTPUT);
  digitalWrite(17, HIGH);
  digitalWrite(15, HIGH);  // SI4684 CS inactive during TFT commands
#ifdef TFT_CS
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif

  DIAG_PRINTF("[%s/TFT] sleep-out wake begin\n", tag ? tag : "WAKE");
  tft.writecommand(0x11);  // SLPOUT
  delay(120);
  tft.writecommand(0x29);  // DISPON
  delay(20);

  // Rotation and byte-order are software state plus one controller command;
  // reassert them before the hidden full redraw. No font/sprite allocation.
  tft.setRotation(displayflip == 0 ? 3 : 1);
  tft.setSwapBytes(true);
  doTheme();
  tft.fillScreen(BackgroundColor);

  DIAG_PRINTF("[%s/TFT] sleep-out wake complete; backlight OFF\n",
                tag ? tag : "WAKE");
}

static void RestoreTftAfterSharedReset(const char* tag) {
  DIAG_PRINTF("[%s] TFT full restore after shared reset begin\n", tag);
  RestoreTftControllerNoReset(tag);
  // Sprite buffers and smooth-font metadata live in ESP32 RAM. Resetting the
  // external ILI9341/SI4684 does not invalidate them, so do not reload fonts
  // or perturb the heap during a band switch/recovery.
  delay(20);
  DIAG_PRINTF("[%s] TFT full restore complete; backlight still OFF\n", tag);
}

static void ShowStatusOverlay(const char* text) {
  // Compact neutral status panel over the current view. Do not reuse the
  // volume bitmap because its embedded loudspeaker has unrelated semantics.
  DrawNeutralStatusPanel();
  tftPrint(0, text ? text : "", 160, 62,
           ActiveColor, BackgroundColor3, 16);
}

static void ShowScanProgressOverlay(const char* text, uint8_t percent) {
  ShowStatusOverlay(text);
  if (percent > 100U) percent = 100U;
  static constexpr int16_t x = 45;
  static constexpr int16_t y = 84;
  static constexpr int16_t width = 230;
  static constexpr int16_t height = 6;
  tft.drawRect(x, y, width, height, ActiveColor);
  tft.fillRect(x + 1, y + 1, width - 2, height - 2, GreyoutColor);
  const int16_t fill = static_cast<int16_t>(
      (static_cast<uint32_t>(width - 2) * percent) / 100U);
  if (fill > 0)
    tft.fillRect(x + 1, y + 1, fill, height - 2, PrimaryColor);
}

bool SwitchRadioMode(RadioMode newMode, bool force) {
  CancelFmFullScan();
  CancelDabFullScan();
  CancelDabAuto();
  const uint32_t switchStartMs = millis();
  DIAG_PRINTF("[SWITCH] request %s -> %s force=%u\n",
                radioMode == RADIO_MODE_FM ? "FM" : "DAB",
                newMode == RADIO_MODE_FM ? "FM" : "DAB",
                force ? 1U : 0U);
  if (!force && newMode == radioMode) {
    DIAG_PRINTLN("[SWITCH] already in requested mode");
    return true;
  }
  const bool modeChanged = newMode != radioMode;
  const bool restoreLiveDab = force && !modeChanged && radioMode == RADIO_MODE_DAB &&
                              radio.ServiceIndex < radio.numberofservices;
  const uint32_t liveSid = restoreLiveDab ? radio.service[radio.ServiceIndex].ServiceID : 0;
  const uint32_t liveCid = restoreLiveDab ? radio.service[radio.ServiceIndex].CompID : 0;
  const char* switchText = newMode == RADIO_MODE_FM
                               ? switchingToFmText[language]
                               : switchingToDabText[language];
  ShowStatusOverlay(switchText);
  // Make the pre-reset overlay perceptible before shared GPIO17 physically
  // blanks the TFT. It is redrawn again immediately after TFT recovery.
  delay(350);
  Headphones.SetMute(true);
  DIAG_PRINTLN("[SWITCH] headphones muted");

  // The TFT shares GPIO17 RESET with the SI4684, so redraw the same compact
  // overlay immediately after TFT recovery and keep it visible during upload.
  const int switchBrightness = ContrastSet * 2 + 27;

  // Hide the ILI9341 reset/default-white interval. The transition overlay has
  // already been visible for 350 ms and will be redrawn while the backlight is
  // off before brightness is restored.
  analogWrite(CONTRASTPIN, 0);

  // GPIO17 is the shared active-low reset line for SI4684 RSTB and TFT RESET.
  // Keep both devices deselected for the complete hardware-reset interval.
#ifdef TFT_CS
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);
  DIAG_PRINTLN("[SWITCH] shared RST GPIO17 LOW (SI4684 + TFT), both CS HIGH");
  pinMode(17, OUTPUT);
  digitalWrite(17, LOW);
  delay(20);
  DIAG_PRINTLN("[SWITCH] shared RST GPIO17 HIGH; settle 20 ms before TFT restore");
  digitalWrite(17, HIGH);
  delay(20);
  // The physical tuner reset has completed. Only now is it safe to invalidate
  // a pre-reset host command and its caller-owned reply pointer.
  radio.onHardwareResetComplete();

  // TFT and radio use separate SPI buses. Restore only the ILI9341 controller
  // registers before the long radio upload; shared GPIO17 remains HIGH.
  RestoreTftAfterSharedReset("SWITCH");
  tft.pushImage(0, 0, 320, 240, Background);
  ShowStatusOverlay(switchText);
  analogWrite(CONTRASTPIN, switchBrightness);
  DIAG_PRINTLN("[SWITCH] transition overlay restored after shared reset");

  DIAG_PRINTF("[SWITCH] radio.begin mode=%s\n", newMode == RADIO_MODE_FM ? "FM" : "DAB");
  if (!radio.begin(15, newMode)) {
    DIAG_PRINTLN("[SWITCH] radio.begin FAILED");
    tft.fillScreen(BackgroundColor);
    tftPrint(0, radioErrorText[language], 160, 110,
             TFT_RED, BackgroundColor, 28);
    analogWrite(CONTRASTPIN, switchBrightness);
    return false;
  }
  DIAG_PRINTLN("[SWITCH] radio.begin OK");

  radioMode = newMode;

  if (modeChanged) {
    Storage.writeByte(EE_BYTE_RADIO_MODE, static_cast<uint8_t>(radioMode));
    MarkEepromDirty();
  }
  LoadPresets();

  seek = false;
  dabSeekStarted = false;
  fmSeekStarted = false;
  tuning = false;
  dabServiceSelectionPending = false;
  trysetservice = false;
  trysetserviceFreq = 0xFF;
  trysetserviceComponentValid = false;
  SlideShowView = false;
  if (radioMode == RADIO_MODE_FM) {
    fmfreq = normalizeFmFrequency(fmfreq, fmRegion);
    uint16_t storedFmFrequency = 0;
    Storage.get(EE_UINT16_FM_FREQUENCY, storedFmFrequency);
    if (storedFmFrequency != fmfreq) {
      Storage.put(EE_UINT16_FM_FREQUENCY, fmfreq);
      MarkEepromDirty();
    }
    radio.setFmFrequency(fmfreq);
  } else {
    radio.setFreq(dabfreq);
    if (restoreLiveDab) _serviceID = liveSid;
    else Storage.get(EE_UINT32_SERVICEID, _serviceID);
    memset(_serviceName, 0, sizeof(_serviceName));
    _serviceNameCharset = 0;
    trysetservice = _serviceID != 0;
    trysetserviceFreq = trysetservice ? dabfreq : 0xFF;
    trysetserviceComponentValid = restoreLiveDab;
    trysetserviceComponentId = liveCid;
  }
  BuildDisplay();
  DIAG_PRINTF("[RADIO/BOOT] total mode switch=%u ms mode=%s control=%s\n",
                static_cast<unsigned>(millis() - switchStartMs),
                radioMode == RADIO_MODE_FM ? "FM" : "DAB",
                radio.controlModeName());
  LogRamUsage(radioMode == RADIO_MODE_FM ? "after FM switch" : "after DAB switch");
  radioSwitchMuted = true;
  RadioSwitchMuteTimer = millis();
  return true;
}

void CaptureSettingsSnapshot(void) {
  settingsOriginal.language = language;
  settingsOriginal.contrast = ContrastSet;
  settingsOriginal.autoslideshow = autoslideshow;
  settingsOriginal.unit = unit;
  settingsOriginal.tot = tot;
  settingsOriginal.theme = CurrentTheme;
  settingsOriginal.radioMode = radioMode;
  settingsOriginal.fmRegion = fmRegion;
  settingsOriginal.fmSeekSensitivity = fmSeekSensitivity;
  settingsOriginal.fmAfEnabled = fmAfEnabled;
  settingsOriginal.gpio12Mode = gpio12Mode;
  settingsOriginal.i2sAudioMode = i2sAudioMode;
  requestedRadioMode = radioMode;
  requestedFmRegion = fmRegion;
  requestedGpio12Mode = gpio12Mode;
  requestedI2sAudioMode = i2sAudioMode;
  settingsSnapshotValid = true;
}

void ExitSettingsMenu(void) {
  if (!menu) return;
  IrRemoteUiAbort();
  if (!settingsSnapshotValid) CaptureSettingsSnapshot();

  const uint8_t selectedRegion = sanitizeFmRegion(requestedFmRegion);
  const uint8_t selectedGpio12 = sanitizeGpio12Mode(requestedGpio12Mode);
  const I2sAudioMode selectedI2s = sanitizeI2sAudioMode(requestedI2sAudioMode);
  const bool i2sChanged = selectedI2s != settingsOriginal.i2sAudioMode;
  const bool gpio12Changed = selectedGpio12 != settingsOriginal.gpio12Mode;
  const bool radioModeChanged = requestedRadioMode != settingsOriginal.radioMode;
  const bool fmRegionChanged = selectedRegion != settingsOriginal.fmRegion;
  const bool fmSeekSensitivityChanged =
      fmSeekSensitivity != settingsOriginal.fmSeekSensitivity;
  const bool fmAfEnabledChanged = fmAfEnabled != settingsOriginal.fmAfEnabled;
  bool settingsChanged = false;

  auto writeByteChanged = [&](int address, uint8_t current, uint8_t original) {
    if (current == original) return;
    Storage.writeByte(address, current);
    settingsChanged = true;
  };

  writeByteChanged(EE_BYTE_LANGUAGE, language, settingsOriginal.language);
  writeByteChanged(EE_BYTE_CONTRASTSET, ContrastSet, settingsOriginal.contrast);
  writeByteChanged(EE_BYTE_AUTOSLIDESHOW, autoslideshow ? 1U : 0U,
                   settingsOriginal.autoslideshow ? 1U : 0U);
  writeByteChanged(EE_BYTE_UNIT, unit, settingsOriginal.unit);
  writeByteChanged(EE_BYTE_TOT, tot, settingsOriginal.tot);
  writeByteChanged(EE_BYTE_THEME, CurrentTheme, settingsOriginal.theme);
  writeByteChanged(EE_BYTE_FM_SEEK_SENSITIVITY, fmSeekSensitivity,
                   settingsOriginal.fmSeekSensitivity);
  writeByteChanged(EE_BYTE_FM_AF_ENABLED, fmAfEnabled ? 1U : 0U,
                   settingsOriginal.fmAfEnabled ? 1U : 0U);

  if (fmRegionChanged) {
    Storage.writeByte(EE_BYTE_FM_REGION, selectedRegion);
    settingsChanged = true;
    const uint16_t previousFrequency = fmfreq;
    fmfreq = normalizeFmFrequency(fmfreq, selectedRegion);
    if (fmfreq != previousFrequency) Storage.put(EE_UINT16_FM_FREQUENCY, fmfreq);
    fmRegion = selectedRegion;
  }

  if (gpio12Changed) {
    Storage.writeByte(EE_BYTE_GPIO12_MODE, selectedGpio12);
    settingsChanged = true;
  }
  if (radioModeChanged) {
    Storage.writeByte(EE_BYTE_RADIO_MODE,
                      static_cast<uint8_t>(requestedRadioMode));
    settingsChanged = true;
  }

  if (i2sChanged) {
    Storage.setI2sAudioMode(selectedI2s);
    settingsChanged = true;
  }
  if (settingsChanged) MarkEepromDirty();
  menu = false;
  menuopen = false;
  settingsSnapshotValid = false;

  if (i2sChanged && !FlushEeprom()) {
    DIAG_PRINTLN("[NVS] I2S selection not committed; radio reload cancelled");
    ShowStatusOverlay("NVS ERROR");
    delay(1200);
    BuildDisplay();
    return;
  }
  if (i2sChanged) {
    i2sAudioMode = selectedI2s;
    radio.setI2sAudioMode(i2sAudioMode);
  }

  if (gpio12Changed) {
    if (!FlushEeprom()) {
      DIAG_PRINTLN("[NVS] GPIO12 setting not committed; restart cancelled");
      ShowStatusOverlay("NVS ERROR");
      delay(1200);
      BuildDisplay();
      return;
    }
    ShowStatusOverlay(restartingText[language]);
    delay(250);
    // Match the FM/DAB transition behaviour: hide the TFT reset/boot interval
    // so the panel cannot flash white or show partial reset contents.
    analogWrite(CONTRASTPIN, 0);
    delay(20);
    ESP.restart();
    return;
  }

  if (fmRegionChanged) {
    const bool stayingInFm = radioMode == RADIO_MODE_FM &&
                             requestedRadioMode == RADIO_MODE_FM;
    radio.setFmRegion(fmRegion, stayingInFm);
    FmStations.load(fmRegion);
    fmScanCompleted = Storage.fmScanCompleted(fmRegion);
    if (stayingInFm) {
      LoadPresets();
      radio.setFmFrequency(fmfreq);
    }
    DIAG_PRINTF("[FM/REGION] selected %s normalized=%u\n",
                  fmRegionProfile(fmRegion).menuName, fmfreq);
  }
  if (fmSeekSensitivityChanged) {
    const bool stayingInFm = radioMode == RADIO_MODE_FM &&
                             requestedRadioMode == RADIO_MODE_FM;
    radio.setFmSeekSensitivity(fmSeekSensitivity, stayingInFm);
  }
  if (fmAfEnabledChanged) radio.setFmAfEnabled(fmAfEnabled);

  if (requestedRadioMode != radioMode || i2sChanged) {
    if (!SwitchRadioMode(requestedRadioMode, i2sChanged)) requestedRadioMode = radioMode;
  } else {
    BuildDisplay();
  }
}

void CycleTuneMode(void) {
  if (EpgVisible()) { EpgBack(); return; }
  // A tune-mode change starts a new input context. Do not let an unfinished
  // AUTO seek or a debounced Rotary2 selection consume the first Rotary1 tick
  // in the newly selected mode.
  seek = false;
  CancelDabAuto();
  dabSeekStarted = false;
  fmSeekStarted = false;
  dabServiceSelectionPending = false;
  CancelFmFullScan();
  CancelDabFullScan();
  const byte previous = tunemode;
  tunemode++;
  if (tunemode > TUNE_MEM) tunemode = TUNE_MAN;
  if (tunemode != previous) {
    Storage.writeByte(EE_BYTE_TUNEMODE, tunemode);
    MarkEepromDirty();
  }
  ShowTuneModeCurrent();
  ShowMemoryPos();
}

void RemoteModeAction(void) {
  tottimer = millis();
  seek = false;
  dabSeekStarted = false;
  fmSeekStarted = false;
  if (menu) ExitSettingsMenu();
  else if (SlideShowView || ChannelListView || ShowServiceInformation) BuildDisplay();
  else CycleTuneMode();
}

// A one-second hold of the learned IR MODE key toggles the radio band. This is
// intentionally separate from RemoteModeAction(): ir_remote.cpp calls exactly
// one of short or long after it has classified the press. Learn/Test intercept
// their IR frames before runtime dispatch, and Settings ignores the long form.
void RemoteModeLongAction(void) {
  tottimer = millis();
  seek = false;
  dabSeekStarted = false;
  fmSeekStarted = false;

  if (menu) {
    DIAG_PRINTLN("[IR/MODE] long DAB/FM switch ignored while Settings is open");
    return;
  }

  const RadioMode target =
      radioMode == RADIO_MODE_DAB ? RADIO_MODE_FM : RADIO_MODE_DAB;
  requestedRadioMode = target;
  DIAG_PRINTF("[IR/MODE] switching %s -> %s\n",
                radioMode == RADIO_MODE_DAB ? "DAB" : "FM",
                target == RADIO_MODE_DAB ? "DAB" : "FM");
  if (!SwitchRadioMode(target)) requestedRadioMode = radioMode;
}

void RemoteVolumeStep(int8_t delta) {
  setvolume = true;
  const int next = constrain(static_cast<int>(volume) + delta, 0, 62);
  if (next != volume) {
    volume = static_cast<byte>(next);
    Storage.writeByte(EE_BYTE_VOLUME, volume);
    MarkEepromDirty();
  }
  ShowVolume();
}

// IR TUNE +/- shares the normal application controls, with two deliberate
// remote-specific rules:
//   - AUTO starts once on the first press; held-key repeats must not restart
//     the already self-running seek/scan.
//   - FM MAN uses the region channel spacing (fine tune) instead of rotary 1's
//     deliberate 1 MHz coarse step, otherwise many stations are unreachable
//     from a remote that has only one TUNE +/- pair.
void RemoteTuneAction(int8_t direction, bool repeat) {
  const bool up = direction > 0;

  // In Settings / service list / slideshow / diagnostics keep exactly the same
  // contextual behavior as the physical primary encoder.
  if (menu || EpgVisible() || ChannelListView || SlideShowView || ShowServiceInformation) {
    if (up) KeyUp(); else KeyDown();
    return;
  }

  // AUTO seek is self-running. One IR press starts it; repeats from the held
  // key are ignored until the key is released and pressed again.
  if (repeat && tunemode == TUNE_AUTO) return;

  // FM manual tuning needs the fine region spacing on IR. Do not call KeyUp2/
  // KeyDown2 directly because rotary 2 has a separate volume-mode behavior.
  if (radioMode == RADIO_MODE_FM && tunemode == TUNE_MAN) {
    if (setvolume) closeVolume();
    tottimer = millis();
    rotary = 0;
    rotary2 = 0;

    const uint8_t spacing = fmRegionProfile(fmRegion).seekSpacing10kHz;
    const int16_t step = up ? static_cast<int16_t>(spacing)
                            : -static_cast<int16_t>(spacing);
    fmfreq = stepFmFrequency(fmfreq, step, fmRegion);
    tuning = true;
    TuningTimer = millis();
    radio.ServiceIndex = 0;
    radio.ServiceStart = false;
    memset(_serviceName, 0, sizeof(_serviceName));
    _serviceNameCharset = 0;
    ShowFreq();
    DIAG_PRINTF("[FM/UI] IR MAN %s -> %c%u kHz, %.1f MHz\n",
                  up ? "UP" : "DOWN", up ? '+' : '-',
                  static_cast<unsigned>(spacing) * 10U,
                  fmfreq / 100.0f);
    return;
  }

  if (up) KeyUp(); else KeyDown();
}

// Repaint only the visual volume panel. Unlike ShowVolume(), this helper does
// not touch the headphone amplifier and does not restart VolumeTimer. It is
// used as the final compositing pass while the overlay is visible, just like
// the slideshow-loading overlay is repainted after dynamic widgets below it.
static void RedrawVolumeOverlay(void) {
  uint8_t segments = map(volume, 0, 63, 0, 100);
  if (segments > 100) segments = 100;

  // Restore the complete 270x50 artwork first. OneBigLineSprite is
  // intentionally only 270x30, so it must never be used as the sole backing
  // store for this 50-pixel-high overlay: doing that left the lower 20 pixels
  // as a flat rectangle and visibly cut through the loudspeaker artwork.
  tft.pushImage(25, 46, 270, 50, volumebackground);

  // Only the upper 30 pixels need a sprite because the moving volume bar lives
  // there. Limit pushImage to the actual sprite height instead of relying on
  // clipping a 270x50 source into a 270x30 sprite.
  OneBigLineSprite.pushImage(0, 0, 270, 30, volumebackground);
  OneBigLineSprite.fillRect(60, 9, 2 * segments, 9, BarInsignificantColor);
  OneBigLineSprite.pushSprite(25, 46);

  char value[5];
  snprintf(value, sizeof(value), "%ld",
           static_cast<long>(map(volume, 0, 62, 0, 100)));
  tftPrintFixed(0, value, 190, 68,
                ActiveColor, ActiveColorSmooth, 28);

  // Match the slideshow-loading visual treatment: draw the crisp theme frame
  // after every content redraw so no dynamic widget can weaken its edge.
  tft.drawRoundRect(25, 46, 270, 50, 8, ActiveColor);

}


// Edge-detect helper for buttons that have a single, immediate action.
// Returns true exactly once per physical press; rearmed only after the
// button is released. This keeps the main loop responsive (no blocking
// "wait for release" loops) while still firing the action just once.
static bool buttonEdge(uint8_t pin, bool& armed) {
  bool pressed = (digitalRead(pin) == LOW);
  if (!pressed) { armed = true; return false; }
  if (!armed) return false;
  armed = false;
  return true;
}

// One-shot initialisation called by the Arduino framework after boot.
// Order matters: Serial → GPIO drive → NVS → audio → display → radio →
// menu state → encoders.
void setup(void) {
  // Establish safe external-peripheral levels before Serial, NVS or heap
  // setup. On a cold power ramp the ESP32 bootloader leaves these GPIOs
  // uncontrolled long enough for some ILI9341 modules to leave reset too soon.
  // Holding the shared reset LOW throughout early setup gives both the LCD and
  // SI4684 a deterministic reset after their supplies have stabilised.
#ifdef TFT_CS
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);
  pinMode(17, OUTPUT);
  digitalWrite(17, LOW);
  pinMode(CONTRASTPIN, OUTPUT);
  digitalWrite(CONTRASTPIN, LOW);

  // Keep the ESP32 brownout detector enabled so undervoltage resets remain
  // distinguishable from software failures.
  diagSerial.begin(115200);
  delay(500);

  DIAG_PRINTLN();
  DIAG_PRINTLN("==================================================");
  DIAG_PRINTLN("SI4684 FM/DAB DIAGNOSTIC BOOT");
  DIAG_PRINTLN("==================================================");
  DIAG_PRINTF("[BOOT] reset reason=%d\n", static_cast<int>(esp_reset_reason()));
  DIAG_PRINTF("[BOOT] free heap=%u min free heap=%u\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap());
  DIAG_PRINTLN("[BOOT] setup() entered");

  // Reduce drive strength on GPIOs sharing PCB tracks with the TFT/SPI
  // to lower EMI and crosstalk.
  gpio_set_drive_capability((gpio_num_t) 4, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 5, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 13, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 14, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 15, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 16, GPIO_DRIVE_CAP_0);
  // GPIO17 drives two reset inputs and is not a high-frequency signal. Keep a
  // stronger edge here; the weakest drive setting can release the shared reset
  // too slowly on boards with higher reset-net capacitance.
  gpio_set_drive_capability((gpio_num_t) 17, GPIO_DRIVE_CAP_2);
  gpio_set_drive_capability((gpio_num_t) 21, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 22, GPIO_DRIVE_CAP_0);
  DIAG_PRINTF("[BOOT] GPIO drive config OK; heap=%u\n", ESP.getFreeHeap());

  // FM presets are small, fixed-size lifetime state: keep them in .bss so
  // startup no longer consumes a heap block for this 99-entry cache.
  memset(fmMemory, 0, sizeof(fmMemory));
  DIAG_PRINTF("[BOOT] FM preset cache static .bss=%u bytes; heap=%u\n",
                static_cast<unsigned>(sizeof(fmMemory)), ESP.getFreeHeap());

  // Direct NVS records are loaded first. Missing or malformed records are
  // replaced with complete safe defaults; legacy EEPROM is not imported.
  DIAG_PRINTLN("[BOOT] NVS begin");
  if (!Storage.begin()) {
    Serial.println("[BOOT] FATAL: NVS begin failed");
    while (true) delay(1000);
  }
  const uint8_t storedSchema = Storage.readByte(EE_BYTE_CHECKBYTE);
  DIAG_PRINTF("[BOOT] NVS ready marker=%u\n", storedSchema);
  bool schemaCommitOk = true;
  if (storedSchema != EE_CHECKBYTE_VALUE) {
    schemaCommitOk = DefaultSettings();
  } else {
    // Writes any optional record that was absent and defaulted by begin().
    schemaCommitOk = Storage.commit();
  }
  if (!schemaCommitOk) {
    Serial.println("[BOOT] FATAL: NVS defaults write failed");
    while (true) delay(1000);
  }
  ContrastSet = Storage.readByte(EE_BYTE_CONTRASTSET);
  language = Storage.readByte(EE_BYTE_LANGUAGE);
  displayflip = Storage.readByte(EE_BYTE_DISPLAYFLIP);
  rotarymode = Storage.readByte(EE_BYTE_ROTARYMODE);
  tunemode = Storage.readByte(EE_BYTE_TUNEMODE);
  if (tunemode > TUNE_MEM) {
    tunemode = TUNE_MAN;
    Storage.writeByte(EE_BYTE_TUNEMODE, tunemode);
    MarkEepromDirty();
  }
  unit = Storage.readByte(EE_BYTE_UNIT);
  dabfreq = Storage.readByte(EE_BYTE_DABFREQ);
  volume = Storage.readByte(EE_BYTE_VOLUME);
  memorypos = Storage.readByte(EE_BYTE_MEMORYPOS);
  autoslideshow = Storage.readByte(EE_BYTE_AUTOSLIDESHOW);
  tot = Storage.readByte(EE_BYTE_TOT);
  CurrentTheme = Storage.readByte(EE_BYTE_THEME);
  const uint8_t storedRadioMode = Storage.readByte(EE_BYTE_RADIO_MODE);
  radioMode = storedRadioMode == RADIO_MODE_FM ? RADIO_MODE_FM : RADIO_MODE_DAB;
  requestedRadioMode = radioMode;
  const uint8_t storedFmRegion = Storage.readByte(EE_BYTE_FM_REGION);
  fmRegion = sanitizeFmRegion(storedFmRegion);
  requestedFmRegion = fmRegion;
  const uint8_t storedGpio12Mode = Storage.readByte(EE_BYTE_GPIO12_MODE);
  gpio12Mode = sanitizeGpio12Mode(storedGpio12Mode);
  requestedGpio12Mode = gpio12Mode;
  requestedI2sAudioMode = i2sAudioMode;
  i2sAudioMode = sanitizeI2sAudioMode(Storage.getI2sAudioMode());
  requestedI2sAudioMode = i2sAudioMode;
  radio.setI2sAudioMode(i2sAudioMode);
  DIAG_PRINTF("[I2S] stored selection=%s\n", i2sAudioModeText(i2sAudioMode));
  fmSeekSensitivity = Storage.readByte(EE_BYTE_FM_SEEK_SENSITIVITY);
  const uint8_t storedFmAfEnabled = Storage.readByte(EE_BYTE_FM_AF_ENABLED);
  fmAfEnabled = storedFmAfEnabled == 1U;
  if (storedGpio12Mode != gpio12Mode) {
    Storage.writeByte(EE_BYTE_GPIO12_MODE, gpio12Mode);
    MarkEepromDirty();
  }
  bool correctedSetting = false;
  auto correctByte = [&](int address, byte& value, byte fallback,
                         bool valid) {
    if (valid) return;
    value = fallback;
    Storage.writeByte(address, value);
    correctedSetting = true;
  };
  correctByte(EE_BYTE_LANGUAGE, language, 0, language < 8U);
  correctByte(EE_BYTE_CONTRASTSET, ContrastSet, 100,
              ContrastSet >= 1U && ContrastSet <= 100U);
  correctByte(EE_BYTE_DISPLAYFLIP, displayflip, 0, displayflip <= 1U);
  correctByte(EE_BYTE_ROTARYMODE, rotarymode, 0, rotarymode <= 1U);
  correctByte(EE_BYTE_TUNEMODE, tunemode, TUNE_MAN, tunemode <= TUNE_MEM);
  correctByte(EE_BYTE_UNIT, unit, 0, unit < 3U);
  correctByte(EE_BYTE_DABFREQ, dabfreq, 0, dabfreq < 38U);
  correctByte(EE_BYTE_VOLUME, volume, 62, volume <= 62U);
  correctByte(EE_BYTE_MEMORYPOS, memorypos, 0, memorypos < EE_PRESETS_CNT);
  if (Storage.readByte(EE_BYTE_AUTOSLIDESHOW) > 1U) {
    autoslideshow = false;
    Storage.writeByte(EE_BYTE_AUTOSLIDESHOW, 0);
    correctedSetting = true;
  }
  const bool validTot = tot == 0U || tot == 15U || tot == 30U ||
                        tot == 60U || tot == 90U;
  correctByte(EE_BYTE_TOT, tot, 0, validTot);
  correctByte(EE_BYTE_THEME, CurrentTheme, 0,
              CurrentTheme < sizeof(Theme) / sizeof(Theme[0]));
  correctByte(EE_BYTE_FM_SEEK_SENSITIVITY, fmSeekSensitivity, 1,
              fmSeekSensitivity <= 2U);
  if (storedFmAfEnabled > 1U) {
    fmAfEnabled = false;
    Storage.writeByte(EE_BYTE_FM_AF_ENABLED, 0U);
    correctedSetting = true;
  }
  if (storedRadioMode > RADIO_MODE_FM) {
    Storage.writeByte(EE_BYTE_RADIO_MODE, RADIO_MODE_DAB);
    correctedSetting = true;
  }
  if (storedFmRegion != fmRegion) {
    Storage.writeByte(EE_BYTE_FM_REGION, fmRegion);
    correctedSetting = true;
  }
  if (correctedSetting) MarkEepromDirty();
  Storage.get(EE_UINT16_FM_FREQUENCY, fmfreq);
  const uint16_t storedFmFrequency = fmfreq;
  fmfreq = normalizeFmFrequency(fmfreq, fmRegion);
  if (fmfreq != storedFmFrequency) {
    Storage.put(EE_UINT16_FM_FREQUENCY, fmfreq);
    MarkEepromDirty();
  }
  radio.setFmRegion(fmRegion, false);
  radio.setFmSeekSensitivity(fmSeekSensitivity, false);
  radio.setFmAfEnabled(fmAfEnabled);
  FmStations.load(fmRegion);
  DabStations.load();
  fmScanCompleted = Storage.fmScanCompleted(fmRegion);
  dabScanCompleted = Storage.dabScanCompleted();
  DIAG_PRINTF("[BOOT] settings loaded: mode=%s DAB=%u FM=%u region=%s GPIO12=%s volume=%u theme=%u\n",
                radioMode == RADIO_MODE_FM ? "FM" : "DAB",
                dabfreq, fmfreq, fmRegionProfile(fmRegion).menuName,
                Gpio12ModeText[gpio12Mode], volume, CurrentTheme);

  // Standalone IR uses fixed learned/work tables. Preparing IR no longer
  // changes the heap allocation order between AUTO/POLL and GPIO12=IR boots.
  if (gpio12Mode == GPIO12_IR) IrRemotePrepare();

  LoadPresets();
  DIAG_PRINTF("[BOOT] presets loaded; heap=%u\n", ESP.getFreeHeap());
  LogRamUsage("after settings/presets");

  // Reserve the shared PNG/JPEG decoder arena before sprites and fonts, while
  // a contiguous 76,800-byte internal-RAM block is still available. The arena
  // is persistent and shared by PNG and mandatory baseline JPEG.
  const bool slideshowWorkspaceReady = SlideshowPrepareWorkspace();
  DIAG_PRINTF("[SLS/WS] early persistent reservation=%s\n",
                slideshowWorkspaceReady ? "READY" : "FAILED");
  if (!slideshowWorkspaceReady) {
    Serial.println("[BOOT] FATAL: slideshow decoder workspace reservation failed");
    while (true) delay(1000);
  }

  // Reserve the 50 KiB MOT collector immediately after the 76,800-byte decoder
  // arena, before TFT sprites/fonts fragment the internal heap. The driver's
  // 4 KiB HOST_LOAD workspace follows while the same large region is intact.
  if (!radio.prepareRuntimeMemory()) {
    Serial.println("[BOOT] FATAL: early SI4684/MOT RAM reservation failed");
    while (true) delay(1000);
  }
  LogRamUsage("after decoder + MOT/radio workspaces");
  LogBootRamUsage("after decoder + MOT/radio workspaces");

  // GPIO17 has remained LOW since the first instructions in setup(), covering
  // the complete cold power ramp. Start radio HSPI/SPI2 while both peripherals
  // are still held in reset; TFT_eSPI starts VSPI/SPI3 later in tft.init().
#ifdef TFT_CS
  digitalWrite(TFT_CS, HIGH);
#endif
  digitalWrite(15, HIGH);
  DIAG_PRINTLN("[BOOT] shared reset held LOW since setup entry; initialise radio HSPI/SPI2");
  if (!radio.prepareSpiBus()) {
    Serial.println("[BOOT] FATAL: radio HSPI/SPI2 init failed");
    while (true) delay(1000);
  }
  // The reset has already been asserted for hundreds of milliseconds. Keep the
  // final guard interval so this remains safe even if early setup is shortened.
  delay(20);
  digitalWrite(17, HIGH);
  DIAG_PRINTLN("[BOOT] shared reset HIGH; wait 200 ms");
  delay(200);

  DIAG_PRINTLN("[BOOT] TPA6130A2 init");
  const byte headphoneInitResult = Headphones.Init();
  DIAG_PRINTF("[BOOT] TPA init result=%u\n", headphoneInitResult);
  delay(50);
  Headphones.SetHiZ(0);
  delay(50);
  Headphones.SetVolume(volume);
  Headphones.SetMute(true);
  DIAG_PRINTLN("[BOOT] headphones configured and muted");

  // Let a slowly rising LCD rail settle, then generate a fresh reset edge while
  // the supply is known to be stable. Merely delaying tft.init() is insufficient
  // when the controller left power-on reset during the voltage ramp. SI4684 has
  // not been booted yet, so pulsing the shared reset here is safe.
  DIAG_PRINTF("[BOOT] LCD cold-rail settle %u ms\n",
                static_cast<unsigned>(LCD_POWER_ON_SETTLE_MS));
  delay(LCD_POWER_ON_SETTLE_MS);
#ifdef TFT_CS
  digitalWrite(TFT_CS, HIGH);
#endif
  digitalWrite(15, HIGH);
  digitalWrite(CONTRASTPIN, LOW);
  DIAG_PRINTF("[BOOT] qualified shared reset LOW %u ms\n",
                static_cast<unsigned>(LCD_QUALIFIED_RESET_LOW_MS));
  digitalWrite(17, LOW);
  delay(LCD_QUALIFIED_RESET_LOW_MS);
  digitalWrite(17, HIGH);
  DIAG_PRINTF("[BOOT] qualified reset released; settle %u ms\n",
                static_cast<unsigned>(LCD_RESET_RELEASE_SETTLE_MS));
  delay(LCD_RESET_RELEASE_SETTLE_MS);

  // The ILI9341 has now received a reset edge after rail stabilisation.
  // Initialise its VSPI host/controller exactly once.
  DIAG_PRINTF("[BOOT] TFT init begin; heap=%u\n", ESP.getFreeHeap());
  tft.init();
  DIAG_PRINTLN("[BOOT] TFT init OK (VSPI/SPI3)");

  // Cold-start fallback: some LCD boards still ignore the first command table
  // even after a qualified reset. Once VSPI is running, wait once more, pulse
  // the shared hardware reset, and replay the ILI9341 register setup without a
  // second tft.init() (which would duplicate ESP32 SPI/APB callbacks). The radio
  // application image is not booted until later, so resetting SI4684 here is safe.
  DIAG_PRINTF("[BOOT2/TFT] post-init wait %u ms before second reset\n",
                static_cast<unsigned>(LCD_POST_INIT_RETRY_DELAY_MS));
  delay(LCD_POST_INIT_RETRY_DELAY_MS);
#ifdef TFT_CS
  digitalWrite(TFT_CS, HIGH);
#endif
  digitalWrite(15, HIGH);
  digitalWrite(CONTRASTPIN, LOW);
  DIAG_PRINTF("[BOOT2/TFT] shared reset LOW %u ms\n",
                static_cast<unsigned>(LCD_QUALIFIED_RESET_LOW_MS));
  digitalWrite(17, LOW);
  delay(LCD_QUALIFIED_RESET_LOW_MS);
  digitalWrite(17, HIGH);
  delay(LCD_RESET_RELEASE_SETTLE_MS);
  RestoreTftControllerNoReset("BOOT2");
  DIAG_PRINTLN("[BOOT2/TFT] second reset + controller init complete");

  LogRamUsage("after TFT init");
  doTheme();
  DIAG_PRINTLN("[BOOT] theme OK");
  if (displayflip == 0) tft.setRotation(3); else tft.setRotation(1);

  pinMode(STANDBYBUTTON, INPUT);
  pinMode(MODEBUTTON, INPUT);
  pinMode(SLBUTTON, INPUT);
  pinMode(ROTARY_BUTTON, INPUT);
  pinMode(ROTARY_BUTTON2, INPUT);
  pinMode(ROTARY_PIN_A, INPUT);
  pinMode(ROTARY_PIN_B, INPUT);
  pinMode(ROTARY_PIN_2A, INPUT);
  pinMode(ROTARY_PIN_2B, INPUT);
  attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_A), read_encoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_B), read_encoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_2A), read_encoder2, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_2B), read_encoder2, CHANGE);
  DIAG_PRINTLN("[BOOT] GPIO inputs + rotary interrupts OK");

  tft.setSwapBytes(true);
  tft.fillScreen(BackgroundColor);

  OneBigLineSprite.createSprite(270, 30);
  OneBigLineSprite.setSwapBytes(true);

  FullLineSprite.createSprite(308, 20);
  FullLineSprite.setSwapBytes(true);
  // Every use of this 20-pixel sprite is a single line. With TFT_eSPI's
  // default horizontal wrapping, the off-screen part of scrolling text is
  // moved down by one font line and its top pixels leak into the bottom row.
  FullLineSprite.setTextWrap(false, false);

  ModeSprite.createSprite(46, 47);
  ModeSprite.setTextDatum(TC_DATUM);
  ModeSprite.setSwapBytes(true);

  // Compose the complete Q/M bar off-screen and transfer it as one image. The
  // drawing the full gradient before clearing unused segments would flash to 100%.
  QualityBarSprite.createSprite(139, 10);
  QualityBarSprite.setSwapBytes(true);
  LogRamUsage("after TFT sprites");
  LogBootRamUsage("after TFT sprites");

  DIAG_PRINTLN("[BOOT] loadFonts begin");
  loadFonts(true);
  LogRamUsage("after sprite smooth fonts");
  LogBootRamUsage("after sprite smooth fonts");

  // Boot-time button shortcuts (held during power-on):
  //   SL only            → invert rotary direction (left/right) and save
  //   MODE only          → flip display orientation 180° and save
  //   SL + ROTARY        → reset all settings (DefaultSettings) and restart
  if (digitalRead(SLBUTTON) == LOW && digitalRead(ROTARY_BUTTON) == HIGH) {
    if (rotarymode == 0) rotarymode = 1; else rotarymode = 0;
    Storage.writeByte(EE_BYTE_ROTARYMODE, rotarymode);
    MarkEepromDirty();

    analogWrite(CONTRASTPIN, ContrastSet * 2 + 27);
    tftPrint(0, myLanguage[language][1], 155, 70, ActiveColor, ActiveColorSmooth, 28);
    tftPrint(0, myLanguage[language][2], 155, 130, ActiveColor, ActiveColorSmooth, 28);
    while (digitalRead(SLBUTTON) == LOW);
  }

  if (digitalRead(MODEBUTTON) == LOW) {
    if (displayflip == 0) {
      displayflip = 1;
      tft.setRotation(1);
    } else {
      displayflip = 0;
      tft.setRotation(3);
    }
    Storage.writeByte(EE_BYTE_DISPLAYFLIP, displayflip);
    MarkEepromDirty();

    analogWrite(CONTRASTPIN, ContrastSet * 2 + 27);
    tftPrint(0, myLanguage[language][3], 155, 70, ActiveColor, ActiveColorSmooth, 28);
    tftPrint(0, myLanguage[language][2], 155, 130, ActiveColor, ActiveColorSmooth, 28);
    while (digitalRead(MODEBUTTON) == LOW);
  }

  if (digitalRead(ROTARY_BUTTON) == LOW && digitalRead(SLBUTTON) == LOW) {
    const bool defaultsSaved = DefaultSettings();

    analogWrite(CONTRASTPIN, ContrastSet * 2 + 27);
    if (defaultsSaved) {
      tftPrint(0, myLanguage[language][10], 155, 70, ActiveColor, ActiveColorSmooth, 28);
      tftPrint(0, myLanguage[language][2], 155, 130, ActiveColor, ActiveColorSmooth, 28);
    } else {
      tftPrint(0, "NVS ERROR", 155, 100, TFT_RED, TFT_DARKGREY, 28);
    }
    while (digitalRead(ROTARY_BUTTON) == LOW && digitalRead(SLBUTTON) == LOW);
    if (defaultsSaved) ESP.restart();
  }

  // The shared cold-boot reset and both SPI hosts are already settled. Draw the
  // normal splash without another reset, tft.init(), or SPI.begin().
  DIAG_PRINTLN("[BOOT] drawing splash after one-time TFT init");
  tft.pushImage(0, 0, 320, 240, SplashScreen);
  tftPrint(0, myLanguage[language][72], 155, 15,
           ActiveColor, ActiveColorSmooth, 28);
  char splashVersion[64];
  snprintf(splashVersion, sizeof(splashVersion), "%s %s",
           myLanguage[language][9], VERSION);
  tftPrintFixed(0, splashVersion, 160, 190, TFT_WHITE, TFT_DARKGREY, 16);
  LogBootRamUsage("after splash TFT font");
  DIAG_PRINTLN("[BOOT] splash OK");

  // Fade in from a genuinely dark backlight.  The previous linear ramp began
  // at duty 27/255, which is already visibly bright on these LCD modules, so
  // the splash appeared to switch on abruptly despite the three-second loop.
  // A short quadratic ramp gives the eye a clear, smooth onset without making
  // the already conservative cold-start sequence noticeably longer.
  constexpr uint8_t LCD_FADE_STEPS = 40;
  constexpr uint16_t LCD_FADE_STEP_MS = 20;
  const uint16_t targetBacklight = ContrastSet * 2U + 27U;
  for (uint16_t step = 0; step <= LCD_FADE_STEPS; ++step) {
    const uint32_t duty = targetBacklight * step * step /
                          (LCD_FADE_STEPS * LCD_FADE_STEPS);
    analogWrite(CONTRASTPIN, duty);
    delay(LCD_FADE_STEP_MS);
  }

  // Start/reuse SI4684. Its SPI bus was already initialised above.
  DIAG_PRINTF("[BOOT] radio.begin mode=%s\n",
                radioMode == RADIO_MODE_FM ? "FM" : "DAB");
  const bool radioBeginOk = radio.begin(15, radioMode);
  DIAG_PRINTF("[BOOT] radio.begin returned=%u\n", radioBeginOk ? 1U : 0U);
  if (!radioBeginOk) {
    Serial.println("[BOOT] FATAL: radio.begin failed");
    tftPrint(0, radioErrorText[language], 160, 210,
             TFT_RED, TFT_DARKGREY, 16);
    for (;;) delay(1000);
  }

  // Firmware identity is detected from the chip; never hard-code it here.
  char detectedRadioVersion[32];
  snprintf(detectedRadioVersion, sizeof(detectedRadioVersion), "%s v%s",
           radio.getChipID(), radio.getFirmwareVersion());
  DIAG_PRINTF("[BOOT] detected radio: %s\n", detectedRadioVersion);
  // Keep normal SLS diagnostics concise. The serial DEBUG command can still
  // toggle detailed MOT segment tracing on demand.
  diagnosticDebug = false;
  radio.SlideShowDebug = false;
  radio.SlideShowVerbose = false;
  DIAG_PRINTLN("[SLS] segment diagnostics OFF (DEBUG toggles details)");
  tftPrintFixed(0, detectedRadioVersion, 160, 210,
                TFT_WHITE, TFT_DARKGREY, 16);

  if (radioMode == RADIO_MODE_FM) LoadPresets();

  delay(1500);

  if (tunemode == TUNE_MEM && !IsStationEmpty()) {
    DoMemoryPosTune();
  } else if (radioMode == RADIO_MODE_FM) {
    if (tunemode == TUNE_MEM) {
      tunemode = TUNE_MAN;
      Storage.writeByte(EE_BYTE_TUNEMODE, tunemode);
      MarkEepromDirty();
    }
    radio.setFmFrequency(fmfreq);
  } else {
    if (tunemode == TUNE_MEM) {
      tunemode = TUNE_MAN;
      Storage.writeByte(EE_BYTE_TUNEMODE, tunemode);
      MarkEepromDirty();
    }
    radio.setFreq(dabfreq);
    Storage.get(EE_UINT32_SERVICEID, _serviceID);
    // Ignore the stored label because it may be stale. SID plus the freshly
    // received service list are authoritative for restoring the station name.
    memset(_serviceName, 0, sizeof(_serviceName));
  }
  if (radioMode == RADIO_MODE_DAB && _serviceID != 0) {
    trysetservice = true;
    trysetserviceFreq = dabfreq;
    trysetserviceComponentValid = false;
  } else {
    trysetservice = false;
    trysetserviceFreq = 0xFF;
    trysetserviceComponentValid = false;
  }

  // GPIO12 role is selected at boot. AUTO never initializes IR.
  if (gpio12Mode == GPIO12_IR) IrRemoteBegin();

  radioSwitchMuted = true;
  RadioSwitchMuteTimer = millis();

  DIAG_PRINTLN("[BOOT] BuildDisplay begin");
  BuildDisplay();
  DIAG_PRINTLN("[BOOT] BuildDisplay OK");
  tottimer = millis();
  DIAG_PRINTF("[BOOT] SETUP COMPLETE free heap=%u min=%u\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap());
  LogMemoryIntegrity("setup-complete");
  diagSerial.enableBuffering();
}

// Main cooperative scheduler. Three subsystems run every iteration:
//   ProcessDAB()    pump radio.Update() + UI updates
//   Communication() serial control protocol
// Everything else is timer-gated or input-driven and must stay non-blocking.
void loop(void) {
  diagSerial.service(64U);
  ProcessDAB();
  ProcessFmFullScan();
  ProcessDabFullScan();
  Communication();
  epg::poll();
  diagSerial.service(64U);
  if (displayreset) ShowTuneModeCurrent();
  displayreset = false;

  // Heap integrity is still checked continuously over long runs, but once per
  // minute is sufficient for leak/corruption diagnostics and avoids UART spam.
  static uint32_t memoryIntegrityTimer = 0;
  const uint32_t memoryIntegrityNow = millis();
  if (memoryIntegrityNow - memoryIntegrityTimer >= 60000UL) {
    memoryIntegrityTimer = memoryIntegrityNow;
    LogMemoryIntegrity("runtime");
  }

  static uint32_t serialDiagnosticsTimer = 0;
  if (memoryIntegrityNow - serialDiagnosticsTimer >= 30000UL) {
    serialDiagnosticsTimer = memoryIntegrityNow;
    DIAG_PRINTF("[SERIAL] queued=%u dropped=%u\n",
                static_cast<unsigned>(diagSerial.queuedBytes()),
                static_cast<unsigned>(diagSerial.droppedBytes()));
  }

  if (eepromDirty && millis() - EepromDirtyTimer >= EEPROM_COMMIT_DELAY_MS) FlushEeprom();
  if (radioSwitchMuted && (!radio.isTunePending() || millis() - RadioSwitchMuteTimer >= 4000UL)) {
    Headphones.SetMute(false);
    radioSwitchMuted = false;
  }

  if (seek) Seek(direction);

  if (tot != 0) {
    unsigned long totprobe = tot * 60000;
    if (millis() - tottimer >= totprobe) deepSleep();
  }

  // Manual FM and DAB tuning are both finalised after 250 ms so both modes
  // feel equally responsive. Preset-store commits use a 500 ms debounce.
  // Encoder interrupts are detached around the deferred NVS write below.
  const uint32_t tuningDebounceMs =
      (tuning && !store) ? 250UL : 500UL;
  if (millis() - TuningTimer >= tuningDebounceMs) {
    if (store) {
      detachInterrupt(digitalPinToInterrupt(ROTARY_PIN_A));
      detachInterrupt(digitalPinToInterrupt(ROTARY_PIN_B));
      if (StoreFrequency()) {
        store = false;
      } else {
        // START_DIGITAL_SERVICE may still be completing asynchronously. Keep
        // the store request armed and retry after another debounce interval.
        TuningTimer = millis();
      }
      attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_A), read_encoder, CHANGE);
      attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_B), read_encoder, CHANGE);
    } else if (tuning) {
      if (radioMode == RADIO_MODE_FM) {
        radio.setFmFrequency(fmfreq);
        Storage.put(EE_UINT16_FM_FREQUENCY, fmfreq);
        MarkEepromDirty();
      } else {
        radio.setFreq(dabfreq);
      }
      tuning = false;
      if (tunemode == TUNE_MEM) {
        trysetservice = _serviceID != 0;
        trysetserviceFreq = trysetservice ? dabfreq : 0xFF;
        trysetserviceComponentValid = false;
      }
    }
  }

  // Scrolling changes only the highlighted DAB row. Commit the last selection
  // after 300 ms without another detent, avoiding a STOP/START pair per step.
  if (dabServiceSelectionPending) {
    if (radioMode != RADIO_MODE_DAB || tuning || seek ||
        (!DabGlobalListView && radio.numberofservices == 0)) {
      dabServiceSelectionPending = false;
    } else if (millis() - DabServiceSelectionTimer >=
               DAB_SERVICE_SELECTION_DEBOUNCE_MS) {
      dabServiceSelectionPending = false;
      ActivateCurrentService();
    }
  }

  if (setvolume && millis() - VolumeTimer >= 3000) {
    closeVolume();
  }

  if (!menu) {
    if (rotary2 == -1) KeyUp2();
    if (rotary2 == 1) KeyDown2();
  }

  if (rotary == -1) KeyUp();
  if (rotary == 1) KeyDown();

  // MODE/standby and SL use non-blocking long-press handling. SL short
  // slideshow action fires only on release; a long press opens EPG once.
  static bool rot2Armed = true;
  static fm_features::PressTracker slTracker;
  ModeButtonPress();                                                            // self-checks pin (long-press)
  const uint8_t slAction = slTracker.update(digitalRead(SLBUTTON) == LOW, millis(), 1000UL);
  if (!menu && slAction == 1U) SlideShowButtonPress();
  if (!menu && slAction == 2U && fmScanState == FmScanState::Idle && dabFullScanState == DabFullScanState::Idle) EpgToggle();
  if (!menu) StandbyButtonPress();                                              // self-checks pin (long-press)
  Rotary1ButtonProcess();
  if (!menu && buttonEdge(ROTARY_BUTTON2, rot2Armed)) Button2Press();

  // IR is an input source just like the encoders/buttons. Process it after
  // displayreset has been consumed for this frame, so an IR action that calls
  // BuildDisplay() leaves displayreset=true for the NEXT ProcessDAB() pass.
  // This makes return-from-list/slideshow redraw semantics identical to the
  // mechanical controls. Learn/Test still intercept IR frames inside
  // IrRemoteProcess() before normal action dispatch.
  IrRemoteProcess();
}

// Always pump an in-flight radio command. During manual selection, suppress
// only new metadata/DSRV scheduling so CTS cannot expire behind the debounce.
void ProcessDAB(void) {
  const bool fmAfAllowed = radioMode == RADIO_MODE_FM && !menu && !EpgVisible() && !tuning &&
      !seek && !store && !memorystore && fmScanState == FmScanState::Idle &&
      dabFullScanState == DabFullScanState::Idle && !ChannelListView &&
      !DabGlobalListView && !SlideShowView && !ShowServiceInformation;
  radio.setFmAfAllowed(fmAfAllowed);
  radio.Update(!tuning);

  const bool afProbing = radio.isFmAfProbing();
  if (afProbing && !fmAfProbeMuted) {
    Headphones.SetMute(true);
    fmAfProbeMuted = true;
    DIAG_PRINTLN("[FM/AF] headphones muted for probe");
  } else if (!afProbing && fmAfProbeMuted) {
    if (!radioSwitchMuted) Headphones.SetMute(false);
    fmAfProbeMuted = false;
    DIAG_PRINTLN("[FM/AF] probe mute released");
  }

  uint16_t afFrequency = 0U;
  if (radio.takeFmAfSwitch(afFrequency)) {
    fmfreq = afFrequency;
    Storage.put(EE_UINT16_FM_FREQUENCY, fmfreq);
    MarkEepromDirty();
    ShowFreq();
  }
  if (!tuning) {
    if (radioMode == RADIO_MODE_DAB && radio.transportStalled()) {
      const uint32_t now = millis();
      if (!RadioStallRecoveryPerformed ||
          static_cast<uint32_t>(now - LastRadioStallRecoveryMs) >=
              RADIO_STALL_RECOVERY_MIN_INTERVAL_MS) {
        RadioStallRecoveryPerformed = true;
        LastRadioStallRecoveryMs = now;
        dabServiceSelectionPending = false;
        DIAG_PRINTLN("[DAB/STALL] rate-limited cold recovery");
        doRecovery();
        return;
      }
    }
    SignalLevel = radio.getRSSI();
    // During FM auto-seek the driver already reports the current candidate
    // frequency from FM_RSQ_STATUS about every 100 ms. Publish it while seek is
    // active; for ordinary tuning keep waiting for the tune operation to finish.
    if (radioMode == RADIO_MODE_FM &&
        (seek || !radio.isTunePending()) &&
        isFmFrequencyValid(radio.fmFrequency10kHz, fmRegion) &&
        fmfreq != radio.fmFrequency10kHz) {
      fmfreq = radio.fmFrequency10kHz;
      if (fmScanState == FmScanState::Idle) ShowFreq();
    } else if (radioMode == RADIO_MODE_DAB &&
               seek && dabfreq != dabfreqold) {
      // DAB AUTO advances one Band-III channel after the previous asynchronous
      // tune has completed. Publish each newly requested channel here, in the
      // same ProcessDAB refresh path used for the live FM seek frequency.
      ShowFreq();
    }
  }

  // Recovery above is based solely on repeated CTS timeouts. Missing DAB lock
  // is intentionally ignored because GPIO17 resets both the tuner and TFT.

  // A pending restore belongs to exactly one DAB frequency. Any user/serial
  // retune that changes dabfreq cancels it immediately, even before the new
  // multiplex has produced a service list. This prevents a stale boot SID from
  // surviving an AUTO scan and matching again after the band wraps around.
  if (radioMode == RADIO_MODE_DAB && trysetservice &&
      dabfreq != trysetserviceFreq) {
    DIAG_PRINTF("[DAB/RESTORE] cancelled by retune oldFreq=%u newFreq=%u\n",
                  static_cast<unsigned>(trysetserviceFreq),
                  static_cast<unsigned>(dabfreq));
    trysetservice = false;
    trysetserviceFreq = 0xFF;
    trysetserviceComponentValid = false;
    memset(_serviceName, 0, sizeof(_serviceName));
  }

  if (radioMode == RADIO_MODE_DAB && trysetservice && radio.signallock &&
      radio.isDabServiceListReady()) {
    bool restored = false;

    // The frequency-mismatch case was consumed above, so this list belongs to
    // the exact tune request for which the restore was armed.
    const int16_t matched = fm_features::findDabServiceByIdentity(
        radio.service, radio.numberofservices, _serviceID,
        trysetserviceComponentId, trysetserviceComponentValid);
    if (matched >= 0) {
      const uint8_t x = static_cast<uint8_t>(matched);
      // Use only the label from the current, validated service list.
      strncpy(_serviceName, radio.service[x].Label, sizeof(_serviceName));
      _serviceName[sizeof(_serviceName) - 1] = '\0';
      _serviceNameCharset = DabServiceLabelCharset(x);
      if (trysetserviceComponentValid &&
          trysetserviceComponentId != radio.service[x].CompID) {
        DIAG_PRINTF("[DAB/RESTORE] unique SID fallback oldCID=%08X newCID=%08X\n",
                    static_cast<unsigned>(trysetserviceComponentId),
                    static_cast<unsigned>(radio.service[x].CompID));
      }
      radio.setService(x);
      // Startup/MEM restore is already represented by the stored SID/frequency.
      // Do not schedule a redundant NVS write after a successful restore.
      restored = true;
      DIAG_PRINTF("[DAB/RESTORE] SID=%08X matched service=%u freq=%u\n",
                    static_cast<unsigned>(_serviceID),
                    static_cast<unsigned>(x),
                    static_cast<unsigned>(dabfreq));
    }

    const bool deferMissingIdentity = fm_features::deferMissingDabIdentity(
        radio.isDabServiceMetadataReady(), restored);
    if (!restored && !deferMissingIdentity) {
      memset(_serviceName, 0, sizeof(_serviceName));
      DIAG_PRINTF("[DAB/RESTORE] SID=%08X not restored on freq=%u; request consumed\n",
                    static_cast<unsigned>(_serviceID),
                    static_cast<unsigned>(dabfreq));
    }
    if (!deferMissingIdentity) {
      trysetservice = false;
      trysetserviceFreq = 0xFF;
      trysetserviceComponentValid = false;
    }
  }

  // Keep the compact scan progress panel intact while the radio scheduler and
  // RDS parser continue running underneath it. After completion/cancellation,
  // restore the normal screen once (immediately or after the result dwell).
  if (fmScanState != FmScanState::Idle ||
      dabFullScanState != DabFullScanState::Idle) return;
  if (fmScanNeedsMainRedraw) {
    if (static_cast<int32_t>(millis() - fmScanOverlayUntilMs) < 0) return;
    fmScanNeedsMainRedraw = false;
    BuildDisplay();
    return;
  }
  if (dabScanNeedsMainRedraw) {
    if (static_cast<int32_t>(millis() - dabScanOverlayUntilMs) < 0) return;
    dabScanNeedsMainRedraw = false;
    BuildDisplay();
    return;
  }

  // EPG is modal for drawing only. Radio, recovery, metadata restore and scans
  // above remain cooperative; no main-screen widget or auto-SLS can overpaint it.
  if (EpgVisible() && !menu) {
    if (radio.SlideShowAvailable && radio.SlideShowUpdate) radio.acknowledgeSlideshow();
    EpgDraw();
    return;
  }

  // A manually armed waiting overlay is not a full-screen slideshow. Keep the
  // normal DAB information alive below it. Once an image completes, leave this
  // branch and let the existing full-screen renderer display that image.
  if (slsWaitingView && !menu && radio.SlideShowAvailable &&
      radio.SlideShowUpdate) {
    slsWaitingView = false;
    DIAG_PRINTLN("[SLS/UI] complete image received; closing waiting overlay");
  }
  // Manual slideshow waiting is sticky. MOT reception can legitimately become
  // idle between objects/segments; that is not a reason to close the view.
  // The user can cancel the waiting view explicitly, or a complete image below
  // will replace the overlay.

  if ((!SlideShowView || slsWaitingView) && !menu) {
    if (!ChannelListView) {
      ShowSignalLevel();
    }
    ShowRT();
    if (!ShowServiceInformation && !ChannelListView) {
      if (radioMode == RADIO_MODE_DAB && autoslideshow && radio.SlideShowAvailable && radio.SlideShowUpdate) {
        SlideShowButtonPress();
      } else {
        ShowBitrate();
        ShowEID();
        ShowSID();
        ShowPTY();
        ShowProtectionlevel();
        ShowPS();
        ShowEN();
        ShowAudioMode();
        ShowClock();
        ShowSlideShowIcon();
        ShowSlideshowReceiveIndicator();
        ShowECC();
      }

      // In manual slideshow mode a newly completed image is cached for the
      // SL button, but it is not rendered immediately. Release the single MOT
      // buffer ownership after the main UI has observed the READY state so a
      // following TransportId can start reception. SlideShowAvailable and the
      // image bytes remain valid until the first segment of that next object;
      // pressing SL before then re-arms SlideShowUpdate and displays the cache.
      // Without this acknowledge, slideshowPublishedPending stays set forever
      // after a background completion and every later TID is ignored as
      // "Hold published image".
      if (!SlideShowView && !autoslideshow &&
          radioMode == RADIO_MODE_DAB && radio.SlideShowAvailable &&
          radio.SlideShowUpdate) {
        radio.acknowledgeSlideshow();
        DIAG_PRINTLN("[SLS/UI] image cached for manual view; collector released");
      }
    }
  } else {
    if (radio.SlideShowAvailable && radio.SlideShowUpdate && !menu) {
      const uint8_t* image = radio.slideshowData();
      const uint32_t imageSize = radio.slideshowSize();
      const uint32_t imageHash = SlideshowFingerprint(image, imageSize);
      const bool duplicate = slsDisplayedFingerprintValid &&
                             imageSize == slsDisplayedSize &&
                             imageHash == slsDisplayedHash;

      if (duplicate) {
        DIAG_PRINTF("[SLS/UI] duplicate image skipped size=%u hash=%08X\n",
                      imageSize, imageHash);
      } else {
        const bool displayed = ShowSlideShow();
        DIAG_PRINTF("[SLS/UI] slideshow render=%s size=%u hash=%08X\n",
                      displayed ? "OK" : "FAIL", imageSize, imageHash);
        if (displayed) {
          slsWaitingOverlayVisible = false;
          slsDisplayedFingerprintValid = true;
          slsDisplayedHash = imageHash;
          slsDisplayedSize = imageSize;
        } else {
          // Unsupported/corrupt images are rejected before TFT changes. Keep
          // an intact main UI as-is, but explicitly remove a manual "loading"
          // overlay or repair a validated render that failed after TFT writes.
          SlideShowView = false;
          slsDisplayedFingerprintValid = false;
          if (SlideshowLastRenderTouchedDisplay() ||
              slsWaitingOverlayVisible)
            RestoreMainDisplayAfterSlideshow();
        }
      }
      radio.acknowledgeSlideshow();
      slsWaitingView = false;
      LogRamUsage("after slideshow display");
    }
  }

  // All dynamic fields paint first. Coalesce their changes into one final
  // overlay repaint, including changes between signal-meter ticks. Volume
  // takes precedence if both panels are requested. Do not restart its timer.
  const bool mainFieldsChanged = TakeMainDisplayChanges();
  if (!menu && !ShowServiceInformation && !ChannelListView) {
    if (setvolume && mainFieldsChanged)
      RedrawVolumeOverlay();
    else if (!setvolume && slsWaitingView &&
             (mainFieldsChanged || !slsWaitingOverlayVisible))
      ShowSlideshowWaitingOverlay();
  }
}

// Leave the volume overlay and redraw whichever view was visible underneath.
void closeVolume(void) {
  if (EpgVisible()) { setvolume = false; EpgDraw(true); return; }
  if (ChannelListView) {
    BuildChannelList();
  } else if (ShowServiceInformation) {
    ShowServiceInfo();
  } else if (SlideShowView) {
    SlideShowView = false;
    SlideShowButtonPress();
  } else {
    BuildDisplay();
  }
}

// Cold-restart the SI4684 when it stops responding. Retunes to the last
// channel and asks ProcessDAB to re-select the previous service.
void doRecovery(void) {
  SwitchRadioMode(radioMode, true);
}

// Activate the currently highlighted service-list row without changing the
// cursor. This is also used after DABSelectService() moves the cursor, so
// movement and OK-confirmation share exactly the same start/tune path.
bool ActivateCurrentService(void) {
  if (radioMode == RADIO_MODE_FM) {
    if (FmStations.count() == 0U || FmStationListIndex >= FmStations.count())
      return false;
    const FmStationRecord& station = FmStations[FmStationListIndex];
    fmfreq = station.frequency10kHz;
    fmPendingStationFrequency = fmfreq;
    strncpy(fmPendingStationPs, station.ps,
            sizeof(fmPendingStationPs) - 1U);
    fmPendingStationPs[sizeof(fmPendingStationPs) - 1U] = '\0';
    DIAG_PRINTF("[FM/UI] target idx=%u freq=%u.%02u PS='%s'\n",
                static_cast<unsigned>(FmStationListIndex),
                static_cast<unsigned>(fmfreq / 100U),
                static_cast<unsigned>(fmfreq % 100U), fmPendingStationPs);
    radio.setFmFrequency(fmfreq);

    uint16_t storedFmFrequency = 0;
    Storage.get(EE_UINT16_FM_FREQUENCY, storedFmFrequency);
    if (storedFmFrequency != fmfreq) {
      Storage.put(EE_UINT16_FM_FREQUENCY, fmfreq);
      MarkEepromDirty();
    }
    return true;
  }

  if (DabGlobalListView) {
    if (DabStations.count() == 0U ||
        DabStationListIndex >= DabStations.count()) return false;
    const DabStationRecord& station = DabStations[DabStationListIndex];
    const bool sameChannel = dabfreq == station.channelIndex;
    dabfreq = station.channelIndex;
    // The global-list selection changes the requested mux immediately, while
    // the actual tune/service start remains debounced. Publish the channel now
    // so the frequency field follows rotary 2 instead of retaining the old mux.
    ShowFreq();
    _serviceID = station.serviceId;
    strncpy(_serviceName, station.label, sizeof(_serviceName) - 1U);
    _serviceName[sizeof(_serviceName) - 1U] = '\0';
    _serviceNameCharset = station.charset;

    // Do not retune the multiplex when the exact scanned component is already
    // present in the live list. This makes adjacent stations in one mux switch
    // like normal services and avoids throwing away a valid list unnecessarily.
    if (sameChannel && radio.isDabServiceListReady()) {
      const int16_t matched = fm_features::findDabServiceByIdentity(
          radio.service, radio.numberofservices, station.serviceId,
          station.componentId, true);
      if (matched >= 0) {
        const uint8_t i = static_cast<uint8_t>(matched);
        radio.ServiceIndex = i;
        strncpy(_serviceName, radio.service[i].Label,
                sizeof(_serviceName) - 1U);
        _serviceName[sizeof(_serviceName) - 1U] = '\0';
        _serviceNameCharset = DabServiceLabelCharset(i);
        trysetservice = false;
        trysetserviceFreq = 0xFF;
        trysetserviceComponentValid = false;
        radio.setService(i);
        Storage.writeByte(EE_BYTE_DABFREQ, dabfreq);
        Storage.put(EE_UINT32_SERVICEID, _serviceID);
        MarkEepromDirty();
        return true;
      }
    }

    radio.ServiceStart = false;
    trysetservice = true;
    trysetserviceFreq = dabfreq;
    trysetserviceComponentId = station.componentId;
    trysetserviceComponentValid = true;
    radio.setFreq(dabfreq);
    Storage.writeByte(EE_BYTE_DABFREQ, dabfreq);
    Storage.put(EE_UINT32_SERVICEID, _serviceID);
    MarkEepromDirty();
    return true;
  }

  if (radio.numberofservices == 0 ||
      radio.ServiceIndex >= radio.numberofservices) {
    return false;
  }

  const uint8_t serviceType = radio.service[radio.ServiceIndex].ServiceType;
  if (serviceType != 0x00 && serviceType != 0x04 && serviceType != 0x05)
    return false;

  // setService() intentionally clears ServiceStart until the asynchronous
  // START_DIGITAL_SERVICE command finishes. Preserve the selected live label
  // so the main UI can transition directly to the requested service.
  strncpy(_serviceName, radio.service[radio.ServiceIndex].Label,
          sizeof(_serviceName));
  _serviceName[sizeof(_serviceName) - 1] = '\0';
  _serviceNameCharset = DabServiceLabelCharset(radio.ServiceIndex);
  trysetserviceComponentValid = false;
  radio.setService(radio.ServiceIndex);
  store = true;
  return true;
}

uint8_t ChannelListCount(void) {
  if (radioMode == RADIO_MODE_FM) return FmStations.count();
  return DabGlobalListView ? DabStations.count() : radio.numberofservices;
}

uint8_t ChannelListIndex(void) {
  if (radioMode == RADIO_MODE_FM) return FmStationListIndex;
  return DabGlobalListView ? DabStationListIndex : radio.ServiceIndex;
}

void MoveChannelList(bool forward) {
  const uint8_t count = ChannelListCount();
  if (count == 0U) return;
  if (radioMode == RADIO_MODE_FM) {
    FmStationListIndex = forward
        ? static_cast<uint8_t>((FmStationListIndex + 1U) % count)
        : static_cast<uint8_t>(FmStationListIndex == 0U
                                   ? count - 1U : FmStationListIndex - 1U);
    return;
  }
  if (DabGlobalListView) {
    DabStationListIndex = forward
        ? static_cast<uint8_t>((DabStationListIndex + 1U) % count)
        : static_cast<uint8_t>(DabStationListIndex == 0U
                                   ? count - 1U : DabStationListIndex - 1U);
    return;
  }
  DABSelectService(forward);
}

// Step through the ensemble's services, skipping anything that isn't a
// regular audio service (type 0x00/0x04/0x05). Wraps at both ends.
void DABSelectService(bool dir) {
  if (radio.numberofservices == 0) return;

  if (radioMode == RADIO_MODE_FM) {
    radio.ServiceIndex =
        dir ? (radio.ServiceIndex + 1) % radio.numberofservices
            : (radio.ServiceIndex == 0
                   ? radio.numberofservices - 1
                   : radio.ServiceIndex - 1);
    ActivateCurrentService();
    return;
  }

  bool hasValidService = false;
  for (uint8_t i = 0; i < radio.numberofservices; ++i) {
    const uint8_t type = radio.service[i].ServiceType;
    if (type == 0x00 || type == 0x04 || type == 0x05) {
      hasValidService = true;
      break;
    }
  }
  if (!hasValidService) return;

  do {
    if (dir) {
      radio.ServiceIndex =
          (radio.ServiceIndex + 1) % radio.numberofservices;
    } else {
      radio.ServiceIndex =
          (radio.ServiceIndex == 0)
              ? radio.numberofservices - 1
              : radio.ServiceIndex - 1;
    }
  } while (radio.service[radio.ServiceIndex].ServiceType != 0x00 &&
           radio.service[radio.ServiceIndex].ServiceType != 0x04 &&
           radio.service[radio.ServiceIndex].ServiceType != 0x05);

  // Publish the target label immediately. setService() is deliberately
  // debounced, but the UI must not fall back to a permanent "Select service"
  // while the selected row is already known.
  strncpy(_serviceName, radio.service[radio.ServiceIndex].Label,
          sizeof(_serviceName) - 1U);
  _serviceName[sizeof(_serviceName) - 1U] = '\0';
  _serviceNameCharset = DabServiceLabelCharset(radio.ServiceIndex);
  trysetserviceComponentValid = false;
  dabServiceSelectionPending = true;
  DabServiceSelectionTimer = millis();
}

// Persist the currently playing channel + service so the next boot can
// restore it. Triggered 500 ms after the user stops scrolling (see loop()).

// Tune-mode field:
//   DAB = MAN / AUTO / MEM
//   FM  = MAN / AUTO / MEM. Rotary 2 remains regional manual stepping.
void ShowTuneModeCurrent(void) {
  // This sprite contains a cut-out of the main-screen background. Never push
  // it over a full-screen overlay such as Service Information.
  if (menu || EpgVisible() || SlideShowView || ChannelListView || ShowServiceInformation) return;

  // Exactly one tune mode is highlighted:
  // selected = ActiveColor, inactive = GreyoutColor.
  // MEM uses SignificantColor only during actual preset-store mode.
  ModeSprite.pushImage(-6, -33, 320, 240, Background);
  ModeSprite.setTextDatum(TC_DATUM);

  {
    ModeSprite.setTextColor(
        tunemode == TUNE_MAN ? ActiveColor : GreyoutColor,
        tunemode == TUNE_MAN ? ActiveColorSmooth : BackgroundColor,
        false);
    ModeSprite.drawString("MAN", 23, 0);

    ModeSprite.setTextColor(
        tunemode == TUNE_AUTO ? ActiveColor : GreyoutColor,
        tunemode == TUNE_AUTO ? ActiveColorSmooth : BackgroundColor,
        false);
    ModeSprite.drawString("AUTO", 23, 16);

    if (tunemode == TUNE_MEM && memorystore) {
      ModeSprite.setTextColor(SignificantColor, SignificantColorSmooth, false);
    } else {
      ModeSprite.setTextColor(
          tunemode == TUNE_MEM ? ActiveColor : GreyoutColor,
          tunemode == TUNE_MEM ? ActiveColorSmooth : BackgroundColor,
          false);
    }
    ModeSprite.drawString("MEM", 23, 32);
  }

  ModeSprite.pushSprite(6, 33);
}

bool StoreFrequency(void) {
  if (radioMode == RADIO_MODE_FM) {
    fmfreq = radio.fmFrequency10kHz;
    uint16_t storedFmFrequency = 0;
    Storage.get(EE_UINT16_FM_FREQUENCY, storedFmFrequency);
    if (storedFmFrequency != fmfreq) {
      Storage.put(EE_UINT16_FM_FREQUENCY, fmfreq);
      MarkEepromDirty();
    }
    return true;
  }

  // Never index service[] while the asynchronous selection is incomplete or
  // while no valid service list exists. Keeping the previous valid restore
  // pair is safer than persisting service[0] from an empty/stale table.
  if (!radio.ServiceStart || !radio.isDabServiceListReady() ||
      radio.numberofservices == 0 ||
      radio.ServiceIndex >= radio.numberofservices) {
    DIAG_PRINTF("[NVS] DAB store skipped: started=%u list=%u count=%u index=%u\n",
                  radio.ServiceStart ? 1U : 0U,
                  radio.isDabServiceListReady() ? 1U : 0U,
                  static_cast<unsigned>(radio.numberofservices),
                  static_cast<unsigned>(radio.ServiceIndex));
    return false;
  }

  const uint32_t serviceId = radio.service[radio.ServiceIndex].ServiceID;
  if (serviceId == 0U) {
    DIAG_PRINTLN("[NVS] DAB store skipped: invalid service ID 0");
    return false;
  }

  bool changed = false;
  uint32_t storedServiceId = 0;
  Storage.get(EE_UINT32_SERVICEID, storedServiceId);
  if (storedServiceId != serviceId) {
    Storage.put(EE_UINT32_SERVICEID, serviceId);
    changed = true;
  }
  if (Storage.readByte(EE_BYTE_DABFREQ) != dabfreq) {
    Storage.writeByte(EE_BYTE_DABFREQ, dabfreq);
    changed = true;
  }

  // EE_CHAR17_SERVICENAME remains reserved for schema compatibility only.
  // Restore is SID-based and obtains the current label from the live service
  // list, so rewriting the label here would add flash wear without value.
  if (changed) MarkEepromDirty();
  _serviceID = serviceId;
  return true;
}

void CancelFmFullScan(void) {
  if (fmScanState == FmScanState::Idle) return;
  fmScanState = FmScanState::Idle;
  fmScanSeekCount = 0;
  // The previous completed list remains in NVS until a new scan finishes.
  // Restore it after cancellation instead of keeping a second 1.2 KiB RAM copy.
  FmStations.load(fmRegion);
  FmStationListIndex = 0;
  fmScanOverlayUntilMs = 0;
  fmScanNeedsMainRedraw = true;
  DIAG_PRINTLN("[FM/SCAN] cancelled");
}

void StartFmFullScan(void) {
  if (radioMode != RADIO_MODE_FM || fmScanState != FmScanState::Idle) return;
  seek = false;
  fmSeekStarted = false;
  tuning = false;
  FmStations.clear(fmRegion);
  fmScanFirstFrequency = 0;
  fmScanCandidateFrequency = 0;
  fmScanSeekCount = 0;
  fmScanNeedsMainRedraw = false;
  // Start at the upper edge so the first wrapped upward seek includes the
  // complete band, including a possible station at the lower edge.
  fmfreq = radio.fmProfile().maxFrequency10kHz;
  radio.setFmFrequency(fmfreq);
  fmScanDeadlineMs = millis() + 4000UL;
  fmScanState = FmScanState::WaitBandStart;
  char initialStatus[24];
  snprintf(initialStatus, sizeof(initialStatus), "%s 0", fmScanText[language]);
  ShowScanProgressOverlay(initialStatus, 0);
  DIAG_PRINTF("[FM/SCAN] start region=%s range=%u..%u\n",
              radio.fmProfile().menuName,
              static_cast<unsigned>(radio.fmProfile().minFrequency10kHz),
              static_cast<unsigned>(radio.fmProfile().maxFrequency10kHz));
}

static void FinishFmFullScan(void) {
  FmStationListIndex = 0;
  const bool saved = FmStations.save();
  if (!saved) FmStations.load(fmRegion);
  fmScanCompleted = Storage.fmScanCompleted(fmRegion);
  fmScanState = FmScanState::Idle;
  fmfreq = radio.fmFrequency10kHz;
  char status[32];
  snprintf(status, sizeof(status), "%s %u %s", fmScanText[language],
           static_cast<unsigned>(FmStations.count()), saved ? "OK" : "NVS ERR");
  ShowScanProgressOverlay(status, 100);
  fmScanOverlayUntilMs = millis() + 1500UL;
  fmScanNeedsMainRedraw = true;
  DIAG_PRINTF("[FM/SCAN] complete count=%u saved=%u\n",
              static_cast<unsigned>(FmStations.count()), saved ? 1U : 0U);
}

void ProcessFmFullScan(void) {
  if (fmScanState == FmScanState::Idle) return;
  if (radioMode != RADIO_MODE_FM) {
    CancelFmFullScan();
    return;
  }
  const uint32_t now = millis();
  switch (fmScanState) {
    case FmScanState::Idle:
      return;
    case FmScanState::WaitBandStart:
      if (radio.isTunePending() &&
          static_cast<int32_t>(now - fmScanDeadlineMs) < 0) return;
      fmScanState = FmScanState::StartSeek;
      return;
    case FmScanState::StartSeek:
      if (radio.startFmSeek(true)) {
        ++fmScanSeekCount;
        fmScanDeadlineMs = now + 13000UL;
        fmScanState = FmScanState::WaitSeek;
      } else if (fmScanSeekCount > 220U) {
        FinishFmFullScan();
      }
      return;
    case FmScanState::WaitSeek:
      if (radio.isTunePending() &&
          static_cast<int32_t>(now - fmScanDeadlineMs) < 0) return;
      if (!radio.fmValid || !radio.signallock ||
          !isFmFrequencyValid(radio.fmFrequency10kHz, fmRegion)) {
        // A wrapped hardware seek already examined the whole configured band.
        FinishFmFullScan();
        return;
      }
      fmScanCandidateFrequency = radio.fmFrequency10kHz;
      if (fmScanFirstFrequency == 0U) {
        fmScanFirstFrequency = fmScanCandidateFrequency;
      } else if (fmScanCandidateFrequency == fmScanFirstFrequency) {
        FinishFmFullScan();
        return;
      }
      fmScanDeadlineMs = now + FM_SCAN_PS_STABILITY_MS;
      memset(fmScanPsCandidate, 0, sizeof(fmScanPsCandidate));
      fmScanPsCandidateValid = false;
      fmScanPsDynamic = false;
      fmScanState = FmScanState::RdsDwell;
      {
        const FmRegionProfile& profile = radio.fmProfile();
        const uint32_t span = profile.maxFrequency10kHz -
                              profile.minFrequency10kHz;
        const uint8_t percent = span == 0U ? 0U : static_cast<uint8_t>(
            (static_cast<uint32_t>(fmScanCandidateFrequency -
                                   profile.minFrequency10kHz) * 100U) / span);
        char progress[28];
        snprintf(progress, sizeof(progress), "%s %u RDS", fmScanText[language],
                 static_cast<unsigned>(FmStations.count()));
        ShowScanProgressOverlay(progress, percent);
      }
      return;
    case FmScanState::RdsDwell:
      if (radio.isFmPsStable() && radio.fmPs[0] != '\0') {
        if (!fmScanPsCandidateValid) {
          memcpy(fmScanPsCandidate, radio.fmPs, 8U);
          fmScanPsCandidate[8] = '\0';
          fmScanPsCandidateValid = true;
        } else if (memcmp(fmScanPsCandidate, radio.fmPs, 8U) != 0) {
          fmScanPsDynamic = true;
        }
      }
      if (static_cast<int32_t>(now - fmScanDeadlineMs) < 0) return;
      {
        FmStationRecord station{};
        station.frequency10kHz = fmScanCandidateFrequency;
        station.pi = radio.fmPi;
        if (fmScanPsCandidateValid && !fmScanPsDynamic)
          strncpy(station.ps, fmScanPsCandidate, sizeof(station.ps) - 1U);
        station.rssi = radio.fmRssi;
        station.snr = radio.fmSnr;
        station.multipath = radio.fmMultipath;
        station.pty = radio.fmPty;
        const bool added = FmStations.add(station);
        if (fmScanPsDynamic)
          DIAG_PRINTF("[FM/SCAN] dynamic PS protected freq=%u first='%s' latest='%.8s'\n",
                      static_cast<unsigned>(station.frequency10kHz),
                      fmScanPsCandidate, radio.fmPs);
        DIAG_PRINTF("[FM/SCAN] found freq=%u PI=%04X PS='%s' RSSI=%d SNR=%d add=%u\n",
                    static_cast<unsigned>(station.frequency10kHz),
                    static_cast<unsigned>(station.pi), station.ps,
                    static_cast<int>(station.rssi), static_cast<int>(station.snr),
                    added ? 1U : 0U);
        char progress[24];
        snprintf(progress, sizeof(progress), "%s %u", fmScanText[language],
                 static_cast<unsigned>(FmStations.count()));
        const FmRegionProfile& profile = radio.fmProfile();
        const uint32_t span = profile.maxFrequency10kHz -
                              profile.minFrequency10kHz;
        const uint8_t percent = span == 0U ? 0U : static_cast<uint8_t>(
            (static_cast<uint32_t>(fmScanCandidateFrequency -
                                   profile.minFrequency10kHz) * 100U) / span);
        ShowScanProgressOverlay(progress, percent);
      }
      if (fmScanSeekCount >= 220U) FinishFmFullScan();
      else fmScanState = FmScanState::StartSeek;
      return;
  }
}

static void RestoreAfterDabFullScan(void) {
  dabfreq = dabScanRestoreChannel;
  _serviceID = dabScanRestoreServiceId;
  memset(_serviceName, 0, sizeof(_serviceName));
  trysetservice = _serviceID != 0U;
  trysetserviceFreq = trysetservice ? dabfreq : 0xFF;
  trysetserviceComponentValid = false;
  radio.setFreq(dabfreq);
}

static void FinishDabFullScan(void) {
  DabStationListIndex = 0;
  const bool saved = DabStations.save();
  if (!saved) DabStations.load();
  dabScanCompleted = Storage.dabScanCompleted();
  dabFullScanState = DabFullScanState::Idle;
  RestoreAfterDabFullScan();
  char status[32];
  snprintf(status, sizeof(status), "%s %u %s", dabScanText[language],
           static_cast<unsigned>(DabStations.count()),
           saved ? "OK" : "NVS ERR");
  ShowScanProgressOverlay(status, 100);
  dabScanOverlayUntilMs = millis() + 1500UL;
  dabScanNeedsMainRedraw = true;
  DIAG_PRINTF("[DAB/SCAN] complete count=%u saved=%u\n",
              static_cast<unsigned>(DabStations.count()), saved ? 1U : 0U);
}

void CancelDabFullScan(void) {
  if (dabFullScanState == DabFullScanState::Idle) return;
  dabFullScanState = DabFullScanState::Idle;
  dabScanChannelsTried = 0;
  // As with FM, NVS is the rollback copy while scanning. This avoids a second
  // 1.8 KiB list in static RAM during boot and font allocation.
  DabStations.load();
  DabStationListIndex = 0;
  RestoreAfterDabFullScan();
  dabScanOverlayUntilMs = 0;
  dabScanNeedsMainRedraw = true;
  DIAG_PRINTLN("[DAB/SCAN] cancelled; previous service restored");
}

void StartDabFullScan(void) {
  if (radioMode != RADIO_MODE_DAB ||
      dabFullScanState != DabFullScanState::Idle) return;
  seek = false;
  CancelDabAuto();
  tuning = false;
  dabServiceSelectionPending = false;
  dabScanRestoreChannel = dabfreq;
  dabScanRestoreServiceId = radio.ServiceStart &&
      radio.ServiceIndex < radio.numberofservices
      ? radio.service[radio.ServiceIndex].ServiceID : _serviceID;
  trysetservice = false;
  trysetserviceFreq = 0xFF;
  trysetserviceComponentValid = false;
  DabStations.clear();
  dabScanChannel = 0;
  dabScanChannelsTried = 0;
  dabScanTuneListGeneration = radio.serviceListGeneration();
  dabScanObservedListGeneration = 0;
  dabScanListRequests = 0;
  dabScanStoredAnyList = false;
  dabScanNeedsMainRedraw = false;
  dabFullScanState = DabFullScanState::TuneChannel;
  ShowScanProgressOverlay(dabScanText[language], 0);
  DIAG_PRINTLN("[DAB/SCAN] start Band III 5A..13F");
}

static uint8_t StoreCurrentDabScanServices(bool addFallbackLabels) {
  uint8_t missingLabels = 0;
  for (uint8_t i = 0; i < radio.numberofservices; ++i) {
    if (!fm_features::isAudioServiceType(radio.service[i].ServiceType))
      continue;
    DabStationRecord station{};
    station.channelIndex = dabfreq;
    station.serviceId = radio.service[i].ServiceID;
    station.componentId = radio.service[i].CompID;
    strncpy(station.label, radio.service[i].Label,
            sizeof(station.label) - 1U);
    if (!dab_scheduler::labelHasContent(station.label)) {
      ++missingLabels;
      if (addFallbackLabels) {
        snprintf(station.label, sizeof(station.label), "SID %08lX",
                 static_cast<unsigned long>(station.serviceId));
      }
    }
    station.charset = DabServiceLabelCharset(i);
    station.serviceType = radio.service[i].ServiceType;
    DabStations.add(station);
  }
  return missingLabels;
}

static void AdvanceDabScanChannel(void) {
  dabScanChannel = fm_features::nextDabChannel(dabScanChannel, true);
  dabFullScanState = DabFullScanState::TuneChannel;
}

void ProcessDabFullScan(void) {
  if (dabFullScanState == DabFullScanState::Idle) return;
  if (radioMode != RADIO_MODE_DAB) {
    CancelDabFullScan();
    return;
  }
  const uint32_t now = millis();
  switch (dabFullScanState) {
    case DabFullScanState::Idle:
      return;

    case DabFullScanState::TuneChannel: {
      if (dabScanChannelsTried >= 38U) {
        FinishDabFullScan();
        return;
      }
      dabfreq = dabScanChannel;
      radio.ServiceIndex = 0;
      radio.ServiceStart = false;
      memset(_serviceName, 0, sizeof(_serviceName));
      dabScanTuneListGeneration = radio.serviceListGeneration();
      dabScanObservedListGeneration = 0;
      dabScanListRequests = 0;
      dabScanStoredAnyList = false;
      radio.setFreq(dabfreq);
      ++dabScanChannelsTried;
      dabScanDeadlineMs =
          now + dab_scheduler::DAB_SCAN_NO_SIGNAL_TIMEOUT_MS;
      dabFullScanState = DabFullScanState::WaitTune;
      char progress[32];
      snprintf(progress, sizeof(progress), "%s %u/38 %s",
               dabScanText[language],
               static_cast<unsigned>(dabScanChannelsTried),
               radio.getChannel(dabfreq));
      ShowScanProgressOverlay(
          progress,
          static_cast<uint8_t>((dabScanChannelsTried * 100U) / 38U));
      return;
    }

    case DabFullScanState::WaitTune:
      if (radio.isTunePending() &&
          static_cast<int32_t>(now - dabScanDeadlineMs) < 0) return;
      if (!radio.signallock) {
        AdvanceDabScanChannel();
        return;
      }
      dabScanHardDeadlineMs =
          now + dab_scheduler::DAB_SCAN_LIST_TIMEOUT_MS;
      dabFullScanState = DabFullScanState::WaitServiceList;
      return;

    case DabFullScanState::WaitServiceList:
      if (!radio.signallock ||
          static_cast<int32_t>(now - dabScanHardDeadlineMs) >= 0) {
        AdvanceDabScanChannel();
        return;
      }
      if (!dab_scheduler::serviceListArrived(
              radio.serviceListGeneration(), dabScanTuneListGeneration))
        return;
      dabScanObservedListGeneration = radio.serviceListGeneration();
      dabScanSettleDeadlineMs =
          now + dab_scheduler::DAB_SCAN_LIST_SETTLE_MS;
      dabFullScanState = DabFullScanState::WaitServiceTypes;
      return;

    case DabFullScanState::WaitServiceTypes:
      if (!radio.signallock) {
        AdvanceDabScanChannel();
        return;
      }
      if (static_cast<int32_t>(now - dabScanHardDeadlineMs) >= 0) {
        if (radio.isDabServiceMetadataReady())
          StoreCurrentDabScanServices(true);
        DIAG_PRINTF("[DAB/SCAN] hard timeout index=%u gen=%u requests=%u saved=%u\n",
                    static_cast<unsigned>(dabfreq),
                    static_cast<unsigned>(radio.serviceListGeneration()),
                    static_cast<unsigned>(dabScanListRequests),
                    dabScanStoredAnyList ? 1U : 0U);
        AdvanceDabScanChannel();
        return;
      }
      if (static_cast<int32_t>(now - dabScanSettleDeadlineMs) < 0 ||
          !radio.isDabServiceMetadataReady()) return;
      {
        const uint8_t missingLabels = StoreCurrentDabScanServices(false);
        dabScanStoredAnyList = true;
        DIAG_PRINTF("[DAB/SCAN] index=%u gen=%u refresh=%u services=%u missingLabels=%u\n",
                    static_cast<unsigned>(dabfreq),
                    static_cast<unsigned>(dabScanObservedListGeneration),
                    static_cast<unsigned>(dabScanListRequests),
                    static_cast<unsigned>(radio.numberofservices),
                    static_cast<unsigned>(missingLabels));

        // Always ask once after the first provisional list. Later complete
        // generations can be committed immediately; incomplete ones receive
        // bounded refresh attempts until the hard deadline.
        if (dabScanListRequests != 0U && missingLabels == 0U) {
          AdvanceDabScanChannel();
          return;
        }
        if (dabScanListRequests >=
            dab_scheduler::DAB_SCAN_LIST_MAX_REQUESTS) {
          if (missingLabels != 0U) StoreCurrentDabScanServices(true);
          AdvanceDabScanChannel();
          return;
        }
        ++dabScanListRequests;
        radio.requestServiceListRefresh();
        dabScanDeadlineMs = now + dab_scheduler::DAB_SCAN_LIST_RETRY_MS;
        dabFullScanState = DabFullScanState::WaitListRefresh;
      }
      return;

    case DabFullScanState::WaitListRefresh:
      if (!radio.signallock) {
        AdvanceDabScanChannel();
        return;
      }
      if (static_cast<int32_t>(now - dabScanHardDeadlineMs) >= 0) {
        if (radio.isDabServiceMetadataReady())
          StoreCurrentDabScanServices(true);
        DIAG_PRINTF("[DAB/SCAN] hard timeout index=%u gen=%u refresh=%u saved=%u\n",
                    static_cast<unsigned>(dabfreq),
                    static_cast<unsigned>(radio.serviceListGeneration()),
                    static_cast<unsigned>(dabScanListRequests),
                    dabScanStoredAnyList ? 1U : 0U);
        AdvanceDabScanChannel();
        return;
      }
      if (dab_scheduler::refreshedServiceListArrived(
              radio.serviceListGeneration(),
              dabScanObservedListGeneration, true)) {
        dabScanObservedListGeneration = radio.serviceListGeneration();
        dabScanSettleDeadlineMs = now;
        dabFullScanState = DabFullScanState::WaitServiceTypes;
        return;
      }
      if (static_cast<int32_t>(now - dabScanDeadlineMs) >= 0 &&
          dabScanListRequests < dab_scheduler::DAB_SCAN_LIST_MAX_REQUESTS) {
        ++dabScanListRequests;
        radio.requestServiceListRefresh();
        dabScanDeadlineMs = now + dab_scheduler::DAB_SCAN_LIST_RETRY_MS;
      }
      return;
  }
}

void Rotary1ButtonProcess(void) {
  static fm_features::PressTracker tracker;
  const uint8_t action = tracker.update(
      digitalRead(ROTARY_BUTTON) == LOW, millis(), 1000UL);
  if (action == 1U) {
    ButtonPress();
    return;
  }
  if (action != 2U) return;
  if (EpgVisible()) return;
  tottimer = millis();
  if (menu || ChannelListView || SlideShowView || ShowServiceInformation ||
      fmScanState != FmScanState::Idle ||
      dabFullScanState != DabFullScanState::Idle) return;
  if (radioMode == RADIO_MODE_FM) {
    StartFmFullScan();
  } else {
    StartDabFullScan();
  }
}

// Rotary 1 click: stores/recalls a memory preset when in TUNE_MEM, toggles
// the channel-list view otherwise, or routes to DoMenu() when the menu is open.
void ButtonPress(void) {
  if (EpgVisible() && !menu) { EpgClick(); return; }
  tottimer = millis();
  if (fmScanState != FmScanState::Idle) {
    CancelFmFullScan();
    return;
  }
  if (dabFullScanState != DabFullScanState::Idle) {
    CancelDabFullScan();
    return;
  }
  if (!menu) {
    if (tunemode == TUNE_MEM) {
      if (!memorystore) {
        memorystore = true;
        if (!IsStationEmpty()) memoryposstatus = MEM_EXIST;
        else memoryposstatus = MEM_NORMAL;
        ShowMemoryPos();
        ShowTuneModeCurrent();
      } else {
        memorystore = false;
        if (radioMode == RADIO_MODE_FM) {
          fmMemory[memorypos].Frequency10kHz = fmfreq;
          fmMemory[memorypos].ProgramId = radio.fmPi;
          memset(fmMemory[memorypos].Label, 0, sizeof(fmMemory[memorypos].Label));
          strncpy(fmMemory[memorypos].Label, radio.fmPs, 8);
          if (fmMemory[memorypos].Label[0] == '\0')
            snprintf(fmMemory[memorypos].Label,
                     sizeof(fmMemory[memorypos].Label), "%.1f", fmfreq / 100.0f);
          Storage.put(EE_FM_PRESETS_FREQ_START + memorypos * 2,
                     fmMemory[memorypos].Frequency10kHz);
          Storage.put(EE_FM_PRESETS_PI_START + memorypos * 2,
                     fmMemory[memorypos].ProgramId);
          for (int x = 0; x < EE_FM_PRESET_NAME_LENGTH; ++x)
            Storage.writeByte(EE_FM_PRESETS_NAME_START +
                                 memorypos * EE_FM_PRESET_NAME_LENGTH + x,
                             fmMemory[memorypos].Label[x]);
          LoadPresets();
        } else {
          Storage.writeByte(memorypos + EE_PRESETS_FREQ_START, dabfreq);
          Storage.put((memorypos * 8) + EE_PRESETS_SERVICEID_START, radio.service[radio.ServiceIndex].ServiceID);
          for (int x = 0; x < 16; x++) {
            char character = radio.service[radio.ServiceIndex].Label[x];
            Storage.writeByte((memorypos * 17) + x + EE_PRESETS_NAME_START, character);
            memory[memorypos].Label[x] = character;
          }
          Storage.writeByte((memorypos * 17) + 16 + EE_PRESETS_NAME_START, '\0');
          memory[memorypos].Label[16] = '\0';
          memory[memorypos].Channel = dabfreq;
          memory[memorypos].ServiceID = radio.service[radio.ServiceIndex].ServiceID;
        }
        if (Storage.readByte(EE_BYTE_MEMORYPOS) != memorypos)
          Storage.writeByte(EE_BYTE_MEMORYPOS, memorypos);
        MarkEepromDirty();

        ShowTuneModeCurrent();
        if (memoryposstatus == MEM_DARK || memoryposstatus == MEM_EXIST) {
          memoryposstatus = MEM_NORMAL;
          ShowMemoryPos();
        }
      }
    } else {
      if (ChannelListView) {
        // An empty global list doubles as the scan offer. A second OK starts
        // the appropriate non-blocking scan; MODE still returns without it.
        if (ChannelListCount() == 0U) {
          ChannelListView = false;
          DabGlobalListView = false;
          if (radioMode == RADIO_MODE_FM) StartFmFullScan();
          else StartDabFullScan();
          return;
        }
        // OK confirms the row that is already highlighted. Previously OK only
        // closed the list, so the initial highlighted row was not started until
        // the user moved away and back (movement calls DABSelectService()).
        dabServiceSelectionPending = false;
        ActivateCurrentService();
        BuildDisplay();
      } else if (SlideShowView || ShowServiceInformation) {
        BuildDisplay();
      } else {
        // DAB Channel List is always the global scan database. Local mux
        // services remain live and selectable with Rotary 2, but are never
        // silently substituted for a not-yet-created global list.
        DabGlobalListView = radioMode == RADIO_MODE_DAB;
        ChannelListView = true;
        BuildChannelList();
      }
    }
  } else {
    DoMenu();
  }
}

// Rotary 2 click: open/close the volume overlay. While open, rotary 2 ticks
// adjust the actual volume; a 3-second idle auto-closes it (see loop()).
void Button2Press(void) {
  memorystore = false;
  memoryposstatus = MEM_NORMAL;
  tottimer = millis();
  if (setvolume) {
    closeVolume();
  } else {
    setvolume = true;
    ShowVolume();
  }
}

// SL button: enter immediately when a complete slideshow is available, or arm
// slideshow view while a MOT is being received so it opens on completion.
// Also called from ProcessDAB() when autoslideshow is enabled and from KeyUp()
// to leave slideshow view via the rotary.
void SlideShowButtonPress(void) {
  if (EpgVisible()) { EpgBack(); return; }
  if (radioMode == RADIO_MODE_FM) return;
  DIAG_PRINTF("[SLS/UI] button available=%u update=%u receiving=%u view=%u\n",
                radio.SlideShowAvailable ? 1U : 0U,
                radio.SlideShowUpdate ? 1U : 0U,
                slsReceiving ? 1U : 0U,
                SlideShowView ? 1U : 0U);
  setvolume = false;
  memorystore = false;
  memoryposstatus = MEM_NORMAL;
  if (!SlideShowView && (radio.SlideShowAvailable || slsReceiving)) {
    // The normal UI or loading screen is currently in TFT GRAM, so the first
    // complete image shown after entering this view must always be rendered.
    slsDisplayedFingerprintValid = false;
    if (radio.SlideShowAvailable) {
      slsWaitingView = false;
      radio.SlideShowUpdate = true;
    } else {
      slsWaitingView = true;
      DIAG_PRINTLN("[SLS/UI] manual view armed; waiting for complete MOT");
      ShowSlideshowWaitingOverlay();
    }
    SlideShowView = true;
    ShowServiceInformation = false;
  } else {
    if (SlideShowView) BuildDisplay();
    SlideShowView = false;
    slsWaitingView = false;
    slsDisplayedFingerprintValid = false;
  }
}

// Mode button with short/long-press distinction.
//   short press  → in main view cycle tunemode; in menu commit + leave;
//                  in any sub-view return to main display.
//   long press (>1 s)  → open the configuration menu.
// Implemented as a non-blocking state machine: called every loop iteration,
// it reads its own pin and only fires the relevant action once.
void ModeButtonPress(void) {
  static unsigned long pressStart = 0;
  static bool actionDone = false;
  static fm_features::PressTracker epgTracker;
  static bool epgOwned = false;
  if (EpgVisible() || epgOwned) {
    epgOwned = true;
    const bool down = digitalRead(MODEBUTTON) == LOW;
    const uint8_t action = epgTracker.update(down, millis(), 1000UL);
    if (action == 1U) EpgBack();
    if (action == 2U) { EpgSuspend(); CaptureSettingsSnapshot(); menu = true; BuildMenu(); }
    if (!down) { epgOwned = false; pressStart = 0; actionDone = false; }
    return;
  }
  const bool pressed = (digitalRead(MODEBUTTON) == LOW);

  if (!pressed) {
    if (pressStart != 0) {
      if (!actionDone) CycleTuneMode();
      pressStart = 0;
      actionDone = false;
    }
    return;
  }

  if (pressStart == 0) {
    pressStart = millis();
    actionDone = false;
    tottimer = pressStart;
    seek = false;
    CancelFmFullScan();
    CancelDabFullScan();
    CancelDabAuto();
    dabSeekStarted = false;
    fmSeekStarted = false;

    if (menu) {
      ExitSettingsMenu();
      actionDone = true;
    } else if (SlideShowView || ChannelListView || ShowServiceInformation) {
      BuildDisplay();
      actionDone = true;
    } else {
      memorystore = false;
      memoryposstatus = MEM_NORMAL;
    }
    return;
  }

  if (!actionDone && (millis() - pressStart) > 1000) {
    CaptureSettingsSnapshot();
    menu = true;
    BuildMenu();
    actionDone = true;
  }
}

// Standby button (also acts as preset-erase confirm and service-info toggle).
//   memorystore active   → short or long press erases the current preset.
//   normal short press   → toggle the Service Information overlay.
//   long press (>1 s)    → enter deep-sleep / standby.
// Non-blocking state machine; same pattern as ModeButtonPress.
void StandbyButtonPress(void) {
  static unsigned long pressStart = 0;
  static bool actionDone = false;
  bool pressed = (digitalRead(STANDBYBUTTON) == LOW);

  // A LOW level on the standby button can be the light-sleep wake source. Do
  // not reinterpret the same physical press as a fresh short/long press after
  // wake. Rearm normal button handling only after a real release.
  if (standbyWakeReleaseGuard) {
    if (!pressed) {
      standbyWakeReleaseGuard = false;
      pressStart = 0;
      actionDone = false;
    }
    return;
  }

  if (!pressed) {
    if (pressStart != 0) {
      if (!actionDone) {
        // Short press release in main view: toggle service info
        if (!EpgVisible() && !ShowServiceInformation && !SlideShowView && !ChannelListView) {
          ShowServiceInfo();
          ShowServiceInformation = true;
        } else if (!EpgVisible()) {
          BuildDisplay();
        }
      }
      pressStart = 0;
      actionDone = false;
    }
    return;
  }

  if (pressStart == 0) {
    pressStart = millis();
    actionDone = false;
    tottimer = pressStart;

      if (memorystore) {
      if (radioMode == RADIO_MODE_FM) {
        Storage.put(EE_FM_PRESETS_FREQ_START + memorypos * 2,
                   static_cast<uint16_t>(EE_FM_PRESET_EMPTY_FREQUENCY));
        Storage.put(EE_FM_PRESETS_PI_START + memorypos * 2,
                   static_cast<uint16_t>(0));
        for (int x = 0; x < EE_FM_PRESET_NAME_LENGTH; ++x) Storage.writeByte(EE_FM_PRESETS_NAME_START + memorypos * EE_FM_PRESET_NAME_LENGTH + x, '\0');
      } else {
        Storage.writeByte(memorypos + EE_PRESETS_FREQ_START, EE_PRESETS_FREQUENCY);
        Storage.put((memorypos * 8) + EE_PRESETS_SERVICEID_START, 0);
        for (int x = 0; x < 17; x++) Storage.writeByte((memorypos * 17) + x + EE_PRESETS_NAME_START, '\0');
      }
      if (radioMode == RADIO_MODE_FM) {
        memset(fmMemory[memorypos].Label, 0, sizeof(fmMemory[memorypos].Label));
        fmMemory[memorypos].Frequency10kHz = EE_FM_PRESET_EMPTY_FREQUENCY;
        fmMemory[memorypos].ProgramId = 0;
        LoadPresets();
      } else {
        memset(memory[memorypos].Label, 0, sizeof(memory[memorypos].Label));
        memory[memorypos].Channel = EE_PRESETS_FREQUENCY;
        memory[memorypos].ServiceID = 0;
      }
      MarkEepromDirty();
      memorystore = false;
      ShowTuneModeCurrent();
      if (memoryposstatus == MEM_DARK || memoryposstatus == MEM_EXIST) {
        memoryposstatus = MEM_NORMAL;
        ShowMemoryPos();
      }
      actionDone = true;
    }
    return;
  }

  if (!actionDone && (millis() - pressStart) > 1000) {
    doStandby();
    actionDone = true;
  }
}

// Rotary 1 turn upwards (or IR Up). Dispatches based on context:
//   slideshow view → leave it
//   menu open      → menu navigation
//   channel list   → next entry
//   TUNE_MAN/AUTO  → next frequency
//   TUNE_MEM       → next preset
void KeyUp(void) {
  if (EpgVisible()) { rotary = rotary2 = 0; tottimer = millis(); EpgMove(1,false); return; }
  if (fmScanState != FmScanState::Idle) CancelFmFullScan();
  if (dabFullScanState != DabFullScanState::Idle) CancelDabFullScan();
  if (setvolume) closeVolume();
  tottimer = millis();
  rotary = 0;
  rotary2 = 0;
  // System info is a modal diagnostics page. Encoder/IR Up must not leave the
  // page and must never start MAN/AUTO/MEM tuning underneath it.
  if (ShowServiceInformation) return;
  if (SlideShowView) SlideShowButtonPress();
  if (!menu) {
    if (!ChannelListView) {
      switch (tunemode) {
        case TUNE_MAN:
          if (radioMode == RADIO_MODE_FM) {
            fmfreq = stepFmFrequency(fmfreq, 100, fmRegion);
            tuning = true;
            TuningTimer = millis();
            radio.ServiceIndex = 0;
            radio.ServiceStart = false;
            memset(_serviceName, 0, sizeof(_serviceName));
            ShowFreq();
            DIAG_PRINTF("[FM/UI] MAN rotary1 UP -> +1.0 MHz, %.1f MHz\n",
                          fmfreq / 100.0f);
            break;
          }
          if (dabfreq >= 37) dabfreq = 0; else dabfreq++;
          tuning = true;
          TuningTimer = millis();
          radio.ServiceIndex = 0;
          radio.ServiceStart = false;
          radio.clearData();
          for (byte x = 0; x < 17; x++) _serviceName[x] = '\0';
          ShowFreq();
          break;

        case TUNE_AUTO:
          // A DAB seek is asynchronous and may take several seconds. Extra
          // detents in the same direction must not repeatedly reset its state;
          // that made rotary 1 appear intermittent. An opposite detent changes
          // direction and restarts once, while the in-flight radio command is
          // still allowed to complete through radio.Update().
          if (radioMode == RADIO_MODE_DAB &&
              seek && direction == true &&
              dabAutoState != DabAutoState::Idle) break;
          if (radioMode == RADIO_MODE_DAB) CancelDabAuto();
          direction = true;
          seek = true;
          break;

        case TUNE_MEM:
          memorypos++;
          if (memorypos > EE_PRESETS_CNT - 1) memorypos = 0;
          if (!memorystore) {
            while (IsStationEmpty()) {
              memorypos++;
              if (memorypos > EE_PRESETS_CNT - 1) {
                memorypos = 0;
                break;
              }
            }
          }
          if (!memorystore) {
            DoMemoryPosTune();
          } else {
            if (!IsStationEmpty()) memoryposstatus = MEM_EXIST;
            else memoryposstatus = MEM_NORMAL;
          }
          ShowMemoryPos();
          break;
      }
    } else {
      const uint8_t oldIndex = ChannelListIndex();
      MoveChannelList(true);
      RedrawChannelListSelection(oldIndex);
    }
  } else {
    MenuUp();
  }
}

// Rotary 1 turn downwards. Mirror of KeyUp() — see that function for context map.
void KeyDown(void) {
  if (EpgVisible()) { rotary = rotary2 = 0; tottimer = millis(); EpgMove(-1,false); return; }
  if (fmScanState != FmScanState::Idle) CancelFmFullScan();
  if (dabFullScanState != DabFullScanState::Idle) CancelDabFullScan();
  if (setvolume) closeVolume();
  tottimer = millis();
  rotary = 0;
  rotary2 = 0;
  // See KeyUp(): keep System info modal and block all tune actions.
  if (ShowServiceInformation) return;
  if (SlideShowView) SlideShowButtonPress();
  if (!menu) {
    if (!ChannelListView) {
      switch (tunemode) {
        case TUNE_MAN:
          if (radioMode == RADIO_MODE_FM) {
            fmfreq = stepFmFrequency(fmfreq, -100, fmRegion);
            tuning = true;
            TuningTimer = millis();
            radio.ServiceIndex = 0;
            radio.ServiceStart = false;
            memset(_serviceName, 0, sizeof(_serviceName));
            ShowFreq();
            DIAG_PRINTF("[FM/UI] MAN rotary1 DOWN -> -1.0 MHz, %.1f MHz\n",
                          fmfreq / 100.0f);
            break;
          }
          if (dabfreq == 0) dabfreq = 37; else dabfreq--;
          tuning = true;
          TuningTimer = millis();
          radio.ServiceIndex = 0;
          radio.ServiceStart = false;
          radio.clearData();
          for (byte x = 0; x < 17; x++) _serviceName[x] = '\0';
          ShowFreq();
          break;

        case TUNE_AUTO:
          if (radioMode == RADIO_MODE_DAB &&
              seek && direction == false &&
              dabAutoState != DabAutoState::Idle) break;
          if (radioMode == RADIO_MODE_DAB) CancelDabAuto();
          direction = false;
          seek = true;
          break;

        case TUNE_MEM:
          memorypos--;
          if (memorypos > EE_PRESETS_CNT - 1) memorypos = EE_PRESETS_CNT - 1;
          if (!memorystore) {
            while (IsStationEmpty()) {
              memorypos--;
              if (memorypos > EE_PRESETS_CNT - 1) {
                memorypos = EE_PRESETS_CNT - 1;
                break;
              }
            }
          }
          if (!memorystore) {
            DoMemoryPosTune();
          } else {
            if (!IsStationEmpty()) memoryposstatus = MEM_EXIST;
            else memoryposstatus = MEM_NORMAL;
          }
          ShowMemoryPos();
          break;
      }
    } else {
      const uint8_t oldIndex = ChannelListIndex();
      MoveChannelList(false);
      RedrawChannelListSelection(oldIndex);
    }
  } else {
    MenuDown();
  }
}

static void SelectAdjacentFmScanStation(bool forward) {
  const uint8_t count = FmStations.count();
  if (count == 0U) return;

  bool exact = false;
  for (uint8_t i = 0; i < count; ++i) {
    if (FmStations[i].frequency10kHz == fmfreq) {
      FmStationListIndex = i;
      exact = true;
      break;
    }
  }

  if (exact) {
    FmStationListIndex = forward
        ? static_cast<uint8_t>((FmStationListIndex + 1U) % count)
        : static_cast<uint8_t>(FmStationListIndex == 0U
                                   ? count - 1U : FmStationListIndex - 1U);
  } else if (forward) {
    FmStationListIndex = 0;
    for (uint8_t i = 0; i < count; ++i) {
      if (FmStations[i].frequency10kHz > fmfreq) {
        FmStationListIndex = i;
        break;
      }
    }
  } else {
    FmStationListIndex = static_cast<uint8_t>(count - 1U);
    for (uint8_t i = count; i > 0U; --i) {
      if (FmStations[i - 1U].frequency10kHz < fmfreq) {
        FmStationListIndex = static_cast<uint8_t>(i - 1U);
        break;
      }
    }
  }

  ActivateCurrentService();
  ShowFreq();
}

static void SelectAdjacentDabGlobalStation(bool forward) {
  const uint8_t count = DabStations.count();
  if (count == 0U) return;

  bool exact = DabGlobalListView &&
      (dabServiceSelectionPending || trysetservice || !radio.ServiceStart);
  if (!exact && radio.ServiceStart &&
      radio.ServiceIndex < radio.numberofservices) {
    const uint32_t currentSid = radio.service[radio.ServiceIndex].ServiceID;
    const uint32_t currentComponent = radio.service[radio.ServiceIndex].CompID;
    for (uint8_t i = 0; i < count; ++i) {
      const DabStationRecord& station = DabStations[i];
      if (station.channelIndex == dabfreq && station.serviceId == currentSid &&
          station.componentId == currentComponent) {
        DabStationListIndex = i;
        exact = true;
        break;
      }
    }
  }

  if (exact) {
    DabStationListIndex = forward
        ? static_cast<uint8_t>((DabStationListIndex + 1U) % count)
        : static_cast<uint8_t>(DabStationListIndex == 0U
                                   ? count - 1U : DabStationListIndex - 1U);
  } else {
    DabStationListIndex = forward ? 0U : static_cast<uint8_t>(count - 1U);
  }

  DabGlobalListView = true;
  const DabStationRecord& target = DabStations[DabStationListIndex];
  _serviceID = target.serviceId;
  snprintf(_serviceName, sizeof(_serviceName), "%s", target.label);
  _serviceNameCharset = target.charset;
  DIAG_PRINTF("[DAB/UI] target idx=%u ch=%u SID=%08X CID=%08X label='%s'\n",
              static_cast<unsigned>(DabStationListIndex),
              static_cast<unsigned>(target.channelIndex),
              static_cast<unsigned>(target.serviceId),
              static_cast<unsigned>(target.componentId), _serviceName);
  dabServiceSelectionPending = true;
  DabServiceSelectionTimer = millis();
}

static void CloseSlideshowWaitingForDabGlobalSelection(void) {
  if (!fm_features::rotary2ClosesDabSlideshowWait(
          radioMode == RADIO_MODE_FM, slsWaitingView,
          DabStations.count() != 0U))
    return;

  DIAG_PRINTLN("[SLS/UI] loading view cancelled by DAB global selection");
  BuildDisplay();
}

// Rotary 2 up:
// - DAB normal view: next global-scan station, or current-mux service when the
//   global database does not exist.
// - FM AUTO: next full-scan station; no action when that list is empty.
// - FM MAN/MEM: existing regional fine step.
// - After pressing rotary 2: volume mode; rotation adjusts volume.
void KeyUp2(void) {
  if (EpgVisible() && !setvolume) { rotary = rotary2 = 0; tottimer = millis(); EpgMove(1,true); return; }
  tottimer = millis();
  CloseSlideshowWaitingForDabGlobalSelection();

  if (setvolume) {
    // Volume remains available only after explicitly opening its overlay.
    RemoteVolumeStep(+2);
  } else if (ShowServiceInformation) {
    // Never use rotary 2 as an FM fine-tune control while System info is open.
  } else if (SlideShowView || ChannelListView) {
    // Full-screen overlays do not assign an implicit rotary-2 action. Volume
    // remains available only after explicitly pressing the rotary-2 button.
  } else if (radioMode == RADIO_MODE_FM) {
    const fm_features::Rotary2Target target = fm_features::rotary2Target(
        true, tunemode == TUNE_AUTO, FmStations.count() != 0U, false);
    if (target == fm_features::Rotary2Target::FmScanList) {
      SelectAdjacentFmScanStation(true);
    } else if (target == fm_features::Rotary2Target::FmFineTune) {
      const uint8_t spacing = fmRegionProfile(fmRegion).seekSpacing10kHz;
      fmfreq = stepFmFrequency(fmfreq, spacing, fmRegion);
      tuning = true;
      TuningTimer = millis();
      radio.ServiceIndex = 0;
      radio.ServiceStart = false;
      for (byte x = 0; x < 17; x++) _serviceName[x] = '\0';
      ShowFreq();
      DIAG_PRINTF("[FM/UI] rotary2 UP -> +%u kHz, %.1f MHz\n",
                    spacing * 10U, fmfreq / 100.0f);
    }
  } else {
    seek = false;
    CancelDabAuto();
    const fm_features::Rotary2Target target = fm_features::rotary2Target(
        false, tunemode == TUNE_AUTO, DabStations.count() != 0U,
        radio.numberofservices > 0U);
    if (target == fm_features::Rotary2Target::DabGlobalList) {
      SelectAdjacentDabGlobalStation(true);
    } else if (target == fm_features::Rotary2Target::DabCurrentMux) {
      DabGlobalListView = false;
      DABSelectService(true);
    }
  }
  rotary2 = 0;
}

// Rotary 2 down:
// - DAB normal view: previous global-scan station, or current-mux fallback.
// - FM AUTO: previous full-scan station; no action when that list is empty.
// - FM MAN/MEM: existing regional fine step.
// - After pressing rotary 2: volume mode; rotation adjusts volume.
void KeyDown2(void) {
  if (EpgVisible() && !setvolume) { rotary = rotary2 = 0; tottimer = millis(); EpgMove(-1,true); return; }
  tottimer = millis();
  rotary = 0;
  rotary2 = 0;
  CloseSlideshowWaitingForDabGlobalSelection();

  if (setvolume) {
    // Volume remains available only after explicitly opening its overlay.
    RemoteVolumeStep(-2);
  } else if (ShowServiceInformation) {
    // Never use rotary 2 as an FM fine-tune control while System info is open.
  } else if (SlideShowView || ChannelListView) {
    // See KeyUp2(): do not enter volume mode from rotation alone.
  } else if (radioMode == RADIO_MODE_FM) {
    const fm_features::Rotary2Target target = fm_features::rotary2Target(
        true, tunemode == TUNE_AUTO, FmStations.count() != 0U, false);
    if (target == fm_features::Rotary2Target::FmScanList) {
      SelectAdjacentFmScanStation(false);
    } else if (target == fm_features::Rotary2Target::FmFineTune) {
      const uint8_t spacing = fmRegionProfile(fmRegion).seekSpacing10kHz;
      fmfreq = stepFmFrequency(fmfreq, -static_cast<int16_t>(spacing), fmRegion);
      tuning = true;
      TuningTimer = millis();
      radio.ServiceIndex = 0;
      radio.ServiceStart = false;
      for (byte x = 0; x < 17; x++) _serviceName[x] = '\0';
      ShowFreq();
      DIAG_PRINTF("[FM/UI] rotary2 DOWN -> -%u kHz, %.1f MHz\n",
                    spacing * 10U, fmfreq / 100.0f);
    }
  } else {
    seek = false;
    CancelDabAuto();
    const fm_features::Rotary2Target target = fm_features::rotary2Target(
        false, tunemode == TUNE_AUTO, DabStations.count() != 0U,
        radio.numberofservices > 0U);
    if (target == fm_features::Rotary2Target::DabGlobalList) {
      SelectAdjacentDabGlobalStation(false);
    } else if (target == fm_features::Rotary2Target::DabCurrentMux) {
      DabGlobalListView = false;
      DABSelectService(false);
    }
  }
}

// Load the current preset position into the live tuning state. Empty slots
// just dim the position indicator (MEM_DARK); used slots actually retune.
void DoMemoryPosTune(void) {
  if (IsStationEmpty()) {
    memoryposstatus = MEM_DARK;
  } else {
    memoryposstatus = MEM_NORMAL;
    if (radioMode == RADIO_MODE_FM) {
      fmfreq = fmMemory[memorypos].Frequency10kHz;
      _serviceID = 0;
      memset(_serviceName, 0, sizeof(_serviceName));
      strncpy(_serviceName, fmMemory[memorypos].Label, 8);
    } else {
      dabfreq = memory[memorypos].Channel;
      _serviceID = memory[memorypos].ServiceID;
      for (int i = 0; i < 16; i++)
        _serviceName[i] = memory[memorypos].Label[i];
    }

    if (Storage.readByte(EE_BYTE_MEMORYPOS) != memorypos) {
      Storage.writeByte(EE_BYTE_MEMORYPOS, memorypos);
      MarkEepromDirty();
    }
    ShowFreq();
    radio.ServiceStart = false;
    TuningTimer = millis();
    tuning = true;
  }
}

// Whether the current preset slot has never been programmed (sentinel value).
bool IsStationEmpty(void) {
  if (radioMode == RADIO_MODE_FM)
    return !isFmFrequencyValid(fmMemory[memorypos].Frequency10kHz, fmRegion);
  return memory[memorypos].Channel == EE_PRESETS_FREQUENCY;
}

// Wait until an active-low wake input has really returned to idle HIGH. This
// matters for level-triggered light-sleep wake: entering sleep while the source
// is already LOW would immediately return from esp_light_sleep_start().
static bool WaitPinHighStable(uint8_t pin, uint32_t stableMs,
                              uint32_t timeoutMs) {
  const uint32_t started = millis();
  uint32_t highSince = 0;
  while (static_cast<uint32_t>(millis() - started) < timeoutMs) {
    if (digitalRead(pin) == HIGH) {
      if (highSince == 0) highSince = millis();
      if (static_cast<uint32_t>(millis() - highSince) >= stableMs) return true;
    } else {
      highSince = 0;
    }
    delay(1);
  }
  return false;
}

// Experimental standby using ESP32 LIGHT SLEEP. CPU/RAM stop, but execution
// resumes here without rebooting, so the SI4684 firmware, service state,
// 76,800-byte decoder workspace and all UI/IR RAM remain resident. GPIO17 is the shared
// active-low TFT/SI4684 reset and belongs to the WROOM VDD_SDIO domain, hence
// that domain is explicitly kept powered during light sleep.
//
// Wake policy is deliberately conservative:
//   - physical STANDBY button GPIO36 LOW is always enabled;
//   - GPIO12 LOW is enabled ONLY for explicit GPIO12=IR with a learned profile;
//   - GPIO12 is NEVER a wake source in AUTO or INTB, even if its live level is
//     LOW (e.g. no INTB wire, a pending radio interrupt, or an undefined net).
static void EnterLightSleep(bool showStandbyScreen) {
  const bool explicitIrMode = sanitizeGpio12Mode(gpio12Mode) == GPIO12_IR;
  const bool irWakeRequested = explicitIrMode && IrRemoteHasProfile();

  if (irWakeRequested) {
    // gpio_wakeup_enable() temporarily programs GPIO12 as a LOW-level wake
    // source. Stop the project-local CHANGE ISR first and restore it after wake.
    IrRemoteStop();
  }

  // The long-press that requested standby must be released before a LOW-level
  // wake can be armed. GPIO34-39 have no internal pull resistors, so this uses
  // the board's existing external bias exactly as normal button handling does.
  if (!WaitPinHighStable(STANDBYBUTTON, 50UL, 3000UL)) {
    DIAG_PRINTF("[SLEEP] cancelled: standby GPIO%u stayed LOW\n",
                  static_cast<unsigned>(STANDBYBUTTON));
    if (irWakeRequested) IrRemoteBegin();
    return;
  }

  bool irWakeEnabled = false;
  if (irWakeRequested) {
    // 250 ms exceeds the decoder's held-frame association window and therefore
    // avoids arming sleep in the quiet gap between repeated IR frames.
    irWakeEnabled = WaitPinHighStable(SI4684_INTB_PIN, 250UL, 3000UL);
    if (!irWakeEnabled) {
      DIAG_PRINTF("[SLEEP] IR wake skipped this cycle: GPIO%u did not become idle HIGH\n",
                    static_cast<unsigned>(SI4684_INTB_PIN));
    }
  }

  if (showStandbyScreen) {
    tft.pushImage(0, 0, 320, 240, standbymode);
    tftPrint(0, myLanguage[language][78], 155, 210,
             ActiveColor, ActiveColorSmooth, 28);
    for (int x = ContrastSet; x > 0; --x) {
      analogWrite(CONTRASTPIN, x * 2);
      delay(25);
    }
    // The loop above ends at duty=2, not zero. Force the backlight fully OFF
    // before TFT Sleep In so an IR pre-wake/qualification can never expose a
    // dim black LCD for the duration of the qualification window.
    analogWrite(CONTRASTPIN, 0);
  } else {
    analogWrite(CONTRASTPIN, 0);
  }

  tft.writecommand(0x10);  // ILI9341 Sleep In; GRAM contents are retained.
  Headphones.Shutdown();

  // GPIO17 must remain HIGH so neither the TFT nor SI4684 is reset while the
  // CPU sleeps. ESP-IDF specifically requires VDD_SDIO to stay on when WROOM
  // pins in this domain (GPIO16/17) must keep their driven level in light sleep.
  pinMode(17, OUTPUT);
  digitalWrite(17, HIGH);
  esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_ON);

  // The display/backlight/amplifier now stay asleep until a wake source has
  // been QUALIFIED. A physical button wake is accepted immediately. GPIO12 is
  // only a hardware pre-wake: the CPU resumes invisibly, the normal edge
  // decoder is restored, and only the learned STANDBY command is allowed to
  // continue to the visible wake path. Any other IR command returns straight
  // to light sleep without waking the TFT or amplifier.
  esp_err_t sleepResult = ESP_OK;
  uint64_t totalSleepUs = 0U;
  bool acceptedWake = false;
  bool wakeButtonLow = false;
  bool wakeIrLow = false;
  bool acceptedByIr = false;
  uint32_t sleepCycle = 0U;

  while (!acceptedWake) {
    ++sleepCycle;

    gpio_wakeup_disable(static_cast<gpio_num_t>(STANDBYBUTTON));
    // Always clear GPIO12 first. AUTO/INTB therefore cannot inherit a stale
    // wake setting even if GPIO12 is physically LOW or not connected.
    gpio_wakeup_disable(static_cast<gpio_num_t>(SI4684_INTB_PIN));
    gpio_wakeup_enable(static_cast<gpio_num_t>(STANDBYBUTTON),
                       GPIO_INTR_LOW_LEVEL);
    if (irWakeEnabled) {
      gpio_wakeup_enable(static_cast<gpio_num_t>(SI4684_INTB_PIN),
                         GPIO_INTR_LOW_LEVEL);
    }

    const esp_err_t wakeEnableResult = esp_sleep_enable_gpio_wakeup();
    DIAG_PRINTF("[SLEEP] LIGHT enter cycle=%u wakeButton=GPIO%u/LOW GPIO12=%s level=%c control=%s setup=%d\n",
                  static_cast<unsigned>(sleepCycle),
                  static_cast<unsigned>(STANDBYBUTTON),
                  irWakeEnabled ? "IR/LOW+QUALIFY" : "DISABLED",
                  digitalRead(SI4684_INTB_PIN) == HIGH ? 'H' : 'L',
                  radio.controlModeName(), static_cast<int>(wakeEnableResult));
    // Do not wait for the UART line before sleeping. Diagnostics are best
    // effort; move one bounded chunk and leave any remainder queued.
    diagSerial.service(64U);

    const int64_t sleepStartedUs = esp_timer_get_time();
    sleepResult = wakeEnableResult == ESP_OK
                      ? esp_light_sleep_start()
                      : wakeEnableResult;
    const uint64_t oneSleepUs = static_cast<uint64_t>(
        esp_timer_get_time() - sleepStartedUs);
    totalSleepUs += oneSleepUs;

    const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
    wakeButtonLow = digitalRead(STANDBYBUTTON) == LOW;
    wakeIrLow = irWakeEnabled && digitalRead(SI4684_INTB_PIN) == LOW;

    gpio_wakeup_disable(static_cast<gpio_num_t>(STANDBYBUTTON));
    if (irWakeEnabled)
      gpio_wakeup_disable(static_cast<gpio_num_t>(SI4684_INTB_PIN));

    if (sleepResult != ESP_OK) {
      DIAG_PRINTF("[SLEEP] light sleep failed result=%d; restoring runtime\n",
                    static_cast<int>(sleepResult));
      if (irWakeRequested) IrRemoteBegin();
      acceptedWake = true;
      break;
    }

    // The physical button has priority if both inputs happen to be LOW.
    if (wakeButtonLow) {
      if (irWakeRequested) IrRemoteBegin();
      acceptedWake = true;
      acceptedByIr = false;
      break;
    }

    // With GPIO wake enabled, a wake with the button HIGH can only be the
    // explicit IR source (or a harmless transient on it). The pin can already
    // be HIGH by the time the CPU resumes, so do not require wakeIrLow=true to
    // enter qualification; wakeIrLow is used only to seed a still-active MARK.
    const bool irWakeCandidate =
        irWakeEnabled && wakeCause == ESP_SLEEP_WAKEUP_GPIO;
    if (irWakeCandidate) {
      IrRemoteResumeAfterLightSleep(wakeIrLow);

      // Keep TFT in Sleep In, backlight fully off and TPA6130A2 in shutdown
      // while the decoder checks the wake command. The wake-causing first IR
      // frame can be incomplete because its leading edge woke the CPU. Give the
      // user a generous 1.5 s window for the next COMPLETE press/frame instead
      // of requiring an unnaturally fast double-click. Protocol repeat frames
      // still cannot qualify by themselves unless the decoder has seen a real
      // learned STANDBY frame in this qualification window.
      if (IrRemoteQualifyStandbyWake(1500UL)) {
        acceptedWake = true;
        acceptedByIr = true;
        break;
      }

      DIAG_PRINTLN("[SLEEP] IR wake rejected; display/audio stayed asleep");
      radio.Update();
      IrRemoteStop();

      // Do not re-arm a LOW-level wake in the middle of a held/repeating key.
      // If GPIO12 never returns to its IR idle HIGH state, disable IR wake for
      // this sleep session and retain the physical standby button as wake.
      if (!WaitPinHighStable(SI4684_INTB_PIN, 250UL, 3000UL)) {
        irWakeEnabled = false;
        DIAG_PRINTF("[SLEEP] GPIO%u stayed LOW after rejected IR; IR wake disabled until visible wake\n",
                      static_cast<unsigned>(SI4684_INTB_PIN));
      }
      continue;
    }

    // Unexpected GPIO wake state: do not expose a half-restored UI. Return to
    // sleep after servicing radio status, unless IR is not available at all.
    DIAG_PRINTF("[SLEEP] unqualified wake cause=%d button=%c ir=%c; re-sleep\n",
                  static_cast<int>(wakeCause),
                  wakeButtonLow ? 'L' : 'H', wakeIrLow ? 'L' : 'H');
    radio.Update();
  }

  // Ignore the wake button until it has physically returned HIGH; otherwise
  // the same LOW level would immediately be interpreted as a new press.
  standbyWakeReleaseGuard = wakeButtonLow;

  // Rebuild the ILI9341 controller registers while the backlight is still OFF.
  // SPI/sprite/font state stays resident in ESP32 RAM and GPIO17 is never
  // pulsed, so the already-running Si4684 is not disturbed.
  WakeTftControllerFromSleep("WAKE");

  // Service any status that accumulated while the CPU was asleep. In INTB
  // mode a falling edge may have happened during sleep and therefore been
  // missed by the CPU ISR; the driver's normal safety-poll path handles it.
  radio.Update();

  const byte headphoneWake = Headphones.Init();
  Headphones.SetMute(true);
  Headphones.SetHiZ(0);
  Headphones.SetVolume(volume);

  // Rebuild while the backlight is still off, then expose a complete UI at
  // normal brightness. The radio itself was never rebooted or re-uploaded.
  RestoreMainDisplayAfterSlideshow();
  analogWrite(CONTRASTPIN, ContrastSet * 2 + 27);
  Headphones.SetMute(false);

  // Timer-based standby must start a fresh interval after wake instead of
  // immediately sleeping again because millis()/esp_timer include sleep time.
  tottimer = millis();

  DIAG_PRINTF("[SLEEP] LIGHT wake result=%d slept=%llu ms source=%s button=%c ir=%c amp=%u heap=%u\n",
                static_cast<int>(sleepResult),
                static_cast<unsigned long long>(totalSleepUs / 1000ULL),
                acceptedByIr ? "IR-STANDBY" : "BUTTON",
                wakeButtonLow ? 'L' : 'H', wakeIrLow ? 'L' : 'H',
                static_cast<unsigned>(headphoneWake), ESP.getFreeHeap());
}

// User standby: persist changes, then enter light sleep and resume without a
// reset or firmware reload.
void doStandby(void) {
  if (!FlushEeprom()) {
    diagSerial.println("[NVS] standby cancelled: commit failed");
    ShowStatusOverlay("NVS ERROR");
    return;
  }
  EnterLightSleep(true);
}

// Auto-seek scan. `mode` true = upwards, false = downwards. Steps one channel
// at a time and lets the next ProcessDAB cycle decide whether the channel has
// signal lock to halt on.
void Seek(bool mode) {
  if (radioMode == RADIO_MODE_FM) {
    if (!fmSeekStarted) fmSeekStarted = radio.startFmSeek(mode);
    if (fmSeekStarted && !radio.isTunePending()) {
      fmSeekStarted = false;
      seek = false;
      fmfreq = radio.fmFrequency10kHz;
      Storage.put(EE_UINT16_FM_FREQUENCY, fmfreq);
      MarkEepromDirty();
      ShowFreq();
    }
    return;
  }
  if (dabAutoState == DabAutoState::Idle) StartDabAuto(mode);
  ProcessDabAuto();
}

void CancelDabAuto(void) {
  dabAutoState = DabAutoState::Idle;
  dabAutoChannelsTried = 0;
  dabSeekStarted = false;
}

void StartDabAuto(bool forward) {
  // AUTO walks the live service list of the current mux before moving through
  // Band III. A previous Rotary2 global-list selection must not redirect
  // ActivateCurrentService() back into DabStations or restore its old target.
  DabGlobalListView = false;
  dabServiceSelectionPending = false;
  trysetservice = false;
  trysetserviceFreq = 0xFF;
  trysetserviceComponentValid = false;
  dabAutoForward = forward;
  dabAutoChannelsTried = 0;

  if (radio.isDabServiceListReady() && !radio.isDabServiceMetadataReady()) {
    dabAutoDeadlineMs = millis() + 5000UL;
    dabAutoState = DabAutoState::WaitCurrentServiceTypes;
    return;
  }

  if (radio.isDabServiceMetadataReady()) {
    const int16_t adjacent = fm_features::adjacentAudioService(
        radio.service, radio.numberofservices, radio.ServiceIndex, forward);
    if (adjacent >= 0) {
      radio.ServiceIndex = static_cast<uint8_t>(adjacent);
      dabServiceSelectionPending = false;
      ActivateCurrentService();
      seek = false;
      DIAG_PRINTF("[DAB/AUTO] %s service index=%u SID=%08X\n",
                  forward ? "next" : "previous",
                  static_cast<unsigned>(radio.ServiceIndex),
                  static_cast<unsigned>(radio.service[radio.ServiceIndex].ServiceID));
      return;
    }
  }

  DIAG_PRINTF("[DAB/AUTO] %s of mux; searching %s channel\n",
              forward ? "end" : "start", forward ? "next" : "previous");
  dabAutoState = DabAutoState::TuneMux;
}

void ProcessDabAuto(void) {
  if (!seek || radioMode != RADIO_MODE_DAB || tunemode != TUNE_AUTO) {
    CancelDabAuto();
    return;
  }

  const uint32_t now = millis();
  switch (dabAutoState) {
    case DabAutoState::Idle:
      return;

    case DabAutoState::WaitCurrentServiceTypes:
      if (!radio.isDabServiceMetadataReady() &&
          static_cast<int32_t>(now - dabAutoDeadlineMs) < 0) return;
      if (radio.isDabServiceMetadataReady()) {
        const int16_t adjacent = fm_features::adjacentAudioService(
            radio.service, radio.numberofservices, radio.ServiceIndex,
            dabAutoForward);
        if (adjacent >= 0) {
          radio.ServiceIndex = static_cast<uint8_t>(adjacent);
          ActivateCurrentService();
          seek = false;
          DIAG_PRINTF("[DAB/AUTO] adjacent service after metadata index=%u\n",
                      static_cast<unsigned>(radio.ServiceIndex));
          CancelDabAuto();
          return;
        }
      }
      dabAutoState = DabAutoState::TuneMux;
      return;

    case DabAutoState::TuneMux:
      if (dabAutoChannelsTried >= 38U) {
        DIAG_PRINTLN("[DAB/AUTO] no receivable audio multiplex found");
        seek = false;
        CancelDabAuto();
        ShowStatusOverlay(noDabServiceText[language]);
        return;
      }
      dabfreq = fm_features::nextDabChannel(dabfreq, dabAutoForward);
      ++dabAutoChannelsTried;
      radio.ServiceIndex = 0;
      radio.ServiceStart = false;
      memset(_serviceName, 0, sizeof(_serviceName));
      radio.setFreq(dabfreq);
      dabSeekStarted = true;
      dabAutoDeadlineMs = now + 6000UL;
      dabAutoState = DabAutoState::WaitTune;
      DIAG_PRINTF("[DAB/AUTO] tune %s index=%u try=%u/38\n",
                  radio.getChannel(dabfreq), static_cast<unsigned>(dabfreq),
                  static_cast<unsigned>(dabAutoChannelsTried));
      return;

    case DabAutoState::WaitTune:
      if (radio.isTunePending() &&
          static_cast<int32_t>(now - dabAutoDeadlineMs) < 0) return;
      dabSeekStarted = false;
      if (!radio.signallock) {
        dabAutoState = DabAutoState::TuneMux;
        return;
      }
      dabAutoDeadlineMs = now + 8000UL;
      dabAutoState = DabAutoState::WaitServiceList;
      return;

    case DabAutoState::WaitServiceList:
      if (!radio.signallock ||
          static_cast<int32_t>(now - dabAutoDeadlineMs) >= 0) {
        dabAutoState = DabAutoState::TuneMux;
        return;
      }
      if (!radio.isDabServiceListReady()) return;
      dabAutoState = DabAutoState::WaitServiceTypes;
      return;

    case DabAutoState::WaitServiceTypes:
      if (!radio.signallock ||
          static_cast<int32_t>(now - dabAutoDeadlineMs) >= 0) {
        dabAutoState = DabAutoState::TuneMux;
        return;
      }
      if (!radio.isDabServiceMetadataReady()) return;
      {
        const int16_t selected = fm_features::edgeAudioService(
            radio.service, radio.numberofservices, dabAutoForward);
        if (selected < 0) {
          dabAutoState = DabAutoState::TuneMux;
          return;
        }
        radio.ServiceIndex = static_cast<uint8_t>(selected);
        ActivateCurrentService();
        seek = false;
        DIAG_PRINTF("[DAB/AUTO] mux found services=%u; selected %s index=%u\n",
                    static_cast<unsigned>(radio.numberofservices),
                    dabAutoForward ? "first" : "last",
                    static_cast<unsigned>(radio.ServiceIndex));
        CancelDabAuto();
      }
      return;
  }
}

// Rotary encoder 1 ISR (CHANGE on both A and B). Uses the classic
// 4-state quadrature decoder via the enc_states lookup table; encval is
// integrated so a full detent (4 transitions) emits exactly one ±1 tick
// in `rotary`. Inverted output when rotarymode is set.
void read_encoder(void) {
  static uint8_t old_AB = 3;
  static int8_t encval = 0;

  old_AB <<= 2;
  if (digitalRead(ROTARY_PIN_A)) old_AB |= 0x02;
  if (digitalRead(ROTARY_PIN_B)) old_AB |= 0x01;
  encval += enc_states[(old_AB & 0x0f)];

  if (encval > 3) {
    if (rotarymode) rotary = -1; else rotary = 1;
    encval = 0;
  } else if (encval < -3) {
    if (rotarymode) rotary = 1; else rotary = -1;
    encval = 0;
  }
}

// Rotary encoder 2 ISR (volume / menu navigation). Same quadrature decoder as
// read_encoder but writes to `rotary2`.
void read_encoder2(void) {
  static uint8_t old_AB = 3;
  static int8_t encval = 0;

  old_AB <<= 2;
  if (digitalRead(ROTARY_PIN_2A)) old_AB |= 0x02;
  if (digitalRead(ROTARY_PIN_2B)) old_AB |= 0x01;
  encval += enc_states[(old_AB & 0x0f)];

  if (encval > 3) {
    if (rotarymode) rotary2 = -1; else rotary2 = 1;
    encval = 0;
  } else if (encval < -3) {
    if (rotarymode) rotary2 = 1; else rotary2 = -1;
    encval = 0;
  }
}

// Factory reset: write known-good defaults to every persistent NVS record,
// including presets, IR profile and the scanned FM list.
bool DefaultSettings(void) {
  Storage.writeByte(EE_BYTE_CHECKBYTE, EE_CHECKBYTE_VALUE);
  Storage.writeByte(EE_BYTE_CONTRASTSET, 100);
  Storage.writeByte(EE_BYTE_LANGUAGE, 0);
  Storage.writeByte(EE_BYTE_DISPLAYFLIP, 0);
  Storage.writeByte(EE_BYTE_ROTARYMODE, 0);
  Storage.writeByte(EE_BYTE_TUNEMODE, 0);
  Storage.writeByte(EE_BYTE_UNIT, 0);
  Storage.writeByte(EE_BYTE_VOLUME, 62);
  Storage.writeByte(EE_BYTE_MEMORYPOS, 0);
  Storage.writeByte(EE_BYTE_AUTOSLIDESHOW, 0);
  Storage.writeByte(EE_BYTE_RADIO_MODE, RADIO_MODE_DAB);
  Storage.writeByte(EE_BYTE_TOT, 0);
  Storage.writeByte(EE_BYTE_THEME, 0);
  Storage.put(EE_UINT32_SERVICEID, (uint32_t)0);
  Storage.writeByte(EE_BYTE_DABFREQ, 0);
  Storage.put(EE_UINT16_FM_FREQUENCY, static_cast<uint16_t>(8750));
  Storage.writeByte(EE_BYTE_FM_REGION, static_cast<uint8_t>(FmRegion::Europe));
  Storage.writeByte(EE_BYTE_GPIO12_MODE, GPIO12_AUTO);
  Storage.setI2sAudioMode(I2S_AUDIO_48K);
  for (int y = 0; y < 17; y++) {
    Storage.writeByte(EE_CHAR17_SERVICENAME + y, '\0');
  }
  Storage.writeByte(EE_BYTE_FM_SEEK_SENSITIVITY, 1);
  Storage.writeByte(EE_BYTE_FM_AF_ENABLED, 0);

  for (int i = 0; i < EE_PRESETS_CNT; i++) {
    Storage.writeByte(i + EE_PRESETS_FREQ_START, EE_PRESETS_FREQUENCY);
    Storage.put((i * 8) + EE_PRESETS_SERVICEID_START, 0);
    for (int y = 0; y < 17; y++) {
      Storage.writeByte((i * 17) + y + EE_PRESETS_NAME_START, '\0');
    }
  }
  WriteEmptyFmPresetsV4();
  ClearIrProfileStorage();
  FmStations.clear();
  DabStations.clear();
  fmScanCompleted = false;
  dabScanCompleted = false;
  const bool recordsSaved = CommitEepromNow("factory defaults");
  const bool fmScanCleared = Storage.clearScan();
  const bool dabScanCleared = Storage.clearDabScan();
  return recordsSaved && fmScanCleared && dabScanCleared;
}

// Heap-stable variant for already formatted fixed char buffers.
// Fixed-buffer path; frequently changing status fields never churn the heap.
void tftReplaceFixed(int8_t offset, const char *textold, const char *text,
                     int16_t x, int16_t y, int color, int smoothcolor,
                     int backcolor, uint8_t fontsize) {
  const uint8_t *selectedFont = nullptr;
  if (fontsize == 16) selectedFont = FONT16;
  if (fontsize == 28) selectedFont = FONT28;
  if (fontsize == 52) selectedFont = FREQFONT;

  if (currentFont != selectedFont || resetFontOnNextCall) {
    if (currentFont != nullptr) tft.unloadFont();

    tft.loadFont(selectedFont);
    currentFont = selectedFont;
    resetFontOnNextCall = false;
  }

  switch (offset) {
    case -1: tft.setTextDatum(TL_DATUM); break;
    case 0: tft.setTextDatum(TC_DATUM); break;
    case 1: tft.setTextDatum(TR_DATUM); break;
  }

  tft.setTextColor(backcolor, backcolor, false);
  tft.drawString(textold != nullptr ? textold : "", x, y);

  tft.setTextColor(color, smoothcolor, false);
  tft.drawString(text != nullptr ? text : "", x, y);
}


// Heap-stable print variant for fixed char buffers.
void tftPrintFixed(int8_t offset, const char *text, int16_t x, int16_t y,
                   int color, int smoothcolor, uint8_t fontsize) {
  const uint8_t *selectedFont = nullptr;
  if (fontsize == 16) selectedFont = FONT16;
  if (fontsize == 28) selectedFont = FONT28;
  if (fontsize == 52) selectedFont = FREQFONT;

  if (currentFont != selectedFont || resetFontOnNextCall) {
    if (currentFont != nullptr) tft.unloadFont();
    tft.loadFont(selectedFont);
    currentFont = selectedFont;
    resetFontOnNextCall = false;
  }

  tft.setTextColor(color, smoothcolor, (fontsize == 52 ? true : false));

  switch (offset) {
    case -1: tft.setTextDatum(TL_DATUM); break;
    case 0: tft.setTextDatum(TC_DATUM); break;
    case 1: tft.setTextDatum(TR_DATUM); break;
  }

  tft.drawString(text != nullptr ? text : "", x, y, 1);
}

// Exact const-char overload for literal and fixed-buffer call sites; it avoids
// temporary text allocation while preserving identical drawing.
void tftPrint(int8_t offset, const char *text, int16_t x, int16_t y,
              int color, int smoothcolor, uint8_t fontsize) {
  tftPrintFixed(offset, text, x, y, color, smoothcolor, fontsize);
}



// Timeout standby uses the same light-sleep path as the manual command.
void deepSleep(void) {
  StoreFrequency();
  if (!FlushEeprom()) {
    diagSerial.println("[NVS] timeout light-sleep cancelled: commit failed");
    // Prevent a tight retry loop on every main-loop iteration.
    tottimer = millis();
    return;
  }
  EnterLightSleep(false);
}

void loadFonts(bool option) {
  if (option) {
    OneBigLineSprite.loadFont(FONT28);
    FullLineSprite.loadFont(FONT16);
    ModeSprite.loadFont(FONT16);
  } else {
    OneBigLineSprite.unloadFont();
    FullLineSprite.unloadFont();
    ModeSprite.unloadFont();
  }
}

// Reuse the main font asset instead of linking another private FONT16 copy.
const uint8_t* EpgFont16() { return FONT16; }
