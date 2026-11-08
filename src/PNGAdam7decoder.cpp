// Adam7-interlaced PNG decoder. See PNGAdam7decoder.h for the public API.
//
// Algorithm outline:
//   1. Parse PNG chunks (IHDR / PLTE / tRNS / IDAT / IEND) and validate every
//      CRC-32 trailer.
//   2. Set up a z_stream whose internal state and dictionary live inside the
//      caller-owned workspace so no heap is touched.
//   3. For each of the 7 Adam7 passes, inflate one filtered row at a time,
//      run the standard PNG filter predictors (none/sub/up/avg/paeth),
//      convert samples to RGB565 and hand the line to the row callback.
//   4. After the final pass make sure every declared IDAT byte has been
//      consumed and the stream reached Z_STREAM_END.

#include "PNGAdam7decoder.h"
#include <PNGdec.h>          // brings in zlib (inflate, z_stream, inflate_state)
#include <string.h>
#include <new>

namespace {

const unsigned kMaxWidth = 320;
const unsigned kMaxRow   = 320 * 8;  // widest filtered row across all passes

// Working arena — placement-new'd on the caller's workspace buffer.
struct Workspace {
  inflate_state state;
  uint8_t       dictionary[32768];
  uint8_t       lines[2][kMaxRow + 4];
  uint8_t       input[512];
  uint8_t       palette[768];
  uint8_t       alpha[256];
  uint16_t      pixels[kMaxWidth];
};

// CRC-32 (IEEE 802.3 polynomial, reflected). Each PNG chunk ends with this
// value over its type + data bytes.
uint32_t crc32_ieee(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (unsigned b = 0; b < 8; b++) {
      c = (c >> 1) ^ ((c & 1u) ? 0xEDB88320u : 0u);
    }
  }
  return c ^ 0xFFFFFFFFu;
}

inline uint32_t be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
       | ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

inline bool tagIs(const uint8_t* p, const char* name) {
  return memcmp(p, name, 4) == 0;
}

// Parsed per-image state assembled from the PNG chunks.
struct Image {
  PngAdam7Info info;
  size_t       idatBytes     = 0;
  unsigned     channels      = 0;
  unsigned     paletteCount  = 0;
  bool         transparency  = false;
  uint16_t     transparent[3] = { 0, 0, 0 };
};

// Walk the chunk list once: validate CRCs, remember header / palette / tRNS,
// and total up IDAT bytes. Does not inflate anything.
bool parsePng(const uint8_t* data, size_t size, Workspace& w, Image& image,
              unsigned maxWidth, unsigned maxHeight,
              PngAdam7Pump pump, void* context) {
  const uint8_t magic[] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  if (!data || size < 45 || memcmp(data, magic, 8) != 0) return false;

  bool header = false, palette = false, trns = false, idat = false, idatEnded = false;
  memset(w.alpha, 255, sizeof(w.alpha));

  for (size_t pos = 8; pos <= size - 12; ) {
    const size_t length = be32(data + pos);
    if (length > size - pos - 12) return false;

    const uint8_t* tag = data + pos + 4;
    const uint8_t* p   = data + pos + 8;

    if (crc32_ieee(tag, length + 4) != be32(p + length)) return false;

    // Chunk name must be alphabetic; reserved bit (bit 5 of byte 2) must be clear.
    for (unsigned i = 0; i < 4; i++) {
      const uint8_t c = tag[i];
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return false;
    }
    if (tag[2] & 32) return false;

    if (!header && !tagIs(tag, "IHDR")) return false;

    if (tagIs(tag, "IHDR")) {
      if (header || pos != 8 || length != 13 || p[10] || p[11] || p[12] != 1) return false;
      image.info.width  = be32(p);
      image.info.height = be32(p + 4);
      image.info.depth  = p[8];
      image.info.type   = p[9];
      if (!image.info.width || !image.info.height
          || image.info.width > maxWidth || image.info.width > kMaxWidth
          || image.info.height > maxHeight || image.info.height > 240) return false;
      switch (image.info.type) {
        case 0: image.channels = 1; break;  // grayscale
        case 2: image.channels = 3; break;  // RGB
        case 3: image.channels = 1; break;  // palette index
        case 4: image.channels = 2; break;  // grayscale + alpha
        case 6: image.channels = 4; break;  // RGBA
        default: return false;
      }
      const unsigned d = image.info.depth;
      const bool depthOk =
          image.info.type == 3 ? (d == 1 || d == 2 || d == 4 || d == 8)
        : image.info.type == 0 ? (d == 1 || d == 2 || d == 4 || d == 8 || d == 16)
        :                        (d == 8 || d == 16);
      if (!depthOk) return false;
      header = true;
    }
    else if (tagIs(tag, "PLTE")) {
      if (palette || idat || trns || !length || length > 768 || length % 3
          || image.info.type == 0 || image.info.type == 4) return false;
      image.paletteCount = (unsigned)(length / 3);
      if (image.info.type == 3 && image.paletteCount > (1u << image.info.depth)) return false;
      memcpy(w.palette, p, length);
      palette = true;
    }
    else if (tagIs(tag, "tRNS")) {
      if (trns || idat) return false;
      if (image.info.type == 3) {
        if (!palette || !length || length > image.paletteCount) return false;
        memcpy(w.alpha, p, length);
      } else if (image.info.type == 0 || image.info.type == 2) {
        const unsigned count = image.info.type == 0 ? 1 : 3;
        if (length != count * 2) return false;
        for (unsigned i = 0; i < count; i++) {
          image.transparent[i] = ((uint16_t)p[i * 2] << 8) | p[i * 2 + 1];
          if (image.info.depth < 16 && image.transparent[i] >= (1u << image.info.depth)) return false;
        }
        image.transparency = true;
      } else {
        return false;
      }
      trns = true;
    }
    else if (tagIs(tag, "IDAT")) {
      if (idatEnded || (image.info.type == 3 && !palette)) return false;
      idat = true;
      image.idatBytes += length;
    }
    else if (tagIs(tag, "IEND")) {
      return idat && image.idatBytes && !length && pos + 12 == size;
    }
    else if (!(tag[0] & 32)) {
      return false;  // unknown critical chunk
    }

    if (idat && !tagIs(tag, "IDAT")) idatEnded = true;
    pos += length + 12;
    if (pump) pump(context);
  }
  return false;
}

