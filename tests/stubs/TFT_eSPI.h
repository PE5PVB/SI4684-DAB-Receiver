#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

struct SerialStub {
  template <typename T> void print(const T&) {}
  template <typename T> void println(const T&) {}
  void println() {}
  template <typename... Args> void printf(const char*, Args...) {}
};

extern SerialStub Serial;

class TFT_eSPI {
 public:
  int pushedLines = 0;
  int pushedPixels = 0;
  std::vector<uint16_t> pixels;

  void pushImage(int32_t, int32_t, int32_t width, int32_t height,
                 uint16_t* data) {
    ++pushedLines;
    pushedPixels += width * height;
    pixels.insert(pixels.end(), data, data + width * height);
  }
};
