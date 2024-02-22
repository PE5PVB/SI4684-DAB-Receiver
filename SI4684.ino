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
#include <JPEGDecoder.h>
#include <PNGdec.h>
#include "FS.h"
#include <LittleFS.h>

PNG png;
File pngfile;
File jpgfile;

#define ROTARY_PIN_A    27
#define ROTARY_PIN_B    34
#define ROTARY_PIN_2A   33
#define ROTARY_PIN_2B   32
#define ROTARY_BUTTON   25
#define PIN_POT         33
#define BANDBUTTON      36
#define SLBUTTON        26
#define MODEBUTTON      39
#define CONTRASTPIN     2
#define DABRESET        12

TFT_eSPI tft = TFT_eSPI(240, 320);

uint8_t service = 0;
uint8_t freq = 0;
bool direction;
bool memorystore;
bool setupmode;
bool menu;
bool tuning;
bool seek;
bool resetFontOnNextCall;
byte dabfreq;
byte dabfreqold;
bool SlideShowView;
bool store;
bool tuned;
bool wifi;
bool wificonnected;
byte ContrastSet;
byte displayflip;
byte language;
byte memorypos;
byte rotarymode;
byte subnetclient;
byte tunemode;
byte unit;
int BackgroundColor;
const uint8_t* currentFont = nullptr;
int ActiveColor;
int ActiveColorSmooth;
int BarSignificantColor;
int BarInsignificantColor;
int charWidth = tft.textWidth("AA");
uint16_t BitrateOld;
int Bitrateupdatetimer;
int FrameColor;
int GreyoutColor;
int InsignificantColor;
int InsignificantColorSmooth;
int menuoption = ITEM1;
int PrimaryColor;
int PrimaryColorSmooth;
int SignificantColor;
int SignificantColorSmooth;

int rotary;
int rotary2;
int rssi;
int rssiold = 200;
int SecondaryColor;
int SecondaryColorSmooth;

int SNRupdatetimer;
int xPos;
int SignalLevelold;
int16_t SAvg;
int16_t SAvg2;
int16_t SignalLevel;
int8_t CNR;
int8_t CNRold;
IPAddress remoteip;
String AIDString;
String EIDold;
String PSold;
String RTold;
String PLold;
String SIDold;
uint32_t _serviceID;
uint32_t _componentID;
unsigned long tuningtimer;
unsigned long rtticker;
unsigned long rttickerhold;
unsigned long rssitimer;

DAB DAB;
ESP32Time rtc(0);
TFT_eSprite RadiotextSprite = TFT_eSprite(&tft);
TFT_eSprite FrequencySprite = TFT_eSprite(&tft);
WiFiConnect wc;
WiFiServer Server(7373);
WiFiClient RemoteClient;

