// Serial control protocol: lets a PC / external app drive the radio
// (tune, switch service, query state) via newline-terminated text commands
// such as "TUNE=5", "SERVICE=2", "ENABLE=1". State updates are streamed back
// as "$L=...", "$I=...", "$S=..." lines while connectedSerial is true.
//
// Communication() is called once per loop iteration; it parses any inbound
// line (non-blocking, 100 ms readUntil timeout) and pushes status diffs out.

#ifndef COMMS_H
#define COMMS_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "language.h"
#include "constants.h"
#include "si4684.h"
#include "mbedtls/base64.h"
#include <LittleFS.h>

extern bool ChannelListView;
extern bool menu;
extern bool ShowServiceInformation;
extern bool SlideShowView;
extern bool store;
extern bool wifi;
extern byte dabfreq;
extern byte language;
extern byte subnetclient;
extern char _serviceName[17];
extern int ActiveColor;
extern int ActiveColorSmooth;
extern int BackgroundColor3;
extern int InsignificantColor;
extern int InsignificantColorSmooth;
extern int SignificantColor;
extern int SignificantColorSmooth;
extern int16_t SignalLevel;

extern DAB radio;
extern TFT_eSPI tft;

void Communication(void);
static char hashCommand(String command);
static void DataPrint(String data);
static String ServiceList(void);
static String ServiceInfo(void);
static void doEnableConnection(void);
static void doMOTShow(void);
static void handleCommunication(void);
static void outputCommunication(void);

extern void tftPrint(int8_t offset, const String & text, int16_t x, int16_t y, int color, int smoothcolor, uint8_t fontsize);
extern void loadFonts(bool option);
extern void ShowFreq(void);
extern void BuildDisplay(void);
#endif