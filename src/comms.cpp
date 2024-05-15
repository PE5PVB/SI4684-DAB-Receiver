#include "comms.h"

void Communication(void) {
}

void tryWiFi(void) {
  if (!setupmode && wifi && WiFi.status() != WL_CONNECTED) {
    tft.drawRoundRect(1, 60, 319, 140, 5, ActiveColor);
    tft.fillRoundRect(3, 62, 315, 136, 5, BackgroundColor3);
    loadFonts(true);
    tftPrint(0, myLanguage[language][69], 155, 88, ActiveColor, ActiveColorSmooth, 28);
    loadFonts(false);
  }

  if (wifi) {
    if (WiFi.status() != WL_CONNECTED) {
      if (wc.autoConnect()) {
        Server.begin();
        loadFonts(true);
        if (!setupmode) tftPrint(0, myLanguage[language][71], 155, 128, InsignificantColor, InsignificantColorSmooth, 28);
        loadFonts(false);
      } else {
        loadFonts(true);
        if (!setupmode) tftPrint(0, myLanguage[language][70], 155, 128, SignificantColor, SignificantColorSmooth, 28);
        loadFonts(false);
        Server.end();
        WiFi.disconnect();
        WiFi.mode(WIFI_OFF);
        wifi = false;
      }
      if (!setupmode) delay(2000);
    }
  } else {
    Server.end();
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
  }
  loadFonts(true);
}