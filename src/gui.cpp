#ifndef GUI_CPP
#define GUI_CPP
#include "gui.h"
#include "language.h"
#include "constants.h"
#include "graphics.h"
#include <WiFi.h>
#include <Wire.h>
#include <EEPROM.h>
#include <cstring>

byte menuitem;

void doTheme(void) {  // Use this to put your own colors in: http://www.barth-dev.de/online/rgb565-color-picker/
  switch (CurrentTheme) {
    case 0:  // Elegant
      PrimaryColor = 0x2e65;
      PrimaryColorSmooth = 0x09a1;
      SecondaryColor = 0x051f;
      SecondaryColorSmooth = 0x08e5;
      GreyoutColor = 0x5b0d;
      BackgroundColor = 0x0063;
      BackgroundColor2 = 0x016a;
      BackgroundColor3 = 0x0108;
      BackgroundColor4 = 0x00c6;
      BackgroundColor5 = 0x00a5;
      BackgroundColor6 = 0x00a4;
      BackgroundColor7 = 0x0042;
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

void ShowServiceInfo(void) {
  setvolume = false;
  displayreset = true;
  tft.pushImage (0, 0, 320, 240, serviceinfobackground);
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

  tftPrint(-1, String(radio.getChannel(dabfreq)) + " - " + String(radio.getFreq(dabfreq) / 1000) + "." + (radio.getFreq(dabfreq) % 1000 < 100 ? "0" : "") + String(radio.getFreq(dabfreq) % 1000) + " MHz", 163, ITEM1 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(-1, String(unitString[unit]) + "  MER:", 190, ITEM2 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(-1, "dB", 278, ITEM2 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(-1, radio.ASCII(radio.EnsembleLabel), 163, ITEM3 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(-1, radio.ASCII(radio.service[radio.ServiceIndex].Label), 163, ITEM4 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(-1, String(radio.pty, DEC) + ": " + String(myLanguage[language][37 + radio.pty]), 163, ITEM5 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(-1, ProtectionText[radio.protectionlevel], 163, ITEM6 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  String bitrateString = String(radio.samplerate);
  bitrateString = bitrateString.substring(0, 2) + "." + bitrateString.substring(2);
  tftPrint(-1, bitrateString + " Hz", 163, ITEM7 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(-1, String(radio.bitrate, DEC) + " kb/s", 163, ITEM8 + 6, PrimaryColor, PrimaryColorSmooth, 16);
  tftPrint(-1, String(ServiceTypeText[radio.servicetype]) + " - " + AudioModeText[radio.audiomode], 163, ITEM9 + 6, PrimaryColor, PrimaryColorSmooth, 16);
}

void BuildChannelList(void) {
  setvolume = false;
  tft.pushImage (0, 0, 320, 240, servicelistbackground);
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
      tftPrint(1, "1/" + String(z), 310, 12, SecondaryColor, SecondaryColorSmooth, 16);
    } else if (y == 9) {
      tftPrint(1, "2/" + String(z), 310, 12, SecondaryColor, SecondaryColorSmooth, 16);
    } else if (y == 17) {
      tftPrint(1, "3/" + String(z), 310, 12, SecondaryColor, SecondaryColorSmooth, 16);
    } else if (y == 25) {
      tftPrint(1, "4/" + String(z), 310, 12, SecondaryColor, SecondaryColorSmooth, 16);
    }
  }


  tft.drawRoundRect(6, 35 + (20 * (radio.ServiceIndex - y)), 309, 21, 5, ActiveColor);

  for (byte i = y; i < radio.numberofservices; i++) {
    tftPrint(-1, String(radio.service[i].CompID & 0xFF, DEC), 14, 38 + (20 * (i - y)), SecondaryColor, SecondaryColorSmooth, 16);
    String serviceIDString = String(radio.service[i].ServiceID & 0xFFFF, HEX);
    serviceIDString.toUpperCase();
    tftPrint(-1, serviceIDString, 50, 38 + (20 * (i - y)), SecondaryColor, SecondaryColorSmooth, 16);
    tftPrint(-1, String(radio.ASCII(radio.service[i].Label)), 86, 38 + (20 * (i - y)), PrimaryColor, PrimaryColorSmooth, 16);
    if (i - y == 8) i = 254;
  }
}

void BuildMenu(void) {
  SlideShowView = false;
  ShowServiceInformation = false;
  ChannelListView = false;

  tft.pushImage (0, 0, 320, 240, configurationbackground);
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

void BuildDisplay(void) {
  SlideShowView = false;
  ShowServiceInformation = false;
  ChannelListView = false;
  displayreset = true;
  setvolume = false;
  rotary = 0;
  rotary2 = 0;

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

  ShowFreq();
  ShowTuneMode();
  ShowMemoryPos();
}

void MenuUp(void) {
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
        tftPrint(0, myLanguage[language][0], 155, 118, BackgroundColor3, BackgroundColor3, 28);
        language ++;
        if (language == (sizeof (myLanguage) / sizeof (myLanguage[0]))) language = 0;
        tftPrint(0, myLanguage[language][0], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM2:
        tftPrint(1, String(ContrastSet, DEC), 155, 118, BackgroundColor3, BackgroundColor3, 28);
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
        tft.fillRoundRect(12, 32, 296, 166, 5, BackgroundColor3);
        tftPrint(0, myLanguage[language][14], 155, 78, ActiveColor, ActiveColorSmooth, 28);
        tftPrint(0, Theme[CurrentTheme], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM4:
        if (autoslideshow) tftPrint(0, myLanguage[language][23], 155, 118, BackgroundColor3, BackgroundColor3, 28); else tftPrint(0, myLanguage[language][24], 155, 118, BackgroundColor3, BackgroundColor3, 28);
        if (autoslideshow) autoslideshow = false; else autoslideshow = true;
        if (autoslideshow) tftPrint(0, myLanguage[language][23], 155, 118, PrimaryColor, PrimaryColorSmooth, 28); else tftPrint(0, myLanguage[language][24], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM5:
        tftPrint(0, unitString[unit], 155, 118, BackgroundColor3, BackgroundColor3, 28);
        unit ++;
        if (unit > sizeof(unitString) / sizeof(unitString[0]) - 1) unit = 0;
        tftPrint(0, unitString[unit], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM6:
        if (tot != 0) tftPrint(-1, myLanguage[language][26], 170, 118, BackgroundColor3, BackgroundColor3, 28);
        if (tot != 0) tftPrint(1, String(tot), 155, 118, BackgroundColor3, BackgroundColor3, 28); else tftPrint(0, myLanguage[language][24], 155, 118, BackgroundColor3, BackgroundColor3, 28);
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
        if (wifi) tftPrint(0, myLanguage[language][23], 155, 118, BackgroundColor3, BackgroundColor3, 28); else tftPrint(0, myLanguage[language][24], 155, 118, BackgroundColor3, BackgroundColor3, 28);
        if (wifi) wifi = false; else wifi = true;
        if (wifi) tftPrint(0, myLanguage[language][23], 155, 118, PrimaryColor, PrimaryColorSmooth, 28); else tftPrint(0, myLanguage[language][24], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;
    }
  }
}

void MenuDown(void) {
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
        tftPrint(0, myLanguage[language][0], 155, 118, BackgroundColor3, BackgroundColor3, 28);
        language --;
        if (language > (sizeof (myLanguage) / sizeof (myLanguage[0]))) language = (sizeof (myLanguage) / sizeof (myLanguage[0])) - 1;
        tftPrint(0, myLanguage[language][0], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM2:
        tftPrint(1, String(ContrastSet, DEC), 155, 118, BackgroundColor3, BackgroundColor3, 28);
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
        tft.fillRoundRect(12, 32, 296, 166, 5, BackgroundColor3);
        tftPrint(0, myLanguage[language][14], 155, 78, ActiveColor, ActiveColorSmooth, 28);
        tftPrint(0, Theme[CurrentTheme], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM4:
        if (autoslideshow) tftPrint(0, myLanguage[language][23], 155, 118, BackgroundColor3, BackgroundColor3, 28); else tftPrint(0, myLanguage[language][24], 155, 118, BackgroundColor3, BackgroundColor3, 28);
        if (autoslideshow) autoslideshow = false; else autoslideshow = true;
        if (autoslideshow) tftPrint(0, myLanguage[language][23], 155, 118, PrimaryColor, PrimaryColorSmooth, 28); else tftPrint(0, myLanguage[language][24], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM5:
        tftPrint(0, unitString[unit], 155, 118, BackgroundColor3, BackgroundColor3, 28);
        unit --;
        if (unit > sizeof(unitString) / sizeof(unitString[0]) - 1) unit = sizeof(unitString) / sizeof(unitString[0]) - 1;
        tftPrint(0, unitString[unit], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;

      case ITEM6:
        if (tot != 0) tftPrint(-1, myLanguage[language][26], 170, 118, BackgroundColor3, BackgroundColor3, 28);
        if (tot != 0) tftPrint(1, String(tot), 155, 118, BackgroundColor3, BackgroundColor3, 28); else tftPrint(0, myLanguage[language][24], 155, 118, BackgroundColor3, BackgroundColor3, 28);
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
        if (wifi) tftPrint(0, myLanguage[language][23], 155, 118, BackgroundColor3, BackgroundColor3, 28); else tftPrint(0, myLanguage[language][24], 155, 118, BackgroundColor3, BackgroundColor3, 28);
        if (wifi) wifi = false; else wifi = true;
        if (wifi) tftPrint(0, myLanguage[language][23], 155, 118, PrimaryColor, PrimaryColorSmooth, 28); else tftPrint(0, myLanguage[language][24], 155, 118, PrimaryColor, PrimaryColorSmooth, 28);
        break;
    }
  }
}

void DoMenu(void) {
  if (!menuopen) {
    tft.drawRoundRect(10, 30, 300, 170, 5, ActiveColor);
    tft.fillRoundRect(12, 32, 296, 166, 5, BackgroundColor3);
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
        if (tot != 0) tftPrint(-1, myLanguage[language][26], 170, 118, ActiveColor, ActiveColorSmooth, 28);
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
    if (menuoption == ITEM7) tryWiFi();
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

void ShowFreq(void) {
  tftReplace(0, radio.getChannel(dabfreqold), radio.getChannel(dabfreq), 145, 45, PrimaryColor, PrimaryColorSmooth, BackgroundColor2, 28);
  tftReplace(-1, dabfreqStringOld, String(radio.getFreq(dabfreq) / 1000) + "." + (radio.getFreq(dabfreq) % 1000 < 100 ? "0" : "") + String(radio.getFreq(dabfreq) % 1000), 184, 43, SecondaryColor, SecondaryColorSmooth, BackgroundColor2, 52);
  dabfreqStringOld = String(radio.getFreq(dabfreq) / 1000) + "." + (radio.getFreq(dabfreq) % 1000 < 100 ? "0" : "") + String(radio.getFreq(dabfreq) % 1000);
  dabfreqold = dabfreq;
}

void ShowPTY(void) {
  if (!radio.ServiceStart) radio.pty = 36;
  if (radio.pty != ptyold || displayreset) {
    tftReplace(0, myLanguage[language][37 + ptyold], myLanguage[language][37 + radio.pty], 78, 162, SecondaryColor, SecondaryColorSmooth, BackgroundColor4, 16);
    ptyold = radio.pty;
  }
}

void ShowRT(void) {
  if (radio.ASCII(radio.ServiceData).length() > 0) {
    RTWidth = tft.textWidth(radio.ASCII(radio.ServiceData));

    if (RTWidth < 300) {
      xPos = 0;
      RadiotextSprite.pushImage (0, 0, 308, 19, rtbackground);
      RadiotextSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
      RadiotextSprite.setTextDatum(TC_DATUM);
      RadiotextSprite.drawString(String(radio.ASCII(radio.ServiceData)), 154, 2);
      RadiotextSprite.pushSprite(6, 219);
    } else {
      if (millis() - rtticker >= 20) {
        xPos --;
        rttickerhold = millis();

        if (xPos < -RTWidth - 50) xPos = 0;
        RadiotextSprite.pushImage (0, 0, 308, 19, rtbackground);
        RadiotextSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        RadiotextSprite.setTextDatum(TL_DATUM);
        RadiotextSprite.drawString(String(radio.ASCII(radio.ServiceData)), xPos, 2);
        RadiotextSprite.drawString(String(radio.ASCII(radio.ServiceData)), xPos + RTWidth + 50, 2);
        RadiotextSprite.pushSprite(6, 219);
        rtticker = millis();
      }
    }
  } else {
    if (ShowServiceInformation || ChannelListView) RadiotextSprite.fillSprite(BackgroundColor7); else RadiotextSprite.fillSprite(BackgroundColor6);
    RadiotextSprite.pushSprite(6, 219);
  }
  if (RTold != radio.ASCII(radio.ServiceData)) xPos = 0;
  RTold = radio.ASCII(radio.ServiceData);
}

void ShowSID(void) {
  if (!radio.ServiceStart) radio.SID[0] = '\0';
  if (String(radio.SID) != SIDold || displayreset) {
    tftReplace(0, SIDold, String(radio.SID), 55, 120 , SecondaryColor, SecondaryColorSmooth, BackgroundColor3, 16);
    SIDold = String(radio.SID);
  }
}

void ShowEID(void) {
  if (tuning) radio.EID[0] = '\0';
  if (String(radio.EID) != EIDold || displayreset) {
    tftReplace(0, EIDold, String(radio.EID), 55, 105 , SecondaryColor, SecondaryColorSmooth, BackgroundColor3, 16);
    EIDold = String(radio.EID);
  }
}

void ShowPS(void) {
  if (tunemode != TUNE_MEM && !radio.ServiceStart && !tuning && !seek) {
    if (radio.signallock) {
      strncpy(_serviceName, myLanguage[language][74], sizeof(_serviceName));
      _serviceName[sizeof(_serviceName) - 1] = '\0';
    } else {
      for (byte x = 0; x < 16; x++) _serviceName[x] = '\0';
    }
  } else if (tunemode != TUNE_MEM && !tuning) {
    strcpy(_serviceName, radio.service[radio.ServiceIndex].Label);
  }

  if ((radio.ServiceStart ? radio.ASCII(radio.service[radio.ServiceIndex].Label) : radio.ASCII(_serviceName)) != PSold || displayreset) {
    if (tunemode != TUNE_MEM || (tunemode == TUNE_MEM && String((radio.signallock && radio.ServiceStart ? radio.ASCII(radio.PStext) : radio.ASCII(_serviceName))).length() != 0)) {
      PSSprite.fillSprite(BackgroundColor5);
      PSSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
      PSSprite.drawString(String((radio.ServiceStart ? radio.ASCII(radio.PStext) : radio.ASCII(_serviceName))), 130, 0);
      PSSprite.pushSprite(44, 187);
    }
    PSold = (radio.ServiceStart ? radio.ASCII(radio.PStext) : radio.ASCII(_serviceName));
  }
}

void ShowEN(void) {
  if (tuning) {
    strncpy(radio.EnsembleLabel, myLanguage[language][75], sizeof(radio.EnsembleLabel));
    radio.EnsembleLabel[sizeof(radio.EnsembleLabel) - 1] = '\0';
  } else if (!radio.signallock) {
    strncpy(radio.EnsembleLabel, myLanguage[language][76], sizeof(radio.EnsembleLabel));
    radio.EnsembleLabel[sizeof(radio.EnsembleLabel) - 1] = '\0';
  }

  if (EnsembleNameOld != radio.ASCII(radio.EnsembleLabel) || displayreset) {
    tftReplace(0, EnsembleNameOld, radio.ASCII(radio.EnsembleLabel), 234, 162, SecondaryColor, SecondaryColorSmooth, BackgroundColor4, 16);
    EnsembleNameOld = radio.ASCII(radio.EnsembleLabel);
  }
  if (!radio.signallock || tuning) radio.EnsembleLabel[0] = '\0';
}

void ShowProtectionlevel(void) {
  if (!radio.ServiceStart) radio.protectionlevel = 0;
  if (String(ProtectionText[radio.protectionlevel]) != PLold || displayreset) {
    tftReplace(0, PLold, String(ProtectionText[radio.protectionlevel]), 38, 90, PrimaryColor, PrimaryColorSmooth, BackgroundColor3, 16);
    PLold = String(ProtectionText[radio.protectionlevel]);
  }
}

void ShowAudioMode(void) {
  if (!radio.ServiceStart) radio.servicetype = 9;
  if (servicetypeold != radio.servicetype || displayreset) {
    tftPrint(-1, ServiceTypeText[4], 67, 33, GreyoutColor, BackgroundColor, 16);
    if (radio.servicetype == 4 || radio.servicetype == 5) tftPrint(-1, ServiceTypeText[radio.servicetype], 67, 33, SecondaryColor, SecondaryColorSmooth, 16);
    servicetypeold = radio.servicetype;
  }

  if (!radio.ServiceStart) radio.audiomode = 4;
  if (audiomodeold != radio.audiomode || displayreset) {
    switch (radio.audiomode) {
      case 0: tft.pushImage(10, 4, 28, 19, mono); break;
      case 1: tft.pushImage(10, 4, 28, 19, mono); break;
      case 2:
      case 3: tft.pushImage(10, 4, 28, 19, stereoon); break;
      case 4: tft.pushImage(10, 4, 28, 19, stereooff); break;
    }
    audiomodeold = radio.audiomode;
  }
}

void ShowECC(void) {
  if (eccold != radio.ecc || displayreset) {
    String ITU = "";
    switch (radio.EID[0]) {
      case '1':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, de); ITU = "D"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, gr); ITU = "GRC"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, ma); ITU = "MRC"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, me); ITU = "MNE"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, md); ITU = "MDA"; break;
          default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
        }
        break;

      case '2':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, dz); ITU = "ALG"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, cy); ITU = "CYP"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, cz); ITU = "CZE"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, ie); ITU = "IRL"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, ee); ITU = "EST"; break;
          default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
        }
        break;

      case '3':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, ad); ITU = "AND"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, sm); ITU = "SM"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, pl); ITU = "POL"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, tr); ITU = "TUR"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, mk); ITU = "MKD"; break;
          default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
        }
        break;

      case '4':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, il); ITU = "ISR"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, ch); ITU = "SUI"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, va); ITU = "CVA"; break;
          default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
        }
        break;

      case '5':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, it); ITU = "I"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, jo); ITU = "JOR"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, sk); ITU = "SVK"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, tj); ITU = "TJK"; break;
          default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
        }
        break;

      case '6':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, be); ITU = "BEL"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, fi); ITU = "FNL"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, sy); ITU = "SYR"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, ua); ITU = "UKR"; break;
          default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
        }
        break;

      case '7':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, ru); ITU = "RUS"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, lu); ITU = "LUX"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, tn); ITU = "TUN"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, kz); ITU = "XXK"; break;
          default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
        }
        break;

      case '8':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, ra); ITU = "AZR"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, bg); ITU = "BUL"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, m1); ITU = "MDR"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, nl); ITU = "HOL"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, pt); ITU = "POR"; break;
          default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
        }
        break;

      case '9':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, al); ITU = "ALB"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, dk); ITU = "DNK"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, li); ITU = "LIE"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, lv); ITU = "LVA"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, si); ITU = "SVN"; break;
          default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
        }
        break;

      case 'A':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, at); ITU = "AUT"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, gi); ITU = "GIB"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, is); ITU = "ISL"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, lb); ITU = "LBN"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, am); ITU = "ARM"; break;
          default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
        }
        break;

      case 'B':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, hu); ITU = "HNG"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, iq); ITU = "IRQ"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, mc); ITU = "MCO"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, az); ITU = "AZE"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, uz); ITU = "UZB"; break;
          default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
        }
        break;

      case 'C':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, mt); ITU = "MLT"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, gb); ITU = "G"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, lt); ITU = "LTU"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, hr); ITU = "HRV"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, ge); ITU = "GEO"; break;
          default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
        }
        break;

      case 'D':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, de); ITU = "D"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, ly); ITU = "LBY"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, rs); ITU = "SRB"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, kz); ITU = "KAZ"; break;
          default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
        }
        break;

      case 'E':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, c1); ITU = "CNR"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, ro); ITU = "ROU"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, es); ITU = "E"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, se); ITU = "S"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, tm); ITU = "TKM"; break;
          default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
        }
        break;

      case 'F':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, eg); ITU = "EGY"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, fr); ITU = "F"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, no); ITU = "NOR"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, by); ITU = "BLR"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, ba); ITU = "BIH"; break;
          default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
        }
        break;
      default: tft.pushImage(80, 110, 36, 23, unknown); ITU = ""; break;
    }
    tftReplace(0, ITUold, ITU, 97, 140, SecondaryColor, SecondaryColorSmooth, BackgroundColor3, 16);
    eccold = radio.ecc;
    ITUold = ITU;
  }
}

