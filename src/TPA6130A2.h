#ifndef TPA6130A2_H
#define TPA6130A2_H

#include "Arduino.h"
#include <Wire.h>

class TPA6130A2 {
  public:
    byte Init(void);
    void SetVolume(byte vol);
    void SetMute(bool mute);
    void SetHiZ(bool hiz);
    void Shutdown(void);
    byte GetVolume(void);
    byte GetMute(void);

  private:
    byte GetValue(byte reg);
};

#endif