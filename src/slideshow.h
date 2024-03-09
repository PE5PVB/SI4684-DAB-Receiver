#ifndef slideshow_h
#define slideshow_h
#include <LittleFS.h>
#include "Arduino.h"
#include <TFT_eSPI.h>
#include <JPEGDecoder.h>            // https://github.com/Bodmer/JPEGDecoder
#include <PNGdec.h>                 // https://github.com/bitbank2/PNGdec
#include "si4684.h"

extern TFT_eSPI tft;
extern DAB radio;

void ShowSlideShow(void);

#endif