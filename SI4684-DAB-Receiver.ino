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
//   INTB  = NOT connected to ESP32; driver must poll CTS/status.
//
// IMPORTANT RESET RULE:
//   GPIO17 resets BOTH the ILI9341 and SI4684. TFT_eSPI is therefore compiled
//   with TFT_RST=-1. GPIO17 is pulsed manually once, then the TFT is
//   reinitialised without letting TFT_eSPI pulse RESET again.
//
// Other subsystems:
//   * TPA6130A2 headphone amp (I2C)
//   * Dual rotary encoders + four buttons + IR remote
//   * RAM-only MOT slideshow buffer
//   * EEPROM settings and presets
//
// setup() initialises everything in a hardware-safe order. loop() is the
// cooperative scheduler for radio, UI, communication and controls.

#include <TFT_eSPI.h>
#include <SPI.h>
#include <TimeLib.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_system.h"
#include <EEPROM.h>
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
#include "src/IRReceiver.h"

TPA6130A2 Headphones;
DAB radio;

TFT_eSPI tft = TFT_eSPI(240, 320);

bool autoslideshow;
bool ChannelListView;
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
bool tuned;
bool tuning;
bool eepromDirty;
bool dabSeekStarted;
bool fmSeekStarted;
bool radioSwitchMuted;
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
uint16_t fmfreq = 8750;
char _serviceName[17];
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
String clockstringOld;
String datestringOld;
String EIDold;
String EnsembleNameOld;
String dabfreqStringOld;
String ITUold;
String PLold;
String PSold;
String RTold;
String SIDold;
String SignalLeveloldString;
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

static const int8_t enc_states[]  = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

typedef struct _Memory {
  byte      Channel;
  uint32_t  ServiceID;
  char      Label[17];
} DABMemory;

TFT_eSprite FullLineSprite = TFT_eSprite(&tft);
TFT_eSprite OneBigLineSprite = TFT_eSprite(&tft);
TFT_eSprite LongSprite = TFT_eSprite(&tft);
TFT_eSprite MediumSprite = TFT_eSprite(&tft);
TFT_eSprite ModeSprite = TFT_eSprite(&tft);
TFT_eSprite ShortSprite = TFT_eSprite(&tft);

DABMemory memory[EE_PRESETS_CNT];

// Forward declarations
void DefaultSettings(void);
void read_encoder(void);
void read_encoder2(void);
void DoMemoryPosTune(void);
void ProcessDAB(void);
void Seek(bool mode);
void deepSleep(void);
void StoreFrequency(void);
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
void DABSelectService(bool dir);
bool IsStationEmpty(void);
void LoadPresets(void);
bool SwitchRadioMode(RadioMode newMode, bool force = false);
static void RestoreTftAfterSharedReset(const char* tag);
void MarkEepromDirty(void);
void FlushEeprom(void);
void LogRamUsage(const char* tag);
void SlideshowReceptionState(bool active);



static bool slsReceiving = false;
static bool slsWaitingView = false;
static bool slsDisplayedFingerprintValid = false;
static uint32_t slsDisplayedHash = 0;
static uint32_t slsDisplayedSize = 0;

void SlideshowReceptionState(bool active) {
  if (slsReceiving == active) return;
  slsReceiving = active;

  if (!active) {
    // The red loading bitmap is drawn directly over the normal icon. Force
    // ShowSlideShowIcon() to repaint that region after a tune/service change,
    // timeout, or completed MOT even when availability stayed false.
    SlideShowAvailableOld = !radio.SlideShowAvailable;
  }

  Serial.printf("[SLS/UI] reception=%s\n", active ? "IN PROGRESS" : "IDLE");
}

static uint16_t tint565PreserveShade(uint16_t src, uint16_t tint, uint8_t maxLum) {
  // Preserve the original icon's internal light/dark structure while changing
  // only its hue. This keeps the same contour/detail lines as slideshowon.
  uint8_t sr = (src >> 11) & 0x1F;
  uint8_t sg = (src >> 5) & 0x3F;
  uint8_t sb = src & 0x1F;
  uint8_t lum = (uint8_t)((sr * 2 + sg + sb * 2) / 5);
  if (maxLum == 0) maxLum = 1;

  uint8_t tr = (tint >> 11) & 0x1F;
  uint8_t tg = (tint >> 5) & 0x3F;
  uint8_t tb = tint & 0x1F;

  uint8_t r = (uint8_t)((uint16_t)tr * lum / maxLum);
  uint8_t g = (uint8_t)((uint16_t)tg * lum / maxLum);
  uint8_t b = (uint8_t)((uint16_t)tb * lum / maxLum);

  return (uint16_t)((r << 11) | (g << 5) | b);
}

static void DrawSlideshowLoadingIcon(int16_t x, int16_t y, bool flatBackground) {
  // Same slideshow "received" icon geometry and shading; only hue is changed.
  // Reception in progress is deliberately red so it cannot be confused with
  // the normal slideshow-available icon.
  uint16_t loadingIcon[30 * 22];

  uint8_t maxLum = 1;
  for (uint16_t i = 0; i < 30U * 22U; ++i) {
    if (slideshowon[i] != slideshowoff[i]) {
      uint16_t px = slideshowon[i];
      uint8_t r = (px >> 11) & 0x1F;
      uint8_t g = (px >> 5) & 0x3F;
      uint8_t b = px & 0x1F;
      uint8_t lum = (uint8_t)((r * 2 + g + b * 2) / 5);
      if (lum > maxLum) maxLum = lum;
    }
  }

  const uint16_t tint = TFT_RED;
  for (uint16_t i = 0; i < 30U * 22U; ++i) {
    if (slideshowon[i] != slideshowoff[i]) {
      loadingIcon[i] = tint565PreserveShade(slideshowon[i], tint, maxLum);
    } else {
      loadingIcon[i] = flatBackground ? BackgroundColor : slideshowoff[i];
    }
  }

  tft.pushImage(x, y, 30, 22, loadingIcon);
}

static void ShowSlideshowReceiveIndicator(void) {
  if (radioMode != RADIO_MODE_DAB || radio.SlideShowAvailable || !slsReceiving) return;
  DrawSlideshowLoadingIcon(10, 187, false);
}

static void ShowSlideshowLoadingScreen(void) {
  tft.fillScreen(BackgroundColor);
  DrawSlideshowLoadingIcon(145, 82, true);
  tftPrint(0, slideshowLoadingText[language], 155, 120,
           PrimaryColor, PrimaryColorSmooth, 28);
  tftPrint(0, slideshowReceivingText[language], 155, 158,
           SecondaryColor, SecondaryColorSmooth, 16);
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

  Serial.printf("[RAM] %s heap total=%u free=%u used=%u minfree=%u maxblock=%u\n",
                tag ? tag : "-",
                heapTotal,
                heapFree,
                heapTotal - heapFree,
                heapMin,
                heapMax);

#if CONFIG_SPIRAM
  Serial.printf("[RAM] %s psram total=%u free=%u used=%u\n",
                tag ? tag : "-",
                ESP.getPsramSize(),
                ESP.getFreePsram(),
                ESP.getPsramSize() - ESP.getFreePsram());
#endif

  Serial.printf("[RAM] %s slideshow single MOT buffer=%u bytes\n",
                tag ? tag : "-", 80U * 512U);
}

