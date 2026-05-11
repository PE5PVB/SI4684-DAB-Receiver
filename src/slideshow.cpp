// Renders the assembled MOT image (/slideshow.img) on the TFT.
// Format detection is done by looking at the magic bytes:
//   FF D8 FF ...                  → JPEG (further check: SOF0 baseline vs SOF2 progressive)
//   89 50 4E 47 0D 0A 1A 0A       → PNG
// Both decoders run synchronously; the screen fades down before redraw
// and fades back up afterwards to avoid a hard flicker.
//
// The PNG decoder is callback-based, so `pngfile` is a file-scope global
// referenced by the read/seek lambdas passed to PNGdec.

#include "slideshow.h"

File pngfile;                       // shared file handle used by PNGdec callbacks
PNG png;

// Walks the JPEG markers to find the Start-Of-Frame:
// 0xC0 = baseline, 0xC2 = progressive. Other markers are skipped using
// their length field. Returns false on any parse error.
static bool isProgressiveJPEG(void) {
  File f = LittleFS.open("/slideshow.img", "rb");
  if (!f) return false;
  f.seek(2);  // skip SOI
  while (f.available() >= 4) {
    uint8_t b = f.read();
    if (b != 0xFF) break;
    while (b == 0xFF && f.available()) b = f.read();
    if (b == 0xC0) { f.close(); return false; }  // SOF0 = baseline
    if (b == 0xC2) { f.close(); return true; }   // SOF2 = progressive
    if (b == 0xD9) break;  // EOI
    if (f.available() >= 2) {
      uint8_t lh = f.read();
      uint8_t ll = f.read();
      uint16_t len = (lh << 8) | ll;
      if (len < 2) break;
      f.seek(f.position() + len - 2);
    }
  }
  f.close();
  return false;
}

// Ramp the backlight to zero with ~5 ms steps to hide the image change.
// Blocking by design — we want a clean visible fade.
static void fadeDown(void) {
  for (int x = ContrastSet; x > 0; x--) {
    analogWrite(CONTRASTPIN, x * 2);
    delay(5);
  }
  analogWrite(CONTRASTPIN, 0);
}

// Ramp the backlight back up to the user-selected ContrastSet value.
static void fadeUp(void) {
  for (int x = 0; x <= ContrastSet; x++) {
    analogWrite(CONTRASTPIN, x * 2 + 27);
    delay(5);
  }
}

// Entry point: opens /slideshow.img, sniffs the format, and dispatches to
// the appropriate decoder. No-op if the file isn't readable.
void ShowSlideShow(void) {
  if (radio.SlideShowDebug) Serial.println("[SLS] ShowSlideShow() called");
  File file = LittleFS.open("/slideshow.img", "r");
  if (!file) { if (radio.SlideShowDebug) Serial.println("[SLS] Failed to open slideshow.img"); return; }
  size_t fileSize = file.size();
  byte header[8];
  file.read(header, sizeof(header));
  file.close();

  bool isJPG = (header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF);
  bool isPNG = (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47 &&
                header[4] == 0x0D && header[5] == 0x0A && header[6] == 0x1A && header[7] == 0x0A);

  if (radio.SlideShowDebug) Serial.printf("[SLS] Display: size=%u, isJPG=%d, isPNG=%d, hdr=%02X%02X%02X%02X\n",
    fileSize, isJPG, isPNG, header[0], header[1], header[2], header[3]);

  if (isJPG && isProgressiveJPEG()) {
    if (radio.SlideShowDebug) Serial.println("[SLS] Decoding as progressive JPEG");
    fadeDown();
    tft.fillScreen(TFT_BLACK);
    fadeUp();
    tft.startWrite();
    bool ok = JPEGdecoder("/slideshow.img", tft);
    tft.endWrite();
    if (radio.SlideShowDebug) Serial.printf("[SLS] Progressive decode result: %s\n", ok ? "OK" : "FAIL");
  } else if (isJPG) {
    if (radio.SlideShowDebug) Serial.println("[SLS] Decoding as baseline JPEG");
    fadeDown();
    tft.fillScreen(TFT_BLACK);
    tft.startWrite();
    bool ok = JPEGdecoder("/slideshow.img", tft);
    tft.endWrite();
    if (radio.SlideShowDebug) Serial.printf("[SLS] Baseline decode result: %s\n", ok ? "OK" : "FAIL");
    fadeUp();
  } else if (isPNG) {
    // PNG: fade down, decode hidden, fade up
    fadeDown();
    pngfile = LittleFS.open("/slideshow.img", "rb");
    if (!pngfile) {
      fadeUp();
      return;
    }
    int16_t rc = png.open("/slideshow.img",
      +[](const char *filename, int32_t *size) -> void * {
        *size = pngfile.size();
        return &pngfile;
      },
      +[](void *handle) {
      },
      +[](PNGFILE *page, uint8_t *buffer, int32_t length) -> int32_t {
        if (!pngfile) return 0;
        return pngfile.read(buffer, length);
      },
      +[](PNGFILE *page, int32_t position) -> int32_t {
        if (!pngfile) return 0;
        return pngfile.seek(position);
      },
      +[](PNGDRAW *pDraw) {
        static uint32_t pngBkgd;
        pngBkgd = png.hasAlpha() ? 0x00FFFFFF : 0xFFFFFFFF;
        uint16_t lineBuffer[320];
        png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, pngBkgd);
        tft.pushImage((320 - png.getWidth()) / 2, ((240 - png.getHeight()) / 2) + pDraw->y, pDraw->iWidth, 1, lineBuffer);
        return 1;
      });

    if (rc != PNG_SUCCESS) {
      pngfile.close();
      fadeUp();
      return;
    }

    tft.fillScreen(png.hasAlpha() ? TFT_WHITE : TFT_BLACK);
    tft.startWrite();
    rc = png.decode(nullptr, 0);
    png.close();
    tft.endWrite();
    pngfile.close();
    fadeUp();
  }
}