void setup() {
  LittleFS.begin();
  LittleFS.format();

  setupmode = true;

  EEPROM.begin(EE_TOTAL_CNT);
  if (EEPROM.readByte(EE_BYTE_CHECKBYTE) != EE_CHECKBYTE_VALUE) DefaultSettings();
  ContrastSet = EEPROM.readByte(EE_BYTE_CONTRASTSET);
  language = EEPROM.readByte(EE_BYTE_LANGUAGE);
  displayflip = EEPROM.readByte(EE_BYTE_DISPLAYFLIP);
  rotarymode = EEPROM.readByte(EE_BYTE_ROTARYMODE);
  tunemode = EEPROM.readByte(EE_BYTE_TUNEMODE);
  memorypos = EEPROM.readByte(EE_BYTE_MEMORYPOS);
  wifi = EEPROM.readByte(EE_BYTE_WIFI);
  unit = EEPROM.readByte(EE_BYTE_UNIT);
  dabfreq = EEPROM.readByte(EE_BYTE_DABFREQ);

  btStop();
  Serial.begin(115200);

  tft.init();
  doTheme();
  if (displayflip == 0) {
    tft.setRotation(3);
  } else {
    tft.setRotation(1);
  }

  pinMode(BANDBUTTON, INPUT);
  pinMode(MODEBUTTON, INPUT);
  pinMode(SLBUTTON, INPUT);
  pinMode(ROTARY_BUTTON, INPUT);
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

  FrequencySprite.createSprite(200, 50);
  FrequencySprite.setTextDatum(TR_DATUM);
  FrequencySprite.loadFont(FREQFONT);

  if (digitalRead(SLBUTTON) == LOW && digitalRead(ROTARY_BUTTON) == HIGH) {
    if (rotarymode == 0) rotarymode = 1; else rotarymode = 0;
    EEPROM.writeByte(EE_BYTE_ROTARYMODE, rotarymode);
    EEPROM.commit();

    analogWrite(CONTRASTPIN, ContrastSet * 2 + 27);
    tftPrint(0, myLanguage[language][1], 155, 70, ActiveColor, ActiveColorSmooth, 28);
    tftPrint(0, myLanguage[language][2], 155, 130, ActiveColor, ActiveColorSmooth, 28);
    while (digitalRead(SLBUTTON) == LOW) delay(50);
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
    while (digitalRead(MODEBUTTON) == LOW) delay(50);
  }

  if (digitalRead(ROTARY_BUTTON) == LOW && digitalRead(SLBUTTON) == HIGH) {
    analogWrite(CONTRASTPIN, ContrastSet * 2 + 27);

    tftPrint(0, myLanguage[language][2], 155, 130, ActiveColor, ActiveColorSmooth, 28);
    while (digitalRead(ROTARY_BUTTON) == LOW) delay(50);
  }

  if (digitalRead(ROTARY_BUTTON) == LOW && digitalRead(SLBUTTON) == LOW) {
    DefaultSettings();

    analogWrite(CONTRASTPIN, ContrastSet * 2 + 27);
    tftPrint(0, myLanguage[language][10], 155, 70, ActiveColor, ActiveColorSmooth, 28);
    tftPrint(0, myLanguage[language][2], 155, 130, ActiveColor, ActiveColorSmooth, 28);
    while (digitalRead(ROTARY_BUTTON) == LOW && digitalRead(SLBUTTON) == LOW) delay(50);
    ESP.restart();
  }

  tft.fillScreen(BackgroundColor);
  tft.pushImage (0, 0, 320, 240, SplashScreen);

  tftPrint(0, String(myLanguage[language][9]) + " " + String(VERSION), 160, 180, TFT_WHITE, TFT_DARKGREY, 16);

  for (int x = 0; x <= ContrastSet; x++) {
    analogWrite(CONTRASTPIN, x * 2 + 27);
    delay(30);
  }

  DAB.begin(15, DABRESET);
  tftPrint(0, String(DAB.getChipID()) + " v" + String(DAB.getFirmwareVersion()), 160, 200, TFT_WHITE, TFT_DARKGREY, 16);

  delay(1500);

  if (wifi) {
    tryWiFi();
    delay(2000);
  } else {
    Server.end();
  }

  DAB.setFreq(dabfreq);
  delay(100);
  EEPROM.get(EE_UINT32_SERVICEID, _serviceID);
  EEPROM.get(EE_UINT32_COMPONENTID, _componentID);
  DAB.setService(_serviceID, _componentID);

  BuildDisplay();

  setupmode = false;
}

void loop() {
  ProcessDAB();

  if (millis() >= tuningtimer + 500) {
    if (store) {
      detachInterrupt(digitalPinToInterrupt(ROTARY_PIN_A));
      detachInterrupt(digitalPinToInterrupt(ROTARY_PIN_B));
      StoreFrequency();
      store = false;
      attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_A), read_encoder, CHANGE);
      attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_B), read_encoder, CHANGE);
    } else if (tuning) {
      DAB.setFreq(dabfreq);
      tuning = false;
    }
  }

  if (rotary2 == -1) {
    if (DAB.numberofservices > 0) DABSelectService(1);
    if (SlideShowView) SlideShowButtonPress();
    store = true;
    tuningtimer = millis();
  }

  if (rotary2 == 1) {
    if (DAB.numberofservices > 0) DABSelectService(0);
    if (SlideShowView) SlideShowButtonPress();
    store = true;
    tuningtimer = millis();
  }

  if (rotary == -1) {
    KeyUp();
  }

  if (rotary == 1) {
    KeyDown();
  }

  if (digitalRead(ROTARY_BUTTON) == LOW) {
  }

  if (digitalRead(MODEBUTTON) == LOW) {
    ShowTuneMode();
  }

  if (digitalRead(SLBUTTON) == LOW) {
    SlideShowButtonPress();
  }
}

void ProcessDAB(void) {
  DAB.Update();
  if (!SlideShowView) {
    ShowRSSI();
    ShowBitrate();
    ShowSignalLevel();
    ShowEID();
    ShowSID();
    ShowProtectionlevel();
    ShowPS();
    ShowRT();
  } else {
    if (DAB.SlideShowAvailable && DAB.SlideShowUpdate) {
      ShowSlideShow();
      DAB.SlideShowUpdate = false;
    }
  }
}