void MarkEepromDirty(void) {
  eepromDirty = true;
  EepromDirtyTimer = millis();
}

void FlushEeprom(void) {
  if (!eepromDirty) return;
  EEPROM.commit();
  eepromDirty = false;
}

// The RAM preset table is shared by the two modes. DAB records keep their
// original schema; FM records use a one-byte 100 kHz band index, PI and PS.
void LoadPresets(void) {
  for (int i = 0; i < EE_PRESETS_CNT; ++i) {

    memset(memory[i].Label, 0, sizeof(memory[i].Label));
    if (radioMode == RADIO_MODE_FM) {
      memory[i].Channel = EEPROM.readByte(EE_FM_PRESETS_FREQ_START + i);
      EEPROM.get(EE_FM_PRESETS_PI_START + i * 4, memory[i].ServiceID);
      for (int y = 0; y < 8; ++y) memory[i].Label[y] = EEPROM.readByte(EE_FM_PRESETS_NAME_START + i * EE_FM_PRESET_NAME_LENGTH + y);
    } else {
      memory[i].Channel = EEPROM.readByte(EE_PRESETS_FREQ_START + i);
      EEPROM.get(EE_PRESETS_SERVICEID_START + i * 8, memory[i].ServiceID);
      for (int y = 0; y < 16; ++y) memory[i].Label[y] = EEPROM.readByte(EE_PRESETS_NAME_START + i * 17 + y);
    }
  }

  if (radioMode == RADIO_MODE_FM) {
    radio.clearData();
    radio.numberofservices = 0;
    radio.ServiceIndex = 0;
    for (int i = 0; i < EE_PRESETS_CNT && radio.numberofservices < 32; ++i) {
      if (memory[i].Channel == EE_PRESETS_FREQUENCY) continue;
      const uint16_t stationFrequency = FM_BAND_BOTTOM_10KHZ + static_cast<uint16_t>(memory[i].Channel) * FM_STEP_10KHZ;
      bool duplicate = false;
      for (uint8_t n = 0; n < radio.numberofservices; ++n) {
        if (radio.service[n].CompID == stationFrequency) { duplicate = true; break; }
      }
      if (duplicate) continue;
      DABService& station = radio.service[radio.numberofservices++];
      station.CompID = stationFrequency;
      station.ServiceID = memory[i].ServiceID;
      station.ServiceType = 8;
      strncpy(station.Label, memory[i].Label, sizeof(station.Label) - 1);
      if (station.Label[0] == '\0') snprintf(station.Label, sizeof(station.Label), "%.1f MHz", station.CompID / 100.0f);
    }
  }
}

static void RestoreTftAfterSharedReset(const char* tag) {
  Serial.printf("[%s] TFT full restore after shared reset begin\n", tag);

  // GPIO17 is shared with the SI4684 RSTB pin.  Earlier hardware testing showed
  // that TFT_eSPI::init() itself does not reset the tuner, so it is safe to
  // reinitialise the TFT here, after the Si4684 firmware has finished booting.
  pinMode(17, OUTPUT);
  digitalWrite(17, HIGH);

  tft.init();
  Serial.printf("[%s] TFT init OK\n", tag);
  tft.initDMA();
  Serial.printf("[%s] TFT DMA OK\n", tag);
  doTheme();
  tft.setSwapBytes(true);
  tft.setRotation(displayflip == 0 ? 3 : 1);
  loadFonts(true);
  tft.fillScreen(BackgroundColor);

  // Do NOT restore the backlight here. During a DAB<->FM switch the TFT has
  // just been reset together with the SI4684; keep the panel dark until the
  // new radio image is booted and the splash/firmware information is drawn.
  analogWrite(CONTRASTPIN, 0);
  delay(20);
  Serial.printf("[%s] TFT full restore complete; backlight still OFF\n", tag);
}

