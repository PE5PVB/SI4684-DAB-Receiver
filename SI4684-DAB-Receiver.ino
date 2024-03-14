#include <TFT_eSPI.h>
#include <LittleFS.h>
#include <TimeLib.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <WiFiClient.h>
#include <EEPROM.h>
#include <Wire.h>
#include "src/WiFiConnect.h"
#include "src/WiFiConnectParam.h"
#include "src/font.h"
#include "src/constants.h"
#include "src/graphics.h"
#include "src/language.h"
#include "src/gui.h"
#include "src/comms.h"
#include "src/slideshow.h"
#include "src/si4684.h"
#include "src/TPA6130A2.h"

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
bool setupmode;
bool setvolume;
bool ShowServiceInformation;
bool SlideShowAvailableOld;
bool SlideShowView;
bool store;
bool trysetservice;
bool tuned;
bool tuning;
bool wifi;
bool wificonnected;
byte audiomodeold;
byte charwidth = 8;
byte ContrastSet;
byte CurrentTheme;
byte dabfreq;
byte dabfreqold;
byte displayflip;
byte eccold;
byte ficold;
byte language;
byte memorydabchannel[EE_PRESETS_CNT];
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
char _serviceName[17];
char memorydabname[EE_PRESETS_CNT][17];
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
int rotary;
int rotary2;
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
IPAddress remoteip;
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
uint32_t memorydabservice[EE_PRESETS_CNT];
uint8_t freq = 0;
uint8_t service = 0;
unsigned long tottimer;
unsigned long rssiTimer;
unsigned long rtticker;
unsigned long rttickerhold;
unsigned long TuningTimer;
unsigned long VolumeTimer;

static const int8_t enc_states[]  = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

TFT_eSprite FullLineSprite = TFT_eSprite(&tft);
TFT_eSprite VolumeSprite = TFT_eSprite(&tft);
TFT_eSprite OneBigLineSprite = TFT_eSprite(&tft);
TFT_eSprite LongSprite = TFT_eSprite(&tft);
TFT_eSprite MediumSprite = TFT_eSprite(&tft);
TFT_eSprite ModeSprite = TFT_eSprite(&tft);
TFT_eSprite ShortSprite = TFT_eSprite(&tft);

WiFiConnect wc;
WiFiServer Server(7373);
WiFiClient RemoteClient;

