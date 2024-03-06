#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <WiFiClient.h>
#include <EEPROM.h>
#include <Wire.h>
#include <ESP32Time.h>              // https://github.com/fbiego/ESP32Time
#include <TFT_eSPI.h>               // https://github.com/ohmytime/TFT_eSPI_DynamicSpeed (Modified version)
#include "src/WiFiConnect.h"
#include "src/WiFiConnectParam.h"
#include "src/FONT16.h"
#include "src/FONT28.h"
#include "src/FONT48DEC.h"
#include "src/FREQFONT.h"
#include "src/constants.h"
#include "src/language.h"
#include "src/gui.h"
#include "src/comms.h"
#include "src/si4684.h"
#include "src/TPA6130A2.h"
#include <JPEGDecoder.h>
#include <PNGdec.h>
#include "FS.h"
#include <LittleFS.h>

PNG png;
TPA6130A2 Headphones;
DAB radio;

File pngfile;
File jpgfile;

#define ROTARY_PIN_A    27
#define ROTARY_PIN_B    34
#define ROTARY_PIN_2A   33
#define ROTARY_PIN_2B   32
#define ROTARY_BUTTON   25
#define ROTARY_BUTTON2  35
#define PIN_POT         33
#define STANDBYBUTTON   36
#define SLBUTTON        26
#define MODEBUTTON      39
#define CONTRASTPIN     2

TFT_eSPI tft = TFT_eSPI(240, 320);

bool autoslideshow = true;
bool channellistview;
bool direction;
bool memorystore;
bool menu;
bool menuopen;
bool resetFontOnNextCall;
bool seek;
bool setupmode;
bool setvolume;
bool ShowServiceInformation;
bool SlideShowView;
bool store;
bool trysetservice;
bool tuned;
bool tuning;
bool wifi;
bool wificonnected;
byte charwidth = 8;
byte ContrastSet;
byte dabfreq;
byte dabfreqold;
byte displayflip;
byte language;
byte memorydabchannel[EE_PRESETS_CNT];
byte memorypos;
byte memoryposold;
byte memoryposstatus;
byte ptyold;
byte rotarymode;
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
int BarInsignificantColor;
int BarSignificantColor;
int Bitrateupdatetimer;
int FrameColor;
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
String EIDold;
String EnsembleNameOld;
String dabfreqStringOld;
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
unsigned long rssitimer;
unsigned long rtticker;
unsigned long rttickerhold;
unsigned long tuningtimer;

ESP32Time rtc(0);
TFT_eSprite RadiotextSprite = TFT_eSprite(&tft);
TFT_eSprite SignalSprite = TFT_eSprite(&tft);
TFT_eSprite VolumeSprite = TFT_eSprite(&tft);
WiFiConnect wc;
WiFiServer Server(7373);
WiFiClient RemoteClient;

void setup() {
  setupmode = true;
  LittleFS.begin();
  LittleFS.format();
  Headphones.Init();
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

  for (int i = 0; i < EE_PRESETS_CNT; i++) {
    memorydabchannel[i] = EEPROM.readByte(i + EE_PRESETS_FREQ_START);
    EEPROM.get((i * 8) + EE_PRESETS_SERVICEID_START, memorydabservice[i]);
    for (int y = 0; y < 16; y++) {
      memorydabname[i][y] = EEPROM.readByte((i * 17) + y + EE_PRESETS_NAME_START);
    }
    memorydabname[i][16] = '\0';
  }

  Headphones.SetHiZ(0);
  delay(100);
  Headphones.SetVolume(volume);

  tft.init();
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

  RadiotextSprite.createSprite(270, 19);
  RadiotextSprite.setTextDatum(TL_DATUM);
  RadiotextSprite.loadFont(FONT16);

  VolumeSprite.createSprite(230, 50);
  VolumeSprite.setTextDatum(TL_DATUM);
  VolumeSprite.setSwapBytes(true);

  SignalSprite.createSprite(20, 19);
  SignalSprite.setTextDatum(TR_DATUM);
  SignalSprite.loadFont(FONT16);

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

  tft.fillScreen(BackgroundColor);
  tft.pushImage (0, 0, 320, 240, SplashScreen);

  tftPrint(0, String(myLanguage[language][9]) + " " + String(VERSION), 160, 180, TFT_WHITE, TFT_DARKGREY, 16);

  for (int x = 0; x <= ContrastSet; x++) {
    analogWrite(CONTRASTPIN, x * 2 + 27);
    delay(30);
  }

  radio.begin(15);
  tftPrint(0, String(radio.getChipID()) + " v" + String(radio.getFirmwareVersion()), 160, 200, TFT_WHITE, TFT_DARKGREY, 16);

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
  if (_serviceID != 0 ) {
    trysetservice = true;
    radio.directtune = true;
  }

  BuildDisplay();
  setupmode = false;
}

