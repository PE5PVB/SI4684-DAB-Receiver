// V16.6.4 diagnostic slideshow renderer.
// Adds structural JPEG diagnostics around the existing RAM decoder path.

#include "slideshow.h"

PNG png;

static bool inspectJPEG(const uint8_t* data, uint32_t size, bool &progressive,
                        uint16_t &jpegW, uint16_t &jpegH) {
  jpegW = jpegH = 0;
  progressive = false;
  if (!data || size < 4 || data[0] != 0xFF || data[1] != 0xD8) {
    Serial.println("[SLS/JPEG] invalid SOI");
    return false;
  }

  Serial.printf("[SLS/JPEG] SOI OK size=%u\n", size);
  uint32_t p = 2;
  bool sawSOF = false;
  bool sawSOS = false;

  while (p + 1 < size) {
    // After scan data we cannot parse arbitrary 0xFF bytes as normal markers.
    // We only need the pre-SOS structure for decoder diagnostics.
    if (sawSOS) break;

    if (data[p] != 0xFF) {
      Serial.printf("[SLS/JPEG] malformed marker prefix at %u: %02X\n", p, data[p]);
      return false;
    }
    while (p < size && data[p] == 0xFF) ++p;
    if (p >= size) return false;

    uint8_t marker = data[p++];
    if (marker == 0xD8) continue;
    if (marker == 0xD9) {
      Serial.printf("[SLS/JPEG] early EOI at %u\n", p - 2);
      break;
    }

    // Stand-alone restart/TEM markers have no length.
    if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;

    if (p + 1 >= size) {
      Serial.printf("[SLS/JPEG] truncated length for marker FF%02X\n", marker);
      return false;
    }
    uint16_t len = ((uint16_t)data[p] << 8) | data[p + 1];
    if (len < 2 || p + len > size) {
      Serial.printf("[SLS/JPEG] bad marker FF%02X len=%u at %u\n", marker, len, p - 1);
      return false;
    }

    if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
      if (len < 8) {
        Serial.printf("[SLS/JPEG] SOF too short len=%u\n", len);
        return false;
      }
      uint16_t height = ((uint16_t)data[p + 3] << 8) | data[p + 4];
      uint16_t width  = ((uint16_t)data[p + 5] << 8) | data[p + 6];
      jpegW = width;
      jpegH = height;
      uint8_t comps = data[p + 7];
      progressive = (marker == 0xC2);
      sawSOF = true;
      Serial.printf("[SLS/JPEG] SOF marker=FF%02X %ux%u components=%u progressive=%u\n",
                    marker, width, height, comps, progressive);
    } else if (marker == 0xDA) {
      sawSOS = true;
      Serial.printf("[SLS/JPEG] SOS at %u len=%u\n", p - 1, len);
    } else if (marker == 0xE0 || marker == 0xE1 || marker == 0xDB ||
               marker == 0xC4 || marker == 0xDD || marker == 0xFE) {
      Serial.printf("[SLS/JPEG] marker FF%02X len=%u\n", marker, len);
    }

    p += len;
  }

  bool eoi = size >= 2 && data[size - 2] == 0xFF && data[size - 1] == 0xD9;
  Serial.printf("[SLS/JPEG] structure SOF=%u SOS=%u EOI=%u\n", sawSOF, sawSOS, eoi);
  return sawSOF && sawSOS && eoi;
}

static void fadeDown(void) {
  for (int x = ContrastSet; x > 0; --x) {
    analogWrite(CONTRASTPIN, x * 2);
    delay(5);
  }
  analogWrite(CONTRASTPIN, 0);
}

static void fadeUp(void) {
  for (int x = 0; x <= ContrastSet; ++x) {
    analogWrite(CONTRASTPIN, x * 2 + 27);
    delay(5);
  }
}


