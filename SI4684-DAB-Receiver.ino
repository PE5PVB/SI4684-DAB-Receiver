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
#define BANDBUTTON      36
#define SLBUTTON        26
#define MODEBUTTON      39
#define CONTRASTPIN     2

TFT_eSPI tft = TFT_eSPI(240, 320);

uint8_t service = 0;
uint8_t freq = 0;
bool direction;
byte charwidth = 8;
bool memorystore;
bool trysetservice;
bool setupmode;
bool menu;
bool tuning;
bool highz;
bool autoslideshow;
bool seek;
byte tot;
bool resetFontOnNextCall;
byte dabfreq;
byte dabfreqold;
bool SlideShowView;
bool store;
bool tuned;
bool wifi;
bool setvolume;
bool wificonnected;
byte ContrastSet;
byte displayflip;
byte language;
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
int RTWidth;
bool channellistview;
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
int RTlengthold;
int rotary;
int rotary2;
int rssi;
int rssiold = 200;
int SecondaryColor;
int SecondaryColorSmooth;
byte volume = 63;
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
char _serviceName[17];
unsigned long tuningtimer;
unsigned long rtticker;
unsigned long rttickerhold;
unsigned long rssitimer;

ESP32Time rtc(0);
TFT_eSprite RadiotextSprite = TFT_eSprite(&tft);
TFT_eSprite FrequencySprite = TFT_eSprite(&tft);
TFT_eSprite VolumeSprite = TFT_eSprite(&tft);
WiFiConnect wc;
WiFiServer Server(7373);
WiFiClient RemoteClient;

