#ifndef TPA6130A2_H
#define TPA6130A2_H

#include "Arduino.h"

class TPA6130A2 {
  public:
    byte Init();
    void SetVolume(byte vol);
    void SetMute(bool mute);
    void SetHiZ(bool hiz);
    void Shutdown();
    byte GetVolume();
    byte GetMute();

  private:
    byte GetValue(byte reg);
};
#endif