bool SwitchRadioMode(RadioMode newMode, bool force) {
  Serial.printf("[SWITCH] request %s -> %s force=%u\n",
                radioMode == RADIO_MODE_FM ? "FM" : "DAB",
                newMode == RADIO_MODE_FM ? "FM" : "DAB",
                force ? 1U : 0U);
  if (!force && newMode == radioMode) {
    Serial.println("[SWITCH] already in requested mode");
    return true;
  }
  const bool modeChanged = newMode != radioMode;
  Headphones.SetMute(true);
  Serial.println("[SWITCH] headphones muted");

  // The TFT shares GPIO17 RESET with the SI4684, so no message can survive the
  // actual hardware reset. Show the transition message briefly BEFORE reset,
  // then fade the backlight completely off for firmware upload/boot.
  const int switchBrightness = ContrastSet * 2 + 27;
  tft.fillScreen(BackgroundColor);
  tftPrint(0, newMode == RADIO_MODE_FM ? switchingToFmText[language]
                                      : switchingToDabText[language],
           160, 96, ActiveColor, ActiveColorSmooth, 28);
  delay(450);
  Serial.println("[SWITCH] fading TFT backlight to 0 before shared reset");
  for (int level = switchBrightness; level >= 0; level -= 8) {
    analogWrite(CONTRASTPIN, level);
    delay(8);
  }
  analogWrite(CONTRASTPIN, 0);

  // GPIO17 is the shared active-low reset line for SI4684 RSTB and TFT RESET.
  // INTB is physically not connected to the ESP32, so the radio driver remains
  // in polling mode.
  Serial.println("[SWITCH] shared RST GPIO17 LOW (SI4684 + TFT)");
  pinMode(17, OUTPUT);
  digitalWrite(17, LOW);
  delay(10);
  Serial.println("[SWITCH] shared RST GPIO17 HIGH");
  digitalWrite(17, HIGH);
  delay(10);

  // Do not talk to the TFT yet. Leave it blank/reset while the Si4684 image
  // is transferred. TFT and radio use separate SPI buses; only RESET is shared.
  pinMode(17, OUTPUT);
  digitalWrite(17, HIGH);
  delay(10);
  Serial.println("[SWITCH] SI4684 RST released; booting radio before TFT restore");

  Serial.printf("[SWITCH] radio.begin mode=%s\n", newMode == RADIO_MODE_FM ? "FM" : "DAB");
  if (!radio.begin(15, newMode)) {
    Serial.println("[SWITCH] radio.begin FAILED");
    tft.fillScreen(BackgroundColor);
    tftPrint(0, myLanguage[language][77], 160, 110, ActiveColor, ActiveColorSmooth, 28);
    return false;
  }
  Serial.println("[SWITCH] radio.begin OK");
  RestoreTftAfterSharedReset("SWITCH");

  // Show the same boot identification after a DAB<->FM firmware-image switch
  // as during power-on. Chip ID and firmware are queried from the Si4684;
  // nothing is hard-coded here.
  Serial.println("[SWITCH] drawing splash + detected firmware");
  tft.pushImage(0, 0, 320, 240, SplashScreen);
  tftPrint(0, myLanguage[language][72], 155, 15,
           ActiveColor, ActiveColorSmooth, 28);
  tftPrint(0, String(myLanguage[language][9]) + " " + String(VERSION),
           160, 190, TFT_WHITE, TFT_DARKGREY, 16);
  const String switchedRadioVersion =
      String(radio.getChipID()) + " v" + String(radio.getFirmwareVersion());
  Serial.printf("[SWITCH] detected radio: %s\n", switchedRadioVersion.c_str());
  tftPrint(0, switchedRadioVersion, 160, 210,
           TFT_WHITE, TFT_DARKGREY, 16);

  Serial.println("[SWITCH] new mode ready; restoring TFT backlight");
  for (int level = 0; level < switchBrightness; level += 8) {
    analogWrite(CONTRASTPIN, level);
    delay(8);
  }
  analogWrite(CONTRASTPIN, switchBrightness);
  delay(1500);

  radioMode = newMode;

  // FM rotary 1 exposes only AUTO and MEM. Manual +/-100 kHz remains
  // permanently available on rotary 2.
  if (radioMode == RADIO_MODE_FM && tunemode == TUNE_MAN) {
    tunemode = TUNE_AUTO;
    EEPROM.writeByte(EE_BYTE_TUNEMODE, tunemode);
    MarkEepromDirty();
  }

  if (modeChanged) {
    EEPROM.writeByte(EE_BYTE_RADIO_MODE, static_cast<uint8_t>(radioMode));
    MarkEepromDirty();
  }
  LoadPresets();

  seek = false;
  dabSeekStarted = false;
  fmSeekStarted = false;
  tuning = false;
  trysetservice = false;
  SlideShowView = false;
  if (radioMode == RADIO_MODE_FM) {
    radio.setFmFrequency(fmfreq);
  } else {
    radio.setFreq(dabfreq);
    EEPROM.get(EE_UINT32_SERVICEID, _serviceID);
    trysetservice = _serviceID != 0;
  }
  BuildDisplay();
  LogRamUsage(radioMode == RADIO_MODE_FM ? "after FM switch" : "after DAB switch");
  radioSwitchMuted = true;
  RadioSwitchMuteTimer = millis();
  return true;
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
// Order matters: brownout off → Serial → GPIO drive → filesystem → EEPROM →
// audio → display → radio → menu state → encoders/IR.
void setup(void) {
  // DIAGNOSTIC BUILD:
  // Keep the ESP32 brownout detector enabled. If supply voltage is marginal,
  // the serial log should report the real reset instead of hiding it.
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("==================================================");
  Serial.println("SI4684 FM/DAB DIAGNOSTIC BOOT");
  Serial.println("==================================================");
  Serial.printf("[BOOT] reset reason=%d\n", static_cast<int>(esp_reset_reason()));
  Serial.printf("[BOOT] free heap=%u min free heap=%u\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap());
  Serial.println("[BOOT] setup() entered");

  // Reduce drive strength on GPIOs sharing PCB tracks with the TFT/SPI
  // to lower EMI and crosstalk.
  gpio_set_drive_capability((gpio_num_t) 4, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 5, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 13, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 14, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 15, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 16, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 17, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 21, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 22, GPIO_DRIVE_CAP_0);
  Serial.printf("[BOOT] GPIO drive config OK; heap=%u\n", ESP.getFreeHeap());

  // EEPROM check byte acts as a schema version: when its value doesn't match
  // EE_CHECKBYTE_VALUE the previous layout is incompatible (or this is a fresh
  // device), so we wipe everything to defaults.
  Serial.println("[BOOT] EEPROM.begin");
  EEPROM.begin(EE_TOTAL_CNT);
  const uint8_t storedSchema = EEPROM.readByte(EE_BYTE_CHECKBYTE);
  Serial.printf("[BOOT] EEPROM OK schema=%u\n", storedSchema);
  if (storedSchema == EE_CHECKBYTE_DAB_ONLY) {
    // Schema 2 -> 3: preserve every DAB setting/preset and initialise only FM.
    EEPROM.writeByte(EE_BYTE_CHECKBYTE, EE_CHECKBYTE_VALUE);
    EEPROM.writeByte(EE_BYTE_RADIO_MODE, RADIO_MODE_DAB);
    EEPROM.put(EE_UINT16_FM_FREQUENCY, static_cast<uint16_t>(8750));
    for (int i = 0; i < EE_PRESETS_CNT; ++i) {
      EEPROM.writeByte(EE_FM_PRESETS_FREQ_START + i, EE_PRESETS_FREQUENCY);
      EEPROM.put(EE_FM_PRESETS_PI_START + i * 4, static_cast<uint32_t>(0));
      for (int y = 0; y < EE_FM_PRESET_NAME_LENGTH; ++y) EEPROM.writeByte(EE_FM_PRESETS_NAME_START + i * EE_FM_PRESET_NAME_LENGTH + y, '\0');
    }
    MarkEepromDirty();
  } else if (storedSchema != EE_CHECKBYTE_VALUE) {
    DefaultSettings();
  }
  ContrastSet = EEPROM.readByte(EE_BYTE_CONTRASTSET);
  language = EEPROM.readByte(EE_BYTE_LANGUAGE);
  displayflip = EEPROM.readByte(EE_BYTE_DISPLAYFLIP);
  rotarymode = EEPROM.readByte(EE_BYTE_ROTARYMODE);
  tunemode = EEPROM.readByte(EE_BYTE_TUNEMODE);
  if (radioMode == RADIO_MODE_FM && tunemode == TUNE_MAN) tunemode = TUNE_AUTO;
  unit = EEPROM.readByte(EE_BYTE_UNIT);
  dabfreq = EEPROM.readByte(EE_BYTE_DABFREQ);
  volume = EEPROM.readByte(EE_BYTE_VOLUME);
  memorypos = EEPROM.readByte(EE_BYTE_MEMORYPOS);
  autoslideshow = EEPROM.readByte(EE_BYTE_AUTOSLIDESHOW);
  tot = EEPROM.readByte(EE_BYTE_TOT);
  CurrentTheme = EEPROM.readByte(EE_BYTE_THEME);
  radioMode = EEPROM.readByte(EE_BYTE_RADIO_MODE) == RADIO_MODE_FM ? RADIO_MODE_FM : RADIO_MODE_DAB;
  requestedRadioMode = radioMode;
  EEPROM.get(EE_UINT16_FM_FREQUENCY, fmfreq);
  if (fmfreq < FM_BAND_BOTTOM_10KHZ || fmfreq > FM_BAND_TOP_10KHZ) fmfreq = FM_BAND_BOTTOM_10KHZ;
  Serial.printf("[BOOT] settings loaded: mode=%s DAB=%u FM=%u volume=%u theme=%u\n",
                radioMode == RADIO_MODE_FM ? "FM" : "DAB",
                dabfreq, fmfreq, volume, CurrentTheme);
  LoadPresets();
  Serial.printf("[BOOT] presets loaded; heap=%u\n", ESP.getFreeHeap());
  LogRamUsage("after settings/presets");

  Serial.println("[BOOT] TPA6130A2 init");
  const byte headphoneInitResult = Headphones.Init();
  Serial.printf("[BOOT] TPA init result=%u\n", headphoneInitResult);
  delay(50);
  Headphones.SetHiZ(0);
  delay(50);
  Headphones.SetVolume(volume);
  Headphones.SetMute(true);
  Serial.println("[BOOT] headphones configured and muted");

  Serial.printf("[BOOT] TFT init begin; heap=%u\n", ESP.getFreeHeap());
  tft.init();
  Serial.println("[BOOT] TFT init OK");
  LogRamUsage("after TFT init");
  tft.initDMA();
  Serial.println("[BOOT] TFT DMA OK");
  doTheme();
  Serial.println("[BOOT] theme OK");
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
  Serial.println("[BOOT] GPIO inputs + rotary interrupts OK");

  tft.setSwapBytes(true);
  tft.fillScreen(BackgroundColor);

  OneBigLineSprite.createSprite(270, 30);
  OneBigLineSprite.setSwapBytes(true);

  ShortSprite.createSprite(36, 16);
  ShortSprite.setSwapBytes(true);

  MediumSprite.createSprite(70, 16);
  MediumSprite.setSwapBytes(true);

  LongSprite.createSprite(150, 17);
  LongSprite.setSwapBytes(true);

  FullLineSprite.createSprite(308, 20);
  FullLineSprite.setSwapBytes(true);

  ModeSprite.createSprite(46, 47);
  ModeSprite.setTextDatum(TC_DATUM);
  ModeSprite.setSwapBytes(true);
  Serial.printf("[BOOT] TFT sprites created; heap=%u min=%u\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap());

  Serial.println("[BOOT] loadFonts begin");
  loadFonts(true);
  Serial.printf("[BOOT] loadFonts OK; heap=%u\n", ESP.getFreeHeap());

  // Boot-time button shortcuts (held during power-on):
  //   SL only            → invert rotary direction (left/right) and save
  //   MODE only          → flip display orientation 180° and save
  //   SL + ROTARY        → reset all settings (DefaultSettings) and restart
  if (digitalRead(SLBUTTON) == LOW && digitalRead(ROTARY_BUTTON) == HIGH) {
    if (rotarymode == 0) rotarymode = 1; else rotarymode = 0;
    EEPROM.writeByte(EE_BYTE_ROTARYMODE, rotarymode);
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
    EEPROM.writeByte(EE_BYTE_DISPLAYFLIP, displayflip);
    MarkEepromDirty();

    analogWrite(CONTRASTPIN, ContrastSet * 2 + 27);
    tftPrint(0, myLanguage[language][3], 155, 70, ActiveColor, ActiveColorSmooth, 28);
    tftPrint(0, myLanguage[language][2], 155, 130, ActiveColor, ActiveColorSmooth, 28);
    while (digitalRead(MODEBUTTON) == LOW);
  }

  if (digitalRead(ROTARY_BUTTON) == LOW && digitalRead(SLBUTTON) == LOW) {
    DefaultSettings();

    analogWrite(CONTRASTPIN, ContrastSet * 2 + 27);
    tftPrint(0, myLanguage[language][10], 155, 70, ActiveColor, ActiveColorSmooth, 28);
    tftPrint(0, myLanguage[language][2], 155, 130, ActiveColor, ActiveColorSmooth, 28);
    while (digitalRead(ROTARY_BUTTON) == LOW && digitalRead(SLBUTTON) == LOW);
    ESP.restart();
  }

  // V16 shared-reset recovery (same proven sequence as V14.1).
  // The TFT and SI4684 are on separate SPI buses; only GPIO17 RESET is shared.
  Serial.println("[V16] shared-reset recovery START");
  Serial.printf("[V16] TFT: CS=%d DC=%d TFT_RST=%d MOSI=%d MISO=%d SCLK=%d\n",
                TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_MISO, TFT_SCLK);
  Serial.println("[V16] SI4684: CS=15 RST=17 MOSI=13 MISO=16 SCLK=14; INT not connected");

  // Initialise the radio SPI object on its own bus.
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);
  SPI.begin(14, 16, 13, 15);

  // One physical reset affects both SI4684 RSTB and ILI9341 RESET.
  pinMode(17, OUTPUT);
  Serial.println("[V16] GPIO17 shared reset LOW 20 ms");
  digitalWrite(17, LOW);
  delay(20);
  digitalWrite(17, HIGH);
  Serial.println("[V16] GPIO17 shared reset HIGH; wait 200 ms");
  delay(200);

  // Recover ILI9341 after the shared hardware reset. TFT_RST=-1 prevents
  // tft.init() from pulsing GPIO17 and resetting the SI4684 again.
  digitalWrite(15, HIGH);
  Serial.println("[V16] TFT re-init after shared reset");
  tft.init();
  delay(20);
  tft.setRotation(displayflip == 0 ? 3 : 1);
  tft.setSwapBytes(true);
  tft.initDMA();
  doTheme();
  tft.fillScreen(BackgroundColor);

  // Restore the normal boot logo after the real reset/re-init.
  Serial.println("[BOOT] drawing splash after TFT recovery");
  tft.pushImage(0, 0, 320, 240, SplashScreen);
  tftPrint(0, myLanguage[language][72], 155, 15,
           ActiveColor, ActiveColorSmooth, 28);
  tftPrint(0, String(myLanguage[language][9]) + " " + String(VERSION),
           160, 190, TFT_WHITE, TFT_DARKGREY, 16);
  Serial.println("[BOOT] splash OK");

  for (int x = 0; x <= ContrastSet; x++) {
    analogWrite(CONTRASTPIN, x * 2 + 27);
    delay(30);
  }

  // Start/reuse SI4684. Its SPI bus was already initialised above.
  Serial.printf("[V16] radio.begin mode=%s\n",
                radioMode == RADIO_MODE_FM ? "FM" : "DAB");
  const bool radioBeginOk = radio.begin(15, radioMode);
  Serial.printf("[V16] radio.begin returned=%u\n", radioBeginOk ? 1U : 0U);
  if (!radioBeginOk) {
    Serial.println("[V16] radio.begin FAILED");
    tftPrint(0, radioErrorText[language], 160, 210,
             TFT_RED, TFT_DARKGREY, 16);
    for (;;) delay(1000);
  }

  // Firmware identity is detected from the chip; never hard-code it here.
  const String detectedRadioVersion =
      String(radio.getChipID()) + " v" + String(radio.getFirmwareVersion());
  Serial.printf("[V16] detected radio: %s\n", detectedRadioVersion.c_str());
  radio.SlideShowDebug = true;
  Serial.println("[SLS] diagnostics ENABLED");
  tftPrint(0, detectedRadioVersion, 160, 210,
           TFT_WHITE, TFT_DARKGREY, 16);

  if (radioMode == RADIO_MODE_FM) LoadPresets();

  delay(1500);

  if (tunemode == TUNE_MEM && !IsStationEmpty()) {
    DoMemoryPosTune();
  } else if (radioMode == RADIO_MODE_FM) {
    if (tunemode == TUNE_MEM) tunemode = TUNE_MAN;
    radio.setFmFrequency(fmfreq);
  } else {
    if (tunemode == TUNE_MEM) tunemode = TUNE_MAN;
    radio.setFreq(dabfreq);
    EEPROM.get(EE_UINT32_SERVICEID, _serviceID);
    for (int i = 0; i < 16; i++) {
      _serviceName[i] = EEPROM.readByte(i + EE_CHAR17_SERVICENAME);
    }
  }
  if (radioMode == RADIO_MODE_DAB && _serviceID != 0) trysetservice = true;

  radioSwitchMuted = true;
  RadioSwitchMuteTimer = millis();

  Serial.println("[BOOT] BuildDisplay begin");
  BuildDisplay();
  Serial.println("[BOOT] BuildDisplay OK");
  tottimer = millis();
  Serial.println("[BOOT] IRReceiverBegin");
  IRReceiverBegin();
  Serial.printf("[BOOT] SETUP COMPLETE free heap=%u min=%u\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap());
}

// Main cooperative scheduler. Three subsystems run every iteration:
//   IRReceiver()    poll IR remote
//   ProcessDAB()    pump radio.Update() + UI updates
//   Communication() serial control protocol
// Everything else is timer-gated or input-driven and must stay non-blocking.
void loop(void) {
  IRReceiver();
  ProcessDAB();
  Communication();
  if (displayreset) ShowTuneModeCurrent();
  displayreset = false;

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

  // 500 ms after the user stops scrolling, commit either a preset store
  // (StoreFrequency) or finalise the tune. We detach the encoder interrupts
  // around the EEPROM write so the long flash erase doesn't lose ticks.
  if (millis() - TuningTimer >= 500) {
    if (store) {
      detachInterrupt(digitalPinToInterrupt(ROTARY_PIN_A));
      detachInterrupt(digitalPinToInterrupt(ROTARY_PIN_B));
      StoreFrequency();
      store = false;
      attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_A), read_encoder, CHANGE);
      attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_B), read_encoder, CHANGE);
    } else if (tuning) {
      if (radioMode == RADIO_MODE_FM) {
        radio.setFmFrequency(fmfreq);
        EEPROM.put(EE_UINT16_FM_FREQUENCY, fmfreq);
        MarkEepromDirty();
      } else {
        radio.setFreq(dabfreq);
      }
      tuning = false;
      if (tunemode == TUNE_MEM) trysetservice = true;
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

  // Buttons: Mode and Standby are long-press capable (1 s) and self-check
  // their own pin every iteration. The three other buttons act on the press
  // edge only and use the buttonEdge() helper to one-shot the action.
  static bool slArmed = true, rotArmed = true, rot2Armed = true;
  ModeButtonPress();                                                            // self-checks pin (long-press)
  if (!menu && buttonEdge(SLBUTTON, slArmed)) SlideShowButtonPress();
  if (!menu) StandbyButtonPress();                                              // self-checks pin (long-press)
  if (buttonEdge(ROTARY_BUTTON, rotArmed)) ButtonPress();
  if (!menu && buttonEdge(ROTARY_BUTTON2, rot2Armed)) Button2Press();
}

// Pump the radio driver and refresh the on-screen indicators. Skipped while
// `tuning` is true so we don't fight an in-progress retune.
void ProcessDAB(void) {
  if (!tuning) {
    radio.Update();
    SignalLevel = radio.getRSSI();
    if (radioMode == RADIO_MODE_FM && !radio.isTunePending() &&
        radio.fmFrequency10kHz >= FM_BAND_BOTTOM_10KHZ && radio.fmFrequency10kHz <= FM_BAND_TOP_10KHZ &&
        fmfreq != radio.fmFrequency10kHz) {
      fmfreq = radio.fmFrequency10kHz;
      ShowFreq();
    }
  }

  // V8 diagnostic: automatic panic/recovery is intentionally disabled.
  // With GPIO17 shared between SI4684 RSTB and TFT RESET, any recovery pulse
  // also blanks the display.  No DAB lock (e.g. with no antenna connected) is
  // not a reason to reset the tuner.  Re-enable recovery only after the display
  // and DAB runtime path are proven stable.

  if (radioMode == RADIO_MODE_DAB && trysetservice && radio.signallock) {
    for (byte x = 0; x < radio.numberofservices; x++) {
      if (_serviceID == radio.service[x].ServiceID) {
        radio.setService(x);
        radio.ServiceStart = true;
        trysetservice = false;
        store = true;
      }
    }
  }

  if (!SlideShowView && !menu) {
    if (!ChannelListView) ShowSignalLevel();
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
        Serial.printf("[SLS/UI] duplicate image skipped size=%u hash=%08X\n",
                      imageSize, imageHash);
      } else {
        const bool displayed = ShowSlideShow();
        Serial.printf("[SLS/UI] slideshow render=%s size=%u hash=%08X\n",
                      displayed ? "OK" : "FAIL", imageSize, imageHash);
        if (displayed) {
          slsDisplayedFingerprintValid = true;
          slsDisplayedHash = imageHash;
          slsDisplayedSize = imageSize;
        } else {
          // A failed decoder may have left a partially cleared frame. Return
          // to a usable UI instead of keeping an empty slideshow view armed.
          SlideShowView = false;
          slsDisplayedFingerprintValid = false;
          RestoreMainDisplayAfterSlideshow();
        }
      }
      radio.SlideShowUpdate = false;
      slsWaitingView = false;
      LogRamUsage("after slideshow display");
    } else if (slsWaitingView && slsReceiving && !menu) {
      // The dedicated waiting screen was drawn when the view was entered.
      // Leave it untouched until a complete MOT image is ready.
    } else if (slsWaitingView && !slsReceiving) {
      // Reception ended without producing a valid image (for example timeout).
      // Do not leave the UI stuck in an empty armed slideshow view.
      slsWaitingView = false;
      SlideShowView = false;
      slsDisplayedFingerprintValid = false;
      RestoreMainDisplayAfterSlideshow();
    }
  }
}