void ShowRT() {
  if (DAB.ASCII(DAB.ServiceData).length() > 0) {
    if (DAB.ASCII(DAB.ServiceData).length() < 29) {
      xPos = 0;
      RadiotextSprite.fillSprite(BackgroundColor);
      RadiotextSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
      RadiotextSprite.drawString(String(DAB.ASCII(DAB.ServiceData)), xPos, 2);
      RadiotextSprite.pushSprite(38, 220);
    } else {
      if (millis() - rtticker >= 5) {
        if (xPos == 0) {
          if (millis() - rttickerhold >= 2000) {
            xPos --;
            rttickerhold = millis();
          }
        } else {
          xPos --;
          rttickerhold = millis();
        }
        if (xPos < -tft.textWidth(DAB.ASCII(DAB.ServiceData)) + (charWidth * 20)) xPos = 0;
        RadiotextSprite.fillSprite(BackgroundColor);
        RadiotextSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        RadiotextSprite.drawString(String(DAB.ASCII(DAB.ServiceData)), xPos, 2);
        RadiotextSprite.pushSprite(38, 220);
      }
      rtticker = millis();
    }
  } else {
    RadiotextSprite.fillSprite(BackgroundColor);
    RadiotextSprite.pushSprite(38, 220);
  }
  if (RTold != DAB.ASCII(DAB.ServiceData)) xPos = 0;
  RTold = DAB.ASCII(DAB.ServiceData);
}

void ShowSID() {
  if (String(DAB.SID) != SIDold) {
    tftReplace(-1, SIDold, String(DAB.SID), 143, 135 , PrimaryColor, PrimaryColorSmooth, 28);
    SIDold = String(DAB.SID);
  }
}

void ShowEID() {
  if (String(DAB.EID) != EIDold) {
    tftReplace(-1, EIDold, String(DAB.EID), 38, 135 , PrimaryColor, PrimaryColorSmooth, 28);
    EIDold = String(DAB.EID);
  }
}

void ShowPS() {
  if ((DAB.ServiceStart ? DAB.ASCII(DAB.service[DAB.ServiceIndex].Label) : DAB.getEnsembleLabel()) != PSold) {
    tftReplace(-1, PSold, String((DAB.ServiceStart ? DAB.ASCII(DAB.service[DAB.ServiceIndex].Label) : DAB.getEnsembleLabel())), 38, 187, PrimaryColor, PrimaryColorSmooth, 28);
    PSold = (DAB.ServiceStart ? DAB.ASCII(DAB.service[DAB.ServiceIndex].Label) : DAB.getEnsembleLabel());
  }
}

void ShowProtectionlevel() {
  if (String(ProtectionText[DAB.protectionlevel]) != PLold) {
    if (DAB.ServiceStart) tftReplace(0, PLold, String(ProtectionText[DAB.protectionlevel]), 105, 4, PrimaryColor, PrimaryColorSmooth, 28); else tftPrint(0, PLold, 105, 4, BackgroundColor, BackgroundColor, 28);
    PLold = String(ProtectionText[DAB.protectionlevel]);
  }
}

