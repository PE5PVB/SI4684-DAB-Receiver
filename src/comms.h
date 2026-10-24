#ifndef COMMS_H
#define COMMS_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "language.h"
#include "constants.h"
#include "si4684.h"

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
extern void ShowFreq(void);
extern void BuildDisplay(void);

#endif