void ShowMemoryPos(void) {
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
  tftReplace(-1, String(memoryposold + 1), String(memorypos + 1), 93, 65, memposcolor, memposcolorsmooth, BackgroundColor2, 16);
  memoryposold = memorypos;
}

void ShowVolume(void) {
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
  VolumeTimer = millis();
}

void ShowSignalLevel(void) {
  if (millis() >= rssiTimer + 100) {
    rssiTimer = millis();
    CNR = radio.cnr;
  }

  SAvg = (((SAvg * 9) + 5) / 10) + SignalLevel;
  SAvg2 = (((SAvg2 * 9) + 5) / 10) + CNR;

  SignalLevel = SAvg / 10;
  CNR = SAvg2 / 10;

  int SignalLevelprint = 0;
  if (unit == 0) SignalLevelprint = SignalLevel;
  if (unit == 1) SignalLevelprint = ((SignalLevel * 100) + 10875) / 100;
  if (unit == 2) SignalLevelprint = round((float(SignalLevel) / 10.0 - 10.0 * log10(75) - 90.0) * 10.0);

  if (!ShowServiceInformation) {
    if (SignalLevelprint > (SignalLevelold + 3) || SignalLevelprint < (SignalLevelold - 3) || displayreset) {
      SignalSprite.fillSprite(BackgroundColor3);
      SignalSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
      SignalSprite.drawString(String(SignalLevelprint / 10) + "." + String(abs(SignalLevelprint % 10)), 30, 0);
      SignalSprite.pushSprite(149, 109);

      byte segments = 0;
      if (SignalLevel > 120) segments = map(SignalLevel, 100, 700, 0, 85);
      tft.fillRect(134, 129, 2 * constrain(segments, 0, 56), 6, BarInsignificantColor);
      tft.fillRect(134 + 2 * 56, 129, 2 * (constrain(segments, 56, 85) - 56), 6, BarSignificantColor);
      tft.fillRect(134 + 2 * constrain(segments, 0, 85), 129, 2 * (85 - constrain(segments, 0, 85)), 6, GreyoutColor);
      SignalLevelold = SignalLevelprint;
    }

    if (CNRold != CNR || displayreset) {
      if (radio.signallock) {
        tftPrint(1, String("--"), 289, 109, BackgroundColor, BackgroundColor3, 16);
        tftReplace(1, String(CNRold), String(CNR), 289, 109, PrimaryColor, PrimaryColorSmooth, BackgroundColor3, 16);
      } else {
        tftReplace(1, String(CNRold), "--", 289, 109, PrimaryColor, PrimaryColorSmooth, BackgroundColor3, 16);
      }
      CNRold = CNR;
    }

    if (ficold != radio.fic || displayreset) {
      byte quality = map(radio.fic, 0, 100, 139, 0);
      for (byte x = 0; x < 10; x++) tft.pushImage (135, 91 + x, 139, 1, QualLine);
      tft.fillRect(274 - quality, 91, quality, 10, BackgroundColor3);
      tftReplace(1, String(ficold) + "%", String(radio.fic) + "%", 315, 90, PrimaryColor, PrimaryColorSmooth, BackgroundColor3, 16);
      ficold = radio.fic;
    }


  } else {
    String SignalLevelString = String(String(SignalLevelprint / 10)) + "." + String(abs(SignalLevelprint % 10));
    if ((SignalLevelString != SignalLeveloldString && !setvolume) || displayreset) {
      tftReplace(1, SignalLeveloldString, SignalLevelString, 188, ITEM2 + 6, PrimaryColor, PrimaryColorSmooth, BackgroundColor, 16);
      SignalLeveloldString = SignalLevelString;
    }

    if (((CNRold != CNR) && !setvolume) || displayreset) {
      tftReplace(1, String(CNRold), String(CNR), 276, ITEM2 + 6, PrimaryColor, PrimaryColorSmooth, BackgroundColor, 16);
      CNRold = CNR;
    }
  }
}

