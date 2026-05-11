// Driver for the TPA6130A2 stereo headphone amp (I2C address 0x60).
// Register map (per the TI datasheet):
//   0x01 - Control: bit0 = software shutdown
//   0x02 - Volume / mute: bits 5..0 = gain, bit6 = mute R, bit7 = mute L
//   0x03 - Output impedance / config
//   0x04 - Status (read-only); 0x02 indicates an OK power-on state

#include "TPA6130A2.h"

// Powers the amp up, clears the mute bits, and verifies the chip is responsive.
// Returns 1 on success, 0 if the chip didn't respond as expected.
byte TPA6130A2::Init(void) {
  Wire.begin();
  byte x = GetValue(0x02);
  bitWrite(x, 6, 0);                  // clear mute R
  bitWrite(x, 7, 0);                  // clear mute L
  Wire.beginTransmission(0x60);
  Wire.write(0x01);
  Wire.write(0xc0);                   // enable both channels, exit shutdown
  Wire.write(x);
  Wire.endTransmission();
  if (GetValue(0x04) == 0x02) return 1; else return 0;
}

// Set the gain register. The TPA's volume range is 0..63; values >62 are clamped to 63.
// Mute bits (6,7) of register 0x02 are preserved so SetMute() state survives a SetVolume() call.
void TPA6130A2::SetVolume(byte vol) {
  if (vol > 62) vol = 63;
  byte x = GetValue(0x02);
  bitWrite(vol, 6, bitRead(x, 6));    // preserve mute R
  bitWrite(vol, 7, bitRead(x, 7));    // preserve mute L
  Wire.beginTransmission(0x60);
  Wire.write(0x02);
  Wire.write(vol);
  Wire.endTransmission();
}

// Mute or un-mute both channels without changing the gain setting.
void TPA6130A2::SetMute(bool mute) {
  byte x = GetValue(0x02);
  bitWrite(x, 6, mute);
  bitWrite(x, 7, mute);
  Wire.beginTransmission(0x60);
  Wire.write(0x02);
  Wire.write(x);
  Wire.endTransmission();
}

// Forces the outputs into a high-impedance state, eliminating audible pops
// when the host (here: SI4684) starts/stops streaming audio.
// NOTE: both branches currently write the same value (0x03) — preserved
// intentionally so that the call still re-asserts the register even though
// the runtime parameter is ignored.
void TPA6130A2::SetHiZ(bool hiz) {
  Wire.beginTransmission(0x60);
  Wire.write(0x03);
  if (hiz == true) Wire.write(0x03); else Wire.write(0x03);
  Wire.endTransmission();
}

// Software shutdown (lowest-power state). Audio output stays silent until
// Init() is called again.
void TPA6130A2::Shutdown(void) {
  byte x = GetValue(0x01);
  bitWrite(x, 0, 1);                  // set software-shutdown bit
  Wire.beginTransmission(0x60);
  Wire.write(0x01);
  Wire.write(x);
  Wire.endTransmission();
}

// Returns the current gain bits (0..63) with the mute bits stripped out.
byte TPA6130A2::GetVolume(void) {
  byte x = GetValue(0x02);
  bitWrite(x, 6, 0);
  bitWrite(x, 7, 0);
  return x;
}

// Returns 1 if the right-channel mute bit is set (left mirrors right in this driver).
byte TPA6130A2::GetMute(void) {
  byte x = GetValue(0x02);
  return bitRead(x, 6);
}

// I2C register read: sends the register address, then reads back one byte.
byte TPA6130A2::GetValue(byte reg) {
  Wire.beginTransmission(0x60);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(0x60, 1);
  byte x = Wire.read();
  return x;
}