void loop() {
  ProcessDAB();
  if (seek) Seek(direction);

  if (millis() >= tuningtimer + 500) {
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

  if (!menu) {
    if (rotary2 == -1) {
      if (setvolume || tunemode == TUNE_MEM) {
        if (tunemode == TUNE_MEM) setvolume = true;
        if (volume < 62) volume++;
        ShowVolume();
        rotary2 = 0;
      } else {
        if (radio.numberofservices > 0) DABSelectService(1);
        if (SlideShowView) SlideShowButtonPress();
        store = true;
        tuningtimer = millis();
      }
    }

    if (rotary2 == 1) {
      if (setvolume || tunemode == TUNE_MEM) {
        if (tunemode == TUNE_MEM) setvolume = true;
        if (volume > 0) volume--;
        ShowVolume();
        rotary2 = 0;
      } else {
        if (radio.numberofservices > 0) DABSelectService(0);
        if (SlideShowView) SlideShowButtonPress();
        store = true;
        tuningtimer = millis();
      }
    }
  }

  if (rotary == -1) {
    if (channellistview) {
      channellistview = false;
      BuildDisplay();
    }
    KeyUp();
  }

  if (rotary == 1) {
    if (channellistview) {
      channellistview = false;
      BuildDisplay();
    }
    KeyDown();
  }

  if (digitalRead(MODEBUTTON) == LOW) ModeButtonPress();
  if (!menu && digitalRead(SLBUTTON) == LOW) SlideShowButtonPress();
  if (!menu && digitalRead(STANDBYBUTTON) == LOW) StandbyButtonPress();

  if (digitalRead(ROTARY_BUTTON) == LOW) {
    if (!menu) {
      if (SlideShowView) BuildDisplay();
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
        if (channellistview) {
          channellistview = false;
          BuildDisplay();
        } else {
          channellistview = true;
          BuildChannelList();
        }
      }
    } else {
      DoMenu();
    }
    while (digitalRead(ROTARY_BUTTON) == LOW);
  }

  if (!menu && digitalRead(ROTARY_BUTTON2) == LOW) {
    if (setvolume) {
      setvolume = false;
      if (!channellistview) BuildDisplay(); else BuildChannelList();
    } else {
      setvolume = true;
      ShowVolume();
    }
    while (digitalRead(ROTARY_BUTTON2) == LOW);
  }
}

void ProcessDAB(void) {
  radio.Update();

  if (trysetservice && radio.signallock) {
    for (byte x; x < radio.numberofservices; x++) {
      if (_serviceID == radio.service[x].ServiceID) {
        radio.setService(x);
        radio.ServiceStart = true;
        trysetservice = false;
      }
    }
  }

  if (!SlideShowView && !menu) {
    if (!channellistview) {
      if (autoslideshow && radio.SlideShowAvailable && radio.SlideShowUpdate) SlideShowButtonPress();
      if (!ShowServiceInformation) {
        //        ShowRSSI();
        ShowBitrate();
        ShowEID();
        ShowSID();
        ShowPTY();
        ShowProtectionlevel();
        ShowPS();
        ShowEN();
      }
            ShowSignalLevel();
    }
    ShowRT();
  } else {
    if (radio.SlideShowAvailable && radio.SlideShowUpdate && !menu) {
      ShowSlideShow();
      radio.SlideShowUpdate = false;
    }
  }
}

void ShowPTY() {
  if (radio.pty != ptyold) {
    tftReplace(-1, myLanguage[language][37 + ptyold], myLanguage[language][37 + radio.pty], 36, 162, SecondaryColor, SecondaryColorSmooth, 16);
    ptyold = radio.pty;
  }
}