// Streams the concatenated IDAT payload for the inflater. Hops across IDAT
// boundaries transparently; stops when there is no more IDAT data.
struct IdatInput {
  const uint8_t* data;
  size_t         size;
  size_t         pos       = 8;
  size_t         remaining = 0;

  IdatInput(const uint8_t* d, size_t n) : data(d), size(n) {}

  bool fill(z_stream& stream, Workspace& w) {
    while (!remaining) {
      if (pos > size - 12) return false;
      const size_t chunkLen = be32(data + pos);
      if (tagIs(data + pos + 4, "IDAT")) {
        remaining = chunkLen;
        pos += 8;
        if (!chunkLen) pos += 4;
      } else {
        pos += chunkLen + 12;
      }
    }
    const size_t take = remaining < sizeof(w.input) ? remaining : sizeof(w.input);
    memcpy(w.input, data + pos, take);
    pos += take;
    remaining -= take;
    if (!remaining) pos += 4;  // skip chunk CRC
    stream.next_in  = w.input;
    stream.avail_in = (unsigned)take;
    return true;
  }
};

// Dummy allocator overrides — zlib would otherwise call malloc/free which we
// cannot afford on an ESP32 with fragmented heap during image decode.
voidpf noAllocate(voidpf, uInt, uInt) { return nullptr; }
void   noFree    (voidpf, voidpf)     { }

// Inflater that stores all state inside the workspace and consumes the IDAT
// stream one row at a time. `exact()` returns false if the stream ends early.
struct Inflater {
  z_stream   stream { };
  IdatInput  input;
  Workspace& work;
  bool       ended = false;
  bool       valid = false;

  Inflater(const uint8_t* data, size_t size, Workspace& w)
    : input(data, size), work(w) {
    stream.state  = reinterpret_cast<internal_state*>(&w.state);
    w.state.window = w.dictionary;
    stream.zalloc = noAllocate;
    stream.zfree  = noFree;
    valid = inflateInit(&stream) == Z_OK;
  }

  ~Inflater() {
    if (valid) inflateEnd(&stream);
  }

