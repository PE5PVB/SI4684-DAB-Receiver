#ifndef COMMS_CPP
#define COMMS_CPP
#include "comms.h"
#include "language.h"
#include "constants.h"
#include <EEPROM.h>

void Communication() {
}

void tryWiFi() {
  if (!setupmode && wifi) {
    tft.drawRoundRect(1, 60, 319, 140, 5, ActiveColor);
    tft.fillRoundRect(3, 62, 315, 136, 5, BackgroundColor3);
    tftPrint(0, myLanguage[language][69], 155, 88, ActiveColor, ActiveColorSmooth, 28);
  }
  if (wifi) {
    if (wc.autoConnect()) {
      Server.begin();
      remoteip = IPAddress (WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], subnetclient);
      if (!setupmode) tftPrint(0, myLanguage[language][71], 155, 128, InsignificantColor, InsignificantColorSmooth, 28);
    } else {
      if (!setupmode) tftPrint(0, myLanguage[language][70], 155, 128, SignificantColor, SignificantColorSmooth, 28);
      Server.end();
      WiFi.disconnect();
      WiFi.mode(WIFI_OFF);
      wifi = false;
    }
  } else {
    Server.end();
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
  }
}
#endif