void ShowRT() {
  if (radio.ASCII(radio.ServiceData).length() > 0) {
    if (String(radio.ASCII(radio.ServiceData)).length() != RTlengthold) {
      RTWidth = (String(radio.ASCII(radio.ServiceData)).length() * charwidth) + 3 * charwidth;
      RTlengthold = String(radio.ASCII(radio.ServiceData)).length();
    }

    if (radio.ASCII(radio.ServiceData).length() < 29) {
      xPos = 0;
      RadiotextSprite.fillSprite(BackgroundColor);
      RadiotextSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
      RadiotextSprite.drawString(String(radio.ASCII(radio.ServiceData)), xPos, 2);
      RadiotextSprite.pushSprite(38, 219);
    } else {
      if (millis() - rtticker >= 20) {
        if (xPos == 0) {
          if (millis() - rttickerhold >= 1000) {
            xPos --;
            rttickerhold = millis();
          }
        } else {
          xPos --;
          rttickerhold = millis();
        }

        if (xPos < -RTWidth) xPos = 0;
        RadiotextSprite.fillSprite(BackgroundColor);
        RadiotextSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        RadiotextSprite.drawString(String(radio.ASCII(radio.ServiceData)), xPos, 2);
        RadiotextSprite.drawString(String(radio.ASCII(radio.ServiceData)), xPos + RTWidth, 2);
        RadiotextSprite.pushSprite(38, 219);
        rtticker = millis();
      }
    }
  } else {
    RadiotextSprite.fillSprite(BackgroundColor);
    RadiotextSprite.pushSprite(38, 219);
  }
  if (RTold != radio.ASCII(radio.ServiceData)) xPos = 0;
  RTold = radio.ASCII(radio.ServiceData);
}

void ShowSID() {
  if (String(radio.SID) != SIDold) {
    tftReplace(0, SIDold, String(radio.SID), 55, 120 , SecondaryColor, SecondaryColorSmooth, 16);
    SIDold = String(radio.SID);
  }
}

void ShowEID() {
  if (String(radio.EID) != EIDold) {
    tftReplace(0, EIDold, String(radio.EID), 55, 105 , SecondaryColor, SecondaryColorSmooth, 16);
    EIDold = String(radio.EID);
  }
}

void ShowPS() {
  if ((radio.signallock && radio.ServiceStart ? radio.ASCII(radio.service[radio.ServiceIndex].Label) : radio.ASCII(_serviceName)) != PSold) {
    if (tunemode != TUNE_MEM || (tunemode == TUNE_MEM && String((radio.signallock && radio.ServiceStart ? radio.ASCII(radio.service[radio.ServiceIndex].Label) : radio.ASCII(_serviceName))).length() != 0)) {
      tftReplace(0, PSold, String((radio.signallock && radio.ServiceStart ? radio.ASCII(radio.service[radio.ServiceIndex].Label) : radio.ASCII(_serviceName))), 165, 188, SecondaryColor, SecondaryColorSmooth, 28);
    }
    PSold = (radio.signallock && radio.ServiceStart ? radio.ASCII(radio.service[radio.ServiceIndex].Label) : radio.ASCII(_serviceName));
  }
}

void ShowEN() {
  if (EnsembleNameOld != radio.ASCII(radio.EnsembleLabel)) {
    tftReplace(0, EnsembleNameOld, (radio.signallock ? radio.ASCII(radio.EnsembleLabel) : ""), 262, 162, SecondaryColor, SecondaryColorSmooth, 16);
    EnsembleNameOld = radio.signallock ? radio.ASCII(radio.EnsembleLabel) : "";
  }
}

void ShowProtectionlevel() {
  if (String(ProtectionText[radio.protectionlevel]) != PLold) {
    tftReplace(0, PLold, String(ProtectionText[radio.protectionlevel]), 38, 90, PrimaryColor, PrimaryColorSmooth, 16);
    PLold = String(ProtectionText[radio.protectionlevel]);
  }
}

