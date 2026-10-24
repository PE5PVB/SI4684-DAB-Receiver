// Slideshow rendering: takes the assembled MOT image from radio RAM and
// pushes it to the TFT, choosing the supported PNG, single-scan baseline JPEG
// or Huffman-coded progressive JPEG path.

#ifndef SLIDESHOW_H
#define SLIDESHOW_H

#include "Arduino.h"
#include "epg_core.h"
#include <TFT_eSPI.h>
#include <PNGdec.h>                 // https://github.com/bitbank2/PNGdec
#include "JPEGdecoder.h"
#include "si4684.h"
#include "constants.h"

extern byte ContrastSet;            // backlight level used during fade-in/out

extern TFT_eSPI tft;
extern DAB radio;

// Reserve the persistent shared PNG/JPEG decoder workspace once. Call after
// the radio/MOT and TFT runtime allocations are established. Failure is
// non-fatal: the decoders keep their compatibility fallback paths.
bool SlideshowPrepareWorkspace(void);

// Decode and display the current RAM image; called from the main loop when
// radio.SlideShowAvailable && radio.SlideShowUpdate are set.
bool ShowSlideShow(void);
bool DrawEpgLogo(epg::Bytes data, size_t size, int x, int y, int width, int height);
// True only when the most recent ShowSlideShow() attempt began modifying TFT.
bool SlideshowLastRenderTouchedDisplay(void);

#endif