  // Read exactly `bytes` decoded bytes into `out`. Keep output chunks below
  // inflate_fast's 258-byte threshold so PNGdec's bulk-copy path never fires.
  bool exact(uint8_t* out, unsigned bytes) {
    if (!valid || ended) return false;
    while (bytes) {
      const unsigned part = bytes < 128 ? bytes : 128;
      stream.next_out  = out;
      stream.avail_out = part;
      while (stream.avail_out) {
        if (!stream.avail_in && !input.fill(stream, work)) return false;
        const unsigned beforeIn  = stream.avail_in;
        const unsigned beforeOut = stream.avail_out;
        const int rc = inflate(&stream, Z_NO_FLUSH, 1);
        if (rc == Z_STREAM_END) {
          ended = true;
          return !stream.avail_out && bytes == part;
        }
        if (rc != Z_OK && rc != Z_BUF_ERROR) return false;
        if (beforeIn == stream.avail_in && beforeOut == stream.avail_out) return false;
      }
      out   += part;
      bytes -= part;
    }
    return true;
  }

  // After the last image byte, confirm the deflate trailer parses cleanly and
  // the total consumed bytes equal the sum of all IDAT lengths.
  bool finish(size_t expectedBytes) {
    if (!ended) {
      uint8_t extra = 0;
      stream.next_out  = &extra;
      stream.avail_out = 1;
      while (!ended) {
        if (!stream.avail_in && !input.fill(stream, work)) return false;
        const unsigned beforeIn = stream.avail_in;
        const int rc = inflate(&stream, Z_NO_FLUSH, 1);
        if (!stream.avail_out) return false;   // any extra output = corrupt
        if (rc == Z_STREAM_END) ended = true;
        else if ((rc != Z_OK && rc != Z_BUF_ERROR) || beforeIn == stream.avail_in) return false;
      }
    }
    return stream.total_in == expectedBytes;
  }
};

// Standard PNG Paeth predictor: pick the neighbour (left/above/upper-left)
// whose value is closest to (a + b - c).
unsigned paeth(unsigned a, unsigned b, unsigned c) {
  const int p  = (int)a + (int)b - (int)c;
  const unsigned x = (unsigned)(p > (int)a ? p - (int)a : (int)a - p);
  const unsigned y = (unsigned)(p > (int)b ? p - (int)b : (int)b - p);
  const unsigned z = (unsigned)(p > (int)c ? p - (int)c : (int)c - p);
  return (x <= y && x <= z) ? a : (y <= z ? b : c);
}

// Apply the per-row filter (byte 0 selects one of five predictors) in-place.
bool unfilter(uint8_t* line, const uint8_t* previous, unsigned bytes, unsigned stride) {
  const unsigned filter = line[0];
  if (filter > 4) return false;
  for (unsigned i = 0; i < bytes; i++) {
    const unsigned left   = i >= stride ? line[1 + i - stride] : 0;
    const unsigned up     = previous[1 + i];
    const unsigned corner = i >= stride ? previous[1 + i - stride] : 0;
    const unsigned predictor =
        filter == 1 ? left
      : filter == 2 ? up
      : filter == 3 ? (left + up) / 2
      : filter == 4 ? paeth(left, up, corner)
      :               0;
    line[i + 1] = (uint8_t)(line[i + 1] + predictor);
  }
  return true;
}