void ShowSlideShow(void) {
  if (radio.isJPG) {
    tft.fillScreen(TFT_BLACK);
    jpgfile = LittleFS.open("/slideshow.img", "rb");
    if (!jpgfile) {
      return;
    }
    bool decoded = JpegDec.decodeFsFile(jpgfile);
    if (decoded) {
      uint16_t *pImg;
      uint16_t mcu_w = JpegDec.MCUWidth;
      uint16_t mcu_h = JpegDec.MCUHeight;
      uint32_t max_x = JpegDec.width;
      uint32_t max_y = JpegDec.height;

      while (JpegDec.read()) {
        pImg = JpegDec.pImage;
        int mcu_x = JpegDec.MCUx * mcu_w;
        int mcu_y = JpegDec.MCUy * mcu_h;
        uint32_t win_w = mcu_w;
        uint32_t win_h = mcu_h;

        if (mcu_x + mcu_w > max_x) win_w = max_x - mcu_x;
        if (mcu_y + mcu_h > max_y) win_h = max_y - mcu_y;

        tft.startWrite();
        tft.setAddrWindow(mcu_x + (320 - JpegDec.width) / 2, mcu_y + (240 - JpegDec.height) / 2, win_w, win_h);

        for (int h = 0; h < win_h; h++) {
          for (int w = 0; w < win_w; w++) {
            tft.pushColor(*pImg++);
          }
        }
        tft.endWrite();
      }
    }
    jpgfile.close();
  } else if (radio.isPNG) {
    tft.fillScreen(TFT_BLACK);
    String filename = "/slideshow.img";
    pngfile = LittleFS.open(filename.c_str(), "rb");

    if (pngfile) {
      int16_t rc = png.open(filename.c_str(),
      [pngfile](const char *filename, int32_t *size) -> void * {
        pngfile = LittleFS.open(filename, "rb");
        *size = pngfile.size();
        return &pngfile;
      },
      [pngfile](void *handle) {
        File *file = static_cast<File *>(handle);
        if (file) file->close();
      },
      [pngfile](PNGFILE * page, uint8_t *buffer, int32_t length) -> int32_t {
        if (!pngfile || !pngfile.available()) return 0;
        return pngfile.read(buffer, length);
      },
      [pngfile](PNGFILE * page, int32_t position) -> int32_t {
        if (!pngfile || !pngfile.available()) return 0;
        return pngfile.seek(position);
      },
      [pngfile](PNGDRAW * pDraw) {
        uint16_t lineBuffer[320];
        png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
        tft.pushImage((320 - png.getWidth()) / 2, ((240 - png.getHeight()) / 2) + pDraw->y, pDraw->iWidth, 1, lineBuffer);
      });

      if (rc == PNG_SUCCESS) {
        tft.startWrite();
        rc = png.decode(nullptr, 0);
        png.close();
        tft.endWrite();
      }
    }
  }
}

void DABSelectService(bool dir) {
  if (radio.numberofservices > 0) {
    if (dir) {
      if (radio.ServiceIndex < (radio.numberofservices - 1)) {
        if (radio.ServiceStart) radio.ServiceIndex++;
      } else {
        radio.ServiceIndex = 0;
      }
    } else {
      if (radio.ServiceIndex > 0) {
        radio.ServiceIndex--;
      } else {
        radio.ServiceIndex = radio.numberofservices - 1;
      }
    }
    radio.setService(radio.ServiceIndex);
  }
  rotary2 = 0;
  radio.ServiceStart = true;
  radio.directtune = false;
  if (channellistview) BuildChannelList();
}

void StoreFrequency() {
  EEPROM.put(EE_UINT32_SERVICEID, radio.service[radio.ServiceIndex].ServiceID);
  EEPROM.put(EE_BYTE_DABFREQ, dabfreq);
  for (int i = 0; i < 16; i++) {
    EEPROM.writeByte(i + EE_CHAR17_SERVICENAME, radio.service[radio.ServiceIndex].Label[i]);
  }
  EEPROM.commit();
}

void ShowMemoryPos() {
  if (!IsStationEmpty()) {
    EEPROM.writeByte(EE_BYTE_MEMORYPOS, memorypos);
    EEPROM.commit();
  }
  int memposcolor = 0;
  int memposcolorsmooth = 0;
  switch (memoryposstatus) {
    case MEM_DARK:
      memposcolor = InsignificantColor;
      memposcolorsmooth = InsignificantColorSmooth;
      break;

    case MEM_NORMAL:
      memposcolor = PrimaryColor;
      memposcolorsmooth = PrimaryColorSmooth;
      break;

    case MEM_EXIST:
      memposcolor = SignificantColor;
      memposcolorsmooth = SignificantColorSmooth;
      break;
  }
  tftReplace(-1, String(memoryposold + 1), String(memorypos + 1), 90, 65, memposcolor, memposcolorsmooth, 16);
  memoryposold = memorypos;
}

