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

void doTheme() {  // Use this to put your own colors in: http://www.barth-dev.de/online/rgb565-color-picker/
  switch (CurrentTheme) {
    case 0:  // Elegant
      PrimaryColor = 0x2e65;
      PrimaryColorSmooth = 0x09a1;
      SecondaryColor = 0x051f;
      SecondaryColorSmooth = 0x08e5;
      FrameColor = 0x001F;
      GreyoutColor = 0x5b0d;
      BackgroundColor = 0x016b;
      BackgroundColor2 = 0x016a;
      BackgroundColor3 = 0x0108;
      BackgroundColor4 = 0x00c6;
      BackgroundColor5 = 0x00a5;
      BackgroundColor6 = 0x00a4;
      ActiveColor = 0xFFFF;
      ActiveColorSmooth = 0x18E3;
      SignificantColor = 0xF800;
      SignificantColorSmooth = 0x2000;
      InsignificantColor = 0x07E0;
      InsignificantColorSmooth = 0x00C0;
      BarSignificantColor = 0xF800;
      BarInsignificantColor = 0x051f;
      break;

    case 1: // GoldenDusk
      PrimaryColor = 0x8ff1;
      PrimaryColorSmooth = 0x10c2;
      SecondaryColor = 0xFFE0;
      SecondaryColorSmooth = 0x2120;
      FrameColor = 0x001F;
      GreyoutColor = 0x5b0d;
      BackgroundColor = 0x016b;
      BackgroundColor2 = 0x016a;
      BackgroundColor3 = 0x0108;
      BackgroundColor4 = 0x00c6;
      BackgroundColor5 = 0x00a5;
      BackgroundColor6 = 0x00a4;
      ActiveColor = 0xFFFF;
      ActiveColorSmooth = 0x18E3;
      SignificantColor = 0xF800;
      SignificantColorSmooth = 0x2000;
      InsignificantColor = 0x07E0;
      InsignificantColorSmooth = 0x00C0;
      BarSignificantColor = 0xF800;
      BarInsignificantColor = 0x07E0;
      break;

    case 2: // Vibrant
      PrimaryColor = 0xF00A;
      PrimaryColorSmooth = 0x3800;
      SecondaryColor = 0xFFE0;
      SecondaryColorSmooth = 0x2120;
      FrameColor = 0x001F;
      GreyoutColor = 0x5b0d;
      BackgroundColor = 0x016b;
      BackgroundColor2 = 0x016a;
      BackgroundColor3 = 0x0108;
      BackgroundColor4 = 0x00c6;
      BackgroundColor5 = 0x00a5;
      BackgroundColor6 = 0x00a4;
      ActiveColor = 0x051F;
      ActiveColorSmooth = 0x0106;
      SignificantColor = 0xF3D5;
      SignificantColorSmooth = 0x3008;
      InsignificantColor = 0x07E0;
      InsignificantColorSmooth = 0x00C0;
      BarSignificantColor = 0xF3D5;
      BarInsignificantColor = 0x07E0;
      break;

    case 3: // Serenity
      PrimaryColor = 0xF3D5;
      PrimaryColorSmooth = 0x3008;
      SecondaryColor = 0x9C96;
      SecondaryColorSmooth = 0x41C8;
      FrameColor = 0x001F;
      GreyoutColor = 0x5b0d;
      BackgroundColor = 0x016b;
      BackgroundColor2 = 0x016a;
      BackgroundColor3 = 0x0108;
      BackgroundColor4 = 0x00c6;
      BackgroundColor5 = 0x00a5;
      BackgroundColor6 = 0x00a4;
      ActiveColor = 0x9B8D;
      ActiveColorSmooth = 0x5207;
      SignificantColor = 0x748E;
      SignificantColorSmooth = 0x3206;
      InsignificantColor = 0x9B90;
      InsignificantColorSmooth = 0x3946;
      BarSignificantColor = 0x748E;
      BarInsignificantColor = 0xF3D5;
      break;

    case 4: // Luminous
      PrimaryColor = 0x051F;
      PrimaryColorSmooth = 0x0106;
      SecondaryColor = 0xFA8D;
      SecondaryColorSmooth = 0x3083;
      FrameColor = 0x001F;
      GreyoutColor = 0x5b0d;
      BackgroundColor = 0x016b;
      BackgroundColor2 = 0x016a;
      BackgroundColor3 = 0x0108;
      BackgroundColor4 = 0x00c6;
      BackgroundColor5 = 0x00a5;
      BackgroundColor6 = 0x00a4;
      ActiveColor = 0xFC00;
      ActiveColorSmooth = 0x3165;
      SignificantColor = 0xFFE0;
      SignificantColorSmooth = 0x2120;
      InsignificantColor = 0xF8C3;
      InsignificantColorSmooth = 0x3800;
      BarSignificantColor = 0xFFE0;
      BarInsignificantColor = 0x867D;
      break;

    case 5: // Radiant
      PrimaryColor = 0x051F;
      PrimaryColorSmooth = 0x0106;
      SecondaryColor = 0xFFE0;
      SecondaryColorSmooth = 0x2120;
      FrameColor = 0x001F;
      GreyoutColor = 0x5b0d;
      BackgroundColor = 0x016b;
      BackgroundColor2 = 0x016a;
      BackgroundColor3 = 0x0108;
      BackgroundColor4 = 0x00c6;
      BackgroundColor5 = 0x00a5;
      BackgroundColor6 = 0x00a4;
      ActiveColor = 0xFC00;
      ActiveColorSmooth = 0x3165;
      SignificantColor = 0xFFE0;
      SignificantColorSmooth = 0x2120;
      InsignificantColor = 0xF8C3;
      InsignificantColorSmooth = 0x3800;
      BarSignificantColor = 0xFFE0;
      BarInsignificantColor = 0x867D;
      break;

    case 6: // Sunset
      PrimaryColor = 0x051F;
      PrimaryColorSmooth = 0x0106;
      SecondaryColor = 0xFFE0;
      SecondaryColorSmooth = 0x2120;
      FrameColor = 0x001F;
      GreyoutColor = 0x5b0d;
      BackgroundColor = 0x016b;
      BackgroundColor2 = 0x016a;
      BackgroundColor3 = 0x0108;
      BackgroundColor4 = 0x00c6;
      BackgroundColor5 = 0x00a5;
      BackgroundColor6 = 0x00a4;
      ActiveColor = 0xED20;
      ActiveColorSmooth = 0x3940;
      SignificantColor = 0xF800;
      SignificantColorSmooth = 0x2000;
      InsignificantColor = 0x07E0;
      InsignificantColorSmooth = 0x00C0;
      BarSignificantColor = 0xF800;
      BarInsignificantColor = 0x07E0;
      break;
  }
}

