// RAM-only slideshow renderer.

#include "slideshow.h"
#include "png_adam7.h"
#include "serial_monitor.h"

#include <esp_heap_caps.h>
#include <new>

// DAB SlideShow Simple Profile limits (ETSI TS 101 499): one JPEG/PNG image
// up to 50 KiB and display support through 320 x 240. Single-scan baseline and
// Huffman-coded progressive JPEG are decoded from the same fixed workspace.
static constexpr size_t SLS_PROFILE_MAX_FILE_BYTES = 50U * 1024U;
static constexpr uint16_t SLS_PROFILE_MAX_WIDTH = 320U;
static constexpr uint16_t SLS_PROFILE_MAX_HEIGHT = 240U;

// ISO/IEC 10918 sequential interleaved scans are limited to 10 DCT blocks per
// MCU.  The largest row workspace for a supported 320-pixel baseline image is
// therefore 40 minimum-width MCUs * 10 blocks * 64 samples *
// (int16 coefficient + uint8 pixel) = 76,800 bytes.  PNGdec stores its zlib
// and line workspace inside the PNG object.  JPEG and PNG never decode at the
// same time, so a single early persistent arena can be shared by both.
static constexpr size_t JPEG_MAX_BLOCKS_PER_MCU = 10U;
static constexpr size_t JPEG_MIN_MCU_WIDTH = 8U;
static constexpr size_t JPEG_MAX_MCUS_PER_ROW =
    (SLS_PROFILE_MAX_WIDTH + JPEG_MIN_MCU_WIDTH - 1U) / JPEG_MIN_MCU_WIDTH;
static constexpr size_t JPEG_BYTES_PER_BLOCK =
    64U * (sizeof(int16_t) + sizeof(uint8_t));
static constexpr size_t JPEG_BASELINE_ROW_WORKSPACE =
    JPEG_MAX_MCUS_PER_ROW * JPEG_MAX_BLOCKS_PER_MCU * JPEG_BYTES_PER_BLOCK;
static constexpr size_t SLS_DECODER_WORKSPACE_BYTES =
    JPEG_BASELINE_ROW_WORKSPACE > sizeof(PNG)
        ? JPEG_BASELINE_ROW_WORKSPACE
        : sizeof(PNG);

static_assert(JPEG_BASELINE_ROW_WORKSPACE == 76800U,
              "Unexpected JPEG baseline workspace calculation");
static uint8_t* decoderWorkspace = nullptr;
static bool decoderWorkspaceAttempted = false;
static bool lastRenderTouchedDisplay = false;

bool SlideshowLastRenderTouchedDisplay(void) {
  return lastRenderTouchedDisplay;
}