void setup(void) {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  gpio_set_drive_capability((gpio_num_t) 4, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 5, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 13, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 14, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 15, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 16, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 17, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 21, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t) 22, GPIO_DRIVE_CAP_0);
  setupmode = true;
  LittleFS.begin();
  LittleFS.format();

  Serial.begin(115200);

  EEPROM.begin(EE_TOTAL_CNT);
  if (EEPROM.readByte(EE_BYTE_CHECKBYTE) != EE_CHECKBYTE_VALUE) DefaultSettings();
  ContrastSet = EEPROM.readByte(EE_BYTE_CONTRASTSET);
  language = EEPROM.readByte(EE_BYTE_LANGUAGE);
  displayflip = EEPROM.readByte(EE_BYTE_DISPLAYFLIP);
  rotarymode = EEPROM.readByte(EE_BYTE_ROTARYMODE);
  tunemode = EEPROM.readByte(EE_BYTE_TUNEMODE);
  wifi = EEPROM.readByte(EE_BYTE_WIFI);
  unit = EEPROM.readByte(EE_BYTE_UNIT);
  dabfreq = EEPROM.readByte(EE_BYTE_DABFREQ);
  volume = EEPROM.readByte(EE_BYTE_VOLUME);
  memorypos = EEPROM.readByte(EE_BYTE_MEMORYPOS);
  autoslideshow = EEPROM.readByte(EE_BYTE_AUTOSLIDESHOW);
  tot = EEPROM.readByte(EE_BYTE_TOT);
  wifi = EEPROM.readByte(EE_BYTE_WIFI);
  CurrentTheme = EEPROM.readByte(EE_BYTE_THEME);

  for (int i = 0; i < EE_PRESETS_CNT; i++) {
    memorydabchannel[i] = EEPROM.readByte(i + EE_PRESETS_FREQ_START);
    EEPROM.get((i * 8) + EE_PRESETS_SERVICEID_START, memorydabservice[i]);
    for (int y = 0; y < 16; y++) {
      memorydabname[i][y] = EEPROM.readByte((i * 17) + y + EE_PRESETS_NAME_START);
    }
    memorydabname[i][16] = '\0';
  }
  Headphones.Init();
  delay(50);
  Headphones.SetHiZ(0);
  delay(50);
  Headphones.SetVolume(volume);

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
  OneBigLineSprite.loadFont(FONT28);
  OneBigLineSprite.setSwapBytes(true);

  ShortSprite.createSprite(36, 16);
  ShortSprite.loadFont(FONT16);
  ShortSprite.setSwapBytes(true);

  MediumSprite.createSprite(70, 16);
  MediumSprite.loadFont(FONT16);
  MediumSprite.setSwapBytes(true);

  LongSprite.createSprite(150, 17);
  LongSprite.loadFont(FONT16);
  LongSprite.setSwapBytes(true);

  FullLineSprite.createSprite(308, 20);
  FullLineSprite.loadFont(FONT16);
  FullLineSprite.setSwapBytes(true);

  VolumeSprite.createSprite(240, 50);
  VolumeSprite.setTextDatum(TC_DATUM);
  VolumeSprite.loadFont(FONT28);
  VolumeSprite.setSwapBytes(true);

  ModeSprite.createSprite(46, 47);
  ModeSprite.setTextDatum(TC_DATUM);
  ModeSprite.loadFont(FONT16);
  ModeSprite.setSwapBytes(true);

  if (digitalRead(SLBUTTON) == LOW && digitalRead(ROTARY_BUTTON) == HIGH) {
    if (rotarymode == 0) rotarymode = 1; else rotarymode = 0;
    EEPROM.writeByte(EE_BYTE_ROTARYMODE, rotarymode);
    EEPROM.commit();

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
    EEPROM.commit();

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

  if (radio.begin(15)) {
    tftPrint(0, String(radio.getChipID()) + " v" + String(radio.getFirmwareVersion()), 160, 210, TFT_WHITE, TFT_DARKGREY, 16);
  } else {
    tftPrint(0, myLanguage[language][77], 160, 210, TFT_WHITE, TFT_DARKGREY, 16);
    for (;;);
  }

  delay(1500);

  if (wifi) {
    tryWiFi();
    delay(2000);
  } else {
    Server.end();
  }

  if (tunemode == TUNE_MEM && !IsStationEmpty()) {
    DoMemoryPosTune();
  } else {
    if (tunemode == TUNE_MEM) tunemode = TUNE_MAN;
    radio.setFreq(dabfreq);
    EEPROM.get(EE_UINT32_SERVICEID, _serviceID);
    for (int i = 0; i < 16; i++) {
      _serviceName[i] = EEPROM.readByte(i + EE_CHAR17_SERVICENAME);
    }
  }
  if (_serviceID != 0 ) trysetservice = true;

  BuildDisplay();
  setupmode = false;
  tottimer = millis();
}

void loop(void) {
  ProcessDAB();
  Communication();
  displayreset = false;

  if (seek) Seek(direction);

  if (tot != 0) {
    unsigned long totprobe = tot * 60000;
    if (millis() >= tottimer + totprobe) deepSleep();
  }

  if (millis() >= TuningTimer + 500) {
    if (store) {
      detachInterrupt(digitalPinToInterrupt(ROTARY_PIN_A));
      detachInterrupt(digitalPinToInterrupt(ROTARY_PIN_B));
      StoreFrequency();
      store = false;
      attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_A), read_encoder, CHANGE);
      attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_B), read_encoder, CHANGE);
    } else if (tuning) {
      radio.setFreq(dabfreq);
      tuning = false;
      if (tunemode == TUNE_MEM) trysetservice = true;
    }
  }

  if (millis() >= VolumeTimer + 3000 && setvolume) {
    closeVolume();
  }

  if (!menu) {
    if (rotary2 == -1) KeyUp2();
    if (rotary2 == 1) KeyDown2();
  }

  if (rotary == -1) KeyUp();
  if (rotary == 1) KeyDown();

  if (digitalRead(MODEBUTTON) == LOW) ModeButtonPress();
  if (!menu && digitalRead(SLBUTTON) == LOW) SlideShowButtonPress();
  if (!menu && digitalRead(STANDBYBUTTON) == LOW) StandbyButtonPress();

  if (digitalRead(ROTARY_BUTTON) == LOW) ButtonPress();

  if (!menu && digitalRead(ROTARY_BUTTON2) == LOW) Button2Press();
}

