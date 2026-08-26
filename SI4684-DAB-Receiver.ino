// SI4684 DAB Receiver - main Arduino sketch.
//
// Wires together every subsystem:
//   * SI4684 radio chip (SPI) - tuning, service data, MOT slideshow
//   * TFT_eSPI display (SPI) - all UI
//   * TPA6130A2 headphone amp (I2C) - audio output
//   * Dual rotary encoders + four buttons + IR remote - user input
//   * RAM-only MOT slideshow buffer
//   * EEPROM - settings and presets
//
// setup() initialises everything (in a specific order: brownout off, Serial,
// GPIO drive strength, EEPROM, headphone amp, TFT, radio).
// loop() is the cooperative scheduler: it calls IRReceiver(), ProcessDAB(),
// Communication(), then handles timers and polls all buttons/encoders.

#include <TFT_eSPI.h>
#include <TimeLib.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
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
void MarkEepromDirty(void);
void FlushEeprom(void);

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

bool SwitchRadioMode(RadioMode newMode, bool force) {
  if (!force && newMode == radioMode) return true;
  const bool modeChanged = newMode != radioMode;
  Headphones.SetMute(true);

  // GPIO17 is shared by SI4684 RSTB and TFT RESET on the existing PCB.
  pinMode(17, OUTPUT);
  digitalWrite(17, LOW);
  delay(10);
  digitalWrite(17, HIGH);
  delay(120);

  // tft.init() also pulses TFT_RST (the same GPIO17), so it must run before
  // the tuner image is uploaded. Its final release leaves SI4684 in bootloader.
  tft.init();
  tft.initDMA();
  tft.setSwapBytes(true);
  tft.setRotation(displayflip == 0 ? 3 : 1);
  loadFonts(true);

  if (!radio.begin(15, newMode)) {
    tft.fillScreen(BackgroundColor);
    tftPrint(0, myLanguage[language][77], 160, 110, ActiveColor, ActiveColorSmooth, 28);
    return false;
  }
  radioMode = newMode;
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
  // Disable brownout detector: occasional brief dips during SPI bursts otherwise
  // trigger a watchdog reset on this board.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(1000000);
  delay(200);  // give USB-serial host time to re-attach after boot reset

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

  // EEPROM check byte acts as a schema version: when its value doesn't match
  // EE_CHECKBYTE_VALUE the previous layout is incompatible (or this is a fresh
  // device), so we wipe everything to defaults.
  EEPROM.begin(EE_TOTAL_CNT);
  const uint8_t storedSchema = EEPROM.readByte(EE_BYTE_CHECKBYTE);
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
  LoadPresets();

  Headphones.Init();
  delay(50);
  Headphones.SetHiZ(0);
  delay(50);
  Headphones.SetVolume(volume);
  Headphones.SetMute(true);

  tft.init();
  tft.initDMA();
  doTheme();
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

  loadFonts(true);

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

  tft.pushImage (0, 0, 320, 240, SplashScreen);
  tftPrint(0, myLanguage[language][72], 155, 15, ActiveColor, ActiveColorSmooth, 28);

  tftPrint(0, String(myLanguage[language][9]) + " " + String(VERSION), 160, 190, TFT_WHITE, TFT_DARKGREY, 16);

  for (int x = 0; x <= ContrastSet; x++) {
    analogWrite(CONTRASTPIN, x * 2 + 27);
    delay(30);
  }

  if (radio.begin(15, radioMode)) {
    tftPrint(0, String(radio.getChipID()) + " v" + String(radio.getFirmwareVersion()), 160, 210, TFT_WHITE, TFT_DARKGREY, 16);
  } else {
    tftPrint(0, myLanguage[language][77], 160, 210, TFT_WHITE, TFT_DARKGREY, 16);
    for (;;);
  }
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

  BuildDisplay();
  tottimer = millis();
  IRReceiverBegin();
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

  // panic() reports if the chip got into a hung state; doRecovery() reboots it.
  static unsigned long panicTimer = 0;
  if (!menu && !radio.isTunePending() && millis() - panicTimer >= 2000UL) {
    panicTimer = millis();
    if (radio.panic()) doRecovery();
  }

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
        ShowECC();
      }
    }
  } else {
    if (radio.SlideShowAvailable && radio.SlideShowUpdate && !menu) {
      ShowSlideShow();
      radio.SlideShowUpdate = false;
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
        ShowTuneMode();
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

        ShowTuneMode();
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

// SL button: enter the full-screen slideshow view if a slideshow is currently
// available; otherwise toggle back to the normal radio display.
// Also called from ProcessDAB() when autoslideshow is enabled and from KeyUp()
// to leave slideshow view via the rotary.
void SlideShowButtonPress(void) {
  if (radioMode == RADIO_MODE_FM) return;
  setvolume = false;
  memorystore = false;
  memoryposstatus = MEM_NORMAL;
  if (!SlideShowView && radio.SlideShowAvailable) {
    tft.fillScreen(TFT_BLACK);
    radio.SlideShowUpdate = true;
    SlideShowView = true;
    ShowServiceInformation = false;
  } else {
    if (SlideShowView) BuildDisplay();
    SlideShowView = false;
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
        tunemode++;
        if (tunemode > 2) tunemode = 0;
        ShowTuneMode();
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
      ShowTuneMode();
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
            fmfreq = fmfreq >= FM_BAND_TOP_10KHZ ? FM_BAND_BOTTOM_10KHZ : fmfreq + FM_STEP_10KHZ;
          } else if (dabfreq >= 37) dabfreq = 0; else dabfreq++;
          tuning = true;
          TuningTimer = millis();
          radio.ServiceIndex = 0;
          radio.ServiceStart = false;
          if (radioMode == RADIO_MODE_DAB) radio.clearData();
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
            fmfreq = fmfreq <= FM_BAND_BOTTOM_10KHZ ? FM_BAND_TOP_10KHZ : fmfreq - FM_STEP_10KHZ;
          } else if (dabfreq == 0) dabfreq = 37; else dabfreq--;
          tuning = true;
          TuningTimer = millis();
          radio.ServiceIndex = 0;
          radio.ServiceStart = false;
          if (radioMode == RADIO_MODE_DAB) radio.clearData();
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

// Rotary 2 up: by default selects the next service in the current ensemble;
// in any overlay/list view it becomes the volume control instead.
void KeyUp2(void) {
  tottimer = millis();
  if (radioMode == RADIO_MODE_FM || setvolume || tunemode == TUNE_MEM || SlideShowView || ShowServiceInformation || ChannelListView) {
    setvolume = true;
    if (volume < 62) volume += 2;
    ShowVolume();
  } else {
    if (radio.numberofservices > 0) DABSelectService(1);
    TuningTimer = millis();
  }
  rotary2 = 0;
}

// Rotary 2 down: mirror of KeyUp2 — previous service / volume down.
void KeyDown2(void) {
  tottimer = millis();
  rotary = 0;
  rotary2 = 0;
  if (radioMode == RADIO_MODE_FM || setvolume || tunemode == TUNE_MEM || SlideShowView || ShowServiceInformation || ChannelListView) {
    setvolume = true;
    if (volume > 0) volume -= 2;
    ShowVolume();
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
