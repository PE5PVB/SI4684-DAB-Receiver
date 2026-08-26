#include "JPEGdecoder.h"

class MemoryFile {
 public:
  MemoryFile(const uint8_t* data, size_t size) : data_(data), size_(size), position_(0) {}
  explicit operator bool() const { return data_ != nullptr && size_ != 0; }
  int read() { return position_ < size_ ? data_[position_++] : -1; }
  bool seek(size_t position) {
    if (position > size_) return false;
    position_ = position;
    return true;
  }
  size_t position() const { return position_; }
  void close() {}

 private:
  const uint8_t* data_;
  size_t size_;
  size_t position_;
};

// ============================================================================
// JPEG decoder for ESP32 (no PSRAM)
//
// Uses multi-pass row-by-row decoding: for each MCU row, re-reads the file
// from the start and decodes all scans, storing only the current row's DCT
// coefficients in RAM (~15-20KB). This avoids needing the full 225KB+
// coefficient buffer that traditional progressive decoders require.
//
// A non-zero bitmap tracks which coefficient positions have been set by
// AC-first scans, so that AC-refine scans read the correct number of bits
// even when blocks are being discarded (not in the target row).
//
// Supports: SOF0 (baseline) and SOF2 (progressive DCT)
// Subsampling: YCbCr 4:4:4 / 4:2:2 / 4:2:0 / grayscale / non-standard
// Max image size: 320x240 pixels
// ============================================================================

#define PJ_MAX_COMPONENTS 3
#define PJ_MAX_HTABLES    4

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
  if (v < vt) v += (-1 << bits) + 1;
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

  void init(MemoryFile* f) {
    file = f; buf = 0; bits = 0;
    hitMarker = false; markerVal = 0;
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

  int getBits(int n) {
    if (bits < n) fillBits();
    if (bits < n) return 0;
    bits -= n;
    return (buf >> bits) & ((1 << n) - 1);
  }

  int getBit() { return getBits(1); }

  int peekBits(int n) {
    if (bits < n) fillBits();
    if (bits < n) return 0;
    return (buf >> (bits - n)) & ((1 << n) - 1);
  }

  void skipBits(int n) { bits -= n; }
};

// --- Huffman decode ---
static int pjHuffDecode(PJBitReader* br, PJHuffTable* ht) {
  int look = br->peekBits(8);
  if (ht->lookLen[look]) {
    br->skipBits(ht->lookLen[look]);
    return ht->lookSym[look];
  }
  int code = br->getBits(1);
  for (int l = 1; l <= 16; l++) {
    if (code <= ht->maxcode[l]) {
      return ht->vals[ht->valptr[l] + code - (ht->maxcode[l] - ht->bits[l] + 1)];
    }
    code = (code << 1) | br->getBits(1);
  }
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
  int totalImageBlocks;
};

