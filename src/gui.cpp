#ifndef GUI_CPP
#define GUI_CPP
#include "gui.h"
#include "language.h"
#include "constants.h"
#include <WiFi.h>
#include <Wire.h>
#include <EEPROM.h>
#include <cstring>

byte menuitem;
byte items[8] = {8, 2, 7, 10, 9, 10, 10, 5};

void doTheme() {  // Use this to put your own colors in: http://www.barth-dev.de/online/rgb565-color-picker/
  PrimaryColor = TFT_YELLOW;
  PrimaryColorSmooth = 0x2120;
  SecondaryColor = TFT_SKYBLUE;
  SecondaryColorSmooth = 0x10E4;
  FrameColor = TFT_BLUE;
  GreyoutColor = 0x39A7;
  BackgroundColor = TFT_BLACK;
  ActiveColor = TFT_WHITE;
  ActiveColorSmooth = 0x18E3;
  SignificantColor = TFT_RED;
  SignificantColorSmooth = 0x2000;
  InsignificantColor = TFT_GREEN;
  InsignificantColorSmooth = 0x00C0;
  BarSignificantColor = TFT_RED;
  BarInsignificantColor = TFT_GREEN;
}

void BuildChannelList() {
  tft.fillScreen(BackgroundColor);
  tft.drawRect(0, 0, 320, 240, FrameColor);
  tft.drawLine(0, 30, 320, 30, FrameColor);
  tft.drawLine(0, 217, 320, 217, FrameColor);  
  tftPrint(0, myLanguage[language][11], 155, 4, ActiveColor, ActiveColorSmooth, 28);

  byte y = 0;
  if (radio.ServiceIndex > 8 && radio.ServiceIndex < 17) {
    y = 9;
  } else if (radio.ServiceIndex > 16 && radio.ServiceIndex < 25) {
    y = 17;
  } else if (radio.ServiceIndex > 24) {
    y = 25;
  }

  if (radio.numberofservices > 8) {
    byte z = 0;
    if (radio.numberofservices < 17) {
      z = 2;
    } else if (radio.numberofservices < 25) {
      z = 3;
    } else if (radio.numberofservices > 24) {
      z = 4;
    }
    if (y == 0) {
      tftPrint(1, "1/" + String(z), 317, 12, SecondaryColor, SecondaryColorSmooth, 14);
    } else if (y == 9) {
      tftPrint(1, "2/" + String(z), 317, 12, SecondaryColor, SecondaryColorSmooth, 14);
    } else if (y == 17) {
      tftPrint(1, "3/" + String(z), 317, 12, SecondaryColor, SecondaryColorSmooth, 14);
    } else if (y == 25) {
      tftPrint(1, "4/" + String(z), 317, 12, SecondaryColor, SecondaryColorSmooth, 14);
    }
  }


  tft.drawRoundRect(3, 35 + (20 * (radio.ServiceIndex - y)), 315, 21, 5, ActiveColor);

  for (byte i = y; i < radio.numberofservices; i++) {
    tftPrint(-1, String(radio.service[i].CompID & 0xFF, DEC), 14, 38 + (20 * (i - y)), SecondaryColor, SecondaryColorSmooth, 16);
    String serviceIDString = String(radio.service[i].ServiceID & 0xFFFF, HEX);
    serviceIDString.toUpperCase();
    tftPrint(-1, serviceIDString, 46, 38 + (20 * (i - y)), SecondaryColor, SecondaryColorSmooth, 16);
    tftPrint(-1, String(radio.ASCII(radio.service[i].Label)), 86, 38 + (20 * (i - y)), PrimaryColor, PrimaryColorSmooth, 16);
    if (i - y == 8) i = 254;
  }
}

void BuildMenu() {
}

void BuildDisplay() {
  SlideShowView = false;
  tft.fillScreen(BackgroundColor);
  tft.drawRect(0, 0, 320, 240, FrameColor);
  tft.drawLine(0, 30, 320, 30, FrameColor);
  tft.drawLine(0, 100, 320, 100, FrameColor);
  tft.drawLine(210, 100, 210, 180, FrameColor);
  tft.drawLine(248, 30, 248, 0, FrameColor);
  tft.drawLine(0, 130, 210, 130, FrameColor);
  tft.drawLine(105, 130, 105, 160, FrameColor);
  tft.drawLine(0, 160, 210, 160, FrameColor);
  tft.drawLine(0, 180, 320, 180, FrameColor);
  tft.drawLine(0, 217, 320, 217, FrameColor);
  tft.drawLine(53, 30, 53, 0, FrameColor);
  tft.drawLine(158, 30, 158, 0, FrameColor);
  tft.drawLine(20, 114, 204, 114, TFT_DARKGREY);
  for (byte segments = 0; segments < 94; segments++) {
    if (segments > 54) {
      if (((segments - 53) % 10) == 0) {
        tft.fillRect(16 + (2 * segments), 112, 2, 2, BarSignificantColor);
      }
    } else {
      if (((segments + 1) % 6) == 0) {
        tft.fillRect(16 + (2 * segments), 112, 2, 2, BarInsignificantColor);
      }
    }
  }
  tftPrint(1, "MER", 270, 163, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "dB", 300, 163, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "PS:", 3, 193, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "RT:", 3, 221, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "PTY:", 3, 163, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "EID:", 3, 140, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "SID:", 111, 140, ActiveColor, ActiveColorSmooth, 16);

  tftPrint(0, "S", 7, 101, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "1", 24, 115, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "3", 48, 115, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "5", 72, 115, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "7", ITEM4 + 6, 115, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "9", 120, 115, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "+10", 134, 115, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "+30", 174, 115, ActiveColor, ActiveColorSmooth, 16);

  tftPrint(-1, "kb/s", 203, 4, ActiveColor, ActiveColorSmooth, 28);
  tftPrint(-1, unitString[unit], 282, 145, ActiveColor, ActiveColorSmooth, 16);

  tftPrint(-1, "MHz", 258, ITEM3 + 6, ActiveColor, ActiveColorSmooth, 28);


  tftPrint(-1, "DAB", 70, 32, PrimaryColor, PrimaryColorSmooth, 16);

  ShowFreq();
  ShowTuneMode();


  rssiold = 2000;

  SignalLevelold = 65535;
  CNRold = 254;
  BitrateOld = 65535;
  EIDold = "";
  SIDold = "";
  PLold = "";
  PSold = "";
}

void MenuUp() {
}

void MenuDown() {
}

void DoMenu() {
}

void Infoboxprint(const char* input) {
  int length = strlen(input);
  int newlineIndex = -1;

  for (int i = 0; i < length; i++) {
    if (input[i] == '\n') {
      newlineIndex = i;
      break;
    }
  }

  if (newlineIndex != -1) {
    char* line1 = (char*)malloc((newlineIndex + 1) * sizeof(char));
    strncpy(line1, input, newlineIndex);
    line1[newlineIndex] = '\0';

    char* line2 = (char*)malloc((length - newlineIndex) * sizeof(char));
    strcpy(line2, input + newlineIndex + 1);

    tftPrint(0, line1, 155, 48, ActiveColor, ActiveColorSmooth, 28);
    tftPrint(0, line2, 155, 78, ActiveColor, ActiveColorSmooth, 28);
    free(line1);
    free(line2);
  } else {
    tftPrint(0, input, 155, 78, ActiveColor, ActiveColorSmooth, 28);
  }
}
#endif