void ShowBitrate(void) {
  if (tuning) radio.bitrate = 0;
  if (radio.bitrate != BitrateOld || displayreset) {
    tftReplace(0, String (BitrateOld, DEC) + " kbit/s", (radio.bitrate != 0 && radio.ServiceStart && !tuning ? String (radio.bitrate, DEC) + " kbit/s" : ""), 39, 140, PrimaryColor, PrimaryColorSmooth, BackgroundColor3, 16);
    BitrateOld = radio.bitrate;
  }
}

void ShowClock(void) {
  if (radio.signallock) setTime(radio.Hours, radio.Minutes, radio.Seconds, radio.Days, radio.Months, radio.Year);
  String clockstring = (hour() < 10 ? "0" : "") + String(hour()) + ":" + (minute() < 10 ? "0" : "") + String(minute());
  String datestring = (day() < 10 ? "0" : "") + String(day()) + "-" + (month() < 10 ? "0" : "") + String(month()) + "-" + String(year());
  if (clockstringOld != clockstring || displayreset) {
    tftReplace(-1, clockstringOld, clockstring, 105, 8, ActiveColor, ActiveColorSmooth, BackgroundColor, 16);
    tftReplace(-1, datestringOld, datestring, 177, 8, ActiveColor, ActiveColorSmooth, BackgroundColor, 16);
    clockstringOld = clockstring;
    datestringOld = datestring;
  }
}

