#include "JPEGdecoder.h"
#include "constants.h"
#include <cstdlib>
#include <cstring>

#define PJ_MAX_COMPONENTS 4
#define PJ_MAX_HTABLES    4

class MemoryFile {
 public:
  MemoryFile(const uint8_t* data, size_t size)
      : data_(data), size_(size), position_(0), valid_(data != nullptr && size != 0) {}
  explicit operator bool() const { return data_ != nullptr && size_ != 0; }
  int read() {
    if (position_ < size_) return data_[position_++];
    valid_ = false;
    return -1;
  }
  bool seek(size_t position) {
    if (position > size_) { valid_ = false; return false; }
    position_ = position;
    valid_ = true;
    return true;
  }
  size_t position() const { return position_; }
  bool valid() const { return valid_; }
  void close() {}

 private:
  const uint8_t* data_;
  size_t size_;
  size_t position_;
  bool valid_;
};

// ============================================================================
// JPEG decoder for ESP32 (no PSRAM)
//
// DAB SlideShow Simple Profile decoder path. ETSI TS 101 499 requires
// baseline JPEG support; progressive/multiscan support is optional. This
// receiver supports single-scan SOF0 baseline and Huffman-coded SOF2
// progressive images using repeated scan passes over one MCU-row workspace.
//
// Display envelope: up to 320 x 240, 8-bit samples, grayscale or three-
// component YCbCr. Four-component JPEG is rejected explicitly rather than
// rendered with an incorrect CMYK/YCCK transform.
// ============================================================================

// JPEG markers
#define M_SOF0  0xC0
#define M_SOF2  0xC2
#define M_DHT   0xC4
#define M_RST0  0xD0
#define M_RST7  0xD7
#define M_SOI   0xD8
#define M_EOI   0xD9
#define M_SOS   0xDA
#define M_DQT   0xDB
#define M_DRI   0xDD
#define M_APP0  0xE0
#define M_APP15 0xEF
#define M_COM   0xFE

const char* JPEGpreflightName(JPEGPreflightResult result) {
  switch (result) {
    case JPEGPreflightResult::SupportedBaseline: return "SUPPORTED_BASELINE_JPEG";
    case JPEGPreflightResult::SupportedProgressive: return "SUPPORTED_PROGRESSIVE_JPEG";
    case JPEGPreflightResult::InvalidJpeg: return "INVALID_JPEG";
    case JPEGPreflightResult::UnsupportedProgressive: return "UNSUPPORTED_PROGRESSIVE_JPEG";
    case JPEGPreflightResult::UnsupportedMultiscan: return "UNSUPPORTED_MULTISCAN_JPEG";
    case JPEGPreflightResult::UnsupportedDimensions: return "UNSUPPORTED_JPEG_DIMENSIONS";
    case JPEGPreflightResult::UnsupportedComponents: return "UNSUPPORTED_JPEG_COMPONENTS";
  }
  return "INVALID_JPEG";
}

JPEGPreflightResult JPEGpreflight(const uint8_t* data, size_t size,
                                  int displayWidth, int displayHeight,
                                  JPEGImageInfo& info) {
  info = JPEGImageInfo();
  if (!data || size < 4U || data[0] != 0xFFU || data[1] != M_SOI)
    return JPEGPreflightResult::InvalidJpeg;

  size_t position = 2U;
  int pendingMarker = -1;
  uint8_t firstScanComponents = 0;
  uint8_t frameComponentIds[PJ_MAX_COMPONENTS] = {0};
  bool foundEoi = false;

  while (position < size || pendingMarker >= 0) {
    int marker = pendingMarker;
    pendingMarker = -1;
    if (marker < 0) {
      if (data[position++] != 0xFFU) return JPEGPreflightResult::InvalidJpeg;
      while (position < size && data[position] == 0xFFU) ++position;
      if (position >= size) return JPEGPreflightResult::InvalidJpeg;
      marker = data[position++];
    }

    if (marker == M_EOI) { foundEoi = true; break; }
    if (marker == M_SOI || (marker >= M_RST0 && marker <= M_RST7))
      return JPEGPreflightResult::InvalidJpeg;
    if (marker == 0xC8 || marker == 0xCC ||
        ((marker >= 0xC0 && marker <= 0xCF) &&
         marker != M_SOF0 && marker != M_SOF2 && marker != M_DHT))
      return JPEGPreflightResult::InvalidJpeg;
    if (position + 2U > size) return JPEGPreflightResult::InvalidJpeg;
    const uint16_t segmentLength = static_cast<uint16_t>(
        (static_cast<uint16_t>(data[position]) << 8) | data[position + 1U]);
    if (segmentLength < 2U || position + segmentLength > size)
      return JPEGPreflightResult::InvalidJpeg;
    const size_t payload = position + 2U;
    const size_t segmentEnd = position + segmentLength;

    if (marker == M_APP0) info.app0 = true;
    if (marker == M_SOF0 || marker == M_SOF2) {
      if (info.sof0 || info.sof2 || segmentLength < 11U || data[payload] != 8U)
        return JPEGPreflightResult::InvalidJpeg;
      info.sof0 = marker == M_SOF0;
      info.sof2 = marker == M_SOF2;
      info.height = static_cast<uint16_t>((data[payload + 1U] << 8) |
                                          data[payload + 2U]);
      info.width = static_cast<uint16_t>((data[payload + 3U] << 8) |
                                         data[payload + 4U]);
      info.components = data[payload + 5U];
      if (info.width == 0U || info.height == 0U || info.components == 0U ||
          segmentLength != static_cast<uint16_t>(8U + 3U * info.components))
        return JPEGPreflightResult::InvalidJpeg;
      for (uint8_t component = 0; component < info.components; ++component) {
        const uint8_t componentId = data[payload + 6U + 3U * component];
        for (uint8_t previous = 0; previous < component; ++previous)
          if (frameComponentIds[previous] == componentId)
            return JPEGPreflightResult::InvalidJpeg;
        frameComponentIds[component] = componentId;
        const uint8_t sampling = data[payload + 7U + 3U * component];
        const uint8_t horizontal = sampling >> 4;
        const uint8_t vertical = sampling & 0x0FU;
        if (horizontal == 0U || horizontal > 4U || vertical == 0U || vertical > 4U)
          return JPEGPreflightResult::InvalidJpeg;
        if (horizontal > info.maxHorizontalSampling)
          info.maxHorizontalSampling = horizontal;
        if (vertical > info.maxVerticalSampling)
          info.maxVerticalSampling = vertical;
      }
    } else if (marker == M_DRI) {
      if (segmentLength != 4U) return JPEGPreflightResult::InvalidJpeg;
      info.restartInterval = static_cast<uint16_t>(
          (static_cast<uint16_t>(data[payload]) << 8) | data[payload + 1U]);
    }

    position = segmentEnd;
    if (marker != M_SOS) continue;
    if (!info.sof0 && !info.sof2) return JPEGPreflightResult::InvalidJpeg;
    const uint8_t scanComponents = data[payload];
    if (scanComponents == 0U || scanComponents > info.components ||
        segmentLength != static_cast<uint16_t>(6U + 2U * scanComponents))
      return JPEGPreflightResult::InvalidJpeg;
    uint8_t seenScanComponents = 0U;
    for (uint8_t scanComponent = 0; scanComponent < scanComponents;
         ++scanComponent) {
      const uint8_t componentId = data[payload + 1U + 2U * scanComponent];
      bool foundComponent = false;
      for (uint8_t frameComponent = 0; frameComponent < info.components;
           ++frameComponent) {
        if (frameComponentIds[frameComponent] == componentId) {
          const uint8_t mask = static_cast<uint8_t>(1U << frameComponent);
          if ((seenScanComponents & mask) != 0U)
            return JPEGPreflightResult::InvalidJpeg;
          seenScanComponents |= mask;
          foundComponent = true;
          break;
        }
      }
      if (!foundComponent) return JPEGPreflightResult::InvalidJpeg;
      const uint8_t tableSelectors =
          data[payload + 2U + 2U * scanComponent];
      if ((tableSelectors >> 4) >= PJ_MAX_HTABLES ||
          (tableSelectors & 0x0FU) >= PJ_MAX_HTABLES)
        return JPEGPreflightResult::InvalidJpeg;
    }
    const size_t spectralOffset = payload + 1U + 2U * scanComponents;
    const uint8_t spectralStart = data[spectralOffset];
    const uint8_t spectralEnd = data[spectralOffset + 1U];
    const uint8_t approximation = data[spectralOffset + 2U];
    const uint8_t approximationHigh = approximation >> 4;
    const uint8_t approximationLow = approximation & 0x0FU;
    if (spectralStart > spectralEnd || spectralEnd > 63U ||
        approximationHigh > 13U || approximationLow > 13U)
      return JPEGPreflightResult::InvalidJpeg;
    if (info.sof2 &&
        ((spectralStart == 0U && spectralEnd != 0U) ||
         (spectralStart != 0U && scanComponents != 1U)))
      return JPEGPreflightResult::InvalidJpeg;
    if (info.sof0 &&
        (spectralStart != 0U || spectralEnd != 63U ||
         approximationHigh != 0U || approximationLow != 0U))
      return JPEGPreflightResult::InvalidJpeg;
    if (info.scans == 0U) firstScanComponents = scanComponents;
    if (info.scans == 0xFFU) return JPEGPreflightResult::InvalidJpeg;
    ++info.scans;

    // Skip entropy-coded data while respecting byte stuffing and restart
    // markers. The first non-stuffed, non-RST marker is processed by the outer
    // loop without treating entropy bytes as segment headers.
    while (position < size) {
      if (data[position++] != 0xFFU) continue;
      while (position < size && data[position] == 0xFFU) ++position;
      if (position >= size) return JPEGPreflightResult::InvalidJpeg;
      const uint8_t entropyMarker = data[position++];
      if (entropyMarker == 0x00U) continue;
      if (entropyMarker >= M_RST0 && entropyMarker <= M_RST7) {
        ++info.restartMarkers;
        continue;
      }
      pendingMarker = entropyMarker;
      break;
    }
  }

  if (!foundEoi || (!info.sof0 && !info.sof2) || info.scans == 0U)
    return JPEGPreflightResult::InvalidJpeg;
  if (info.width > static_cast<uint16_t>(displayWidth) ||
      info.height > static_cast<uint16_t>(displayHeight)) {
    const uint16_t halfWidth = static_cast<uint16_t>((info.width + 1U) / 2U);
    const uint16_t halfHeight = static_cast<uint16_t>((info.height + 1U) / 2U);
    if (halfWidth > static_cast<uint16_t>(displayWidth) ||
        halfHeight > static_cast<uint16_t>(displayHeight))
      return JPEGPreflightResult::UnsupportedDimensions;
    info.scaleDivisor = 2U;
  }
  if (info.components != 1U && info.components != 3U)
    return JPEGPreflightResult::UnsupportedComponents;
  if (info.sof2) return JPEGPreflightResult::SupportedProgressive;
  if (info.scans != 1U || firstScanComponents != info.components)
    return JPEGPreflightResult::UnsupportedMultiscan;
  return JPEGPreflightResult::SupportedBaseline;
}