// --- Compute global block offsets after SOF parsing ---
static void pjComputeBlockOffsets(PJDecoder* d) {
  int offset = 0;
  for (int c = 0; c < d->nComp; c++) {
    d->compBlockOffset[c] = offset;
    offset += (d->mcuCntX * d->comp[c].hSamp) * (d->mcuCntY * d->comp[c].vSamp);
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
  int len = pjRead16(f) - 2;
  while (len > 0) {
    int info = pjRead8(f); len--;
    int tblIdx = info & 0x0F;
    int prec = (info >> 4) & 0x0F;
    if (tblIdx > 3) return false;
    for (int i = 0; i < 64; i++) {
      if (prec) {
        d->qtable[tblIdx][zigzag[i]] = pjRead16(f);
        len -= 2;
      } else {
        d->qtable[tblIdx][zigzag[i]] = pjRead8(f);
        len--;
      }
    }
  }
  return true;
}

// --- Parse DHT ---
// DHT (Define Huffman Table) parser - stores DC/AC tables for each component.
static bool pjParseDHT(MemoryFile& f, PJDecoder* d) {
  int len = pjRead16(f) - 2;
  while (len > 0) {
    int info = pjRead8(f); len--;
    int cls = (info >> 4) & 0x0F;
    int tblIdx = info & 0x0F;
    if (tblIdx > 3) return false;
    PJHuffTable* ht = (cls == 0) ? &d->dcHuff[tblIdx] : &d->acHuff[tblIdx];
    int total = 0;
    for (int i = 1; i <= 16; i++) {
      ht->bits[i] = pjRead8(f); len--;
      total += ht->bits[i];
    }
    for (int i = 0; i < total; i++) {
      ht->vals[i] = pjRead8(f); len--;
    }
    pjBuildHuff(ht);
  }
  return true;
}

// --- Parse SOF2 ---
// SOF (Start Of Frame) parser - extracts image dimensions and subsampling info.
static bool pjParseSOF(MemoryFile& f, PJDecoder* d) {
  pjRead16(f); // length
  if (pjRead8(f) != 8) return false; // precision must be 8
  d->height = pjRead16(f);
  d->width = pjRead16(f);
  d->nComp = pjRead8(f);
  if (d->nComp > PJ_MAX_COMPONENTS) return false;

  d->maxH = 0; d->maxV = 0;
  for (int i = 0; i < d->nComp; i++) {
    d->comp[i].id = pjRead8(f);
    int samp = pjRead8(f);
    d->comp[i].hSamp = (samp >> 4) & 0x0F;
    d->comp[i].vSamp = samp & 0x0F;
    d->comp[i].qtSel = pjRead8(f);
    if (d->comp[i].hSamp > d->maxH) d->maxH = d->comp[i].hSamp;
    if (d->comp[i].vSamp > d->maxV) d->maxV = d->comp[i].vSamp;
  }

  d->mcuW = d->maxH * 8;
  d->mcuH = d->maxV * 8;
  d->mcuCntX = (d->width + d->mcuW - 1) / d->mcuW;
  d->mcuCntY = (d->height + d->mcuH - 1) / d->mcuH;

  d->blocksPerMCU = 0;
  for (int i = 0; i < d->nComp; i++) {
    d->blocksPerMCU += d->comp[i].hSamp * d->comp[i].vSamp;
  }

  pjComputeBlockOffsets(d);
  return true;
}

// --- Parse SOS ---
// SOS (Start Of Scan) parser - reads component selectors and the band/Ah/Al
// fields that drive progressive-mode pass dispatch.
static bool pjParseSOS(MemoryFile& f, PJDecoder* d) {
  pjRead16(f); // length
  d->scanNComp = pjRead8(f);
  if (d->scanNComp > PJ_MAX_COMPONENTS) return false;

  for (int i = 0; i < d->scanNComp; i++) {
    int id = pjRead8(f);
    int tbl = pjRead8(f);
    d->scanCompIdx[i] = 0;
    for (int c = 0; c < d->nComp; c++) {
      if (d->comp[c].id == id) { d->scanCompIdx[i] = c; break; }
    }
    d->scanDcTbl[i] = (tbl >> 4) & 0x0F;
    d->scanAcTbl[i] = tbl & 0x0F;
  }
  d->ss = pjRead8(f);
  d->se = pjRead8(f);
  int approx = pjRead8(f);
  d->ah = (approx >> 4) & 0x0F;
  d->al = approx & 0x0F;
  return true;
}

// --- Parse DRI ---
static void pjParseDRI(MemoryFile& f, PJDecoder* d) {
  pjRead16(f);
  d->restartInterval = pjRead16(f);
}

// --- Entropy decoders ---

static void pjDecodeDCFirst(PJDecoder* d, int16_t* coef, int compScanIdx) {
  PJHuffTable* ht = &d->dcHuff[d->scanDcTbl[compScanIdx]];
  int s = pjHuffDecode(&d->br, ht);
  int diff = (s > 0) ? pjReceive(&d->br, s) : 0;
  int ci = d->scanCompIdx[compScanIdx];
  d->comp[ci].dcPred += diff;
  coef[0] = (int16_t)(d->comp[ci].dcPred << d->al);
}

static void pjDecodeDCRefine(PJDecoder* d, int16_t* coef) {
  coef[0] |= (d->br.getBit() << d->al);
}

static void pjDecodeACFirst(PJDecoder* d, int16_t* coef, int compScanIdx) {
  PJHuffTable* ht = &d->acHuff[d->scanAcTbl[compScanIdx]];

  if (d->eobRun > 0) { d->eobRun--; return; }

  for (int k = d->ss; k <= d->se; k++) {
    int rs = pjHuffDecode(&d->br, ht);
    int s = rs & 0x0F;
    int r = rs >> 4;

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
      if (k > d->se) break;
      int v = pjReceive(&d->br, s);
      coef[zigzag[k]] = (int16_t)(v << d->al);
    }
  }
}

