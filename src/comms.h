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
#include "constants.h"
#include "si4684.h"
#include "mbedtls/base64.h"
#include <LittleFS.h>

extern bool ChannelListView;
extern bool menu;
extern bool ShowServiceInformation;
extern bool SlideShowView;
extern bool store;
extern byte dabfreq;
extern char _serviceName[17];
extern int16_t SignalLevel;

extern DAB radio;

void Communication(void);

extern void ShowFreq(void);
extern void BuildDisplay(void);
#endif