void SlideShowButtonPress() {
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

void ModeButtonPress() {
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
    EEPROM.commit();
    menu = false;
    BuildDisplay();
  } else if (SlideShowView) {
    SlideShowView = false;
    BuildDisplay();
  } else {
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

void StandbyButtonPress() {
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
    if (!ShowServiceInformation) {
      ShowServiceInfo();
      ShowServiceInformation = true;
      SlideShowView = false;
    } else {
      BuildDisplay();
    }
  }
  while (digitalRead(STANDBYBUTTON) == LOW);
}

void KeyUp() {
  rotary = 0;
  rotary2 = 0;
  if (SlideShowView) SlideShowButtonPress();
  if (!menu) {
    switch (tunemode) {
      case TUNE_MAN:
        dabfreq++;
        if (dabfreq > 37) dabfreq = 0;
        tuning = true;
        tuningtimer = millis();
        radio.ServiceIndex = 0;
        radio.ServiceStart = false;
        radio.pty = 0;
        for (byte x; x < 17; x++) _serviceName[x] = '\0';
        ShowFreq();
        break;

      case TUNE_AUTO:
        for (byte x; x < 17; x++) _serviceName[x] = '\0';
        radio.ServiceIndex = 0;
        radio.ServiceStart = false;
        radio.pty = 0;
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
    MenuUp();
  }
}

void KeyDown() {
  rotary = 0;
  rotary2 = 0;
  if (SlideShowView) SlideShowButtonPress();
  if (!menu) {
    switch (tunemode) {
      case TUNE_MAN:
        dabfreq--;
        if (dabfreq > 37) dabfreq = 37;
        tuning = true;
        tuningtimer = millis();
        radio.ServiceIndex = 0;
        radio.ServiceStart = false;
        radio.pty = 0;
        for (byte x; x < 17; x++) _serviceName[x] = '\0';
        ShowFreq();
        break;

      case TUNE_AUTO:
        for (byte x; x < 17; x++) _serviceName[x] = '\0';
        radio.ServiceIndex = 0;
        radio.ServiceStart = false;
        radio.pty = 0;
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
    MenuDown();
  }
}

void DoMemoryPosTune() {
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
    tuningtimer = millis();
    tuning = true;
    radio.directtune = true;
  }
}

bool IsStationEmpty() {
  if (memorydabchannel[memorypos] == EE_PRESETS_FREQUENCY) {
    return true;
  }
  return false;
}

void ShowFreq() {
  detachInterrupt(digitalPinToInterrupt(ROTARY_PIN_A));
  detachInterrupt(digitalPinToInterrupt(ROTARY_PIN_B));
  tftReplace(0, radio.getChannel(dabfreqold), radio.getChannel(dabfreq), 142, 45, PrimaryColor, PrimaryColorSmooth, 28);
  tftReplace(-1, dabfreqStringOld, String(radio.getFreq(dabfreq) / 1000) + "." + (radio.getFreq(dabfreq) % 1000 < 100 ? "0" : "") + String(radio.getFreq(dabfreq) % 1000), 184, 43, SecondaryColor, SecondaryColorSmooth, 52);
  dabfreqStringOld = String(radio.getFreq(dabfreq) / 1000) + "." + (radio.getFreq(dabfreq) % 1000 < 100 ? "0" : "") + String(radio.getFreq(dabfreq) % 1000);
  dabfreqold = dabfreq;
  attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_A), read_encoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_B), read_encoder, CHANGE);
}

