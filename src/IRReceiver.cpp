#include "IRReceiver.h"

uint16_t RECV_PIN = 12;
IRrecv irrecv(RECV_PIN);
decode_results results;
uint32_t lastCode = 0;

void IRReceiverBegin() {
  irrecv.enableIRIn();
}

void IRReceiver() {
 if (irrecv.decode(&results)) {
    uint32_t code = results.value;

    Serial.println(code ,HEX);

   if (code == 0xFFFFFFFF) {
        if (lastCode == 0x77E1604F || lastCode == 0x77E1E04E || lastCode == 0xFF5AA5 || lastCode == 0x77E1904F || lastCode == 0x77E1104E || lastCode == 0xFF10EF) {
            code = lastCode;
        } else {
            code = 0;  
        }
    } else {
        lastCode = code;
    }
    switch (code) {
      case 0x77E1504F:
      case 0x77E1D04E:
      case 0xFF18E7:   // Up_Key
        KeyUp();
        break;
      case 0x77E1304F:
      case 0x77E1B04E:
      case 0xFF4AB5:   // Down_Key
        KeyDown();
        break;
      case 0x77E1604F:
      case 0x77E1E04E:
      case 0xFF5AA5:   // RightKey
        KeyUp2();
        break;
      case 0x77E1904F:
      case 0x77E1104E:
      case 0xFF10EF:   // Left_Key
        KeyDown2();
        break;
      case 0x77E13A4F:
      case 0x77E1BA4E:
      case 0xFF38C7:   // OK_Key
        SlideShowButtonPress();
        break;
      case 0x77E1C04F:
      case 0x77E1404E:
      case 0xFF6897:   // Menu_Key
        ButtonPress();
        break;
      case 0x77E1FA4F:
      case 0x77E17A4E:
      case 0xFFB04F:   // Play/Pause_Key
        Button2Press();
        break;
      default:
        break;
    }
    irrecv.resume();  // Receive the next value
  }
}
