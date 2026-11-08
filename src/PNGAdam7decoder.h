#pragma once
#include <stdint.h>
#include <stddef.h>

// Adam7-interlaced PNG decoder for /slideshow.img. The PNGdec library used
// elsewhere in this project does not reliably decode interlaced PNGs on
// ESP32, so stations that transmit Adam7 slideshow images render as black or
// garbled output. This decoder re-uses PNGdec's embedded zlib engine but runs
// the Adam7 pass logic itself, streaming each decoded pixel row to the TFT
// through a caller-supplied row callback.
//
// The caller provides an aligned DRAM workspace (76800 bytes is enough) so
// the decoder does not allocate any heap memory of its own. Pass a null row
// callback to validate the input without drawing anything.
//
// Supported PNG colour types: 0 (grayscale), 2 (RGB), 3 (palette), 4 (gray+alpha),
// 6 (RGBA). Supported bit depths: 1, 2, 4, 8, 16. tRNS, palette transparency
// and all five standard filter predictors (none/sub/up/avg/paeth) are handled.

struct PngAdam7Info {
  unsigned    width   = 0;
  unsigned    height  = 0;
  unsigned    depth   = 0;
  unsigned    type    = 0;
  const char* stage   = "entry";
  const char* detail  = "";
  unsigned    pass    = 0;
  unsigned    row     = 0;
  unsigned    input   = 0;
  unsigned    output  = 0;
  unsigned    padding = 0;
};

// Callback invoked for each decoded pass-row. Return false to abort the decode.
//   x, y   : origin of the row within the final image
//   step   : horizontal pass stride (so one callback emits pixels at columns x, x+step, x+2*step, ...)
//   pixels : RGB565 line buffer, `count` entries
typedef bool (*PngAdam7Row)(void* context, unsigned x, unsigned y, unsigned step,
                            const uint16_t* pixels, unsigned count);

// Optional pump invoked at safe points inside the decoder (every chunk + every
// row). Used by slideshow.cpp to service the SI4684 DSRV queue so MOT segments
// are not lost while decoding a large image.
typedef void (*PngAdam7Pump)(void* context);

// Minimum aligned DRAM required to decode any supported image (320x240 max).
size_t PNGAdam7WorkspaceBytes(void);

// Decode a PNG image from a contiguous memory buffer. Returns true when every
// Adam7 pass completed, the inflate stream ended cleanly and all declared IDAT
// bytes were consumed. On failure `info.stage` indicates the step that failed.
bool PNGAdam7decoder(const uint8_t* data, size_t size,
                     void* workspace, size_t capacity,
                     unsigned maxWidth, unsigned maxHeight,
                     PngAdam7Info& info,
                     PngAdam7Row rowCallback = nullptr,
                     void* context = nullptr,
                     PngAdam7Pump pump = nullptr);