void ShowSignalLevel() {
  if (millis() >= rssitimer + 100) {
    rssitimer = millis();
    SignalLevel = radio.getRSSI();
    CNR = radio.cnr;
  }

  SAvg = (((SAvg * 9) + 5) / 10) + SignalLevel;
  SAvg2 = (((SAvg2 * 9) + 5) / 10) + CNR;

  float sval = 0;
  int16_t smeter = 0;
  int16_t segments;

  if (SignalLevel > 0) {
    if (SignalLevel < 1000) {
      sval = 51 * ((pow(10, (((float)SignalLevel) / 1000))) - 1);
      smeter = int16_t(sval);
    } else {
      smeter = 511;
    }
  }

  smeter = int16_t(sval);
  SignalLevel = SAvg / 10;
  CNR = SAvg2 / 10;

  int SignalLevelprint = 0;
  if (unit == 0) SignalLevelprint = SignalLevel;
  if (unit == 1) SignalLevelprint = ((SignalLevel * 100) + 10875) / 100;
  if (unit == 2) SignalLevelprint = round((float(SignalLevel) / 10.0 - 10.0 * log10(75) - 90.0) * 10.0);

  if (!ShowServiceInformation) {
    if (SignalLevelprint > (SignalLevelold + 3) || SignalLevelprint < (SignalLevelold - 3)) {

//      tftReplace(1, String(SignalLevelold / 10) + "." + String(abs(SignalLevelold % 10)), String(SignalLevelprint / 10) + "." + String(abs(SignalLevelprint % 10)), 188, 115, PrimaryColor, PrimaryColorSmooth, 16);

      SignalSprite.fillSprite(BackgroundColor);
      SignalSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
      SignalSprite.drawString(String(SignalLevelprint / 10) + "." + String(abs(SignalLevelprint % 10)), 1, 1);
      SignalSprite.pushSprite(188, 115);
      segments = (SignalLevel + 200) / 10;

//      tft.fillRect(16, 105, 2 * constrain(segments, 0, 54), 6, BarInsignificantColor);
//      tft.fillRect(16 + 2 * 54, 105, 2 * (constrain(segments, 54, 94) - 54), 6, BarSignificantColor);
//      tft.fillRect(16 + 2 * constrain(segments, 0, 94), 105, 2 * (94 - constrain(segments, 0, 94)), 6, GreyoutColor);
      SignalLevelold = SignalLevelprint;
    }
/*
    if (CNRold != CNR) {
      if (radio.signallock) {
        tftPrint(1, String("--"), 295, 163, BackgroundColor, BackgroundColor, 16);
        tftReplace(1, String(CNRold), String(CNR), 295, 163, PrimaryColor, PrimaryColorSmooth, 16);
      } else {
        tftReplace(1, String(CNRold), "--", 295, 163, PrimaryColor, PrimaryColorSmooth, 16);
      }
      CNRold = CNR;
    }
*/  } else {
    if (CNRold != CNR || SignalLevelprint != SignalLevelold) {
      String SignalLevelString = String(String(SignalLevelprint / 10)) + "." + String(abs(SignalLevelprint % 10)) + String(unitString[unit]) + " | MER: " + String(CNR) + "dB";
      if (SignalLevelString != SignalLeveloldString) {
        tftReplace(-1, SignalLeveloldString, SignalLevelString, 155, ITEM2 + 6, PrimaryColor, PrimaryColorSmooth, 16);
        SignalLeveloldString = SignalLevelString;
      }
    }
  }
}

void ShowVolume() {
  uint8_t segments = map(volume, 0, 63, 0, 94);
  VolumeSprite.drawRoundRect(0, 0, 230, 50, 5, ActiveColor);
  VolumeSprite.pushImage (4, 13, 24, 24, headphones);
  VolumeSprite.fillRect(36, 20, 2 * constrain(segments, 0, 74), 10, BarInsignificantColor);
  VolumeSprite.fillRect(36 + 2 * 74, 20, 2 * (constrain(segments, 74, 94) - 74), 10, BarSignificantColor);
  VolumeSprite.fillRect(36 + 2 * constrain(segments, 0, 94), 20, 2 * (94 - constrain(segments, 0, 94)), 10, GreyoutColor);
  VolumeSprite.pushSprite(46, 46);
  Headphones.SetVolume(volume);
  EEPROM.writeByte(EE_BYTE_VOLUME, volume);
  EEPROM.commit();
}


void ShowBitrate() {
  if (radio.bitrate != BitrateOld) {
    tftReplace(0, String (BitrateOld, DEC) + " kbit/s", String (radio.bitrate, DEC) + " kbit/s", 38, 140, PrimaryColor, PrimaryColorSmooth, 16);
    BitrateOld = radio.bitrate;
  }
}