// Leave the volume overlay and redraw whichever view was visible underneath.
void closeVolume(void) {
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

// Step through the ensemble's services, skipping anything that isn't a
// regular audio service (type 0x00/0x04/0x05). Wraps at both ends.
void DABSelectService(bool dir) {
  if (radioMode == RADIO_MODE_FM) {
    if (radio.numberofservices == 0) return;
    radio.ServiceIndex = dir ? (radio.ServiceIndex + 1) % radio.numberofservices
                             : (radio.ServiceIndex == 0 ? radio.numberofservices - 1 : radio.ServiceIndex - 1);
    fmfreq = static_cast<uint16_t>(radio.service[radio.ServiceIndex].CompID);
    radio.setFmFrequency(fmfreq);
    EEPROM.put(EE_UINT16_FM_FREQUENCY, fmfreq);
    MarkEepromDirty();
    return;
  }
  if (radio.numberofservices > 0) {
    bool hasValidService = false;
    for (int i = 0; i < radio.numberofservices; i++) {
      if (radio.service[i].ServiceType == 0x00 ||
          radio.service[i].ServiceType == 0x04 ||
          radio.service[i].ServiceType == 0x05) {
        hasValidService = true;
        break;
      }
    }

    if (!hasValidService) return;

    if (dir) {
      radio.ServiceIndex = (radio.ServiceIndex + 1) % radio.numberofservices;
    } else {
      radio.ServiceIndex = (radio.ServiceIndex == 0) ? (radio.numberofservices - 1) : (radio.ServiceIndex - 1);
    }

    while (radio.service[radio.ServiceIndex].ServiceType != 0x00 &&
           radio.service[radio.ServiceIndex].ServiceType != 0x04 &&
           radio.service[radio.ServiceIndex].ServiceType != 0x05) {
      if (dir) {
        radio.ServiceIndex = (radio.ServiceIndex + 1) % radio.numberofservices;
      } else {
        radio.ServiceIndex = (radio.ServiceIndex == 0) ? (radio.numberofservices - 1) : (radio.ServiceIndex - 1);
      }
    }

    radio.setService(radio.ServiceIndex);
    radio.ServiceStart = true;
    store = true;
  }
}

// Persist the currently playing channel + service so the next boot can
// restore it. Triggered 500 ms after the user stops scrolling (see loop()).

// Tune-mode field:
//   DAB = original MAN / AUTO / MEM
//   FM  = AUTO / MEM only. Manual +/-100 kHz is always on rotary 2.
void ShowTuneModeCurrent(void) {
  // This sprite contains a cut-out of the main-screen background. Never push
  // it over a full-screen overlay such as Service Information.
  if (menu || SlideShowView || ChannelListView || ShowServiceInformation) return;

  // Exactly one tune mode is highlighted:
  // selected = ActiveColor, inactive = GreyoutColor.
  // MEM uses SignificantColor only during actual preset-store mode.
  ModeSprite.pushImage(-6, -33, 320, 240, Background);
  ModeSprite.setTextDatum(TC_DATUM);

  if (radioMode == RADIO_MODE_FM) {
    ModeSprite.setTextColor(
        tunemode == TUNE_AUTO ? ActiveColor : GreyoutColor,
        tunemode == TUNE_AUTO ? ActiveColorSmooth : BackgroundColor,
        false);
    ModeSprite.drawString("AUTO", 23, 8);

    if (tunemode == TUNE_MEM && memorystore) {
      ModeSprite.setTextColor(SignificantColor, SignificantColorSmooth, false);
    } else {
      ModeSprite.setTextColor(
          tunemode == TUNE_MEM ? ActiveColor : GreyoutColor,
          tunemode == TUNE_MEM ? ActiveColorSmooth : BackgroundColor,
          false);
    }
    ModeSprite.drawString("MEM", 23, 26);
  } else {
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
  EEPROM.writeByte(EE_BYTE_TUNEMODE, tunemode);
  MarkEepromDirty();
}

void StoreFrequency(void) {
  if (radioMode == RADIO_MODE_FM) {
    fmfreq = radio.fmFrequency10kHz;
    EEPROM.put(EE_UINT16_FM_FREQUENCY, fmfreq);
    MarkEepromDirty();
    return;
  }
  EEPROM.put(EE_UINT32_SERVICEID, radio.service[radio.ServiceIndex].ServiceID);
  EEPROM.put(EE_BYTE_DABFREQ, dabfreq);
  for (int i = 0; i < 16; i++) {
    EEPROM.writeByte(i + EE_CHAR17_SERVICENAME, radio.PStext[i]);
  }
  MarkEepromDirty();
  _serviceID = radio.service[radio.ServiceIndex].ServiceID;
}

// Rotary 1 click: stores/recalls a memory preset when in TUNE_MEM, toggles
// the channel-list view otherwise, or routes to DoMenu() when the menu is open.
void ButtonPress(void) {
  tottimer = millis();
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
          const uint8_t frequencyIndex = static_cast<uint8_t>((fmfreq - FM_BAND_BOTTOM_10KHZ) / FM_STEP_10KHZ);
          memory[memorypos].Channel = frequencyIndex;
          memory[memorypos].ServiceID = radio.fmPi;
          memset(memory[memorypos].Label, 0, sizeof(memory[memorypos].Label));
          strncpy(memory[memorypos].Label, radio.fmPs, 8);
          if (memory[memorypos].Label[0] == '\0') snprintf(memory[memorypos].Label, sizeof(memory[memorypos].Label), "%.1f MHz", fmfreq / 100.0f);
          EEPROM.writeByte(EE_FM_PRESETS_FREQ_START + memorypos, frequencyIndex);
          EEPROM.put(EE_FM_PRESETS_PI_START + memorypos * 4, static_cast<uint32_t>(radio.fmPi));
          for (int x = 0; x < EE_FM_PRESET_NAME_LENGTH; ++x) EEPROM.writeByte(EE_FM_PRESETS_NAME_START + memorypos * EE_FM_PRESET_NAME_LENGTH + x, memory[memorypos].Label[x]);
          LoadPresets();
        } else {
          EEPROM.writeByte(memorypos + EE_PRESETS_FREQ_START, dabfreq);
          EEPROM.put((memorypos * 8) + EE_PRESETS_SERVICEID_START, radio.service[radio.ServiceIndex].ServiceID);
          for (int x = 0; x < 16; x++) {
            char character = radio.service[radio.ServiceIndex].Label[x];
            EEPROM.writeByte((memorypos * 17) + x + EE_PRESETS_NAME_START, character);
            memory[memorypos].Label[x] = character;
          }
          EEPROM.writeByte((memorypos * 17) + 16 + EE_PRESETS_NAME_START, '\0');
          memory[memorypos].Label[16] = '\0';
          memory[memorypos].Channel = dabfreq;
          memory[memorypos].ServiceID = radio.service[radio.ServiceIndex].ServiceID;
        }
        MarkEepromDirty();

        ShowTuneModeCurrent();
        if (memoryposstatus == MEM_DARK || memoryposstatus == MEM_EXIST) {
          memoryposstatus = MEM_NORMAL;
          ShowMemoryPos();
        }
      }
    } else {
      if (ChannelListView || SlideShowView || ShowServiceInformation) {
        BuildDisplay();
      } else {
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
  if (radioMode == RADIO_MODE_FM) return;
  Serial.printf("[SLS/UI] button available=%u update=%u receiving=%u view=%u\n",
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
      Serial.println("[SLS/UI] manual view armed; waiting for complete MOT");
      ShowSlideshowLoadingScreen();
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
  bool pressed = (digitalRead(MODEBUTTON) == LOW);

  if (!pressed) {
    if (pressStart != 0) {
      if (!actionDone) {
        if (radioMode == RADIO_MODE_FM) {
          tunemode = (tunemode == TUNE_MEM) ? TUNE_AUTO : TUNE_MEM;
        } else {
          tunemode++;
          if (tunemode > TUNE_MEM) tunemode = TUNE_MAN;
        }
        ShowTuneModeCurrent();
        ShowMemoryPos();
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
    seek = false;
    dabSeekStarted = false;
    fmSeekStarted = false;

    if (menu) {
      EEPROM.writeByte(EE_BYTE_LANGUAGE, language);
      EEPROM.writeByte(EE_BYTE_CONTRASTSET, ContrastSet);
      EEPROM.writeByte(EE_BYTE_AUTOSLIDESHOW, autoslideshow);
      EEPROM.writeByte(EE_BYTE_UNIT, unit);
      EEPROM.writeByte(EE_BYTE_TOT, tot);
      EEPROM.writeByte(EE_BYTE_THEME, CurrentTheme);
      MarkEepromDirty();
      menu = false;
      if (requestedRadioMode != radioMode) {
        if (!SwitchRadioMode(requestedRadioMode)) requestedRadioMode = radioMode;
      }
      else BuildDisplay();
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
    requestedRadioMode = radioMode;
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

  if (!pressed) {
    if (pressStart != 0) {
      if (!actionDone) {
        // Short press release in main view: toggle service info
        if (!ShowServiceInformation && !SlideShowView && !ChannelListView) {
          ShowServiceInfo();
          ShowServiceInformation = true;
        } else {
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
        EEPROM.writeByte(EE_FM_PRESETS_FREQ_START + memorypos, EE_PRESETS_FREQUENCY);
        EEPROM.put(EE_FM_PRESETS_PI_START + memorypos * 4, static_cast<uint32_t>(0));
        for (int x = 0; x < EE_FM_PRESET_NAME_LENGTH; ++x) EEPROM.writeByte(EE_FM_PRESETS_NAME_START + memorypos * EE_FM_PRESET_NAME_LENGTH + x, '\0');
      } else {
        EEPROM.writeByte(memorypos + EE_PRESETS_FREQ_START, EE_PRESETS_FREQUENCY);
        EEPROM.put((memorypos * 8) + EE_PRESETS_SERVICEID_START, 0);
        for (int x = 0; x < 17; x++) EEPROM.writeByte((memorypos * 17) + x + EE_PRESETS_NAME_START, '\0');
      }
      memset(memory[memorypos].Label, 0, sizeof(memory[memorypos].Label));
      memory[memorypos].Channel = EE_PRESETS_FREQUENCY;
      memory[memorypos].ServiceID = 0;
      if (radioMode == RADIO_MODE_FM) LoadPresets();
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
  if (setvolume) closeVolume();
  tottimer = millis();
  rotary = 0;
  rotary2 = 0;
  if (SlideShowView) SlideShowButtonPress();
  if (!menu) {
    if (!ChannelListView) {
      if (ShowServiceInformation) BuildDisplay();
      switch (tunemode) {
        case TUNE_MAN:
          if (radioMode == RADIO_MODE_FM) {
            direction = true;
            seek = true;
            Serial.println("[FM/UI] rotary1 UP -> autotune/seek");
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
          radio.ServiceIndex = 0;
          radio.ServiceStart = false;
          if (radioMode == RADIO_MODE_DAB) radio.clearData();
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
      byte y = 0;
      byte y_old = 0;
      if (radio.ServiceIndex > 8 && radio.ServiceIndex < 17) {
        y_old = 9;
      } else if (radio.ServiceIndex > 16 && radio.ServiceIndex < 25) {
        y_old = 17;
      } else if (radio.ServiceIndex > 24) {
        y_old = 25;
      }

      ShowOneLine(20 * (radio.ServiceIndex - y_old), radio.ServiceIndex, false);

      if (radio.numberofservices > 0) DABSelectService(1);

      if (radio.ServiceIndex > 8 && radio.ServiceIndex < 17) {
        y = 9;
      } else if (radio.ServiceIndex > 16 && radio.ServiceIndex < 25) {
        y = 17;
      } else if (radio.ServiceIndex > 24) {
        y = 25;
      }

      if (y_old != y) {
        BuildChannelList();
      } else {
        ShowOneLine(20 * (radio.ServiceIndex - y), radio.ServiceIndex, true);
      }
    }
  } else {
    MenuUp();
  }
}

// Rotary 1 turn downwards. Mirror of KeyUp() — see that function for context map.
void KeyDown(void) {
  if (setvolume) closeVolume();
  tottimer = millis();
  rotary = 0;
  rotary2 = 0;
  if (SlideShowView) SlideShowButtonPress();
  if (!menu) {
    if (!ChannelListView) {
      if (ShowServiceInformation) BuildDisplay();
      switch (tunemode) {
        case TUNE_MAN:
          if (radioMode == RADIO_MODE_FM) {
            direction = false;
            seek = true;
            Serial.println("[FM/UI] rotary1 DOWN -> autotune/seek");
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
          radio.ServiceIndex = 0;
          radio.ServiceStart = false;
          if (radioMode == RADIO_MODE_DAB) radio.clearData();
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
      byte y = 0;
      byte y_old = 0;
      if (radio.ServiceIndex > 8 && radio.ServiceIndex < 17) {
        y_old = 9;
      } else if (radio.ServiceIndex > 16 && radio.ServiceIndex < 25) {
        y_old = 17;
      } else if (radio.ServiceIndex > 24) {
        y_old = 25;
      }

      ShowOneLine(20 * (radio.ServiceIndex - y_old), radio.ServiceIndex, false);

      if (radio.numberofservices > 0) DABSelectService(0);

      if (radio.ServiceIndex > 8 && radio.ServiceIndex < 17) {
        y = 9;
      } else if (radio.ServiceIndex > 16 && radio.ServiceIndex < 25) {
        y = 17;
      } else if (radio.ServiceIndex > 24) {
        y = 25;
      }

      if (y_old != y) {
        BuildChannelList();
      } else {
        ShowOneLine(20 * (radio.ServiceIndex - y), radio.ServiceIndex, true);
      }
    }
  } else {
    MenuDown();
  }
}

// Rotary 2 up:
// - DAB normal view (MAN/AUTO/MEM): select next service.
// - FM normal view: +100 kHz.
// - After pressing rotary 2: volume mode; rotation adjusts volume.
void KeyUp2(void) {
  tottimer = millis();

  if (setvolume) {
    if (volume < 62) volume += 2;
    ShowVolume();
  } else if (radioMode == RADIO_MODE_FM) {
    fmfreq = fmfreq >= FM_BAND_TOP_10KHZ
                ? FM_BAND_BOTTOM_10KHZ
                : static_cast<uint16_t>(fmfreq + FM_STEP_10KHZ);
    tuning = true;
    TuningTimer = millis();
    radio.ServiceIndex = 0;
    radio.ServiceStart = false;
    for (byte x = 0; x < 17; x++) _serviceName[x] = '\0';
    ShowFreq();
    Serial.printf("[FM/UI] rotary2 UP -> +100 kHz, %.1f MHz\n", fmfreq / 100.0f);
  } else if (SlideShowView || ShowServiceInformation || ChannelListView) {
    // Full-screen overlays do not assign an implicit rotary-2 action. Volume
    // remains available only after explicitly pressing the rotary-2 button.
  } else {
    if (radio.numberofservices > 0) DABSelectService(1);
    TuningTimer = millis();
  }
  rotary2 = 0;
}

// Rotary 2 down:
// - DAB normal view (MAN/AUTO/MEM): previous service.
// - FM normal view: -100 kHz.
// - After pressing rotary 2: volume mode; rotation adjusts volume.
void KeyDown2(void) {
  tottimer = millis();
  rotary = 0;
  rotary2 = 0;

  if (setvolume) {
    if (volume > 0) volume -= 2;
    ShowVolume();
  } else if (radioMode == RADIO_MODE_FM) {
    fmfreq = fmfreq <= FM_BAND_BOTTOM_10KHZ
                ? FM_BAND_TOP_10KHZ
                : static_cast<uint16_t>(fmfreq - FM_STEP_10KHZ);
    tuning = true;
    TuningTimer = millis();
    radio.ServiceIndex = 0;
    radio.ServiceStart = false;
    for (byte x = 0; x < 17; x++) _serviceName[x] = '\0';
    ShowFreq();
    Serial.printf("[FM/UI] rotary2 DOWN -> -100 kHz, %.1f MHz\n", fmfreq / 100.0f);
  } else if (SlideShowView || ShowServiceInformation || ChannelListView) {
    // See KeyUp2(): do not enter volume mode from rotation alone.
  } else {
    if (radio.numberofservices > 0) DABSelectService(0);
    TuningTimer = millis();
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
      fmfreq = FM_BAND_BOTTOM_10KHZ + static_cast<uint16_t>(memory[memorypos].Channel) * FM_STEP_10KHZ;
      _serviceID = 0;
    } else {
      dabfreq = memory[memorypos].Channel;
      _serviceID = memory[memorypos].ServiceID;
    }
    for (int i = 0; i < 16; i++) {
      _serviceName[i] = memory[memorypos].Label[i];
    }

    ShowFreq();
    radio.ServiceStart = false;
    TuningTimer = millis();
    tuning = true;
  }
}

// Whether the current preset slot has never been programmed (sentinel value).
bool IsStationEmpty(void) {
  if (memory[memorypos].Channel == EE_PRESETS_FREQUENCY) return true; else return false;
}

// Fade out the backlight, draw the standby splash, then go into ESP deep-sleep.
// Wake source is configured in this function (button / power switch on the board).
void doStandby(void) {
  FlushEeprom();
  tft.pushImage (0, 0, 320, 240, standbymode);
  tftPrint(0, myLanguage[language][78], 155, 210, ActiveColor, ActiveColorSmooth, 28);

  for (int x = ContrastSet; x > 0; x--) {
    analogWrite(CONTRASTPIN, x * 2);
    delay(25);
  }

  tft.writecommand(0x10);
  Headphones.Shutdown();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_34, LOW);
  esp_deep_sleep_start();
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
      EEPROM.put(EE_UINT16_FM_FREQUENCY, fmfreq);
      MarkEepromDirty();
      ShowFreq();
    }
    return;
  }
  if (!dabSeekStarted) {
    if (mode) {
      if (dabfreq >= 37) dabfreq = 0; else dabfreq++;
    } else {
      if (dabfreq == 0) dabfreq = 37; else dabfreq--;
    }
    radio.setFreq(dabfreq);
    radio.ServiceIndex = 0;
    radio.ServiceStart = false;
    ShowFreq();
    dabSeekStarted = true;
    return;
  }
  if (!radio.isTunePending()) {
    if (radio.signallock) seek = false;
    dabSeekStarted = false;
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

// Factory reset: write known-good defaults to every EEPROM slot, including
// emptying all preset entries. Also called automatically on first boot or
// when the schema version byte changes.
void DefaultSettings(void) {
  EEPROM.writeByte(EE_BYTE_CHECKBYTE, EE_CHECKBYTE_VALUE);
  EEPROM.writeByte(EE_BYTE_CONTRASTSET, 100);
  EEPROM.writeByte(EE_BYTE_LANGUAGE, 0);
  EEPROM.writeByte(EE_BYTE_DISPLAYFLIP, 0);
  EEPROM.writeByte(EE_BYTE_ROTARYMODE, 0);
  EEPROM.writeByte(EE_BYTE_TUNEMODE, 0);
  EEPROM.writeByte(EE_BYTE_UNIT, 0);
  EEPROM.writeByte(EE_BYTE_VOLUME, 62);
  EEPROM.writeByte(EE_BYTE_MEMORYPOS, 0);
  EEPROM.writeByte(EE_BYTE_AUTOSLIDESHOW, 0);
  EEPROM.writeByte(EE_BYTE_RADIO_MODE, RADIO_MODE_DAB);
  EEPROM.writeByte(EE_BYTE_TOT, 0);
  EEPROM.writeByte(EE_BYTE_THEME, 0);
  EEPROM.put(EE_UINT32_SERVICEID, (uint32_t)0);
  EEPROM.writeByte(EE_BYTE_DABFREQ, 0);
  EEPROM.put(EE_UINT16_FM_FREQUENCY, static_cast<uint16_t>(8750));
  for (int y = 0; y < 17; y++) {
    EEPROM.writeByte(EE_CHAR17_SERVICENAME + y, '\0');
  }

  for (int i = 0; i < EE_PRESETS_CNT; i++) {
    EEPROM.writeByte(i + EE_PRESETS_FREQ_START, EE_PRESETS_FREQUENCY);
    EEPROM.put((i * 8) + EE_PRESETS_SERVICEID_START, 0);
    for (int y = 0; y < 17; y++) {
      EEPROM.writeByte((i * 17) + y + EE_PRESETS_NAME_START, '\0');
    }
    EEPROM.writeByte(EE_FM_PRESETS_FREQ_START + i, EE_PRESETS_FREQUENCY);
    EEPROM.put(EE_FM_PRESETS_PI_START + i * 4, static_cast<uint32_t>(0));
    for (int y = 0; y < EE_FM_PRESET_NAME_LENGTH; ++y) EEPROM.writeByte(EE_FM_PRESETS_NAME_START + i * EE_FM_PRESET_NAME_LENGTH + y, '\0');
  }
  EEPROM.commit();
  eepromDirty = false;
}

void tftReplace(int8_t offset, const String & textold, const String & text, int16_t x, int16_t y, int color, int smoothcolor, int backcolor, uint8_t fontsize) {
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

  tft.setTextColor(backcolor, backcolor, false);

  switch (offset) {
    case -1: tft.setTextDatum(TL_DATUM); break;
    case 0: tft.setTextDatum(TC_DATUM); break;
    case 1: tft.setTextDatum(TR_DATUM); break;
  }

  tft.drawString(textold, x, y);
  tft.setTextColor(color, smoothcolor, false);

  switch (offset) {
    case -1: tft.setTextDatum(TL_DATUM); break;
    case 0: tft.setTextDatum(TC_DATUM); break;
    case 1: tft.setTextDatum(TR_DATUM); break;
  }

  String modifiedText = text;
  modifiedText.replace("\n", " ");

  tft.drawString(modifiedText, x, y);
}

void tftPrint(int8_t offset, const String & text, int16_t x, int16_t y, int color, int smoothcolor, uint8_t fontsize) {
  // ShowEN() clears only 16 px, but FONT16 descenders can extend below it.
  // Clear a slightly taller region for this exact status field.
  if (fontsize == 16 && offset == 0 && x == 238 && y == 162) {
    tft.fillRect(165, 159, 149, 23, BackgroundColor4);
  }

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

  String modifiedText = text;
  modifiedText.replace("\n", " ");

  tft.drawString(modifiedText, x, y, 1);
}

// Final-stage shutdown: detach interrupts, mute amp, blank screen, and call
// esp_deep_sleep_start(). Wakes only on a button-pin trigger.
void deepSleep(void) {
  StoreFrequency();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_34, LOW);
  esp_deep_sleep_start();
}

void loadFonts(bool option) {
  if (option) {
    OneBigLineSprite.loadFont(FONT28);
    ShortSprite.loadFont(FONT16);
    MediumSprite.loadFont(FONT16);
    LongSprite.loadFont(FONT16);
    FullLineSprite.loadFont(FONT16);
    ModeSprite.loadFont(FONT16);
  } else {
    OneBigLineSprite.unloadFont();
    ShortSprite.unloadFont();
    MediumSprite.unloadFont();
    LongSprite.unloadFont();
    FullLineSprite.unloadFont();
    ModeSprite.unloadFont();
  }
}
