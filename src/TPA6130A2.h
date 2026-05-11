// TPA6130A2: Texas Instruments stereo headphone amplifier driven over I2C.
// We use it to amplify the SI4684's line-level audio output and to provide
// soft volume control + mute/hi-Z + shutdown for power saving.

#ifndef TPA6130A2_H
#define TPA6130A2_H

#include "Arduino.h"
#include <Wire.h>

class TPA6130A2 {
  public:
    byte Init(void);             // Wire.begin() + apply default register state; returns chip status byte
    void SetVolume(byte vol);    // 0..63 maps to the amp's gain register (see datasheet)
    void SetMute(bool mute);     // Mute both channels without changing the volume setting
    void SetHiZ(bool hiz);       // High-impedance outputs (silent, click-free)
    void Shutdown(void);         // Full power-down for standby
    byte GetVolume(void);
    byte GetMute(void);

  private:
    byte GetValue(byte reg);     // I2C register read helper
};

#endif