void setup() {
  setupmode = true;
  LittleFS.begin();
  LittleFS.format();
  Headphones.Init();

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

  Headphones.SetHiZ(1);
  delay(100);
  Headphones.SetVolume(volume);

  Serial.begin(115200);

  tft.init();
  doTheme();
  if (displayflip == 0) tft.setRotation(3); else tft.setRotation(1);

  pinMode(BANDBUTTON, INPUT);
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

  radio.begin(15, -1);
  tftPrint(0, String(radio.getChipID()) + " v" + String(radio.getFirmwareVersion()), 160, 200, TFT_WHITE, TFT_DARKGREY, 16);

  delay(1500);

  if (wifi) {
    tryWiFi();
    delay(2000);
  } else {
    Server.end();
  }

  radio.setFreq(dabfreq);
  EEPROM.get(EE_UINT32_SERVICEID, _serviceID);
  EEPROM.get(EE_UINT32_COMPONENTID, _componentID);
  for (int i = 0; i < 16; i++) {
    _serviceName[i] = EEPROM.readByte(i + EE_CHAR17_SERVICENAME);
  }
  if (_serviceID != 0 && _componentID != 0) {
    trysetservice = true;
    Serial.println(String(radio.ASCII(_serviceName)));
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
    }
  }

  if (!menu) {
    if (rotary2 == -1) {
      if (setvolume) {
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
      if (setvolume) {
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

  if (digitalRead(ROTARY_BUTTON) == LOW) {
    if (!menu) {
      if (channellistview) {
        channellistview = false;
        BuildDisplay();
      } else {
        channellistview = true;
        BuildChannelList();
      }
    } else {

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
    radio.setService(_serviceID, _componentID);
    trysetservice = false;
  }

  if (!SlideShowView && !menu) {
    if (!channellistview) {
      ShowRSSI();
      ShowBitrate();
      ShowSignalLevel();
      ShowEID();
      ShowSID();
      ShowProtectionlevel();
      ShowPS();
    }
    ShowRT();
  } else {
    if (radio.SlideShowAvailable && radio.SlideShowUpdate && !menu) {
      ShowSlideShow();
      radio.SlideShowUpdate = false;
    }
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
      RadiotextSprite.pushSprite(38, 220);
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
        RadiotextSprite.pushSprite(38, 220);
        rtticker = millis();
      }
    }
  } else {
    RadiotextSprite.fillSprite(BackgroundColor);
    RadiotextSprite.pushSprite(38, 220);
  }
  if (RTold != radio.ASCII(radio.ServiceData)) xPos = 0;
  RTold = radio.ASCII(radio.ServiceData);
}

void ShowSID() {
  if (String(radio.SID) != SIDold) {
    tftReplace(-1, SIDold, String(radio.SID), 143, 135 , PrimaryColor, PrimaryColorSmooth, 28);
    SIDold = String(radio.SID);
  }
}

void ShowEID() {
  if (String(radio.EID) != EIDold) {
    tftReplace(-1, EIDold, String(radio.EID), 38, 135 , PrimaryColor, PrimaryColorSmooth, 28);
    EIDold = String(radio.EID);
  }
}

void ShowPS() {
  if ((radio.ServiceStart ? radio.ASCII(radio.service[radio.ServiceIndex].Label) : radio.getEnsembleLabel()) != PSold) {
    tftReplace(-1, PSold, String((radio.ServiceStart ? radio.ASCII(radio.service[radio.ServiceIndex].Label) : radio.getEnsembleLabel())), 38, 187, PrimaryColor, PrimaryColorSmooth, 28);
    PSold = (radio.ServiceStart ? radio.ASCII(radio.service[radio.ServiceIndex].Label) : radio.getEnsembleLabel());
  }
}

void ShowProtectionlevel() {
  if (String(ProtectionText[radio.protectionlevel]) != PLold) {
    if (radio.ServiceStart) tftReplace(0, PLold, String(ProtectionText[radio.protectionlevel]), 105, 4, PrimaryColor, PrimaryColorSmooth, 28); else tftPrint(0, PLold, 105, 4, BackgroundColor, BackgroundColor, 28);
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
  if (channellistview) BuildChannelList();
}

void BANDBUTTONPress() {
}

void StoreFrequency() {
  EEPROM.put(EE_UINT32_SERVICEID, radio.service[radio.ServiceIndex].ServiceID);
  EEPROM.put(EE_UINT32_COMPONENTID, radio.service[radio.ServiceIndex].CompID);
  EEPROM.put(EE_BYTE_DABFREQ, dabfreq);
  for (int i = 0; i < 16; i++) {
    EEPROM.writeByte(i + EE_CHAR17_SERVICENAME, radio.service[radio.ServiceIndex].Label[i]);
  }
  EEPROM.commit();
}

void SlideShowButtonPress() {
  if (!SlideShowView && radio.SlideShowAvailable) {
    tft.fillScreen(TFT_BLACK);
    radio.SlideShowUpdate = true;
    SlideShowView = true;
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
    } else {
      menu = true;
      BuildMenu();

    }
  }
  while (digitalRead(MODEBUTTON) == LOW);
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
        ShowFreq();
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
        dabfreq--;
        if (dabfreq > 37) dabfreq = 37;
        tuning = true;
        tuningtimer = millis();
        radio.ServiceIndex = 0;
        radio.ServiceStart = false;
        ShowFreq();
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
  FrequencySprite.drawString(String(radio.getFreq(dabfreq) / 1000) + "." + (radio.getFreq(dabfreq) % 1000 < 100 ? "0" : "") + String(radio.getFreq(dabfreq) % 1000), 200, -6) + " ";
  tftReplace(1, radio.getChannel(dabfreqold), radio.getChannel(dabfreq), 316, 38, SecondaryColor, SecondaryColorSmooth, 28);
  FrequencySprite.pushSprite(46, 46);
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
    if (radio.signallock) {
      tftPrint(1, String("--"), 295, 163, BackgroundColor, BackgroundColor, 16);
      tftReplace(1, String(CNRold), String(CNR), 295, 163, PrimaryColor, PrimaryColorSmooth, 16);
    } else {
      tftReplace(1, String(CNRold), "--", 295, 163, PrimaryColor, PrimaryColorSmooth, 16);
    }
    CNRold = CNR;
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
    if (radio.bitrate == 0) {
      tftReplace(1, String (BitrateOld, DEC), "-", 201, 4, ActiveColor, ActiveColorSmooth, 28);
    } else {
      tftPrint(1, "-", 201, 4, BackgroundColor, BackgroundColor, 28);
      tftReplace(1, String (BitrateOld, DEC), String (radio.bitrate, DEC), 201, 4, ActiveColor, ActiveColorSmooth, 28);
    }
    BitrateOld = radio.bitrate;
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
  EEPROM.put(EE_UINT32_SERVICEID, 0);
  EEPROM.put(EE_UINT32_COMPONENTID, 0);
  EEPROM.put(EE_BYTE_DABFREQ, 0);
  EEPROM.writeByte(EE_CHAR17_SERVICENAME, 0x00);
  EEPROM.commit();
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