static bool baselineDecoderPreflight(const uint8_t* data, uint32_t size) {
  if (!data || size < 4 || data[0] != 0xFF || data[1] != 0xD8) {
    Serial.println("[SLS/PREFLIGHT] FAIL SOI");
    return false;
  }

  uint32_t p = 2;
  uint16_t width = 0, height = 0;
  uint8_t maxH = 0, maxV = 0, components = 0;
  uint8_t hSamp[3] = {0}, vSamp[3] = {0};
  bool haveSOF = false;

  while (p + 1 < size) {
    while (p < size && data[p] != 0xFF) p++;
    if (p >= size) break;
    while (p < size && data[p] == 0xFF) p++;
    if (p >= size) break;
    uint8_t marker = data[p++];

    if (marker == 0xD9) {
      Serial.println("[SLS/PREFLIGHT] FAIL EOI before SOS");
      return false;
    }
    if (marker >= 0xD0 && marker <= 0xD7) continue;
    if (marker == 0x01) continue;

    if (p + 1 >= size) {
      Serial.println("[SLS/PREFLIGHT] FAIL marker length missing");
      return false;
    }
    uint16_t len = ((uint16_t)data[p] << 8) | data[p + 1];
    if (len < 2 || p + len > size) {
      Serial.printf("[SLS/PREFLIGHT] FAIL marker FF%02X len=%u p=%u size=%u\n",
                    marker, len, (unsigned)p, (unsigned)size);
      return false;
    }

    const uint32_t seg = p + 2;
    const uint32_t segEnd = p + len;

    if (marker == 0xC0) { // SOF0
      if (len < 8) {
        Serial.println("[SLS/PREFLIGHT] FAIL SOF length");
        return false;
      }
      uint8_t precision = data[seg];
      height = ((uint16_t)data[seg + 1] << 8) | data[seg + 2];
      width  = ((uint16_t)data[seg + 3] << 8) | data[seg + 4];
      components = data[seg + 5];
      Serial.printf("[SLS/PREFLIGHT] SOF precision=%u size=%ux%u comp=%u\n",
                    precision, width, height, components);
      if (precision != 8) {
        Serial.println("[SLS/PREFLIGHT] FAIL SOF precision != 8");
        return false;
      }
      if (components == 0 || components > 3 || len < (uint16_t)(8 + 3 * components)) {
        Serial.println("[SLS/PREFLIGHT] FAIL SOF components/length");
        return false;
      }
      maxH = maxV = 0;
      for (uint8_t c = 0; c < components; c++) {
        uint8_t samp = data[seg + 7 + c * 3];
        hSamp[c] = (samp >> 4) & 0x0F;
        vSamp[c] = samp & 0x0F;
        if (hSamp[c] > maxH) maxH = hSamp[c];
        if (vSamp[c] > maxV) maxV = vSamp[c];
        Serial.printf("[SLS/PREFLIGHT] comp%u samp=%ux%u qt=%u\n",
                      c, hSamp[c], vSamp[c], data[seg + 8 + c * 3]);
      }
      haveSOF = true;
    }
    else if (marker == 0xDB) { // DQT
      uint32_t q = seg;
      while (q < segEnd) {
        uint8_t info = data[q++];
        uint8_t idx = info & 0x0F;
        uint8_t prec = info >> 4;
        if (idx > 3 || prec > 1) {
          Serial.printf("[SLS/PREFLIGHT] FAIL DQT info=%02X\n", info);
          return false;
        }
        uint32_t bytes = prec ? 128 : 64;
        if (q + bytes > segEnd) {
          Serial.println("[SLS/PREFLIGHT] FAIL DQT truncated");
          return false;
        }
        q += bytes;
      }
    }
    else if (marker == 0xC4) { // DHT
      uint32_t q = seg;
      while (q < segEnd) {
        if (q + 17 > segEnd) {
          Serial.println("[SLS/PREFLIGHT] FAIL DHT header truncated");
          return false;
        }
        uint8_t info = data[q++];
        uint8_t cls = info >> 4;
        uint8_t idx = info & 0x0F;
        if (cls > 1 || idx > 3) {
          Serial.printf("[SLS/PREFLIGHT] FAIL DHT info=%02X\n", info);
          return false;
        }
        uint16_t total = 0;
        for (int i = 0; i < 16; i++) total += data[q++];
        if (total > 256 || q + total > segEnd) {
          Serial.printf("[SLS/PREFLIGHT] FAIL DHT symbols=%u\n", total);
          return false;
        }
        q += total;
      }
    }
    else if (marker == 0xDA) { // SOS
      if (!haveSOF || len < 6) {
        Serial.println("[SLS/PREFLIGHT] FAIL SOS before SOF/short");
        return false;
      }
      uint8_t scanComp = data[seg];
      Serial.printf("[SLS/PREFLIGHT] SOS comp=%u len=%u\n", scanComp, len);
      if (scanComp == 0 || scanComp > 3 || len < (uint16_t)(6 + 2 * scanComp)) {
        Serial.println("[SLS/PREFLIGHT] FAIL SOS components/length");
        return false;
      }

      if (maxH == 0 || maxV == 0) {
        Serial.println("[SLS/PREFLIGHT] FAIL invalid sampling");
        return false;
      }
      uint16_t mcuW = maxH * 8;
      uint16_t mcuH = maxV * 8;
      uint16_t mcuCntX = (width + mcuW - 1) / mcuW;
      uint16_t mcuCntY = (height + mcuH - 1) / mcuH;
      uint16_t blocksPerMCU = 0;
      for (uint8_t c = 0; c < components; c++) blocksPerMCU += hSamp[c] * vSamp[c];
      uint32_t blocksPerRow = (uint32_t)mcuCntX * blocksPerMCU;
      uint32_t alloc = blocksPerRow * 64u * 3u; // 2 bytes coeff + 1 byte pixels
      Serial.printf("[SLS/PREFLIGHT] MCU=%ux%u grid=%ux%u blocksMCU=%u rowAlloc=%u heapFree=%u maxBlock=%u\n",
                    mcuW, mcuH, mcuCntX, mcuCntY, blocksPerMCU,
                    (unsigned)alloc,
                    (unsigned)ESP.getFreeHeap(),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
      if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < alloc) {
        Serial.println("[SLS/PREFLIGHT] FAIL row buffer would not fit");
        return false;
      }
      Serial.println("[SLS/PREFLIGHT] PASS all pre-entropy decoder checks");
      return true;
    }

    p += len;
  }

  Serial.println("[SLS/PREFLIGHT] FAIL no SOS");
  return false;
}

