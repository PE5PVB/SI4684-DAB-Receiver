#pragma once

#include <stdint.h>

// Host-side diagnostic state only. No application selection or payload routing.
namespace dab_diag {

static constexpr uint16_t XPAD_REQUESTED = 0x0097U;
static constexpr int8_t NOT_ATTEMPTED = 127; // outside vendor Result values

struct XpadReadback {
  uint16_t value = 0;
  int8_t setResult = NOT_ATTEMPTED;
  int8_t getResult = NOT_ATTEMPTED;
  bool valid = false;
  bool matches() const {
    return valid && setResult == 0 && getResult == 0 && value == XPAD_REQUESTED;
  }
};

inline const char* rawMeaning(uint8_t source, uint16_t raw) {
  if (source == 3U) return "reserved";
  if (source == 2U) return "DLS-raw";
  if (raw == 2U) return "SLS-candidate";
  if (raw == 7U) return "SPI-candidate";
  return "unknown";
}

inline const char* sourceName(uint8_t source) {
  switch (source) {
    case 0: return "data";
    case 1: return "PAD";
    case 2: return "DLS";
    default: return "reserved";
  }
}

// A global bound is stronger than per-key pacing: rapidly alternating identities
// cannot flood UART. The suppression count covers all samples since the last line.
struct RawLog {
  uint32_t generation = 0;
  uint32_t lastMs = 0;
  uint32_t suppressed = 0;
  bool logged = false;

  bool ready(uint32_t now, uint32_t token, bool debug, int capacity) {
    if (!debug || generation != token) {
      *this = RawLog{};
      generation = token;
    }
    if (!debug) return false;
    if (capacity < 256 || (logged && static_cast<uint32_t>(now - lastMs) < 5000U)) {
      if (suppressed != UINT32_MAX) ++suppressed;
      return false;
    }
    logged = true;
    lastMs = now;
    return true;
  }
  void emitted() { suppressed = 0; }
};

} // namespace dab_diag