static void pjDecodeACRefine(PJDecoder* d, int16_t* coef, int compScanIdx) {
  PJHuffTable* ht = &d->acHuff[d->scanAcTbl[compScanIdx]];
  int p1 = 1 << d->al;
  int m1 = (-1) << d->al;
  int k = d->ss;

  if (d->eobRun == 0) {
    while (k <= d->se) {
      int rs = pjHuffDecode(&d->br, ht);
      int s = rs & 0x0F;
      int r = rs >> 4;

      if (s == 0) {
        if (r < 15) {
          d->eobRun = (1 << r);
          if (r > 0) d->eobRun += d->br.getBits(r);
          break;
        }
      } else if (s != 1) {
        return;
      }

      int newVal = 0;
      if (s == 1) {
        newVal = d->br.getBit() ? p1 : m1;
      }

      while (k <= d->se) {
        int zz = zigzag[k];
        if (coef[zz] != 0) {
          if (d->br.getBit()) {
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
        if (d->br.getBit()) {
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
  int diff = (s > 0) ? pjReceive(&d->br, s) : 0;
  int ci = d->scanCompIdx[compScanIdx];
  d->comp[ci].dcPred += diff;
  coef[0] = d->comp[ci].dcPred;

  PJHuffTable* acHt = &d->acHuff[d->scanAcTbl[compScanIdx]];
  for (int k = 1; k <= 63; k++) {
    int rs = pjHuffDecode(&d->br, acHt);
    int r = rs >> 4;
    s = rs & 0x0F;
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

// --- Handle restart marker ---
static void pjHandleRestart(PJDecoder* d) {
  if (d->restartInterval > 0) {
    d->mcuCount++;
    if (d->mcuCount >= d->restartInterval) {
      d->mcuCount = 0;
      for (int i = 0; i < d->nComp; i++) d->comp[i].dcPred = 0;
      d->eobRun = 0;
      d->br.reset();
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

// --- Decode a scan's entropy data for the target MCU row ---
static void pjDecodeScan(PJDecoder* d, int16_t* rowCoefs, int targetMCURow,
                         uint8_t* nzBitmap) {
  d->br.reset();
  d->eobRun = 0;
  d->mcuCount = 0;
  for (int i = 0; i < d->nComp; i++) d->comp[i].dcPred = 0;

  if (d->scanNComp > 1) {
    // --- Interleaved scan ---
    int totalMCUs = d->mcuCntX * (targetMCURow + 1);
    int startMCU = d->mcuCntX * targetMCURow;

    for (int mcu = 0; mcu < totalMCUs; mcu++) {
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
          }
        }
      }
      pjHandleRestart(d);
    }
  } else {
    // --- Non-interleaved scan (single component) ---
    int ci = d->scanCompIdx[0];
    int blockCols = d->mcuCntX * d->comp[ci].hSamp;
    int startBlockRow = targetMCURow * d->comp[ci].vSamp;
    int endBlockRow = startBlockRow + d->comp[ci].vSamp - 1;
    int totalBlocks = blockCols * (endBlockRow + 1);

    for (int blk = 0; blk < totalBlocks; blk++) {
      int bCol = blk % blockCols;
      int bRow = blk / blockCols;
      bool store = (bRow >= startBlockRow && bRow <= endBlockRow);

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

      // Restart handling for non-interleaved scans
      if (d->restartInterval > 0) {
        d->mcuCount++;
        if (d->mcuCount >= d->restartInterval) {
          d->mcuCount = 0;
          d->comp[ci].dcPred = 0;
          d->eobRun = 0;
          d->br.reset();
        }
      }
    }
  }
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
static void pjOutputMCURow(PJDecoder* d, int16_t* rowCoefs, int mcuRow,
                           TFT_eSPI& tft, int offsetX, int offsetY,
                           uint8_t* allBlocks) {
  int totalBlocks = d->mcuCntX * d->blocksPerMCU;
  uint16_t lineBuffer[320];

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

    for (int mcuX = 0; mcuX < d->mcuCntX; mcuX++) {
      int mcuBase = mcuX * d->blocksPerMCU;

      for (int px = 0; px < (int)d->mcuW; px++) {
        int absX = mcuX * d->mcuW + px;
        if (absX >= d->width) break;

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

        lineBuffer[absX] = pjYCbCrToRGB565(yVal, cbVal, crVal);
      }
    }

    tft.pushImage(offsetX, offsetY + absY, d->width, 1, lineBuffer);
  }
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
                                int targetRow, uint8_t* nzBitmap) {
  f.seek(0);
  if (pjRead8(f) != 0xFF || pjRead8(f) != M_SOI) return false;

  bool sofDone = false;

  while (true) {
    int marker = pjSkipToMarker(f);
    if (marker < 0) return false;
    if (marker == M_EOI) break;
    if (marker >= M_RST0 && marker <= M_RST7) continue;

    switch (marker) {
      case M_SOF2:
        if (!sofDone) {
          if (!pjParseSOF(f, d)) return false;
          sofDone = true;
        } else {
          int len = pjRead16(f); pjSkip(f, len - 2);
        }
        break;
      case M_DHT:
        if (!pjParseDHT(f, d)) return false;
        break;
      case M_DQT:
        if (!pjParseDQT(f, d)) return false;
        break;
      case M_DRI:
        pjParseDRI(f, d);
        break;
      case M_SOS:
        if (!pjParseSOS(f, d)) return false;
        d->br.init(&f);
        pjDecodeScan(d, rowCoefs, targetRow, nzBitmap);
        if (!d->br.hitMarker) {
          marker = pjSkipEntropy(f);
          if (marker == M_EOI) return true;
          if (marker < 0) return false;
          f.seek(f.position() - 2);
        } else {
          if (d->br.markerVal == M_EOI) return true;
          f.seek(f.position() - 2);
        }
        break;
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
  return true;
}

// --- Baseline single-pass decode ---
// Single-pass baseline decoder: walks the file once, decoding and rendering
// each MCU row on the fly. Used for SOF0 images where no multi-pass needed.
static bool pjDecodeBaselinePass(MemoryFile& f, PJDecoder* d, TFT_eSPI& tft,
                                  int offsetX, int offsetY) {
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
        pjParseDRI(f, d);
        break;
      case M_SOS: {
        if (!pjParseSOS(f, d)) return false;
        d->br.init(&f);

        int blocksPerRow = d->mcuCntX * d->blocksPerMCU;
        size_t coefSize = blocksPerRow * 64 * sizeof(int16_t);
        size_t pixelBufSize = blocksPerRow * 64;
        size_t totalSize = coefSize + pixelBufSize;
        uint8_t* baseBuf = (uint8_t*)malloc(totalSize);
        if (!baseBuf) return false;
        int16_t* rowCoefs = (int16_t*)baseBuf;
        uint8_t* allBlocks = baseBuf + coefSize;

        d->mcuCount = 0;
        for (int i = 0; i < d->nComp; i++) d->comp[i].dcPred = 0;

        for (int row = 0; row < d->mcuCntY; row++) {
          memset(rowCoefs, 0, coefSize);

          for (int mcuX = 0; mcuX < d->mcuCntX; mcuX++) {
            for (int si = 0; si < d->scanNComp; si++) {
              int ci = d->scanCompIdx[si];
              for (int bv = 0; bv < d->comp[ci].vSamp; bv++) {
                for (int bh = 0; bh < d->comp[ci].hSamp; bh++) {
                  int idx = pjRowBlockIndex(d, mcuX, ci, bh, bv);
                  pjDecodeBaseline(d, &rowCoefs[idx * 64], si);
                }
              }
            }
            if (d->restartInterval > 0) {
              d->mcuCount++;
              if (d->mcuCount >= d->restartInterval) {
                d->mcuCount = 0;
                for (int i = 0; i < d->nComp; i++) d->comp[i].dcPred = 0;
                d->br.reset();
              }
            }
            // Handle RST markers encountered by bitreader read-ahead
            if (d->br.hitMarker && d->br.markerVal >= M_RST0 && d->br.markerVal <= M_RST7) {
              d->mcuCount = 0;
              for (int i = 0; i < d->nComp; i++) d->comp[i].dcPred = 0;
              d->br.reset();
            }
            if (d->br.hitMarker) break;
          }

          pjOutputMCURow(d, rowCoefs, row, tft, offsetX, offsetY, allBlocks);
          if (d->br.hitMarker) break;
        }

        free(baseBuf);
        return true;
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

// --- Main entry point ---
// Public entry point: opens the file, parses headers, then dispatches to
// either the single-pass baseline decoder (SOF0) or the multi-pass
// progressive decoder (SOF2). Returns true on success.
bool JPEGdecoder(const uint8_t* data, size_t size, TFT_eSPI& tft,
                 int displayWidth, int displayHeight) {
  MemoryFile f(data, size);
  if (!f) return false;

  PJDecoder* d = (PJDecoder*)calloc(1, sizeof(PJDecoder));
  if (!d) { f.close(); return false; }

  // Pre-scan to get dimensions and type
  f.seek(0);
  bool foundSOF = false;
  bool isBaseline = false;
  while (!foundSOF) {
    int marker = pjSkipToMarker(f);
    if (marker < 0 || marker == M_EOI) break;
    if (marker == M_SOF0) {
      pjParseSOF(f, d);
      isBaseline = true;
      foundSOF = true;
    } else if (marker == M_SOF2) {
      pjParseSOF(f, d);
      foundSOF = true;
    } else if (marker != M_SOI && !(marker >= M_RST0 && marker <= M_RST7)) {
      int len = pjRead16(f);
      if (len >= 2) pjSkip(f, len - 2);
    }
  }

  if (!foundSOF || d->width == 0 || d->height == 0) {
    free(d); f.close(); return false;
  }

  int offsetX = (displayWidth - d->width) / 2;
  int offsetY = (displayHeight - d->height) / 2;

  bool result;
  if (isBaseline) {
    result = pjDecodeBaselinePass(f, d, tft, offsetX, offsetY);
  } else {
    // Progressive: multi-pass row-by-row decode
    // Single allocation to reduce heap fragmentation on ESP32
    int blocksPerRow = d->mcuCntX * d->blocksPerMCU;
    size_t coefSize = blocksPerRow * 64 * sizeof(int16_t);
    size_t pixelBufSize = blocksPerRow * 64;
    size_t bitmapSize = d->totalImageBlocks * 8;
    size_t totalSize = coefSize + pixelBufSize + bitmapSize;

    uint8_t* progBuf = (uint8_t*)calloc(1, totalSize);
    uint8_t* nzBitmap;
    if (!progBuf) {
      // Fallback: allocate without nzBitmap (AC refine may be slightly degraded)
      bitmapSize = 0;
      totalSize = coefSize + pixelBufSize;
      progBuf = (uint8_t*)calloc(1, totalSize);
      if (!progBuf) {
        free(d); f.close();
        return false;
      }
      nzBitmap = nullptr;
    } else {
      nzBitmap = progBuf + coefSize + pixelBufSize;
    }

    int16_t* rowCoefs = (int16_t*)progBuf;
    uint8_t* allBlocks = progBuf + coefSize;

    for (int row = 0; row < d->mcuCntY; row++) {
      memset(rowCoefs, 0, coefSize);
      if (nzBitmap) memset(nzBitmap, 0, bitmapSize);
      pjProcessFileForRow(f, d, rowCoefs, row, nzBitmap);
      pjOutputMCURow(d, rowCoefs, row, tft, offsetX, offsetY, allBlocks);
    }

    free(progBuf);
    result = true;
  }

  free(d);
  f.close();
  return result;
}