static uint32_t slideshowFingerprint(const uint8_t* data, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; data && i < size; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

static void pumpRadioDuringImageDecode(void*) {
  // Only complete the current transport operation. The slideshow buffer stays
  // published/locked and no new metadata or DSRV command is scheduled here.
  radio.Update(false);
  yield();
}

bool SlideshowPrepareWorkspace(void) {
  if (decoderWorkspace) return true;
  if (decoderWorkspaceAttempted) return false;
  decoderWorkspaceAttempted = true;

  decoderWorkspace = static_cast<uint8_t*>(heap_caps_malloc(
      SLS_DECODER_WORKSPACE_BYTES,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

  if (!decoderWorkspace) {
    DIAG_PRINTF("[SLS/WS] shared decoder workspace allocation FAILED (%u bytes)\n",
                  static_cast<unsigned>(SLS_DECODER_WORKSPACE_BYTES));
    return false;
  }

  DIAG_PRINTF("[SLS/WS] shared decoder workspace=%u bytes PNG-object=%u addr=%p "
                "free=%u largest=%u\n",
                static_cast<unsigned>(SLS_DECODER_WORKSPACE_BYTES),
                static_cast<unsigned>(sizeof(PNG)),
                decoderWorkspace,
                ESP.getFreeHeap(),
                heap_caps_get_largest_free_block(
                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  return true;
}

static PNG* acquirePngDecoder(bool& inSharedWorkspace) {
  if (!decoderWorkspace || sizeof(PNG) > SLS_DECODER_WORKSPACE_BYTES) {
    inSharedWorkspace = false;
    DIAG_PRINTLN("[SLS/PNG] shared decoder workspace unavailable");
    return nullptr;
  }
  inSharedWorkspace = true;
  return new (decoderWorkspace) PNG;
}

static void releasePngDecoder(PNG* decoder, bool inSharedWorkspace) {
  if (decoder && inSharedWorkspace) decoder->~PNG();
}

static void fadeDown(void) {
  for (int x = ContrastSet; x > 0; --x) {
    analogWrite(CONTRASTPIN, x * 2);
    delay(5);
    radio.Update(false);
  }
  analogWrite(CONTRASTPIN, 0);
}

static void fadeUp(void) {
  for (int x = 0; x <= ContrastSet; ++x) {
    analogWrite(CONTRASTPIN, x * 2 + 27);
    delay(5);
    radio.Update(false);
  }
}


struct Adam7DrawContext { int x,y; };
static void pumpAdam7(void*) { radio.Update(false); yield(); }
static bool adam7Pixels(void* user,unsigned x,unsigned y,unsigned step,const uint16_t* pixels,unsigned count) {
  const Adam7DrawContext& context=*static_cast<Adam7DrawContext*>(user);
  if(step==1) tft.pushImage(context.x+int(x),context.y+int(y),int(count),1,const_cast<uint16_t*>(pixels));
  else for(unsigned i=0;i<count;++i) tft.pushImage(context.x+int(x+i*step),context.y+int(y),1,1,const_cast<uint16_t*>(pixels+i));
  return true;
}
static bool validateAdam7(epg::Bytes data,size_t size,unsigned width,unsigned height,PngAdam7Info& info) {
  return PngAdam7Decode(data,size,decoderWorkspace,SLS_DECODER_WORKSPACE_BYTES,width,height,info,nullptr,nullptr,pumpAdam7);
}
static bool drawAdam7(epg::Bytes data,size_t size,int x,int y,unsigned width,unsigned height) {
  Adam7DrawContext context{x,y};PngAdam7Info info;
  return PngAdam7Decode(data,size,decoderWorkspace,SLS_DECODER_WORKSPACE_BYTES,width,height,info,adam7Pixels,&context,pumpAdam7);
}

bool ShowSlideShow(void) {
  lastRenderTouchedDisplay = false;
  if (diagnosticDebug) DIAG_PRINTLN("[SLS] ShowSlideShow() called");

  // The decoder arena is mandatory and is reserved once during setup().
  if (!decoderWorkspace) {
    DIAG_PRINTLN("[SLS] display aborted: decoder workspace unavailable");
    return false;
  }

  const uint8_t* image = radio.slideshowData();
  const uint32_t fileSize = radio.slideshowSize();
  if (diagnosticDebug)
    DIAG_PRINTF("[SLS] image ptr=%p size=%u available=%u update=%u\n",
                  image, fileSize, radio.SlideShowAvailable, radio.SlideShowUpdate);

  if (!image || fileSize < 8) {
    DIAG_PRINTLN("[SLS] display aborted: no complete image");
    return false;
  }
  if (fileSize > SLS_PROFILE_MAX_FILE_BYTES) {
    DIAG_PRINTF("[SLS] image exceeds Simple Profile limit size=%u max=%u\n",
                  static_cast<unsigned>(fileSize),
                  static_cast<unsigned>(SLS_PROFILE_MAX_FILE_BYTES));
    return false;
  }

  bool isJPG = image[0] == 0xFF && image[1] == 0xD8 && image[2] == 0xFF;
  bool isPNG = image[0] == 0x89 && image[1] == 0x50 && image[2] == 0x4E &&
               image[3] == 0x47 && image[4] == 0x0D && image[5] == 0x0A &&
               image[6] == 0x1A && image[7] == 0x0A;

  if (diagnosticDebug)
    DIAG_PRINTF("[SLS] Display: size=%u isJPG=%u isPNG=%u hdr=%02X %02X %02X %02X tail=%02X %02X\n",
                  fileSize, isJPG, isPNG,
                  image[0], image[1], image[2], image[3],
                  image[fileSize - 2], image[fileSize - 1]);

  if (isJPG) {
    JPEGImageInfo info;
    const JPEGPreflightResult preflight = JPEGpreflight(
        image, fileSize, SLS_PROFILE_MAX_WIDTH, SLS_PROFILE_MAX_HEIGHT, info);
    const uint32_t fingerprint = slideshowFingerprint(image, fileSize);
    DIAG_PRINTF("[SLS/JPEG] tid=%u size=%u hash=%08X coding=%s dimensions=%ux%u scale=1/%u components=%u sampling=%ux%u scans=%u dri=%u rst=%u\n",
                  static_cast<unsigned>(radio.slideshowTransportId()),
                  static_cast<unsigned>(fileSize),
                  static_cast<unsigned>(fingerprint),
                  JPEGpreflightName(preflight),
                  static_cast<unsigned>(info.width),
                  static_cast<unsigned>(info.height),
                  static_cast<unsigned>(info.scaleDivisor),
                  static_cast<unsigned>(info.components),
                  static_cast<unsigned>(info.maxHorizontalSampling),
                  static_cast<unsigned>(info.maxVerticalSampling),
                  static_cast<unsigned>(info.scans),
                  static_cast<unsigned>(info.restartInterval),
                  static_cast<unsigned>(info.restartMarkers));
    if (preflight != JPEGPreflightResult::SupportedBaseline &&
        preflight != JPEGPreflightResult::SupportedProgressive) return false;

    // Validate the complete entropy stream before changing the visible frame.
    // The published MOT buffer remains locked until acknowledgeSlideshow().
    if (!JPEGvalidate(image, fileSize, SLS_PROFILE_MAX_WIDTH,
                      SLS_PROFILE_MAX_HEIGHT, decoderWorkspace,
                      SLS_DECODER_WORKSPACE_BYTES, &info,
                      pumpRadioDuringImageDecode, nullptr,
                      radio.SlideShowVerbose)) {
      DIAG_PRINTF("[SLS/JPEG] validation=FAIL tid=%u size=%u hash=%08X lastMCURow=%d\n",
                    static_cast<unsigned>(radio.slideshowTransportId()),
                    static_cast<unsigned>(fileSize),
                    static_cast<unsigned>(fingerprint),
                    static_cast<int>(info.lastRenderedMcuRow));
      return false;
    }

    lastRenderTouchedDisplay = true;
    fadeDown();
    tft.fillScreen(TFT_BLACK);
    tft.startWrite();
    bool ok = JPEGdecoder(image, fileSize, tft, SLS_PROFILE_MAX_WIDTH,
                          SLS_PROFILE_MAX_HEIGHT,
                          decoderWorkspace,
                          decoderWorkspace ? SLS_DECODER_WORKSPACE_BYTES : 0,
                          &info, pumpRadioDuringImageDecode, nullptr);
    tft.endWrite();

    DIAG_PRINTF("[SLS/JPEG] render=%s size=%u lastMCURow=%d\n",
                  ok ? "OK" : "FAIL", static_cast<unsigned>(fileSize),
                  static_cast<int>(info.lastRenderedMcuRow));
    fadeUp();
    return ok;
  }

  if (isPNG) {
    // PNG IHDR starts at byte 8 and stores width/height as big-endian uint32
    // values at offsets 16 and 20.  Reject unsupported dimensions before the
    // decoder touches the image.  Simple Profile permits larger slides to be
    // ignored by a 320 x 240 receiver.
    if (fileSize < 24U) {
      DIAG_PRINTLN("[SLS/PNG] truncated IHDR");
      return false;
    }
    const uint32_t pngHeaderWidth =
        (static_cast<uint32_t>(image[16]) << 24) |
        (static_cast<uint32_t>(image[17]) << 16) |
        (static_cast<uint32_t>(image[18]) << 8) |
        static_cast<uint32_t>(image[19]);
    const uint32_t pngHeaderHeight =
        (static_cast<uint32_t>(image[20]) << 24) |
        (static_cast<uint32_t>(image[21]) << 16) |
        (static_cast<uint32_t>(image[22]) << 8) |
        static_cast<uint32_t>(image[23]);
    if (pngHeaderWidth == 0U || pngHeaderHeight == 0U ||
        pngHeaderWidth > SLS_PROFILE_MAX_WIDTH ||
        pngHeaderHeight > SLS_PROFILE_MAX_HEIGHT) {
      DIAG_PRINTF("[SLS/PNG] unsupported dimensions=%ux%u max=%ux%u\n",
                    static_cast<unsigned>(pngHeaderWidth),
                    static_cast<unsigned>(pngHeaderHeight),
                    static_cast<unsigned>(SLS_PROFILE_MAX_WIDTH),
                    static_cast<unsigned>(SLS_PROFILE_MAX_HEIGHT));
      return false;
    }

    if(fileSize>=29 && image[28]==1) {
      PngAdam7Info info;
      if(!validateAdam7(image,fileSize,SLS_PROFILE_MAX_WIDTH,SLS_PROFILE_MAX_HEIGHT,info)) {
        DIAG_PRINTLN("[SLS/PNG] Adam7 validation=FAIL");return false;
      }
      lastRenderTouchedDisplay=true;fadeDown();tft.fillScreen(TFT_WHITE);tft.startWrite();
      const bool ok=drawAdam7(image,fileSize,(SLS_PROFILE_MAX_WIDTH-int(info.width))/2,
                              (SLS_PROFILE_MAX_HEIGHT-int(info.height))/2,SLS_PROFILE_MAX_WIDTH,SLS_PROFILE_MAX_HEIGHT);
      tft.endWrite();fadeUp();DIAG_PRINTF("[SLS/PNG] Adam7 render=%s size=%u\n",ok?"OK":"FAIL",unsigned(fileSize));return ok;
    }
    const uint32_t fingerprint = slideshowFingerprint(image, fileSize);
    bool pngInSharedWorkspace = false;
    PNG* png = acquirePngDecoder(pngInSharedWorkspace);
    if (!png) return false;

    // First pass validates the complete PNG without drawing. Reopen the same
    // immutable RAM object for the render pass only after decode succeeds.
    int16_t rc = png->openRAM(const_cast<uint8_t*>(image), fileSize,
      +[](PNGDRAW*) {
        radio.Update(false);
        yield();
        return 1;
      });
    if (diagnosticDebug) DIAG_PRINTF("[SLS/PNG] validation openRAM rc=%d\n", rc);
    if (rc != PNG_SUCCESS) {
      releasePngDecoder(png, pngInSharedWorkspace);
      DIAG_PRINTF("[SLS/PNG] tid=%u size=%u hash=%08X result=INVALID_PNG_OPEN\n",
                    static_cast<unsigned>(radio.slideshowTransportId()),
                    static_cast<unsigned>(fileSize),
                    static_cast<unsigned>(fingerprint));
      return false;
    }
    const int pngWidth = png->getWidth();
    const int pngHeight = png->getHeight();
    if (pngWidth <= 0 || pngWidth > SLS_PROFILE_MAX_WIDTH ||
        pngHeight <= 0 || pngHeight > SLS_PROFILE_MAX_HEIGHT) {
      DIAG_PRINTF("[SLS/PNG] unsupported dimensions=%dx%d\n",
                    pngWidth, pngHeight);
      png->close();
      releasePngDecoder(png, pngInSharedWorkspace);
      return false;
    }
    rc = png->decode(png, 0);
    png->close();
    if (rc != PNG_SUCCESS) {
      DIAG_PRINTF("[SLS/PNG] tid=%u size=%u hash=%08X dimensions=%dx%d result=INVALID_PNG_DECODE rc=%d\n",
                    static_cast<unsigned>(radio.slideshowTransportId()),
                    static_cast<unsigned>(fileSize),
                    static_cast<unsigned>(fingerprint), pngWidth, pngHeight, rc);
      releasePngDecoder(png, pngInSharedWorkspace);
      return false;
    }

    rc = png->openRAM(const_cast<uint8_t*>(image), fileSize,
      +[](PNGDRAW *pDraw) {
        if (!pDraw || !pDraw->pUser ||
            pDraw->iWidth <= 0 || pDraw->iWidth > SLS_PROFILE_MAX_WIDTH) {
          return 0;
        }

        PNG* decoder = static_cast<PNG*>(pDraw->pUser);
        uint32_t pngBkgd = decoder->hasAlpha() ? 0x00FFFFFF : 0xFFFFFFFF;
        uint16_t lineBuffer[SLS_PROFILE_MAX_WIDTH];
        decoder->getLineAsRGB565(pDraw, lineBuffer,
                                 PNG_RGB565_LITTLE_ENDIAN, pngBkgd);
        tft.pushImage((SLS_PROFILE_MAX_WIDTH - decoder->getWidth()) / 2,
                      ((SLS_PROFILE_MAX_HEIGHT - decoder->getHeight()) / 2) + pDraw->y,
                      pDraw->iWidth, 1, lineBuffer);
        radio.Update(false);
        yield();
        return 1;
      });

    if (diagnosticDebug) DIAG_PRINTF("[SLS/PNG] render openRAM rc=%d\n", rc);
    if (rc != PNG_SUCCESS) {
      releasePngDecoder(png, pngInSharedWorkspace);
      return false;
    }

    if (diagnosticDebug)
      DIAG_PRINTF("[SLS/PNG] dimensions=%dx%d alpha=%u\n",
                    pngWidth, pngHeight, png->hasAlpha());
    lastRenderTouchedDisplay = true;
    fadeDown();
    tft.fillScreen(png->hasAlpha() ? TFT_WHITE : TFT_BLACK);
    tft.startWrite();
    rc = png->decode(png, 0);
    tft.endWrite();
    DIAG_PRINTF("[SLS/PNG] render=%s size=%u\n",
                  rc == PNG_SUCCESS ? "OK" : "FAIL",
                  static_cast<unsigned>(fileSize));
    png->close();
    releasePngDecoder(png, pngInSharedWorkspace);
    fadeUp();
    return rc == PNG_SUCCESS;
  }

  DIAG_PRINTLN("[SLS] unsupported/invalid image signature");
  return false;
}

// Small EPG logos use the existing decoder arena. No slideshow ownership,
// fades, full-screen clear, cache flag or MOT collector is changed.
struct EpgLogoFile { epg::Bytes data; int32_t size; };
static EpgLogoFile* epgLogoFile=nullptr;
static void* epgLogoOpen(const char*,int32_t* size) {
  if(!epgLogoFile || !size) return nullptr;
  *size=epgLogoFile->size; return epgLogoFile;
}
static void epgLogoClose(void*) {}
static int32_t epgLogoRead(PNGFILE* file,uint8_t* out,int32_t count) {
  if(!file || !file->fHandle || !out || count<0 || file->iPos<0 || file->iPos>file->iSize) return 0;
  EpgLogoFile& input=*static_cast<EpgLogoFile*>(file->fHandle);
  if(count>file->iSize-file->iPos) count=file->iSize-file->iPos;
  (input.data+size_t(file->iPos)).copyTo(out,size_t(count)); file->iPos+=count;
  return count;
}
static int32_t epgLogoSeek(PNGFILE* file,int32_t position) {
  if(!file || position<0 || position>file->iSize) return -1;
  file->iPos=position; return position;
}
bool DrawEpgLogo(epg::Bytes data, size_t size, int x, int y, int width, int height) {
  if(!data || !size || size>epg::kObjectBytes || epgLogoFile) return false;
  if(size>=29 && data[28]==1) {
    PngAdam7Info info;
    const bool valid=width>0 && height>0 && validateAdam7(data,size,unsigned(width),unsigned(height),info);
    if(diagSerial.availableForWrite()>=384) diagSerial.printf("[EPG/LOGO] bytes=%u interlace=1 validated=%u stage=%s detail=%s w=%u h=%u in=%u out=%u pass=%u row=%u pad=%u\n",unsigned(size),valid,info.stage,info.detail,info.width,info.height,info.input,info.output,info.pass,info.row,info.padding);
    if(!valid) return false;
    tft.startWrite();const bool ok=drawAdam7(data,size,x,y,unsigned(width),unsigned(height));tft.endWrite();
    if(diagSerial.availableForWrite()>=64) diagSerial.printf("[EPG/LOGO] draw=%u\n",ok);
    return ok;
  }
  EpgLogoFile input{data,int32_t(size)};
  struct InputGuard { ~InputGuard() { epgLogoFile=nullptr; } } inputGuard;
  epgLogoFile=&input;
  bool shared=false; PNG* png=acquirePngDecoder(shared);
  if(!png) return false;
  // Validate the complete compressed image before touching TFT, using the same
  // existing PNG decoder (including radio/CTS service between scanlines).
  int validated=png->open("EPG",epgLogoOpen,epgLogoClose,epgLogoRead,epgLogoSeek,+[](PNGDRAW*) {
    radio.Update(false); yield(); return 1;
  });
  if(validated!=PNG_SUCCESS) { releasePngDecoder(png,shared); return false; }
  const bool fits=png->getWidth()<=width && png->getHeight()<=height;
  if(fits) validated=png->decode(nullptr,0);
  png->close();
  if(!fits || validated!=PNG_SUCCESS) { releasePngDecoder(png,shared); return false; }
  struct Context { PNG* png; int x,y,width,height; } context{png,x,y,width,height};
  const int rc=png->open("EPG",epgLogoOpen,epgLogoClose,epgLogoRead,epgLogoSeek,+[](PNGDRAW* draw) {
    if(!draw || !draw->pUser) return 0;
    Context& c=*static_cast<Context*>(draw->pUser);
    if(draw->y>=c.height || draw->iWidth>320 || draw->iWidth<=0) return 0;
    uint16_t line[320];
    c.png->getLineAsRGB565(draw,line,PNG_RGB565_LITTLE_ENDIAN,0xFFFFFFFF);
    tft.pushImage(c.x,c.y+draw->y,draw->iWidth,1,line);
    radio.Update(false); yield(); return 1;
  });
  bool ok=false;
  if(rc==PNG_SUCCESS) {
    if(png->getWidth()<=width && png->getHeight()<=height) ok=png->decode(&context,0)==PNG_SUCCESS;
    png->close();
  }
  releasePngDecoder(png,shared); return ok;
}