void ShowSlideShow(void) {
  Serial.println("[SLS] ShowSlideShow() called");

  const uint8_t* image = radio.slideshowData();
  const uint32_t fileSize = radio.slideshowSize();
  Serial.printf("[SLS] image ptr=%p size=%u available=%u update=%u\n",
                image, fileSize, radio.SlideShowAvailable, radio.SlideShowUpdate);

  if (!image || fileSize < 8) {
    Serial.println("[SLS] display aborted: no complete image");
    return;
  }

  bool isJPG = image[0] == 0xFF && image[1] == 0xD8 && image[2] == 0xFF;
  bool isPNG = image[0] == 0x89 && image[1] == 0x50 && image[2] == 0x4E &&
               image[3] == 0x47 && image[4] == 0x0D && image[5] == 0x0A &&
               image[6] == 0x1A && image[7] == 0x0A;

  Serial.printf("[SLS] Display: size=%u isJPG=%u isPNG=%u hdr=%02X %02X %02X %02X tail=%02X %02X\n",
                fileSize, isJPG, isPNG,
                image[0], image[1], image[2], image[3],
                image[fileSize - 2], image[fileSize - 1]);

  if (isJPG) {
    bool progressive = false;
    uint16_t jpegW = 0, jpegH = 0;
    bool structureOK = inspectJPEG(image, fileSize, progressive, jpegW, jpegH);
    Serial.printf("[SLS/JPEG] predecode structure=%s type=%s size=%ux%u\n",
                  structureOK ? "OK" : "FAIL",
                  progressive ? "progressive" : "baseline/non-progressive",
                  jpegW, jpegH);

    fadeDown();
    tft.fillScreen(TFT_BLACK);

    // The original RAM decoder is reliable when its output coordinates stay
    // non-negative. For images taller than the TFT (e.g. DAB 320x320),
    // its default centering would calculate offsetY=-40. Some TFT_eSPI paths
    // then produce a black frame. Keep the original decoder unchanged and
    // center-crop only the oversized dimension by shifting the TFT origin.
    //
    // This is intentionally a conservative V17.1 step: no alternate JPEG
    // decoder, no colour conversion changes and no scaling callback.
    int32_t originX = 0;
    int32_t originY = 0;
    int decoderW = 320;
    int decoderH = 240;

    if (jpegW > 320) {
      originX = -(int32_t)(jpegW - 320) / 2;
      decoderW = jpegW;
    }
    if (jpegH > 240) {
      originY = -(int32_t)(jpegH - 240) / 2;
      decoderH = jpegH;
    }

    Serial.printf("[SLS/JPEG] original decoder canvas=%dx%d origin=%ld,%ld mode=%s\n",
                  decoderW, decoderH, (long)originX, (long)originY,
                  (jpegW > 320 || jpegH > 240) ? "CENTER-CROP" : "1:1");

    tft.setOrigin(originX, originY);
    if (!progressive) baselineDecoderPreflight(image, fileSize);
    tft.startWrite();
    bool ok = JPEGdecoder(image, fileSize, tft, decoderW, decoderH);
    tft.endWrite();
    tft.setOrigin(0, 0);

    Serial.printf("[SLS/JPEG] JPEGdecoder result=%s\n", ok ? "OK" : "FAIL");
    fadeUp();
    return;
  }

  if (isPNG) {
    fadeDown();
    int16_t rc = png.openRAM(const_cast<uint8_t*>(image), fileSize,
      +[](PNGDRAW *pDraw) {
        static uint32_t pngBkgd;
        pngBkgd = png.hasAlpha() ? 0x00FFFFFF : 0xFFFFFFFF;
        uint16_t lineBuffer[320];
        png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, pngBkgd);
        tft.pushImage((320 - png.getWidth()) / 2,
                      ((240 - png.getHeight()) / 2) + pDraw->y,
                      pDraw->iWidth, 1, lineBuffer);
        return 1;
      });

    Serial.printf("[SLS/PNG] openRAM rc=%d\n", rc);
    if (rc != PNG_SUCCESS) {
      fadeUp();
      return;
    }

    Serial.printf("[SLS/PNG] dimensions=%dx%d alpha=%u\n",
                  png.getWidth(), png.getHeight(), png.hasAlpha());
    tft.fillScreen(png.hasAlpha() ? TFT_WHITE : TFT_BLACK);
    tft.startWrite();
    rc = png.decode(nullptr, 0);
    tft.endWrite();
    Serial.printf("[SLS/PNG] decode rc=%d\n", rc);
    png.close();
    fadeUp();
    return;
  }

  Serial.println("[SLS] unsupported/invalid image signature");
}