void ShowSlideShowIcon(void) {
  if (SlideShowAvailableOld != radio.SlideShowAvailable || displayreset) {
    if (radio.SlideShowAvailable) {
      tft.pushImage (10, 187, 30, 22, slideshowon);
    } else {
      tft.pushImage (10, 187, 30, 22, slideshowoff);
    }
    SlideShowAvailableOld = radio.SlideShowAvailable;
  }
}

void ShowTuneMode(void) {
  switch (tunemode) {
    case TUNE_MAN:
      tftPrint(0, "MAN", 27, 33, ActiveColor, ActiveColorSmooth, 16);
      tftPrint(0, "AUTO", 27, 49, SecondaryColor, SecondaryColorSmooth, 16);
      tftPrint(0, "MEM", 27, 65, SecondaryColor, SecondaryColorSmooth, 16);
      break;

    case TUNE_AUTO:
      tftPrint(0, "MAN", 27, 33, SecondaryColor, SecondaryColorSmooth, 16);
      tftPrint(0, "AUTO", 27, 49, ActiveColor, ActiveColorSmooth, 16);
      tftPrint(0, "MEM", 27, 65, SecondaryColor, SecondaryColorSmooth, 16);
      break;

    case TUNE_MEM:
      tftPrint(0, "MAN", 27, 33, SecondaryColor, SecondaryColorSmooth, 16);
      tftPrint(0, "AUTO", 27, 49, SecondaryColor, SecondaryColorSmooth, 16);

      if (memorystore) {
        tftPrint(0, "MEM", 27, 65, SignificantColor, SignificantColorSmooth, 16);
      } else {
        tftPrint(0, "MEM", 27, 65, ActiveColor, ActiveColorSmooth, 16);
      }
      break;
  }
  EEPROM.writeByte(EE_BYTE_TUNEMODE, tunemode);
  EEPROM.commit();
}

void ShowRSSI(void) {
  if (wifi) rssi = WiFi.RSSI(); else rssi = 0;
  if (rssiold != rssi || displayreset) {
    if (rssi == 0) {
      tft.pushImage(290, 4, 24, 19, WiFi4);
    } else if (rssi > -50) {
      tft.pushImage(290, 4, 24, 19, WiFi0);
    } else if (rssi > -60) {
      tft.pushImage(290, 4, 24, 19, WiFi1);
    } else if (rssi > -70) {
      tft.pushImage(290, 4, 24, 19, WiFi2);
    } else if (rssi < -70) {
      tft.pushImage(290, 4, 24, 19, WiFi3);
    }
    rssiold = rssi;
  }
}
#endif