void ShowTuneMode() {
  switch (tunemode) {
    case TUNE_MAN:
      tftPrint(0, "MAN", 24, 33, GreyoutColor, BackgroundColor, 16);
      tftPrint(0, "AUTO", 24, 49, SecondaryColor, SecondaryColorSmooth, 16);
      tftPrint(0, "MEM", 24, 65, SecondaryColor, SecondaryColorSmooth, 16);
      break;

    case TUNE_AUTO:
      tftPrint(0, "MAN", 24, 33, SecondaryColor, SecondaryColorSmooth, 16);
      tftPrint(0, "AUTO", 24, 49, GreyoutColor, BackgroundColor, 16);
      tftPrint(0, "MEM", 24, 65, SecondaryColor, SecondaryColorSmooth, 16);
      break;

    case TUNE_MEM:
      tftPrint(0, "MAN", 24, 33, SecondaryColor, SecondaryColorSmooth, 16);
      tftPrint(0, "AUTO", 24, 49, SecondaryColor, SecondaryColorSmooth, 16);

      if (memorystore) {
        tftPrint(0, "MEM", 24, 65, SignificantColor, SignificantColorSmooth, 16);
      } else {
        tftPrint(0, "MEM", 24, 65, GreyoutColor, BackgroundColor, 16);
      }
      break;
  }
  EEPROM.writeByte(EE_BYTE_TUNEMODE, tunemode);
  EEPROM.commit();
}

void ShowRSSI() {
  if (wifi) rssi = WiFi.RSSI(); else rssi = 0;
  if (rssiold != rssi) {
    rssiold = rssi;
    if (rssi == 0) {
      tft.drawBitmap(290, 4, WiFi4, 25, 25, GreyoutColor);
    } else if (rssi > -50 && rssi < 0) {
      tft.drawBitmap(290, 4, WiFi4, 25, 25, PrimaryColor);
    } else if (rssi > -60) {
      tft.drawBitmap(290, 4, WiFi4, 25, 25, GreyoutColor);
      tft.drawBitmap(290, 4, WiFi3, 25, 25, PrimaryColor);
    } else if (rssi > -70) {
      tft.drawBitmap(290, 4, WiFi4, 25, 25, GreyoutColor);
      tft.drawBitmap(290, 4, WiFi2, 25, 25, PrimaryColor);
    } else if (rssi < -70) {
      tft.drawBitmap(290, 4, WiFi4, 25, 25, GreyoutColor);
      tft.drawBitmap(290, 4, WiFi1, 25, 25, PrimaryColor);
    }
  }
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

void read_encoder() {
  static uint8_t old_AB = 3;
  static int8_t encval = 0;
  static const int8_t enc_states[]  = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

  old_AB <<= 2;

  if (digitalRead(ROTARY_PIN_A)) old_AB |= 0x02;
  if (digitalRead(ROTARY_PIN_B)) old_AB |= 0x01;
  encval += enc_states[( old_AB & 0x0f )];

  if (encval > 3) {
    if (rotarymode) rotary = -1; else rotary = 1;
    encval = 0;
  } else if (encval < -3) {
    if (rotarymode) rotary = 1; else rotary = -1;
    encval = 0;
  }
}

void read_encoder2() {
  static uint8_t old_AB = 3;
  static int8_t encval = 0;
  static const int8_t enc_states[]  = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

  old_AB <<= 2;

  if (digitalRead(ROTARY_PIN_2A)) old_AB |= 0x02;
  if (digitalRead(ROTARY_PIN_2B)) old_AB |= 0x01;
  encval += enc_states[( old_AB & 0x0f )];

  if (encval > 3) {
    if (rotarymode) rotary2 = -1; else rotary2 = 1;
    encval = 0;
  } else if (encval < -3) {
    if (rotarymode) rotary2 = 1; else rotary2 = -1;
    encval = 0;
  }
}

void DefaultSettings() {
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

void tftReplace(int8_t offset, const String & textold, const String & text, int16_t x, int16_t y, int color, int smoothcolor, uint8_t fontsize) {
  const uint8_t *selectedFont = nullptr;
  if (fontsize == 16) selectedFont = FONT16;
  if (fontsize == 28) selectedFont = FONT28;
  if (fontsize == 48) selectedFont = FONT48;
  if (fontsize == 52) selectedFont = FREQFONT;

  if (currentFont != selectedFont || resetFontOnNextCall) {
    if (currentFont != nullptr) tft.unloadFont();

    tft.loadFont(selectedFont);
    currentFont = selectedFont;
    resetFontOnNextCall = false;
  }

  tft.setTextColor(BackgroundColor, BackgroundColor, false);

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
  if (fontsize == 48) selectedFont = FONT48;
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

void deepSleep() {
  StoreFrequency();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_34, LOW);
  esp_deep_sleep_start();
}
