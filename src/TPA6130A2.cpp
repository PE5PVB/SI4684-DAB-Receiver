#ifndef TPA6130A2_CPP
#define TPA6130A2_CPP

#include "tpa6130a2.h"
#include <Wire.h>

byte TPA6130A2::Init() {
  Wire.begin();
  byte x = GetValue(0x02);
  bitWrite(x, 6, 0);
  bitWrite(x, 7, 0);
  Wire.beginTransmission(0x60);
  Wire.write(0x01);
  Wire.write(0xc0);
  Wire.write(x);
  Wire.endTransmission();
  if (GetValue(0x04) == 0x02) return 1; else return 0;
}

void TPA6130A2::SetVolume(byte vol) {
  if (vol > 62) vol = 63;
  byte x = GetValue(0x02);
  bitWrite(vol, 6, bitRead(x, 6));
  bitWrite(vol, 7, bitRead(x, 7));
  Wire.beginTransmission(0x60);
  Wire.write(0x02);
  Wire.write(vol);
  Wire.endTransmission();
}

void TPA6130A2::SetMute(bool mute) {
  byte x = GetValue(0x02);
  bitWrite(x, 6, mute);
  bitWrite(x, 7, mute);
  Wire.beginTransmission(0x60);
  Wire.write(0x02);
  Wire.write(x);
  Wire.endTransmission();
}

void TPA6130A2::SetHiZ(bool hiz) {
  Wire.beginTransmission(0x60);
  Wire.write(0x03);
  if (hiz == true) Wire.write(0x03); else Wire.write(0x03);
  Wire.endTransmission();
}

void TPA6130A2::Shutdown() {
  byte x = GetValue(0x01);
  bitWrite(x, 0, 1);
  Wire.beginTransmission(0x60);
  Wire.write(0x01);
  Wire.write(x);
  Wire.endTransmission();
}

byte TPA6130A2::GetVolume() {
  byte x = GetValue(0x02);
  bitWrite(x, 6, 0);
  bitWrite(x, 7, 0);
  return x;
}

byte TPA6130A2::GetMute() {
  byte x = GetValue(0x02);
  return bitRead(x, 6);
}

byte TPA6130A2::GetValue(byte reg) {
  Wire.beginTransmission(0x60);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(0x60, 1);
  byte x = Wire.read();
  return x;
}
#endif