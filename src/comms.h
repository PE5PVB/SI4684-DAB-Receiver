#ifndef COMMS_H
#define COMMS_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFiClient.h>
#include "WiFiConnect.h"
#include "WiFiConnectParam.h"
#include "language.h"
#include "constants.h"

extern int SignificantColor;
extern int SignificantColorSmooth;
extern byte language;
extern bool setupmode;
extern int InsignificantColor;
extern int InsignificantColorSmooth;
extern byte subnetclient;
extern bool wifi;
extern int ActiveColor;
extern int ActiveColorSmooth;
extern int BackgroundColor3;


extern TFT_eSPI tft;
extern WiFiClient RemoteClient;
extern WiFiServer Server;
extern WiFiConnect wc;

void Communication(void);
void tryWiFi(void);

extern void tftPrint(int8_t offset, const String & text, int16_t x, int16_t y, int color, int smoothcolor, uint8_t fontsize);
extern void loadFonts(bool option);

#endif