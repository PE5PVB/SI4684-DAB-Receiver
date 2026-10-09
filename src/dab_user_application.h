#pragma once

#include <stddef.h>
#include <stdint.h>

namespace dab_ua {

static const uint16_t SLIDESHOW = 0x002U;
static const uint16_t TPEG = 0x004U;
static const uint16_t SPI = 0x007U;

inline const char* name(uint16_t type) {
  switch (type) {
    case SLIDESHOW: return "Slideshow";
    case TPEG: return "TPEG";
    case SPI: return "SPI";
    default: return "unknown";
  }
}

struct Application {
  uint16_t type;
  uint8_t length;
  const uint8_t* data;
};

// AN649 DAB_GET_COMPONENT_INFO: LE16 UATYPE, U8 UADATALEN, data,
// then one padding byte when UADATALEN is EVEN (including zero).
// LENUA includes padding; UADATALEN excludes it. This is the Si468x
// representation, not the packed over-the-air FIG 0/13 representation.
// Views refer to the caller's DabComponentInfo::userApplicationRaw buffer.
class Reader {
 public:
  Reader(const uint8_t* data, size_t bytes, uint8_t count)
      : data_(data), bytes_(bytes), remaining_(count),
        error_((data == nullptr && bytes != 0U) || bytes > 255U) {}

  bool next(Application& out) {
    out = Application{};
    if (error_ || remaining_ == 0U) return false;
    if (bytes_ - offset_ < 3U) return fail();
    const uint8_t length = data_[offset_ + 2U];
    const size_t recordBytes = 3U + length + ((length & 1U) ? 0U : 1U);
    if (recordBytes > bytes_ - offset_) return fail();
    out.type = static_cast<uint16_t>(data_[offset_]) |
               (static_cast<uint16_t>(data_[offset_ + 1U]) << 8);
    out.length = length;
    out.data = data_ + offset_ + 3U;
    offset_ += recordBytes;
    --remaining_;
    return true;
  }

  bool complete() const {
    return !error_ && remaining_ == 0U && offset_ == bytes_;
  }

 private:
  bool fail() { error_ = true; return false; }
  const uint8_t* data_;
  size_t bytes_;
  size_t offset_ = 0;
  uint8_t remaining_;
  bool error_;
};

// Validate the whole list before exposing any application to a consumer.
inline bool valid(const uint8_t* data, size_t bytes, uint8_t count) {
  Reader reader(data, bytes, count);
  Application application;
  while (reader.next(application)) {}
  return reader.complete();
}

}  // namespace dab_ua