void ShowSlideShow(void) {
  if (DAB.isJPG) {
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
  } else if (DAB.isPNG) {
    tft.fillScreen(TFT_BLACK);
    String filename = "/slideshow.img";
    pngfile = LittleFS.open(filename.c_str(), "rb");

    if (pngfile) {
      int16_t rc = png.open(
                     filename.c_str(),
      [pngfile](const char *filename, int32_t *size) -> void * {
        pngfile = LittleFS.open(filename, "rb");
        *size = pngfile.size();
        return &pngfile;
      },
      [pngfile](void *handle) {
        File *file = static_cast<File *>(handle);
        if (file && file->available()) {
          file->close();
        }
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
  if (DAB.numberofservices > 0) {
    if (dir) {
      if (DAB.ServiceIndex < (DAB.numberofservices - 1)) {
        if (DAB.ServiceStart) DAB.ServiceIndex++;
      } else {
        DAB.ServiceIndex = 0;
      }
    } else {
      if (DAB.ServiceIndex > 0) {
        DAB.ServiceIndex--;
      } else {
        DAB.ServiceIndex = DAB.numberofservices - 1;
      }
    }
    DAB.setService(DAB.ServiceIndex);
  }
  rotary2 = 0;
  DAB.ServiceStart = true;
}

void BANDBUTTONPress() {
}

void StoreFrequency() {
  EEPROM.put(EE_UINT32_SERVICEID, DAB.service[DAB.ServiceIndex].ServiceID);
  EEPROM.put(EE_UINT32_COMPONENTID, DAB.service[DAB.ServiceIndex].CompID);
  EEPROM.put(EE_BYTE_DABFREQ, dabfreq);
  EEPROM.commit();
}

void SlideShowButtonPress() {
  if (!SlideShowView && DAB.SlideShowAvailable) {
    tft.fillScreen(TFT_BLACK);
    DAB.SlideShowUpdate = true;
    SlideShowView = true;
  } else {
    if (SlideShowView) BuildDisplay();
    SlideShowView = false;
  }
  while (digitalRead(SLBUTTON) == LOW) delay(100);
}

void ModeButtonPress() {
  while (digitalRead(MODEBUTTON) == LOW) delay(50);
}

void KeyUp() {
  rotary = 0;
  rotary2 = 0;
  if (SlideShowView) SlideShowButtonPress();
  if (!menu) {
    switch (tunemode) {
      case TUNE_MAN:
        TuneUp();
        break;

      case TUNE_AUTO:
        direction = true;
        seek = true;
        break;

      case TUNE_MEM:
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
        TuneDown();
        break;

      case TUNE_AUTO:
        direction = false;
        seek = true;
        break;

      case TUNE_MEM:
        break;
    }
  } else {
    MenuDown();
  }
}

void ShowFreq() {
  detachInterrupt(digitalPinToInterrupt(ROTARY_PIN_A));
  detachInterrupt(digitalPinToInterrupt(ROTARY_PIN_B));
  FrequencySprite.fillSprite(BackgroundColor);
  FrequencySprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
  FrequencySprite.drawString(String(DAB.getFreq(dabfreq) / 1000) + "." + (DAB.getFreq(dabfreq) % 1000 < 100 ? "0" : "") + String(DAB.getFreq(dabfreq) % 1000), 200, -6) + " ";
  tftReplace(1, DAB.getChannel(dabfreqold), DAB.getChannel(dabfreq), 316, 38, SecondaryColor, SecondaryColorSmooth, 28);
  FrequencySprite.pushSprite(46, 46);
  dabfreqold = dabfreq;
  attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_A), read_encoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ROTARY_PIN_B), read_encoder, CHANGE);
}

void ShowSignalLevel() {
  if (millis() >= rssitimer + 100) {
    rssitimer = millis();
    SignalLevel = DAB.getRSSI();
    CNR = DAB.cnr;
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

  if (SignalLevelprint > (SignalLevelold + 3) || SignalLevelprint < (SignalLevelold - 3)) {
    tftReplace(1, String(SignalLevelold / 10), String(SignalLevelprint / 10), 288, 105, PrimaryColor, PrimaryColorSmooth, 48);
    tftReplace(1, "." + String(abs(SignalLevelold % 10)), "." + String(abs(SignalLevelprint % 10)), 310, 105, PrimaryColor, PrimaryColorSmooth, 28);

    segments = (SignalLevel + 200) / 10;

    tft.fillRect(16, 105, 2 * constrain(segments, 0, 54), 6, BarInsignificantColor);
    tft.fillRect(16 + 2 * 54, 105, 2 * (constrain(segments, 54, 94) - 54), 6, BarSignificantColor);
    tft.fillRect(16 + 2 * constrain(segments, 0, 94), 105, 2 * (94 - constrain(segments, 0, 94)), 6, GreyoutColor);
    SignalLevelold = SignalLevelprint;
  }

  if (CNRold != CNR) {
    if (DAB.signallock) {
      tftPrint(1, String("--"), 295, 163, BackgroundColor, BackgroundColor, 16);
      tftReplace(1, String(CNRold), String(CNR), 295, 163, PrimaryColor, PrimaryColorSmooth, 16);
    } else {
      tftReplace(1, String(CNRold), "--", 295, 163, PrimaryColor, PrimaryColorSmooth, 16);
    }
    CNRold = CNR;
  }
}

void ShowBitrate() {
  if (DAB.bitrate != BitrateOld) {
    if (DAB.bitrate == 0) {
      tftReplace(1, String (BitrateOld, DEC), "-", 201, 4, ActiveColor, ActiveColorSmooth, 28);
    } else {
      tftPrint(1, "-", 201, 4, BackgroundColor, BackgroundColor, 28);
      tftReplace(1, String (BitrateOld, DEC), String (DAB.bitrate, DEC), 201, 4, ActiveColor, ActiveColorSmooth, 28);
    }
    BitrateOld = DAB.bitrate;
  }
}

void ShowTuneMode() {
  switch (tunemode) {
    case TUNE_MAN:
      tft.drawRoundRect(1, 57, 42, 20, 5, GreyoutColor);
      tftPrint(0, "AUTO", 22, 60, GreyoutColor, BackgroundColor, 16);

      tft.drawRoundRect(1, 35, 42, 20, 5, ActiveColor);
      tftPrint(0, "MAN", 22, 38, ActiveColor, ActiveColorSmooth, 16);

      tft.drawRoundRect(1, 79, 42, 20, 5, GreyoutColor);
      tftPrint(0, "MEM", 22, 82, GreyoutColor, BackgroundColor, 16);
      break;

    case TUNE_AUTO:
      tft.drawRoundRect(1, 57, 42, 20, 5, ActiveColor);
      tftPrint(0, "AUTO", 22, 60, ActiveColor, ActiveColorSmooth, 16);

      tft.drawRoundRect(1, 35, 42, 20, 5, GreyoutColor);
      tftPrint(0, "MAN", 22, 38, GreyoutColor, BackgroundColor, 16);

      tft.drawRoundRect(1, 79, 42, 20, 5, GreyoutColor);
      tftPrint(0, "MEM", 22, 82, GreyoutColor, BackgroundColor, 16);
      break;

    case TUNE_MEM:
      tft.drawRoundRect(1, 57, 42, 20, 5, GreyoutColor);
      tftPrint(0, "AUTO", 22, 60, GreyoutColor, BackgroundColor, 16);

      tft.drawRoundRect(1, 35, 42, 20, 5, GreyoutColor);
      tftPrint(0, "MAN", 22, 39, GreyoutColor, BackgroundColor, 16);

      if (memorystore) {
        tft.drawRoundRect(1, 79, 42, 20, 5, SignificantColor);
        tftPrint(0, "MEM", 22, 82, SignificantColor, SignificantColorSmooth, 16);
      } else {
        tft.drawRoundRect(1, 79, 42, 20, 5, ActiveColor);
        tftPrint(0, "MEM", 22, 82, ActiveColor, ActiveColorSmooth, 16);
      }
      break;
  }
}

void ShowRSSI() {
  if (wifi) rssi = WiFi.RSSI(); else rssi = 0;
  if (rssiold != rssi) {
    rssiold = rssi;
    if (rssi == 0) {
      tft.drawBitmap(250, 4, WiFi4, 25, 25, GreyoutColor);
    } else if (rssi > -50 && rssi < 0) {
      tft.drawBitmap(250, 4, WiFi4, 25, 25, PrimaryColor);
    } else if (rssi > -60) {
      tft.drawBitmap(250, 4, WiFi4, 25, 25, GreyoutColor);
      tft.drawBitmap(250, 4, WiFi3, 25, 25, PrimaryColor);
    } else if (rssi > -70) {
      tft.drawBitmap(250, 4, WiFi4, 25, 25, GreyoutColor);
      tft.drawBitmap(250, 4, WiFi2, 25, 25, PrimaryColor);
    } else if (rssi < -70) {
      tft.drawBitmap(250, 4, WiFi4, 25, 25, GreyoutColor);
      tft.drawBitmap(250, 4, WiFi1, 25, 25, PrimaryColor);
    }
  }
}

void TuneUp() {
  dabfreq++;
  if (dabfreq > 37) dabfreq = 0;
  tuning = true;
  tuningtimer = millis();
  DAB.ServiceIndex = 0;
  DAB.ServiceStart = false;
  ShowFreq();
}

void TuneDown() {
  dabfreq--;
  if (dabfreq > 37) dabfreq = 37;
  tuning = true;
  tuningtimer = millis();
  DAB.ServiceIndex = 0;
  DAB.ServiceStart = false;
  ShowFreq();
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
}

void tftReplace(int8_t offset, const String & textold, const String & text, int16_t x, int16_t y, int color, int smoothcolor, uint8_t fontsize) {
  const uint8_t *selectedFont = nullptr;
  if (fontsize == 16) selectedFont = FONT16;
  if (fontsize == 28) selectedFont = FONT28;
  if (fontsize == 48) selectedFont = FONT48;

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