static const uint8_t zigzag[64] = {
   0,  1,  8, 16,  9,  2,  3, 10,
  17, 24, 32, 25, 18, 11,  4,  5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13,  6,  7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

static inline int pjExtend(int v, int bits) {
  int vt = 1 << (bits - 1);
  if (v < vt) v -= (1 << bits) - 1;
  return v;
}

static inline uint8_t pjClamp(int v) {
  return (v < 0) ? 0 : (v > 255) ? 255 : (uint8_t)v;
}

// --- Non-zero coefficient bitmap ---
// Tracks which spectral positions (0-63) are non-zero for each block.
// Needed so AC refine scans read the correct number of refinement bits
// when discarding blocks not in the target MCU row.

static inline void nzSet(uint8_t* bm, int blockIdx, int k) {
  int bit = blockIdx * 64 + k;
  bm[bit >> 3] |= (1 << (bit & 7));
}

static inline bool nzGet(uint8_t* bm, int blockIdx, int k) {
  int bit = blockIdx * 64 + k;
  return (bm[bit >> 3] >> (bit & 7)) & 1;
}

// --- Huffman table ---
struct PJHuffTable {
  uint8_t bits[17];
  uint8_t vals[256];
  uint8_t lookLen[256];
  uint8_t lookSym[256];
  int32_t maxcode[18];
  int16_t valptr[18];
  int total;
};

// Pre-compute the canonical Huffman lookup tables (mincode/maxcode/valptr)
// from the per-bit-length counts found in a DHT segment.
static void pjBuildHuff(PJHuffTable* ht) {
  int code = 0, p = 0;
  uint16_t huffcode[256];
  uint8_t huffsize[256];

  for (int l = 1; l <= 16; l++) {
    for (int i = 0; i < ht->bits[l]; i++) {
      huffsize[p] = l;
      huffcode[p] = code;
      p++;
      code++;
    }
    code <<= 1;
  }
  ht->total = p;

  p = 0;
  for (int l = 1; l <= 16; l++) {
    if (ht->bits[l]) {
      ht->valptr[l] = p;
      ht->maxcode[l] = huffcode[p + ht->bits[l] - 1];
      p += ht->bits[l];
    } else {
      ht->maxcode[l] = -1;
    }
  }
  ht->maxcode[17] = 0x7FFFF;

  memset(ht->lookLen, 0, sizeof(ht->lookLen));
  for (int i = 0; i < ht->total; i++) {
    if (huffsize[i] <= 8) {
      int prefix = huffcode[i] << (8 - huffsize[i]);
      int count = 1 << (8 - huffsize[i]);
      for (int j = 0; j < count; j++) {
        ht->lookLen[prefix + j] = huffsize[i];
        ht->lookSym[prefix + j] = ht->vals[i];
      }
    }
  }
}

// --- Component info ---
struct PJComponent {
  uint8_t id;
  uint8_t hSamp, vSamp;
  uint8_t qtSel;
  int16_t dcPred;
};

// --- Bit reader ---
struct PJBitReader {
  MemoryFile* file;
  uint32_t buf;
  int bits;
  bool hitMarker;
  uint8_t markerVal;
  bool safeTail;
  bool decodeError;

  void init(MemoryFile* f) {
    file = f; buf = 0; bits = 0;
    hitMarker = false; markerVal = 0; safeTail = false; decodeError = false;
  }

  void reset() {
    buf = 0; bits = 0;
    hitMarker = false; markerVal = 0;
  }

  void fillBits() {
    while (bits <= 24 && !hitMarker) {
      int c = file->read();
      if (c < 0) { hitMarker = true; return; }
      if (c == 0xFF) {
        int c2 = file->read();
        if (c2 < 0) { hitMarker = true; return; }
        if (c2 == 0x00) {
          buf = (buf << 8) | 0xFF;
          bits += 8;
        } else {
          hitMarker = true;
          markerVal = c2;
          return;
        }
      } else {
        buf = (buf << 8) | c;
        bits += 8;
      }
    }
  }

  void fillBitsMin(int required) {
    while (bits < required && !hitMarker) {
      int c = file->read();
      if (c < 0) { hitMarker = true; return; }
      if (c == 0xFF) {
        int c2 = file->read();
        if (c2 < 0) { hitMarker = true; return; }
        if (c2 == 0x00) {
          buf = (buf << 8) | 0xFF; bits += 8;
        } else {
          hitMarker = true; markerVal = c2; return;
        }
      } else {
        buf = (buf << 8) | c; bits += 8;
      }
    }
  }

  int getBits(int n) {
    if (bits < n) { if (safeTail) fillBitsMin(n); else fillBits(); }
    if (bits < n) { decodeError = true; return 0; }
    bits -= n;
    return (buf >> bits) & ((1 << n) - 1);
  }

  int getBit() { return getBits(1); }

  int peekBits(int n) {
    if (bits < n) { if (safeTail) fillBitsMin(n); else fillBits(); }
    if (bits < n) return 0;
    return (buf >> (bits - n)) & ((1 << n) - 1);
  }

  void skipBits(int n) { bits -= n; }
};

// --- Huffman decode ---
static int pjHuffDecode(PJBitReader* br, PJHuffTable* ht) {
  if (!ht || ht->total <= 0) { br->decodeError = true; return 0; }
  if (br->safeTail) {
    if (br->bits < 8 && !br->hitMarker) br->fillBitsMin(8);
    if (br->bits >= 8) {
      int look = (br->buf >> (br->bits - 8)) & 0xFF;
      if (ht->lookLen[look]) {
        br->skipBits(ht->lookLen[look]);
        return ht->lookSym[look];
      }
    }
  } else {
    int look = br->peekBits(8);
    if (ht->lookLen[look]) {
      br->skipBits(ht->lookLen[look]);
      return ht->lookSym[look];
    }
  }
  int code = br->getBits(1);
  for (int l = 1; l <= 16; l++) {
    if (br->decodeError) return 0;
    if (code <= ht->maxcode[l]) {
      return ht->vals[ht->valptr[l] + code - (ht->maxcode[l] - ht->bits[l] + 1)];
    }
    code = (code << 1) | br->getBits(1);
  }
  br->decodeError = true;
  return 0;
}

static int pjReceive(PJBitReader* br, int nbits) {
  return pjExtend(br->getBits(nbits), nbits);
}

// --- JPEG decoder state ---
struct PJDecoder {
  uint16_t width, height;
  uint8_t nComp;
  PJComponent comp[PJ_MAX_COMPONENTS];
  uint8_t maxH, maxV;
  uint16_t mcuW, mcuH;
  uint16_t mcuCntX, mcuCntY;
  uint8_t blocksPerMCU;

  int16_t qtable[4][64];
  bool qtableDefined[4];
  PJHuffTable dcHuff[PJ_MAX_HTABLES];
  PJHuffTable acHuff[PJ_MAX_HTABLES];
  uint16_t restartInterval;

  PJBitReader br;

  uint8_t scanNComp;
  uint8_t scanCompIdx[PJ_MAX_COMPONENTS];
  uint8_t scanDcTbl[PJ_MAX_COMPONENTS];
  uint8_t scanAcTbl[PJ_MAX_COMPONENTS];
  uint8_t ss, se, ah, al;

  int eobRun;
  int mcuCount;

  // Global block indexing for bitmap
  int compBlockOffset[PJ_MAX_COMPONENTS];
  uint16_t compBlockCols[PJ_MAX_COMPONENTS];
  uint16_t compBlockRows[PJ_MAX_COMPONENTS];
  int totalImageBlocks;
  int8_t coefficientBits[PJ_MAX_COMPONENTS][64];
  uint8_t scanCount;
};

// Persistent decoder state. The receiver is single-threaded, so one zeroed
// instance can be safely reused for every slideshow decode without runtime
// allocation.
static PJDecoder pjDecoderState;

// --- Compute global block offsets after SOF parsing ---
static void pjComputeBlockOffsets(PJDecoder* d) {
  int offset = 0;
  for (int c = 0; c < d->nComp; c++) {
    d->compBlockOffset[c] = offset;
    d->compBlockCols[c] = static_cast<uint16_t>(
        (static_cast<uint32_t>(d->width) * d->comp[c].hSamp +
         static_cast<uint32_t>(d->maxH) * 8U - 1U) /
        (static_cast<uint32_t>(d->maxH) * 8U));
    d->compBlockRows[c] = static_cast<uint16_t>(
        (static_cast<uint32_t>(d->height) * d->comp[c].vSamp +
         static_cast<uint32_t>(d->maxV) * 8U - 1U) /
        (static_cast<uint32_t>(d->maxV) * 8U));
    // Allocate bitmap indices for the padded interleaved-MCU envelope. Dummy
    // edge blocks are never displayed, but indexing them keeps interleaved
    // scans bounded and stable when the image size is not MCU-aligned.
    offset += (d->mcuCntX * d->comp[c].hSamp) *
              (d->mcuCntY * d->comp[c].vSamp);
  }
  d->totalImageBlocks = offset;
}

// Compute global block index for a block at (blockCol, blockRow) of component ci
static inline int pjGlobalBlockIdx(PJDecoder* d, int ci, int blockCol, int blockRow) {
  return d->compBlockOffset[ci] + blockRow * (d->mcuCntX * d->comp[ci].hSamp) + blockCol;
}

// --- Marker reading helpers ---
static int pjRead8(MemoryFile& f) { return f.read(); }

static int pjRead16(MemoryFile& f) {
  int hi = f.read();
  int lo = f.read();
  return (hi << 8) | lo;
}

static void pjSkip(MemoryFile& f, int n) {
  while (n-- > 0) f.read();
}

// --- Parse DQT ---
// DQT (Define Quantization Table) parser - up to 4 tables, 8 or 16 bit each.
static bool pjParseDQT(MemoryFile& f, PJDecoder* d) {
  const int segmentLength = pjRead16(f);
  if (!f.valid() || segmentLength < 3) return false;
  int len = segmentLength - 2;
  while (len > 0) {
    int info = pjRead8(f); len--;
    int tblIdx = info & 0x0F;
    int prec = (info >> 4) & 0x0F;
    const int tableBytes = prec ? 128 : 64;
    if (tblIdx > 3 || prec > 1 || len < tableBytes) return false;
    for (int i = 0; i < 64; i++) {
      if (prec) {
        d->qtable[tblIdx][zigzag[i]] = pjRead16(f);
        len -= 2;
      } else {
        d->qtable[tblIdx][zigzag[i]] = pjRead8(f);
        len--;
      }
    }
    d->qtableDefined[tblIdx] = true;
  }
  return len == 0 && f.valid();
}

// --- Parse DHT ---
// DHT (Define Huffman Table) parser - stores DC/AC tables for each component.
static bool pjParseDHT(MemoryFile& f, PJDecoder* d) {
  const int segmentLength = pjRead16(f);
  if (!f.valid() || segmentLength < 19) return false;
  int len = segmentLength - 2;
  while (len > 0) {
    if (len < 17) return false;
    int info = pjRead8(f); len--;
    int cls = (info >> 4) & 0x0F;
    int tblIdx = info & 0x0F;
    if (cls > 1 || tblIdx > 3) return false;
    PJHuffTable* ht = (cls == 0) ? &d->dcHuff[tblIdx] : &d->acHuff[tblIdx];
    int total = 0;
    for (int i = 1; i <= 16; i++) {
      ht->bits[i] = pjRead8(f); len--;
      total += ht->bits[i];
    }
    if (total > 256 || total > len) return false;
    for (int i = 0; i < total; i++) {
      ht->vals[i] = pjRead8(f); len--;
    }
    pjBuildHuff(ht);
  }
  return len == 0 && f.valid();
}

// --- Parse SOF2 ---
// SOF (Start Of Frame) parser - extracts image dimensions and subsampling info.
static bool pjParseSOF(MemoryFile& f, PJDecoder* d) {
  const int segmentLength = pjRead16(f);
  if (!f.valid() || segmentLength < 11) return false;
  if (pjRead8(f) != 8) return false; // precision must be 8 bit
  d->height = pjRead16(f);
  d->width = pjRead16(f);
  d->nComp = pjRead8(f);
  if (d->nComp == 0 || d->nComp > PJ_MAX_COMPONENTS ||
      segmentLength != 8 + 3 * d->nComp) return false;

  d->maxH = 0;
  d->maxV = 0;
  uint16_t blocksPerMcu = 0;
  for (int i = 0; i < d->nComp; i++) {
    d->comp[i].id = pjRead8(f);
    const int samp = pjRead8(f);
    d->comp[i].hSamp = (samp >> 4) & 0x0F;
    d->comp[i].vSamp = samp & 0x0F;
    d->comp[i].qtSel = pjRead8(f);

    // ISO/IEC 10918 bounds used by baseline interleaved JPEG: sampling
    // factors 1..4 and at most 10 data units per MCU. Enforce them before
    // any workspace-size arithmetic.
    if (d->comp[i].hSamp < 1 || d->comp[i].hSamp > 4 ||
        d->comp[i].vSamp < 1 || d->comp[i].vSamp > 4 ||
        d->comp[i].qtSel > 3) return false;
    blocksPerMcu += static_cast<uint16_t>(d->comp[i].hSamp) *
                    static_cast<uint16_t>(d->comp[i].vSamp);
    if (blocksPerMcu > 10U) return false;

    if (d->comp[i].hSamp > d->maxH) d->maxH = d->comp[i].hSamp;
    if (d->comp[i].vSamp > d->maxV) d->maxV = d->comp[i].vSamp;
  }
  if (d->maxH == 0 || d->maxV == 0) return false;

  d->mcuW = d->maxH * 8;
  d->mcuH = d->maxV * 8;
  d->mcuCntX = (d->width + d->mcuW - 1) / d->mcuW;
  d->mcuCntY = (d->height + d->mcuH - 1) / d->mcuH;
  d->blocksPerMCU = static_cast<uint8_t>(blocksPerMcu);

  pjComputeBlockOffsets(d);
  return f.valid();
}

// --- Parse SOS ---
// SOS (Start Of Scan) parser - reads component selectors and the band/Ah/Al
// fields that drive progressive-mode pass dispatch.
static bool pjParseSOS(MemoryFile& f, PJDecoder* d) {
  const int segmentLength = pjRead16(f);
  if (!f.valid() || segmentLength < 8) return false;
  d->scanNComp = pjRead8(f);
  if (d->scanNComp == 0 || d->scanNComp > PJ_MAX_COMPONENTS ||
      d->scanNComp > d->nComp || segmentLength != 6 + 2 * d->scanNComp)
    return false;

  uint8_t seenComponents = 0;
  for (int i = 0; i < d->scanNComp; i++) {
    int id = pjRead8(f);
    int tbl = pjRead8(f);
    bool found = false;
    for (int c = 0; c < d->nComp; c++) {
      if (d->comp[c].id == id) {
        if (seenComponents & (1U << c)) return false;
        seenComponents |= static_cast<uint8_t>(1U << c);
        d->scanCompIdx[i] = c;
        found = true;
        break;
      }
    }
    if (!found) return false;
    d->scanDcTbl[i] = (tbl >> 4) & 0x0F;
    d->scanAcTbl[i] = tbl & 0x0F;
    if (d->scanDcTbl[i] >= PJ_MAX_HTABLES ||
        d->scanAcTbl[i] >= PJ_MAX_HTABLES) return false;
  }
  d->ss = pjRead8(f);
  d->se = pjRead8(f);
  int approx = pjRead8(f);
  d->ah = (approx >> 4) & 0x0F;
  d->al = approx & 0x0F;
  if (!f.valid() || d->ss > d->se || d->se > 63U || d->ah > 13U ||
      d->al > 13U) return false;
  return true;
}

// Validate the progressive scan script and remember the approximation bit
// currently available for every coefficient. This rejects duplicate first
// scans, skipped refinement levels and AC scans containing multiple components.
static bool pjAcceptProgressiveScan(PJDecoder* d) {
  if (d->scanCount == 0xFFU) return false;
  ++d->scanCount;

  if (d->ss == 0U) {
    if (d->se != 0U) return false;
    for (uint8_t scanComponent = 0; scanComponent < d->scanNComp;
         ++scanComponent) {
      const uint8_t component = d->scanCompIdx[scanComponent];
      int8_t& current = d->coefficientBits[component][0];
      if (d->ah == 0U) {
        if (current >= 0) return false;
      } else if (d->ah != static_cast<uint8_t>(d->al + 1U) ||
                 current != static_cast<int8_t>(d->ah)) {
        return false;
      }
      current = static_cast<int8_t>(d->al);
    }
    return true;
  }

  if (d->scanNComp != 1U) return false;
  const uint8_t component = d->scanCompIdx[0];
  for (uint8_t coefficient = d->ss; coefficient <= d->se; ++coefficient) {
    int8_t& current = d->coefficientBits[component][coefficient];
    if (d->ah == 0U) {
      if (current >= 0) return false;
    } else if (d->ah != static_cast<uint8_t>(d->al + 1U) ||
               current != static_cast<int8_t>(d->ah)) {
      return false;
    }
    current = static_cast<int8_t>(d->al);
  }
  return true;
}

// --- Parse DRI ---
static bool pjParseDRI(MemoryFile& f, PJDecoder* d) {
  if (pjRead16(f) != 4) return false;
  d->restartInterval = pjRead16(f);
  return f.valid();
}

// --- Entropy decoders ---

static void pjDecodeDCFirst(PJDecoder* d, int16_t* coef, int compScanIdx) {
  PJHuffTable* ht = &d->dcHuff[d->scanDcTbl[compScanIdx]];
  int s = pjHuffDecode(&d->br, ht);
  if (d->br.decodeError || s > 11) { d->br.decodeError = true; return; }
  int diff = (s > 0) ? pjReceive(&d->br, s) : 0;
  int ci = d->scanCompIdx[compScanIdx];
  d->comp[ci].dcPred += diff;
  coef[0] = static_cast<int16_t>(d->comp[ci].dcPred * (1 << d->al));
}

static void pjDecodeDCRefine(PJDecoder* d, int16_t* coef) {
  coef[0] |= (d->br.getBit() << d->al);
}

static void pjDecodeACFirst(PJDecoder* d, int16_t* coef, int compScanIdx) {
  PJHuffTable* ht = &d->acHuff[d->scanAcTbl[compScanIdx]];

  if (d->eobRun > 0) { d->eobRun--; return; }

  for (int k = d->ss; k <= d->se; k++) {
    int rs = pjHuffDecode(&d->br, ht);
    if (d->br.decodeError) return;
    int s = rs & 0x0F;
    int r = rs >> 4;

    if (s > 10) { d->br.decodeError = true; return; }

    if (s == 0) {
      if (r == 15) {
        k += 15;
      } else {
        d->eobRun = (1 << r);
        if (r > 0) d->eobRun += d->br.getBits(r);
        d->eobRun--;
        return;
      }
    } else {
      k += r;
      if (k > d->se) { d->br.decodeError = true; return; }
      int v = pjReceive(&d->br, s);
      coef[zigzag[k]] = static_cast<int16_t>(v * (1 << d->al));
    }
  }
}

static void pjDecodeACRefine(PJDecoder* d, int16_t* coef, int compScanIdx) {
  PJHuffTable* ht = &d->acHuff[d->scanAcTbl[compScanIdx]];
  int p1 = 1 << d->al;
  int m1 = -(1 << d->al);
  int k = d->ss;

  if (d->eobRun == 0) {
    while (k <= d->se) {
      int rs = pjHuffDecode(&d->br, ht);
      if (d->br.decodeError) return;
      int s = rs & 0x0F;
      int r = rs >> 4;

      if (s == 0) {
        if (r < 15) {
          d->eobRun = (1 << r);
          if (r > 0) d->eobRun += d->br.getBits(r);
          break;
        }
      } else if (s != 1) {
        d->br.decodeError = true;
        return;
      }

      int newVal = 0;
      if (s == 1) {
        newVal = d->br.getBit() ? p1 : m1;
      }

      while (k <= d->se) {
        int zz = zigzag[k];
        if (coef[zz] != 0) {
          if (d->br.getBit() && (coef[zz] & p1) == 0) {
            if (coef[zz] > 0) coef[zz] += p1;
            else               coef[zz] += m1;
          }
        } else {
          if (r == 0) {
            if (s == 1) coef[zz] = newVal;
            k++;
            break;
          }
          r--;
        }
        k++;
      }
    }
  }

  if (d->eobRun > 0) {
    while (k <= d->se) {
      int zz = zigzag[k];
      if (coef[zz] != 0) {
        if (d->br.getBit() && (coef[zz] & p1) == 0) {
          if (coef[zz] > 0) coef[zz] += p1;
          else               coef[zz] += m1;
        }
      }
      k++;
    }
    d->eobRun--;
  }
}

// --- Baseline block decode (DC + all AC in one call) ---
static void pjDecodeBaseline(PJDecoder* d, int16_t* coef, int compScanIdx) {
  PJHuffTable* dcHt = &d->dcHuff[d->scanDcTbl[compScanIdx]];
  int s = pjHuffDecode(&d->br, dcHt);
  if (d->br.decodeError || s > 11) { d->br.decodeError = true; return; }
  int diff = (s > 0) ? pjReceive(&d->br, s) : 0;
  int ci = d->scanCompIdx[compScanIdx];
  d->comp[ci].dcPred += diff;
  coef[0] = d->comp[ci].dcPred;

  PJHuffTable* acHt = &d->acHuff[d->scanAcTbl[compScanIdx]];
  for (int k = 1; k <= 63; k++) {
    int rs = pjHuffDecode(&d->br, acHt);
    if (d->br.decodeError) return;
    int r = rs >> 4;
    s = rs & 0x0F;
    if (s > 10) { d->br.decodeError = true; return; }
    if (s == 0) {
      if (r == 15) { k += 15; continue; }
      break;
    }
    k += r;
    if (k > 63) break;
    coef[zigzag[k]] = pjReceive(&d->br, s);
  }
}

// --- Decode one block ---
// When store=true, writes to coef (target row buffer).
// When store=false, uses local dummy; bitmap tracks non-zero positions
// so AC refine reads the correct number of bits.
static void pjDecodeBlock(PJDecoder* d, int16_t* coef, int compScanIdx,
                          bool store, uint8_t* nzBitmap, int globalBlockIdx) {
  int16_t dummy[64];
  int16_t* target;

  if (store) {
    target = coef;
  } else {
    memset(dummy, 0, sizeof(dummy));
    // AC refine in discard mode: restore non-zero pattern from bitmap
    if (d->ss > 0 && d->ah > 0 && nzBitmap) {
      for (int k = d->ss; k <= d->se; k++) {
        if (nzGet(nzBitmap, globalBlockIdx, k)) {
          dummy[zigzag[k]] = 1; // any non-zero value
        }
      }
    }
    target = dummy;
  }

  if (d->ss == 0 && d->se == 0) {
    if (d->ah == 0) pjDecodeDCFirst(d, target, compScanIdx);
    else             pjDecodeDCRefine(d, target);
  } else {
    if (d->ah == 0) pjDecodeACFirst(d, target, compScanIdx);
    else             pjDecodeACRefine(d, target, compScanIdx);
  }

  // Update bitmap for discarded AC blocks
  if (!store && nzBitmap && d->ss > 0) {
    for (int k = d->ss; k <= d->se; k++) {
      if (target[zigzag[k]] != 0) {
        nzSet(nzBitmap, globalBlockIdx, k);
      }
    }
  }
}

// --- Get block index within MCU row buffer ---
static int pjRowBlockIndex(PJDecoder* d, int mcuX, int compIdx, int bh, int bv) {
  int offset = 0;
  for (int c = 0; c < compIdx; c++) {
    offset += d->comp[c].hSamp * d->comp[c].vSamp;
  }
  offset += bv * d->comp[compIdx].hSamp + bh;
  return mcuX * d->blocksPerMCU + offset;
}

static int pjSkipEntropy(MemoryFile& f);

static int pjTakeEntropyMarker(PJDecoder* d) {
  if (d->br.hitMarker) return d->br.markerVal;
  return pjSkipEntropy(*d->br.file);
}

static bool pjConsumeRestart(PJDecoder* d, uint8_t& expectedRestart) {
  const int marker = pjTakeEntropyMarker(d);
  if (marker != M_RST0 + expectedRestart) return false;
  expectedRestart = static_cast<uint8_t>((expectedRestart + 1U) & 7U);
  MemoryFile* file = d->br.file;
  d->br.init(file);
  d->br.safeTail = true;
  d->mcuCount = 0;
  d->eobRun = 0;
  for (int i = 0; i < d->nComp; ++i) d->comp[i].dcPred = 0;
  return true;
}

static bool pjScanFailure(PJDecoder* d, const char* reason,
                          int decodedUnits, int expectedUnits) {
  const int marker = d->br.hitMarker ? d->br.markerVal : -1;
  const size_t offset = d->br.file ? d->br.file->position() : 0U;
  DIAG_PRINTF("[SLS/JPEG] scan=FAIL index=%u reason=%s Ss=%u Se=%u Ah=%u Al=%u "
              "components=%u decoded=%d expected=%d offset=%u marker=%02X\n",
              static_cast<unsigned>(d->scanCount), reason,
              static_cast<unsigned>(d->ss), static_cast<unsigned>(d->se),
              static_cast<unsigned>(d->ah), static_cast<unsigned>(d->al),
              static_cast<unsigned>(d->scanNComp), decodedUnits,
              expectedUnits, static_cast<unsigned>(offset),
              static_cast<unsigned>(marker & 0xFF));
  return false;
}

// Decode a progressive scan through the target frame MCU row. Earlier rows
// are entropy-decoded but represented only by a non-zero coefficient bitmap;
// the target row retains complete coefficients across all scans.
static bool pjDecodeScan(PJDecoder* d, int16_t* rowCoefs, int targetMCURow,
                         uint8_t* nzBitmap, int& nextMarker) {
  d->eobRun = 0;
  d->mcuCount = 0;
  for (int i = 0; i < d->nComp; i++) d->comp[i].dcPred = 0;
  uint8_t expectedRestart = 0U;
  int decodedUnits = 0;
  int totalUnits = 0;
  int unitsToDecode = 0;

  if (d->scanNComp > 1) {
    // --- Interleaved scan ---
    totalUnits = d->mcuCntX * d->mcuCntY;
    unitsToDecode = d->mcuCntX * (targetMCURow + 1);
    int startMCU = d->mcuCntX * targetMCURow;

    for (int mcu = 0; mcu < unitsToDecode; mcu++) {
      bool store = (mcu >= startMCU);
      int mcuX = mcu % d->mcuCntX;
      int mcuY = mcu / d->mcuCntX;

      for (int si = 0; si < d->scanNComp; si++) {
        int ci = d->scanCompIdx[si];
        for (int bv = 0; bv < d->comp[ci].vSamp; bv++) {
          for (int bh = 0; bh < d->comp[ci].hSamp; bh++) {
            int16_t* coef = nullptr;
            if (store) {
              int idx = pjRowBlockIndex(d, mcuX, ci, bh, bv);
              coef = &rowCoefs[idx * 64];
            }
            int blockCol = mcuX * d->comp[ci].hSamp + bh;
            int blockRow = mcuY * d->comp[ci].vSamp + bv;
            int gbi = pjGlobalBlockIdx(d, ci, blockCol, blockRow);
            pjDecodeBlock(d, coef, si, store, nzBitmap, gbi);
            if (d->br.decodeError)
              return pjScanFailure(d, "entropy-decode", decodedUnits,
                                   unitsToDecode);
          }
        }
      }
      ++decodedUnits;
      ++d->mcuCount;
      if (d->restartInterval > 0U &&
          d->mcuCount == d->restartInterval && decodedUnits < totalUnits) {
        if (!pjConsumeRestart(d, expectedRestart))
          return pjScanFailure(d, "restart-sequence", decodedUnits,
                               unitsToDecode);
      }
      // hitMarker can be true while valid entropy bits for later MCUs remain
      // buffered. Only getBits()/Huffman decode can determine truncation.
    }
  } else {
    // --- Non-interleaved scan (single component) ---
    int ci = d->scanCompIdx[0];
    int blockCols = d->compBlockCols[ci];
    int blockRows = d->compBlockRows[ci];
    int startBlockRow = targetMCURow * d->comp[ci].vSamp;
    int endBlockRow = startBlockRow + d->comp[ci].vSamp;
    if (endBlockRow > blockRows) endBlockRow = blockRows;
    totalUnits = blockCols * blockRows;
    unitsToDecode = blockCols * endBlockRow;

    for (int blk = 0; blk < unitsToDecode; blk++) {
      int bCol = blk % blockCols;
      int bRow = blk / blockCols;
      bool store = (bRow >= startBlockRow && bRow < endBlockRow);

      int16_t* coef = nullptr;
      if (store) {
        int mcuX = bCol / d->comp[ci].hSamp;
        int bh = bCol % d->comp[ci].hSamp;
        int bv = bRow - startBlockRow;
        int idx = pjRowBlockIndex(d, mcuX, ci, bh, bv);
        coef = &rowCoefs[idx * 64];
      }
      int gbi = pjGlobalBlockIdx(d, ci, bCol, bRow);
      pjDecodeBlock(d, coef, 0, store, nzBitmap, gbi);
      if (d->br.decodeError)
        return pjScanFailure(d, "entropy-decode", decodedUnits,
                             unitsToDecode);

      ++decodedUnits;
      ++d->mcuCount;
      if (d->restartInterval > 0U &&
          d->mcuCount == d->restartInterval && decodedUnits < totalUnits) {
        if (!pjConsumeRestart(d, expectedRestart))
          return pjScanFailure(d, "restart-sequence", decodedUnits,
                               unitsToDecode);
      }
      // Do not reject a marker merely because fillBitsMin() read it ahead of
      // the still-buffered tail; the next entropy decode validates that tail.
    }
  }

  if (decodedUnits != unitsToDecode || d->br.decodeError)
    return pjScanFailure(d, "unit-count", decodedUnits, unitsToDecode);

  int marker = pjTakeEntropyMarker(d);
  if (decodedUnits == totalUnits) {
    // Tolerate a final scheduled restart marker only when the scan ends
    // exactly on its interval boundary, then require the real next marker.
    if (marker >= M_RST0 && marker <= M_RST7) {
      if (d->restartInterval == 0U || d->mcuCount != d->restartInterval ||
          marker != M_RST0 + expectedRestart)
        return pjScanFailure(d, "final-restart", decodedUnits, totalUnits);
      marker = pjSkipEntropy(*d->br.file);
    }
  } else {
    // This row pass intentionally stops early. Skip the remaining entropy and
    // any restart markers; the final-row validation pass checks them exactly.
    while (marker >= M_RST0 && marker <= M_RST7)
      marker = pjSkipEntropy(*d->br.file);
  }
  nextMarker = marker;
  if (marker < 0)
    return pjScanFailure(d, "missing-next-marker", decodedUnits, totalUnits);
  return true;
}

// --- Integer IDCT (LLM algorithm, 13-bit fixed point) ---
#define FIX_0_298  2446
#define FIX_0_390  3196
#define FIX_0_541  4433
#define FIX_0_765  6270
#define FIX_0_899  7373
#define FIX_1_175  9633
#define FIX_1_501 12299
#define FIX_1_847 15137
#define FIX_1_961 16069
#define FIX_2_053 16819
#define FIX_2_562 20995
#define FIX_3_072 25172

#define IDCT_BITS  13
#define PASS1_BITS 2

// 8x8 inverse DCT (AAN scaled algorithm). Reads dequantized coefficients,
// writes back 64 spatial-domain samples clipped to 0..255.
static void pjIDCT(int16_t* coef, const int16_t* qt, uint8_t* out) {
  int32_t ws[64];

  // Pass 1: columns (dequantize + butterfly)
  for (int col = 0; col < 8; col++) {
    int32_t s0 = coef[0*8+col] * qt[0*8+col];
    int32_t s1 = coef[1*8+col] * qt[1*8+col];
    int32_t s2 = coef[2*8+col] * qt[2*8+col];
    int32_t s3 = coef[3*8+col] * qt[3*8+col];
    int32_t s4 = coef[4*8+col] * qt[4*8+col];
    int32_t s5 = coef[5*8+col] * qt[5*8+col];
    int32_t s6 = coef[6*8+col] * qt[6*8+col];
    int32_t s7 = coef[7*8+col] * qt[7*8+col];

    if (!(s1 | s2 | s3 | s4 | s5 | s6 | s7)) {
      int32_t dc = s0 << PASS1_BITS;
      for (int i = 0; i < 8; i++) ws[i*8+col] = dc;
      continue;
    }

    int32_t z2 = s2, z3 = s6;
    int32_t z1 = (z2 + z3) * FIX_0_541;
    int32_t t2 = z1 - z3 * FIX_1_847;
    int32_t t3 = z1 + z2 * FIX_0_765;
    int32_t t0 = (s0 + s4) << IDCT_BITS;
    int32_t t1 = (s0 - s4) << IDCT_BITS;
    int32_t t10 = t0 + t3, t13 = t0 - t3;
    int32_t t11 = t1 + t2, t12 = t1 - t2;

    z1 = s7 + s1; z2 = s5 + s3; z3 = s7 + s3; int32_t z4 = s5 + s1;
    int32_t z5 = (z3 + z4) * FIX_1_175;
    t0 = s7 * FIX_0_298; t1 = s5 * FIX_2_053;
    t2 = s3 * FIX_3_072; t3 = s1 * FIX_1_501;
    z1 *= -FIX_0_899; z2 *= -FIX_2_562; z3 *= -FIX_1_961; z4 *= -FIX_0_390;
    z3 += z5; z4 += z5;
    t0 += z1 + z3; t1 += z2 + z4; t2 += z2 + z3; t3 += z1 + z4;

    int32_t rnd = 1 << (IDCT_BITS - PASS1_BITS - 1);
    int shift = IDCT_BITS - PASS1_BITS;
    ws[0*8+col] = (t10 + t3 + rnd) >> shift;
    ws[7*8+col] = (t10 - t3 + rnd) >> shift;
    ws[1*8+col] = (t11 + t2 + rnd) >> shift;
    ws[6*8+col] = (t11 - t2 + rnd) >> shift;
    ws[2*8+col] = (t12 + t1 + rnd) >> shift;
    ws[5*8+col] = (t12 - t1 + rnd) >> shift;
    ws[3*8+col] = (t13 + t0 + rnd) >> shift;
    ws[4*8+col] = (t13 - t0 + rnd) >> shift;
  }

  // Pass 2: rows (butterfly + output 8-bit pixels)
  for (int row = 0; row < 8; row++) {
    int32_t* w = ws + row * 8;

    if (!(w[1] | w[2] | w[3] | w[4] | w[5] | w[6] | w[7])) {
      uint8_t v = pjClamp(((w[0] + (1 << (PASS1_BITS + 2))) >> (PASS1_BITS + 3)) + 128);
      for (int i = 0; i < 8; i++) out[row*8+i] = v;
      continue;
    }

    int32_t z2 = w[2], z3 = w[6];
    int32_t z1 = (z2 + z3) * FIX_0_541;
    int32_t t2 = z1 - z3 * FIX_1_847;
    int32_t t3 = z1 + z2 * FIX_0_765;
    int32_t t0 = (w[0] + w[4]) << IDCT_BITS;
    int32_t t1 = (w[0] - w[4]) << IDCT_BITS;
    int32_t t10 = t0 + t3, t13 = t0 - t3;
    int32_t t11 = t1 + t2, t12 = t1 - t2;

    z1 = w[7] + w[1]; z2 = w[5] + w[3]; z3 = w[7] + w[3]; int32_t z4 = w[5] + w[1];
    int32_t z5 = (z3 + z4) * FIX_1_175;
    t0 = w[7] * FIX_0_298; t1 = w[5] * FIX_2_053;
    t2 = w[3] * FIX_3_072; t3 = w[1] * FIX_1_501;
    z1 *= -FIX_0_899; z2 *= -FIX_2_562; z3 *= -FIX_1_961; z4 *= -FIX_0_390;
    z3 += z5; z4 += z5;
    t0 += z1 + z3; t1 += z2 + z4; t2 += z2 + z3; t3 += z1 + z4;

    int32_t rnd = 1 << (IDCT_BITS + PASS1_BITS + 2);
    int shift = IDCT_BITS + PASS1_BITS + 3;
    out[row*8+0] = pjClamp(((t10 + t3 + rnd) >> shift) + 128);
    out[row*8+7] = pjClamp(((t10 - t3 + rnd) >> shift) + 128);
    out[row*8+1] = pjClamp(((t11 + t2 + rnd) >> shift) + 128);
    out[row*8+6] = pjClamp(((t11 - t2 + rnd) >> shift) + 128);
    out[row*8+2] = pjClamp(((t12 + t1 + rnd) >> shift) + 128);
    out[row*8+5] = pjClamp(((t12 - t1 + rnd) >> shift) + 128);
    out[row*8+3] = pjClamp(((t13 + t0 + rnd) >> shift) + 128);
    out[row*8+4] = pjClamp(((t13 - t0 + rnd) >> shift) + 128);
  }
}

// --- YCbCr to RGB565 ---
static inline uint16_t pjYCbCrToRGB565(int y, int cb, int cr) {
  cb -= 128; cr -= 128;
  int r = y + ((91881 * cr + 32768) >> 16);
  int g = y - ((22554 * cb + 46802 * cr + 32768) >> 16);
  int b = y + ((116130 * cb + 32768) >> 16);
  uint16_t pixel = ((pjClamp(r) >> 3) << 11) | ((pjClamp(g) >> 2) << 5) | (pjClamp(b) >> 3);
  return pixel;
}

// --- Render one MCU row to TFT ---
// IDCT every block in one MCU row, then convert YCbCr→RGB565 and push the
// resulting pixel rows to the TFT (centered horizontally and vertically).
static bool pjOutputMCURow(PJDecoder* d, int16_t* rowCoefs, int mcuRow,
                           TFT_eSPI& tft, int offsetX, int offsetY,
                           uint8_t* allBlocks, uint8_t scaleDivisor) {
  int totalBlocks = d->mcuCntX * d->blocksPerMCU;
  uint16_t lineBuffer[320];
  if (scaleDivisor != 2U) scaleDivisor = 1U;
  const int outputWidth =
      (static_cast<int>(d->width) + scaleDivisor - 1) / scaleDivisor;

  // IDCT all blocks in this row
  for (int b = 0; b < totalBlocks; b++) {
    int blockInMCU = b % d->blocksPerMCU;
    int compIdx = 0, acc = 0;
    for (int c = 0; c < d->nComp; c++) {
      int nb = d->comp[c].hSamp * d->comp[c].vSamp;
      if (blockInMCU < acc + nb) { compIdx = c; break; }
      acc += nb;
    }
    pjIDCT(&rowCoefs[b * 64], d->qtable[d->comp[compIdx].qtSel], &allBlocks[b * 64]);
  }

  // Output pixel rows
  for (int py = 0; py < (int)d->mcuH; py++) {
    int absY = mcuRow * d->mcuH + py;
    if (absY >= d->height) break;
    if ((absY % scaleDivisor) != 0) continue;
    int outputX = 0;

    for (int mcuX = 0; mcuX < d->mcuCntX; mcuX++) {
      int mcuBase = mcuX * d->blocksPerMCU;

      for (int px = 0; px < (int)d->mcuW; px++) {
        int absX = mcuX * d->mcuW + px;
        if (absX >= d->width) break;
        if ((absX % scaleDivisor) != 0) continue;

        int yVal, cbVal, crVal;

        if (d->nComp == 1) {
          int bi = mcuBase + (py / 8) * d->comp[0].hSamp + (px / 8);
          yVal = allBlocks[bi * 64 + (py % 8) * 8 + (px % 8)];
          cbVal = crVal = 128;
        } else {
          // Y
          int yBi = mcuBase + (py / 8) * d->comp[0].hSamp + (px / 8);
          yVal = allBlocks[yBi * 64 + (py % 8) * 8 + (px % 8)];
          // Cb
          int cbOff = d->comp[0].hSamp * d->comp[0].vSamp;
          int cbPx = px * d->comp[1].hSamp / d->maxH;
          int cbPy = py * d->comp[1].vSamp / d->maxV;
          int cbBi = mcuBase + cbOff + (cbPy / 8) * d->comp[1].hSamp + (cbPx / 8);
          cbVal = allBlocks[cbBi * 64 + (cbPy % 8) * 8 + (cbPx % 8)];
          // Cr
          int crOff = cbOff + d->comp[1].hSamp * d->comp[1].vSamp;
          int crPx = px * d->comp[2].hSamp / d->maxH;
          int crPy = py * d->comp[2].vSamp / d->maxV;
          int crBi = mcuBase + crOff + (crPy / 8) * d->comp[2].hSamp + (crPx / 8);
          crVal = allBlocks[crBi * 64 + (crPy % 8) * 8 + (crPx % 8)];
        }

        lineBuffer[outputX++] = pjYCbCrToRGB565(yVal, cbVal, crVal);
      }
    }

    if (outputX != outputWidth) return false;
    tft.pushImage(offsetX, offsetY + absY / scaleDivisor,
                  outputWidth, 1, lineBuffer);
  }
  return true;
}

// --- Skip to next marker ---
static int pjSkipToMarker(MemoryFile& f) {
  int c;
  do { c = f.read(); if (c < 0) return -1; } while (c != 0xFF);
  do { c = f.read(); if (c < 0) return -1; } while (c == 0xFF);
  return c;
}

// --- Skip entropy data to next marker ---
static int pjSkipEntropy(MemoryFile& f) {
  while (true) {
    int c = f.read();
    if (c < 0) return -1;
    if (c == 0xFF) {
      int c2;
      do { c2 = f.read(); if (c2 < 0) return -1; } while (c2 == 0xFF);
      if (c2 != 0x00) return c2;
    }
  }
}

// --- Process entire file for one MCU row ---
// One pass over the file for the progressive decoder: replays every scan,
// only retaining coefficients that belong to the target MCU row.
static bool pjProcessFileForRow(MemoryFile& f, PJDecoder* d, int16_t* rowCoefs,
                                int targetRow, uint8_t* nzBitmap,
                                JPEGRowCallback progressCallback,
                                void* progressContext,
                                bool verboseScanDiagnostics) {
  if (!f.seek(0)) return false;
  memset(d, 0, sizeof(*d));
  memset(d->coefficientBits, -1, sizeof(d->coefficientBits));
  if (pjRead8(f) != 0xFF || pjRead8(f) != M_SOI) return false;

  bool sofDone = false;
  int pendingMarker = -1;

  while (true) {
    int marker = pendingMarker;
    pendingMarker = -1;
    if (marker < 0) marker = pjSkipToMarker(f);
    if (marker < 0) return false;
    if (marker == M_EOI) {
      if (!sofDone || d->scanCount == 0U) return false;
      for (uint8_t component = 0; component < d->nComp; ++component) {
        if (d->coefficientBits[component][0] < 0 ||
            !d->qtableDefined[d->comp[component].qtSel]) return false;
      }
      return true;
    }
    if (marker == M_SOI || (marker >= M_RST0 && marker <= M_RST7))
      return false;

    switch (marker) {
      case M_SOF2:
        if (sofDone || !pjParseSOF(f, d)) return false;
        sofDone = true;
        break;
      case M_SOF0:
        return false;
      case M_DHT:
        if (!pjParseDHT(f, d)) return false;
        break;
      case M_DQT:
        if (!pjParseDQT(f, d)) return false;
        break;
      case M_DRI:
        if (!pjParseDRI(f, d)) return false;
        break;
      case M_SOS:
        if (!sofDone) {
          DIAG_PRINTF("[SLS/JPEG] scan=FAIL reason=SOS-before-SOF offset=%u\n",
                      static_cast<unsigned>(f.position()));
          return false;
        }
        if (!pjParseSOS(f, d)) {
          DIAG_PRINTF("[SLS/JPEG] scan=FAIL reason=SOS-parse index=%u offset=%u\n",
                      static_cast<unsigned>(d->scanCount + 1U),
                      static_cast<unsigned>(f.position()));
          return false;
        }
        if (!pjAcceptProgressiveScan(d)) {
          DIAG_PRINTF("[SLS/JPEG] scan=FAIL reason=scan-script index=%u "
                      "Ss=%u Se=%u Ah=%u Al=%u components=%u offset=%u\n",
                      static_cast<unsigned>(d->scanCount),
                      static_cast<unsigned>(d->ss),
                      static_cast<unsigned>(d->se),
                      static_cast<unsigned>(d->ah),
                      static_cast<unsigned>(d->al),
                      static_cast<unsigned>(d->scanNComp),
                      static_cast<unsigned>(f.position()));
          return false;
        }
        if (verboseScanDiagnostics && targetRow == d->mcuCntY - 1) {
          DIAG_PRINTF("[SLS/JPEG] scan=%u Ss=%u Se=%u Ah=%u Al=%u "
                      "components=%u entropyOffset=%u\n",
                      static_cast<unsigned>(d->scanCount),
                      static_cast<unsigned>(d->ss),
                      static_cast<unsigned>(d->se),
                      static_cast<unsigned>(d->ah),
                      static_cast<unsigned>(d->al),
                      static_cast<unsigned>(d->scanNComp),
                      static_cast<unsigned>(f.position()));
        }
        if (d->ss == 0U && d->ah == 0U) {
          for (uint8_t scanComponent = 0;
               scanComponent < d->scanNComp; ++scanComponent) {
            if (d->dcHuff[d->scanDcTbl[scanComponent]].total <= 0) {
              DIAG_PRINTF("[SLS/JPEG] scan=FAIL index=%u reason=missing-DC-table table=%u\n",
                          static_cast<unsigned>(d->scanCount),
                          static_cast<unsigned>(d->scanDcTbl[scanComponent]));
              return false;
            }
          }
        } else if (d->ss != 0U &&
                   d->acHuff[d->scanAcTbl[0]].total <= 0) {
          DIAG_PRINTF("[SLS/JPEG] scan=FAIL index=%u reason=missing-AC-table table=%u\n",
                      static_cast<unsigned>(d->scanCount),
                      static_cast<unsigned>(d->scanAcTbl[0]));
          return false;
        }
        d->br.init(&f);
        d->br.safeTail = true;
        if (!pjDecodeScan(d, rowCoefs, targetRow, nzBitmap,
                          pendingMarker)) return false;
        if (progressCallback) progressCallback(progressContext);
        break;
      default:
        // All remaining supported metadata/table markers carry a length.
        // Standalone or reserved markers are rejected rather than guessed.
        if (marker == 0x01 || marker == 0xC8 || marker == 0xCC ||
            ((marker >= 0xC0 && marker <= 0xCF) && marker != M_DHT) ||
            (marker >= 0xD0 && marker <= 0xD9)) return false;
        {
          const int len = pjRead16(f);
          if (!f.valid() || len < 2) return false;
          pjSkip(f, len - 2);
          if (!f.valid()) return false;
        }
        break;
    }
  }
}

// --- Baseline single-pass decode ---
// Single-pass baseline decoder: walks the file once, decoding and rendering
// each MCU row on the fly. Used for SOF0 images where no multi-pass needed.
static bool pjDecodeBaselinePass(MemoryFile& f, PJDecoder* d, TFT_eSPI* tft,
                                  int offsetX, int offsetY,
                                  uint8_t* workspace, size_t workspaceSize,
                                  JPEGImageInfo* info,
                                  JPEGRowCallback rowCallback,
                                  void* rowContext) {
  f.seek(0);
  if (pjRead8(f) != 0xFF || pjRead8(f) != M_SOI) return false;

  while (true) {
    int marker = pjSkipToMarker(f);
    if (marker < 0 || marker == M_EOI) return false;
    if (marker >= M_RST0 && marker <= M_RST7) continue;

    switch (marker) {
      case M_SOF0:
        if (!pjParseSOF(f, d)) return false;
        break;
      case M_DHT:
        if (!pjParseDHT(f, d)) return false;
        break;
      case M_DQT:
        if (!pjParseDQT(f, d)) return false;
        break;
      case M_DRI:
        if (!pjParseDRI(f, d)) return false;
        break;
      case M_SOS: {
        if (!pjParseSOS(f, d)) return false;
        if (d->ss != 0U || d->se != 63U || d->ah != 0U || d->al != 0U)
          return false;
        // Baseline multiscan is optional for SlideShow. This renderer handles
        // one interleaved scan only; reject separate-component scans rather
        // than displaying an incomplete image.
        if (d->scanNComp != d->nComp) {
          DIAG_PRINTF("[SLS/JPEG] multiscan baseline unsupported scanComp=%u frameComp=%u\n",
                        static_cast<unsigned>(d->scanNComp),
                        static_cast<unsigned>(d->nComp));
          return false;
        }
        for (uint8_t component = 0; component < d->nComp; ++component)
          if (!d->qtableDefined[d->comp[component].qtSel]) return false;
        d->br.init(&f);
        // Read only as many entropy bytes as the current symbol needs. This
        // keeps EOI/RST markers observable at deterministic MCU boundaries.
        d->br.safeTail = true;

        int blocksPerRow = d->mcuCntX * d->blocksPerMCU;
        size_t coefSize = blocksPerRow * 64 * sizeof(int16_t);
        size_t pixelBufSize = blocksPerRow * 64;
        size_t totalSize = coefSize + pixelBufSize;

        // Runtime heap allocation is forbidden: the slideshow arena must fit
        // every baseline row workspace for a supported image.
        if (!workspace || workspaceSize < totalSize) {
          DIAG_PRINTF("[SLS/JPEG] baseline workspace too small need=%u have=%u\n",
                        static_cast<unsigned>(totalSize),
                        static_cast<unsigned>(workspaceSize));
          return false;
        }
        uint8_t* baseBuf = workspace;

        int16_t* rowCoefs = (int16_t*)baseBuf;
        uint8_t* allBlocks = baseBuf + coefSize;

        d->mcuCount = 0;
        for (int i = 0; i < d->nComp; i++) d->comp[i].dcPred = 0;
        const int totalMcus = d->mcuCntX * d->mcuCntY;
        int decodedMcus = 0;
        uint8_t expectedRestart = 0;

        for (int row = 0; row < d->mcuCntY; row++) {
          memset(rowCoefs, 0, coefSize);

          for (int mcuX = 0; mcuX < d->mcuCntX; mcuX++) {
            for (int si = 0; si < d->scanNComp; si++) {
              int ci = d->scanCompIdx[si];
              for (int bv = 0; bv < d->comp[ci].vSamp; bv++) {
                for (int bh = 0; bh < d->comp[ci].hSamp; bh++) {
                  int idx = pjRowBlockIndex(d, mcuX, ci, bh, bv);
                  pjDecodeBaseline(d, &rowCoefs[idx * 64], si);
                  if (d->br.decodeError) return false;
                }
              }
            }
            ++decodedMcus;
            ++d->mcuCount;

            const bool restartBoundary = d->restartInterval > 0 &&
                d->mcuCount >= d->restartInterval && decodedMcus < totalMcus;
            if (restartBoundary) {
              int restartMarker = d->br.hitMarker
                  ? d->br.markerVal
                  : pjSkipEntropy(f);
              if (restartMarker != M_RST0 + expectedRestart) return false;
              expectedRestart = static_cast<uint8_t>((expectedRestart + 1U) & 7U);
              d->mcuCount = 0;
              for (int i = 0; i < d->nComp; i++) d->comp[i].dcPred = 0;
              d->br.reset();
              d->br.safeTail = true;
            } else if (d->br.hitMarker && decodedMcus < totalMcus) {
              // EOI, SOS or an out-of-place RST before all expected MCUs is a
              // truncated/corrupt stream, never a partial success.
              return false;
            }
          }

          if (tft) {
            if (!pjOutputMCURow(d, rowCoefs, row, *tft, offsetX, offsetY,
                                allBlocks, info ? info->scaleDivisor : 1U))
              return false;
            if (info) info->lastRenderedMcuRow = static_cast<int16_t>(row);
          }
          if (rowCallback) rowCallback(rowContext);
        }

        if (decodedMcus != totalMcus || d->br.decodeError) return false;
        int finalMarker = d->br.hitMarker ? d->br.markerVal : pjSkipEntropy(f);
        // Some encoders emit the scheduled restart marker even when the final
        // MCU lands exactly on the interval boundary. Accept it only in the
        // expected sequence, then require EOI immediately afterwards.
        if (finalMarker >= M_RST0 && finalMarker <= M_RST7) {
          if (finalMarker != M_RST0 + expectedRestart) return false;
          finalMarker = pjSkipEntropy(f);
        }
        return finalMarker == M_EOI;
      }
      default:
        if ((marker >= M_APP0 && marker <= M_APP15) || marker == M_COM) {
          int len = pjRead16(f); pjSkip(f, len - 2);
        } else {
          int len = pjRead16(f);
          if (len >= 2) pjSkip(f, len - 2);
        }
        break;
    }
  }
  return false;
}

// --- Progressive multi-pass decode ---
// A full 320x240 4:2:0 coefficient image would consume about 230 KiB. Instead,
// replay every scan for one frame-MCU row at a time. A compact bitmap preserves
// only the non-zero pattern of discarded earlier rows, which is sufficient to
// consume AC-refinement bits correctly.
static bool pjDecodeProgressivePass(MemoryFile& f, PJDecoder* d,
                                    TFT_eSPI* tft, int offsetX, int offsetY,
                                    uint8_t* workspace, size_t workspaceSize,
                                    JPEGImageInfo* info,
                                    JPEGRowCallback progressCallback,
                                    void* progressContext,
                                    bool verboseScanDiagnostics) {
  const uint16_t expectedWidth = d->width;
  const uint16_t expectedHeight = d->height;
  const uint8_t expectedComponents = d->nComp;
  const int blocksPerRow = d->mcuCntX * d->blocksPerMCU;
  const size_t coefficientBytes = static_cast<size_t>(blocksPerRow) * 64U *
                                  sizeof(int16_t);
  const size_t pixelBytes = tft
      ? static_cast<size_t>(blocksPerRow) * 64U
      : 0U;
  const size_t bitmapBytes =
      (static_cast<size_t>(d->totalImageBlocks) * 64U + 7U) / 8U;
  const size_t requiredBytes = coefficientBytes + pixelBytes + bitmapBytes;
  if (!workspace || workspaceSize < requiredBytes) {
    DIAG_PRINTF("[SLS/JPEG] progressive workspace too small need=%u have=%u\n",
                  static_cast<unsigned>(requiredBytes),
                  static_cast<unsigned>(workspaceSize));
    return false;
  }

  int16_t* rowCoefficients = reinterpret_cast<int16_t*>(workspace);
  uint8_t* pixelBlocks = tft ? workspace + coefficientBytes : nullptr;
  uint8_t* nonZeroBitmap = workspace + coefficientBytes + pixelBytes;

  // Validation needs one final-row pass: it decodes every unit of every scan.
  // Rendering reconstructs each row separately and never stores a full frame.
  const int firstRow = tft ? 0 : d->mcuCntY - 1;
  for (int row = firstRow; row < d->mcuCntY; ++row) {
    memset(rowCoefficients, 0, coefficientBytes);
    memset(nonZeroBitmap, 0, bitmapBytes);
    if (!pjProcessFileForRow(f, d, rowCoefficients, row, nonZeroBitmap,
                             progressCallback, progressContext,
                             verboseScanDiagnostics)) return false;
    if (d->width != expectedWidth || d->height != expectedHeight ||
        d->nComp != expectedComponents) return false;

    if (tft) {
      if (!pjOutputMCURow(d, rowCoefficients, row, *tft,
                          offsetX, offsetY, pixelBlocks,
                          info ? info->scaleDivisor : 1U)) return false;
      if (info) info->lastRenderedMcuRow = static_cast<int16_t>(row);
      if (progressCallback) progressCallback(progressContext);
    }
  }
  return true;
}

// --- Main entry points ---
// Parse and decode the complete baseline/progressive stream. A null TFT
// performs entropy/marker validation without changing display state.
static bool JPEGdecodePass(const uint8_t* data, size_t size, TFT_eSPI* tft,
                           int displayWidth, int displayHeight,
                           uint8_t* workspace, size_t workspaceSize,
                           JPEGImageInfo* callerInfo,
                           JPEGRowCallback rowCallback,
                           void* rowContext,
                           bool verboseScanDiagnostics) {
  JPEGImageInfo localInfo;
  JPEGImageInfo& info = callerInfo ? *callerInfo : localInfo;
  const JPEGPreflightResult preflight =
      JPEGpreflight(data, size, displayWidth, displayHeight, info);
  if (preflight != JPEGPreflightResult::SupportedBaseline &&
      preflight != JPEGPreflightResult::SupportedProgressive) return false;

  MemoryFile f(data, size);
  if (!f) return false;

  PJDecoder* d = &pjDecoderState;
  memset(d, 0, sizeof(*d));

  // Pre-scan only far enough to identify frame coding and dimensions.
  f.seek(0);
  bool foundSOF = false;
  while (!foundSOF) {
    const int marker = pjSkipToMarker(f);
    if (marker < 0 || marker == M_EOI) break;
    if (marker == M_SOF0 || marker == M_SOF2) {
      if ((marker == M_SOF0) !=
          (preflight == JPEGPreflightResult::SupportedBaseline)) {
        f.close();
        return false;
      }
      if (!pjParseSOF(f, d)) { f.close(); return false; }
      foundSOF = true;
    } else if (marker != M_SOI && !(marker >= M_RST0 && marker <= M_RST7)) {
      const int len = pjRead16(f);
      if (len >= 2) pjSkip(f, len - 2);
    }
  }

  if (!foundSOF || d->width == 0 || d->height == 0) {
    f.close();
    return false;
  }
  const uint8_t scaleDivisor = info.scaleDivisor == 2U ? 2U : 1U;
  const uint16_t outputWidth = static_cast<uint16_t>(
      (d->width + scaleDivisor - 1U) / scaleDivisor);
  const uint16_t outputHeight = static_cast<uint16_t>(
      (d->height + scaleDivisor - 1U) / scaleDivisor);
  if (outputWidth > static_cast<uint16_t>(displayWidth) ||
      outputHeight > static_cast<uint16_t>(displayHeight)) {
    DIAG_PRINTF("[SLS/JPEG] unsupported dimensions=%ux%u max=%dx%d\n",
                  static_cast<unsigned>(d->width),
                  static_cast<unsigned>(d->height),
                  displayWidth, displayHeight);
    f.close();
    return false;
  }
  if (d->nComp != 1U && d->nComp != 3U) {
    // ETSI permits up to four JPEG components, but this compact renderer does
    // not implement CMYK/YCCK conversion. Reject it explicitly rather than
    // producing wrong colours.
    DIAG_PRINTF("[SLS/JPEG] unsupported component count=%u (renderer supports 1 or 3)\n",
                  static_cast<unsigned>(d->nComp));
    f.close();
    return false;
  }

  const int offsetX = (displayWidth - outputWidth) / 2;
  const int offsetY = (displayHeight - outputHeight) / 2;
  const bool result = preflight == JPEGPreflightResult::SupportedProgressive
      ? pjDecodeProgressivePass(f, d, tft, offsetX, offsetY,
                                workspace, workspaceSize, &info,
                                rowCallback, rowContext,
                                verboseScanDiagnostics)
      : pjDecodeBaselinePass(f, d, tft, offsetX, offsetY,
                             workspace, workspaceSize, &info,
                             rowCallback, rowContext);
  f.close();
  return result;
}

bool JPEGvalidate(const uint8_t* data, size_t size,
                  int displayWidth, int displayHeight,
                  uint8_t* workspace, size_t workspaceSize,
                  JPEGImageInfo* info,
                  JPEGRowCallback rowCallback,
                  void* rowContext,
                  bool verboseScanDiagnostics) {
  return JPEGdecodePass(data, size, nullptr, displayWidth, displayHeight,
                        workspace, workspaceSize, info,
                        rowCallback, rowContext, verboseScanDiagnostics);
}

bool JPEGdecoder(const uint8_t* data, size_t size, TFT_eSPI& tft,
                 int displayWidth, int displayHeight,
                 uint8_t* workspace, size_t workspaceSize,
                 JPEGImageInfo* info,
                 JPEGRowCallback rowCallback,
                 void* rowContext,
                 bool verboseScanDiagnostics) {
  return JPEGdecodePass(data, size, &tft, displayWidth, displayHeight,
                        workspace, workspaceSize, info,
                        rowCallback, rowContext, verboseScanDiagnostics);
}