void ProcessDAB(void) {
  if (!tuning) {
    radio.Update();
    SignalLevel = radio.getRSSI();
  }

  if (radio.panic()) doRecovery();

  if (trysetservice && radio.signallock) {
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
    if (!ShowServiceInformation && !ChannelListView) {
      if (autoslideshow && radio.SlideShowAvailable && radio.SlideShowUpdate) SlideShowButtonPress();
      ShowRSSI();
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
    if (!ChannelListView) ShowSignalLevel();
    ShowRT();
  } else {
    if (radio.SlideShowAvailable && radio.SlideShowUpdate && !menu) {
      ShowSlideShow();
      radio.SlideShowUpdate = false;
    }
  }
}

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

void doRecovery(void) {
  radio.begin(15);
  radio.setFreq(dabfreq);
  trysetservice = true;
}

void DABSelectService(bool dir) {
  if (radio.numberofservices > 0) {
    if (dir) {
      radio.ServiceIndex = (radio.ServiceIndex + 1) % radio.numberofservices;
    } else {
      radio.ServiceIndex = (radio.ServiceIndex == 0) ? (radio.numberofservices - 1) : (radio.ServiceIndex - 1);
    }

    while (radio.service[radio.ServiceIndex].ServiceType != 0x00 && radio.service[radio.ServiceIndex].ServiceType != 0x04 && radio.service[radio.ServiceIndex].ServiceType != 0x05) {
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

void StoreFrequency(void) {
  EEPROM.put(EE_UINT32_SERVICEID, radio.service[radio.ServiceIndex].ServiceID);
  EEPROM.put(EE_BYTE_DABFREQ, dabfreq);
  for (int i = 0; i < 16; i++) {
    EEPROM.writeByte(i + EE_CHAR17_SERVICENAME, radio.PStext[i]);
  }
  EEPROM.commit();
  _serviceID = radio.service[radio.ServiceIndex].ServiceID;
}

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
        EEPROM.writeByte(memorypos + EE_PRESETS_FREQ_START, dabfreq);
        EEPROM.put((memorypos * 8) + EE_PRESETS_SERVICEID_START, radio.service[radio.ServiceIndex].ServiceID);
        for (int x = 0; x < 16; x++) {
          char character = radio.service[radio.ServiceIndex].Label[x];
          EEPROM.writeByte((memorypos * 17) + x + EE_PRESETS_NAME_START, character);
          memorydabname[memorypos][x] = character;
        }
        EEPROM.writeByte((memorypos * 17) + 16 + EE_PRESETS_NAME_START, '\0');
        memorydabname[memorypos][16] = '\0';
        memorydabchannel[memorypos] = dabfreq;
        memorydabservice[memorypos] = radio.service[radio.ServiceIndex].ServiceID;
        EEPROM.commit();

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
  while (digitalRead(ROTARY_BUTTON) == LOW);
}

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
  while (digitalRead(ROTARY_BUTTON2) == LOW);
}

void SlideShowButtonPress(void) {
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
  while (digitalRead(SLBUTTON) == LOW);
}

void ModeButtonPress(void) {
  tottimer = millis();
  seek = false;
  unsigned long counterold = millis();
  unsigned long counter = millis();
  while (digitalRead(MODEBUTTON) == LOW && counter - counterold <= 1000) counter = millis();

  if (menu) {
    EEPROM.writeByte(EE_BYTE_LANGUAGE, language);
    EEPROM.writeByte(EE_BYTE_CONTRASTSET, ContrastSet);
    EEPROM.writeByte(EE_BYTE_AUTOSLIDESHOW, autoslideshow);
    EEPROM.writeByte(EE_BYTE_UNIT, unit);
    EEPROM.writeByte(EE_BYTE_TOT, tot);
    EEPROM.writeByte(EE_BYTE_WIFI, wifi);
    EEPROM.writeByte(EE_BYTE_THEME, CurrentTheme);
    EEPROM.commit();
    menu = false;
    BuildDisplay();
  } else if (SlideShowView || ChannelListView || ShowServiceInformation) {
    BuildDisplay();
  } else {
    memorystore = false;
    memoryposstatus = MEM_NORMAL;

    if (counter - counterold <= 1000) {
      tunemode++;
      if (tunemode > 2) tunemode = 0;
      ShowTuneMode();
      ShowMemoryPos();
    } else {
      menu = true;
      BuildMenu();

    }
  }
  while (digitalRead(MODEBUTTON) == LOW);
}

void StandbyButtonPress(void) {
  tottimer = millis();
  unsigned long counterold = millis();
  unsigned long counter = millis();
  if (memorystore) {
    EEPROM.writeByte(memorypos + EE_PRESETS_FREQ_START, EE_PRESETS_FREQUENCY);
    EEPROM.put((memorypos * 8) + EE_PRESETS_SERVICEID_START, 0);
    for (int x = 0; x < 16; x++) {
      EEPROM.writeByte((memorypos * 17) + x + EE_PRESETS_NAME_START, '\0');
      memorydabname[memorypos][x] = '\0';
    }
    EEPROM.writeByte((memorypos * 17) + 16 + EE_PRESETS_NAME_START, '\0');
    memorydabname[memorypos][16] = '\0';
    memorydabchannel[memorypos] = EE_PRESETS_FREQUENCY;
    memorydabservice[memorypos] = 0;
    EEPROM.commit();
    memorystore = false;
    ShowTuneMode();
    if (memoryposstatus == MEM_DARK || memoryposstatus == MEM_EXIST) {
      memoryposstatus = MEM_NORMAL;
      ShowMemoryPos();
    }
  } else {
    while (digitalRead(STANDBYBUTTON) == LOW && counter - counterold <= 1000) counter = millis();
    if (counter - counterold <= 1000) {
      if (!ShowServiceInformation && !SlideShowView && !ChannelListView) {
        ShowServiceInfo();
        ShowServiceInformation = true;
      } else {
        BuildDisplay();
      }
    } else {
      doStandby();
    }
  }
  while (digitalRead(STANDBYBUTTON) == LOW);
}

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
          dabfreq++;
          if (dabfreq > 37) dabfreq = 0;
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
          radio.clearData();
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
          dabfreq--;
          if (dabfreq > 37) dabfreq = 37;
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
          radio.clearData();
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

void KeyUp2(void) {
  tottimer = millis();
  if (setvolume || tunemode == TUNE_MEM || SlideShowView || ShowServiceInformation || ChannelListView) {
    setvolume = true;
    if (volume < 62) volume += 2;
    ShowVolume();
  } else {
    if (radio.numberofservices > 0) DABSelectService(1);
    TuningTimer = millis();
  }
  rotary2 = 0;
}

void KeyDown2(void) {
  tottimer = millis();
  rotary = 0;
  rotary2 = 0;
  if (setvolume || tunemode == TUNE_MEM || SlideShowView || ShowServiceInformation || ChannelListView) {
    setvolume = true;
    if (volume > 0) volume -= 2;
    ShowVolume();
  } else {
    if (radio.numberofservices > 0) DABSelectService(0);
    TuningTimer = millis();
  }
}

void DoMemoryPosTune(void) {
  if (IsStationEmpty()) {
    memoryposstatus = MEM_DARK;
  } else {
    memoryposstatus = MEM_NORMAL;
    dabfreq = memorydabchannel[memorypos];
    _serviceID = memorydabservice[memorypos];
    for (int i = 0; i < 16; i++) {
      _serviceName[i] = memorydabname[memorypos][i];
    }

    ShowFreq();
    radio.ServiceStart = false;
    TuningTimer = millis();
    tuning = true;
  }
}

bool IsStationEmpty(void) {
  if (memorydabchannel[memorypos] == EE_PRESETS_FREQUENCY) return true; else return false;
}

void doStandby(void) {
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

void Seek(bool mode) {
  if (mode) {
    dabfreq++;
    if (dabfreq > 37) dabfreq = 0;
  } else {
    dabfreq--;
    if (dabfreq > 37) dabfreq = 37;
  }
  radio.setFreq(dabfreq);
  radio.ServiceIndex = 0;
  radio.ServiceStart = false;
  ShowFreq();
  radio.Update();
  if (radio.signallock) seek = false;
}

void read_encoder(void) {
  static uint8_t old_AB = 3;
  static int8_t encval = 0;

  old_AB <<= 2;
  if (digitalRead(ROTARY_PIN_A)) old_AB |= 0x02;
  if (digitalRead(ROTARY_PIN_B)) old_AB |= 0x01;
  encval += enc_states[( old_AB & 0x0f )];

  if (encval > 2) {
    if (rotarymode) rotary = -1; else rotary = 1;
    encval = 0;
  } else if (encval < -2) {
    if (rotarymode) rotary = 1; else rotary = -1;
    encval = 0;
  }
}

void read_encoder2(void) {
  static uint8_t old_AB = 3;
  static int8_t encval = 0;

  old_AB <<= 2;
  if (digitalRead(ROTARY_PIN_2A)) old_AB |= 0x02;
  if (digitalRead(ROTARY_PIN_2B)) old_AB |= 0x01;
  encval += enc_states[( old_AB & 0x0f )];

  if (encval > 2) {
    if (rotarymode) rotary2 = -1; else rotary2 = 1;
    encval = 0;
  } else if (encval < -2) {
    if (rotarymode) rotary2 = 1; else rotary2 = -1;
    encval = 0;
  }
}

void DefaultSettings(void) {
  EEPROM.writeByte(EE_BYTE_CHECKBYTE, EE_CHECKBYTE_VALUE);
  EEPROM.writeByte(EE_BYTE_CONTRASTSET, 100);
  EEPROM.writeByte(EE_BYTE_LANGUAGE, 0);
  EEPROM.writeByte(EE_BYTE_DISPLAYFLIP, 0);
  EEPROM.writeByte(EE_BYTE_ROTARYMODE, 0);
  EEPROM.writeByte(EE_BYTE_TUNEMODE, 0);
  EEPROM.writeByte(EE_BYTE_WIFI, 0);
  EEPROM.writeByte(EE_BYTE_UNIT, 0);
  EEPROM.writeByte(EE_BYTE_VOLUME, 40);
  EEPROM.writeByte(EE_BYTE_MEMORYPOS, 0);
  EEPROM.writeByte(EE_BYTE_AUTOSLIDESHOW, 0);
  EEPROM.writeByte(EE_BYTE_TOT, 0);
  EEPROM.writeByte(EE_BYTE_THEME, 0);
  EEPROM.put(EE_UINT32_SERVICEID, 0);
  EEPROM.put(EE_BYTE_DABFREQ, 0);
  for (int y = 0; y < 17; y++) {
    EEPROM.writeByte(EE_CHAR17_SERVICENAME + y, '\0');
  }

  for (int i = 0; i < EE_PRESETS_CNT; i++) {
    EEPROM.writeByte(i + EE_PRESETS_FREQ_START, EE_PRESETS_FREQUENCY);
    EEPROM.put((i * 8) + EE_PRESETS_SERVICEID_START, 0);
    for (int y = 0; y < 17; y++) {
      EEPROM.writeByte((i * 17) + y + EE_PRESETS_NAME_START, '\0');
    }
  }
  EEPROM.commit();
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

void deepSleep(void) {
  StoreFrequency();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_34, LOW);
  esp_deep_sleep_start();
}
