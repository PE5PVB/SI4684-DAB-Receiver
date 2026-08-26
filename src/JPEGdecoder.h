#pragma once
#include <TFT_eSPI.h>

// Decode a JPEG (baseline or progressive) directly from RAM and render it.
// Uses multi-pass row-by-row decoding to minimize RAM usage (~30KB).
// Supports up to 320x240 pixels, YCbCr 4:4:4, 4:2:2, 4:2:0, and non-standard subsampling.
// Returns true on success.
bool JPEGdecoder(const uint8_t* data, size_t size, TFT_eSPI& tft,
                 int displayWidth = 320, int displayHeight = 240);
