// Slideshow rendering: takes the assembled MOT image at /slideshow.img on
// LittleFS and pushes it to the TFT, choosing the right decoder (PNG or
// baseline/progressive JPEG) based on the magic bytes.

#ifndef SLIDESHOW_H
#define SLIDESHOW_H

#include "Arduino.h"
#include <FS.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <PNGdec.h>                 // https://github.com/bitbank2/PNGdec
#include "JPEGdecoder.h"
#include "si4684.h"
#include "constants.h"

extern byte ContrastSet;            // backlight level used during fade-in/out

extern TFT_eSPI tft;
extern DAB radio;

// Decode and display /slideshow.img; called from the main loop when
// radio.SlideShowAvailable && radio.SlideShowUpdate are set.
void ShowSlideShow(void);

#endif