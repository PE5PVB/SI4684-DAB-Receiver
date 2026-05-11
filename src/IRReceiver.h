// IR remote receiver: decodes NEC-style codes and dispatches them to the
// existing button/encoder handlers so the remote behaves like the front panel.

#ifndef IRRECEIVER_H
#define IRRECEIVER_H

#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

void IRReceiver();        // Poll for new IR code; call every loop iteration
void IRReceiverBegin();   // Initialise IR receiver hardware; call once from setup()

// Forward declarations of front-panel handlers that the IR codes map to.
// Definitions live in SI4684-DAB-Receiver.ino.
void KeyUp();
void KeyDown();
void KeyUp2();
void KeyDown2();
void SlideShowButtonPress();
void ButtonPress();
void Button2Press();
void doStandby();

#endif