void ShowServiceInfo() {
  tft.fillScreen(BackgroundColor);
  tft.drawRect(0, 0, 320, 240, FrameColor);
  tft.drawLine(0, 30, 320, 30, FrameColor);
  tft.drawLine(0, 217, 320, 217, FrameColor);
  tftPrint(0, myLanguage[language][27], 155, 4, ActiveColor, ActiveColorSmooth, 28);
  tftPrint(-1, myLanguage[language][28], 8, ITEM1 + 6, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, myLanguage[language][36], 8, ITEM2 + 6, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, myLanguage[language][29], 8, ITEM3 + 6, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, myLanguage[language][30], 8, ITEM4 + 6, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, myLanguage[language][31], 8, ITEM5 + 6, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, myLanguage[language][32], 8, ITEM6 + 6, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, myLanguage[language][33], 8, ITEM7 + 6, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, myLanguage[language][34], 8, ITEM8 + 6, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, myLanguage[language][35], 8, ITEM9 + 6, ActiveColor, ActiveColorSmooth, 16);

  tftPrint(-1, String(radio.getChannel(dabfreq)) + " - " + String(radio.getFreq(dabfreq) / 1000) + "." + (radio.getFreq(dabfreq) % 1000 < 100 ? "0" : "") + String(radio.getFreq(dabfreq) % 1000) + " MHz", 155, ITEM1 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(-1, radio.ASCII(radio.EnsembleLabel), 155, ITEM3 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(-1, radio.ASCII(radio.service[radio.ServiceIndex].Label), 155, ITEM4 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(-1, String(radio.pty, DEC) + ": " + String(myLanguage[language][37 + radio.pty]), 155, ITEM5 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(-1, ProtectionText[radio.protectionlevel], 155, ITEM6 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  String bitrateString = String(radio.samplerate);
  bitrateString = bitrateString.substring(0, 2) + "." + bitrateString.substring(2);
  tftPrint(-1, bitrateString + " Hz", 155, ITEM7 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(-1, String(radio.bitrate, DEC) + " kb/s", 155, ITEM8 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(-1, String(ServiceTypeText[radio.servicetype]) + " - " + AudioModeText[radio.audiomode], 155, ITEM9 + 6, PrimaryColor, PrimaryColorSmooth, 16);

  SignalLeveloldString = "";
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
  SlideShowView = false;
  tft.fillScreen(BackgroundColor);
  tft.drawRect(0, 0, 320, 240, FrameColor);
  tft.drawLine(0, 30, 320, 30, FrameColor);
  tft.drawLine(0, 217, 320, 217, FrameColor);
  tftPrint(0, myLanguage[language][20], 155, 4, PrimaryColor, PrimaryColorSmooth, 28);
  tftPrint(0, myLanguage[language][19], 155, 222, SecondaryColor, SecondaryColorSmooth, 16);
  tft.drawRoundRect(3, menuoption + 3, 315, 21, 5, ActiveColor);

  tftPrint(-1, myLanguage[language][12], 8, ITEM1 + 6, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, myLanguage[language][13], 8, ITEM2 + 6, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, myLanguage[language][14], 8, ITEM3 + 6, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, myLanguage[language][15], 8, ITEM4 + 6, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, myLanguage[language][16], 8, ITEM5 + 6, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, myLanguage[language][25], 8, ITEM6 + 6, ActiveColor, ActiveColorSmooth, 16);
  if (wifi) tftPrint(-1, String(myLanguage[language][17]) + " IP: " + String(WiFi.localIP().toString()), 8, ITEM7 + 6, ActiveColor, ActiveColorSmooth, 16); else tftPrint(-1, myLanguage[language][17], 8, ITEM7 + 6, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, myLanguage[language][18], 8, ITEM8 + 6, ActiveColor, ActiveColorSmooth, 16);

  tftPrint(1, myLanguage[language][0], 310, ITEM1 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(1, "%", 310, ITEM2 + 6, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(1, String(ContrastSet, DEC), 270, ITEM2 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(1, Theme[CurrentTheme], 310, ITEM3 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  if (autoslideshow) tftPrint(1, myLanguage[language][23], 310, ITEM4 + 6, PrimaryColor, PrimaryColorSmooth, 16); else tftPrint(1, myLanguage[language][24], 310, ITEM4 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(1, unitString[unit], 310, ITEM5 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  if (tot != 0) tftPrint(1, String(tot), 270, ITEM6 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  if (tot != 0) tftPrint(1, myLanguage[language][26], 310, ITEM6 + 6, PrimaryColor, PrimaryColorSmooth, 16); else tftPrint(1, myLanguage[language][24], 310, ITEM6 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  if (wifi) tftPrint(1, myLanguage[language][23], 310, ITEM7 + 6, PrimaryColor, PrimaryColorSmooth, 16); else tftPrint(1, myLanguage[language][24], 310, ITEM7 + 6, PrimaryColor, PrimaryColorSmooth, 16);
}

void BuildDisplay() {
  SlideShowView = false;
  tft.pushImage (0, 0, 320, 240, Background);
  tftPrint(1, "PR:", 84, 65, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "EID", 10, 105, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "SID", 10, 120, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(1, "MHz", 310, 55, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "SIG:", 123, 109, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, unitString[unit], 183, 109, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "MER:", 237, 109, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(1, "dB", 309, 109, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "Q", 122, 90, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "S", 122, 127, SecondaryColor, SecondaryColorSmooth, 16);
  tftPrint(1, "ECC", 110, 90, ActiveColor, ActiveColorSmooth, 16);
  tft.pushImage (80, 5, 19, 19, ClockSymbol);
  tft.pushImage (150, 5, 19, 19, DateSymbol);

  for (byte segments = 0; segments < 13; segments++) tft.fillRect(134 + (segments * 14), 135, 2, 3, (segments < 8 ? BarInsignificantColor : BarSignificantColor));
  tft.drawLine(134, 138, 302, 138, ActiveColor);
  tftPrint(-1, "1   3   5   7   9  +10  +30 +60", 134, 140, ActiveColor, ActiveColorSmooth, 16);
  tft.drawRect(134, 90, 141, 12, GreyoutColor);

  ShowServiceInformation = false;
  ShowFreq();
  ShowTuneMode();
  ShowMemoryPos();

  displayreset = true;
}

void MenuUp() {
  if (!menuopen) {
    tft.drawRoundRect(3, menuoption + 3, 315, 21, 5, BackgroundColor);
    menuoption += ITEM_GAP;
    menuitem++;
    if (menuitem > 7) {
      menuitem = 0;
      menuoption = ITEM1;
    }
    tft.drawRoundRect(3, menuoption + 3, 315, 21, 5, ActiveColor);
  } else {
    switch (menuoption) {
      case ITEM1:
        tftPrint(0, myLanguage[language][0], 155, 118, BackgroundColor, BackgroundColor, 28);
        language ++;
        if (language == (sizeof (myLanguage) / sizeof (myLanguage[0]))) language = 0;
        tftPrint(0, myLanguage[language][0], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM2:
        tftPrint(1, String(ContrastSet, DEC), 155, 118, BackgroundColor, BackgroundColor, 28);
        ContrastSet ++;
        if (ContrastSet > 100) ContrastSet = 1;
        tftPrint(1, String(ContrastSet, DEC), 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        analogWrite(CONTRASTPIN, ContrastSet * 2 + 27);
        break;

      case ITEM3:
        CurrentTheme ++;
        if (CurrentTheme > sizeof(Theme) / sizeof(Theme[0]) - 1) CurrentTheme = 0;
        doTheme();
        tft.drawRoundRect(10, 30, 300, 170, 5, ActiveColor);
        tft.fillRoundRect(12, 32, 296, 166, 5, BackgroundColor);
        tftPrint(0, myLanguage[language][14], 155, 78, ActiveColor, ActiveColorSmooth, 28);
        tftPrint(0, Theme[CurrentTheme], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM4:
        if (autoslideshow) tftPrint(0, myLanguage[language][23], 155, 118, BackgroundColor, BackgroundColor, 28); else tftPrint(0, myLanguage[language][24], 155, 118, BackgroundColor, BackgroundColor, 28);
        if (autoslideshow) autoslideshow = false; else autoslideshow = true;
        if (autoslideshow) tftPrint(0, myLanguage[language][23], 155, 118, PrimaryColor, PrimaryColorSmooth, 28); else tftPrint(0, myLanguage[language][24], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM5:
        tftPrint(0, unitString[unit], 155, 118, BackgroundColor, BackgroundColor, 28);
        unit ++;
        if (unit > sizeof(unitString) / sizeof(unitString[0]) - 1) unit = 0;
        tftPrint(0, unitString[unit], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM6:
        if (tot != 0) tftPrint(-1, myLanguage[language][26], 170, 118, BackgroundColor, BackgroundColor, 28);
        if (tot != 0) tftPrint(1, String(tot), 155, 118, BackgroundColor, BackgroundColor, 28); else tftPrint(0, myLanguage[language][24], 155, 118, BackgroundColor, BackgroundColor, 28);
        switch (tot) {
          case 0: tot = 15; break;
          case 15: tot = 30; break;
          case 30: tot = 60; break;
          case 60: tot = 90; break;
          default: tot = 0; break;
        }
        if (tot != 0) tftPrint(-1, myLanguage[language][26], 170, 118, ActiveColor, ActiveColorSmooth, 28);
        if (tot != 0) tftPrint(1, String(tot), 155, 118, PrimaryColor, PrimaryColorSmooth, 28); else tftPrint(0, myLanguage[language][24], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM7:
        if (wifi) tftPrint(0, myLanguage[language][23], 155, 118, BackgroundColor, BackgroundColor, 28); else tftPrint(0, myLanguage[language][24], 155, 118, BackgroundColor, BackgroundColor, 28);
        if (wifi) wifi = false; else wifi = true;
        if (wifi) tftPrint(0, myLanguage[language][23], 155, 118, PrimaryColor, PrimaryColorSmooth, 28); else tftPrint(0, myLanguage[language][24], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;
    }
  }
}

void MenuDown() {
  if (!menuopen) {
    tft.drawRoundRect(3, menuoption + 3, 315, 21, 5, BackgroundColor);
    menuoption -= ITEM_GAP;
    menuitem--;
    if (menuitem > 7) {
      menuoption = ITEM8;
      menuitem = 7;
    }
    tft.drawRoundRect(3, menuoption + 3, 315, 21, 5, ActiveColor);
  } else {
    switch (menuoption) {
      case ITEM1:
        tftPrint(0, myLanguage[language][0], 155, 118, BackgroundColor, BackgroundColor, 28);
        language --;
        if (language > (sizeof (myLanguage) / sizeof (myLanguage[0]))) language = (sizeof (myLanguage) / sizeof (myLanguage[0])) - 1;
        tftPrint(0, myLanguage[language][0], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM2:
        tftPrint(1, String(ContrastSet, DEC), 155, 118, BackgroundColor, BackgroundColor, 28);
        ContrastSet --;
        if (ContrastSet < 1) ContrastSet = 100;
        tftPrint(1, String(ContrastSet, DEC), 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        analogWrite(CONTRASTPIN, ContrastSet * 2 + 27);
        break;

      case ITEM3:
        CurrentTheme --;
        if (CurrentTheme > sizeof(Theme) / sizeof(Theme[0]) - 1) CurrentTheme = sizeof(Theme) / sizeof(Theme[0]) - 1;
        doTheme();
        tft.drawRoundRect(10, 30, 300, 170, 5, ActiveColor);
        tft.fillRoundRect(12, 32, 296, 166, 5, BackgroundColor);
        tftPrint(0, myLanguage[language][14], 155, 78, ActiveColor, ActiveColorSmooth, 28);
        tftPrint(0, Theme[CurrentTheme], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM4:
        if (autoslideshow) tftPrint(0, myLanguage[language][23], 155, 118, BackgroundColor, BackgroundColor, 28); else tftPrint(0, myLanguage[language][24], 155, 118, BackgroundColor, BackgroundColor, 28);
        if (autoslideshow) autoslideshow = false; else autoslideshow = true;
        if (autoslideshow) tftPrint(0, myLanguage[language][23], 155, 118, PrimaryColor, PrimaryColorSmooth, 28); else tftPrint(0, myLanguage[language][24], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM5:
        tftPrint(0, unitString[unit], 155, 118, BackgroundColor, BackgroundColor, 28);
        unit --;
        if (unit > sizeof(unitString) / sizeof(unitString[0]) - 1) unit = sizeof(unitString) / sizeof(unitString[0]) - 1;
        tftPrint(0, unitString[unit], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM6:
        if (tot != 0) tftPrint(-1, myLanguage[language][26], 170, 118, BackgroundColor, BackgroundColor, 28);
        if (tot != 0) tftPrint(1, String(tot), 155, 118, BackgroundColor, BackgroundColor, 28); else tftPrint(0, myLanguage[language][24], 155, 118, BackgroundColor, BackgroundColor, 28);
        switch (tot) {
          case 15: tot = 0; break;
          case 30: tot = 15; break;
          case 60: tot = 30; break;
          case 90: tot = 60; break;
          default: tot = 90; break;
        }
        if (tot != 0) tftPrint(-1, myLanguage[language][26], 170, 118, ActiveColor, ActiveColorSmooth, 28);
        if (tot != 0) tftPrint(1, String(tot), 155, 118, PrimaryColor, PrimaryColorSmooth, 28); else tftPrint(0, myLanguage[language][24], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM7:
        if (wifi) tftPrint(0, myLanguage[language][23], 155, 118, BackgroundColor, BackgroundColor, 28); else tftPrint(0, myLanguage[language][24], 155, 118, BackgroundColor, BackgroundColor, 28);
        if (wifi) wifi = false; else wifi = true;
        if (wifi) tftPrint(0, myLanguage[language][23], 155, 118, PrimaryColor, PrimaryColorSmooth, 28); else tftPrint(0, myLanguage[language][24], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;
    }
  }
}

void DoMenu() {
  if (!menuopen) {
    tft.drawRoundRect(10, 30, 300, 170, 5, ActiveColor);
    tft.fillRoundRect(12, 32, 296, 166, 5, BackgroundColor);
    menuopen = true;
    switch (menuoption) {
      case ITEM1:
        Infoboxprint(myLanguage[language][12]);
        tftPrint(0, myLanguage[language][0], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM2:
        Infoboxprint(myLanguage[language][13]);
        tftPrint(-1, "%", 170, 118, ActiveColor, ActiveColorSmooth, 28);
        tftPrint(1, String(ContrastSet, DEC), 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM3:
        Infoboxprint(myLanguage[language][14]);
        tftPrint(0, Theme[CurrentTheme], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM4:
        Infoboxprint(myLanguage[language][15]);
        tftPrint(0, (autoslideshow ? myLanguage[language][23] : myLanguage[language][24]), 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM5:
        Infoboxprint(myLanguage[language][16]);
        tftPrint(0, unitString[unit], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM6:
        Infoboxprint(myLanguage[language][25]);
        if (tot != 0) tftPrint(-1, myLanguage[language][80], 170, 118, ActiveColor, ActiveColorSmooth, 28);
        if (tot != 0) tftPrint(1, String(tot), 155, 118, PrimaryColor, PrimaryColorSmooth, 28); else tftPrint(0, myLanguage[language][24], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM7:
        Infoboxprint(myLanguage[language][17]);
        if (wifi) tftPrint(0, myLanguage[language][23], 155, 118, PrimaryColor, PrimaryColorSmooth, 28); else tftPrint(0, myLanguage[language][24], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM8: {
          tftPrint(0, myLanguage[language][68], 155, 58, ActiveColor, ActiveColorSmooth, 28);
          tftPrint(0, "ESP_" + String(ESP_getChipId()), 155, 98, PrimaryColor, PrimaryColorSmooth, 28);
          tftPrint(0, myLanguage[language][67], 155, 138, ActiveColor, ActiveColorSmooth, 28);
          tftPrint(0, "http://192.168.4.1", 155, 174, PrimaryColor, PrimaryColorSmooth, 16);
          wc.startConfigurationPortal(AP_WAIT);
          wifi = true;
          tryWiFi();
          delay(2000);
          menuopen = false;
          BuildMenu();
        } break;
    }
  } else {
    menuopen = false;
    if (menuoption == ITEM7 && wifi) {
      tryWiFi();
      delay(2000);
    }
    BuildMenu();
  }
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