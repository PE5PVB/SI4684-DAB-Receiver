#ifndef IRRECEIVER_H
#define IRRECEIVER_H

#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

void IRReceiver();
void IRReceiverBegin();

void KeyUp();
void KeyDown();
void KeyUp2();
void KeyDown2();
void SlideShowButtonPress();
void ButtonPress();
void Button2Press();
void doStandby();

#endif