// Extract one sample from a filtered scanline. Handles bit depths 1/2/4/8/16.
unsigned sample(const uint8_t* p, unsigned index, unsigned depth) {
  if (depth == 16) return ((unsigned)p[index * 2] << 8) | p[index * 2 + 1];
  if (depth == 8)  return p[index];
  const unsigned bit = index * depth;
  return (p[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
}

// Normalise a sample of arbitrary bit depth to 8 bits.
unsigned to8bit(unsigned value, unsigned depth) {
  if (depth == 16) return value >> 8;
  if (depth == 8)  return value;
  return value * 255 / ((1u << depth) - 1);
}

// Convert one pixel to RGB565 with pre-multiplied alpha against a white BG.
bool toRGB565(const uint8_t* p, unsigned x, const Image& image, const Workspace& w, uint16_t& out) {
  unsigned v[4] = { 0, 0, 0, 0 };
  for (unsigned i = 0; i < image.channels; i++) {
    v[i] = sample(p, x * image.channels + i, image.info.depth);
  }

  unsigned r, g, b, a = 255;
  if (image.info.type == 3) {
    if (v[0] >= image.paletteCount) return false;
    r = w.palette[v[0] * 3];
    g = w.palette[v[0] * 3 + 1];
    b = w.palette[v[0] * 3 + 2];
    a = w.alpha[v[0]];
  } else {
    r = to8bit(v[0], image.info.depth);
    if (image.info.type == 0 || image.info.type == 4) {
      g = b = r;
      if (image.info.type == 4) a = to8bit(v[1], image.info.depth);
    } else {
      g = to8bit(v[1], image.info.depth);
      b = to8bit(v[2], image.info.depth);
      if (image.info.type == 6) a = to8bit(v[3], image.info.depth);
    }
    if (image.transparency) {
      bool transparent = true;
      for (unsigned i = 0; i < (image.info.type == 0 ? 1u : 3u); i++) {
        if (v[i] != image.transparent[i]) transparent = false;
      }
      if (transparent) a = 0;
    }
  }

  // Premultiply alpha against white background so rendering over a cleared
  // (white) framebuffer looks the same as the authored image.
  r = (r * a + 255 * (255 - a) + 127) / 255;
  g = (g * a + 255 * (255 - a) + 127) / 255;
  b = (b * a + 255 * (255 - a) + 127) / 255;
  out = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
  return true;
}

}  // anonymous namespace

size_t PNGAdam7WorkspaceBytes(void) {
  return sizeof(Workspace);
}

bool PNGAdam7decoder(const uint8_t* data, size_t size,
                     void* workspace, size_t capacity,
                     unsigned maxWidth, unsigned maxHeight,
                     PngAdam7Info& info,
                     PngAdam7Row rowCallback, void* context,
                     PngAdam7Pump pump) {
  info = PngAdam7Info{};
  info.stage = "workspace";
  if (!workspace) return false;

  // Caller-provided buffer may be byte-aligned; nudge forward to Workspace's
  // natural alignment so the embedded inflate_state's 8-byte accumulator sits
  // on the right boundary.
  const size_t padding =
    (alignof(Workspace) - (uintptr_t)workspace % alignof(Workspace)) % alignof(Workspace);
  info.padding = (unsigned)padding;
  if (padding > capacity || capacity - padding < sizeof(Workspace)) return false;

  void* aligned = (uint8_t*)workspace + padding;
  Workspace& w = *new (aligned) Workspace{};
  Image image;

  const bool parsed = parsePng(data, size, w, image, maxWidth, maxHeight, pump, context);
  image.info.padding = (unsigned)padding;
  info = image.info;
  info.stage = "header";
  if (!parsed) return false;

  info.stage = "inflate-init";
  Inflater inflater(data, size, w);
  if (!inflater.valid) return false;

  // Adam7 pass origins + strides (ISO 15948 §8.2).
  const unsigned x0[] = { 0, 4, 0, 2, 0, 1, 0 };
  const unsigned y0[] = { 0, 0, 4, 0, 2, 0, 1 };
  const unsigned dx[] = { 8, 8, 4, 4, 2, 2, 1 };
  const unsigned dy[] = { 8, 8, 8, 4, 4, 2, 2 };

  const unsigned bits   = image.channels * image.info.depth;
  const unsigned stride = (bits + 7) / 8;

  for (unsigned pass = 0; pass < 7; pass++) {
    if (image.info.width <= x0[pass] || image.info.height <= y0[pass]) continue;

    const unsigned width = (image.info.width - x0[pass] + dx[pass] - 1) / dx[pass];
    const unsigned pitch = (width * bits + 7) / 8;
    if (pitch > kMaxRow) return false;

    uint8_t* current  = w.lines[0];
    uint8_t* previous = w.lines[1];
    memset(previous, 0, pitch + 1);

    for (unsigned y = y0[pass]; y < image.info.height; y += dy[pass]) {
      info.pass  = pass + 1;
      info.row   = y;
      info.stage = "inflate";
      const bool inflated = inflater.exact(current, pitch + 1);
      info.input  = (unsigned)inflater.stream.total_in;
      info.output = (unsigned)inflater.stream.total_out;
      info.detail = inflater.stream.msg ? inflater.stream.msg : "";
      if (!inflated) return false;

      info.stage = "filter";
      if (!unfilter(current, previous, pitch, stride)) return false;

      info.stage = "pixels";
      for (unsigned x = 0; x < width; x++) {
        if (!toRGB565(current + 1, x, image, w, w.pixels[x])) return false;
      }

      info.stage = "draw-callback";
      if (rowCallback && !rowCallback(context, x0[pass], y, dx[pass], w.pixels, width)) return false;

      if (pump) pump(context);

      uint8_t* swap = previous;
      previous = current;
      current  = swap;
    }
  }

  info.stage = "finish";
  const bool finished = inflater.finish(image.idatBytes);
  info.input  = (unsigned)inflater.stream.total_in;
  info.output = (unsigned)inflater.stream.total_out;
  info.detail = inflater.stream.msg ? inflater.stream.msg : "";
  if (!finished) return false;

  info.stage = "ok";
  return true;
}
