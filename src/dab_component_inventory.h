#pragma once

#include <stddef.h>
#include <stdint.h>

namespace dab_inventory {

inline uint16_t le16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
inline uint32_t le32(const uint8_t* p) {
  return le16(p) | (static_cast<uint32_t>(le16(p + 2)) << 16);
}

// AN649 Rev 1.9 Table 14. The retained CID itself is the metadata source;
// do not allocate a second component table or truncate command IDs.
struct Component {
  uint32_t cid;
  uint8_t tmid() const { return static_cast<uint8_t>((cid >> 14) & 3U); }
  uint8_t type() const { return static_cast<uint8_t>((cid >> 18) & 63U); }
  bool dataGroups() const { return tmid() == 3U && (cid & 0x2000U) != 0U; }
  bool primary() const { return (cid & 0x00020000UL) != 0U; }
  bool ca() const { return (cid & 0x00010000UL) != 0U; }
  bool uaValid() const { return (cid & 0x01000000UL) != 0U; }
  uint16_t reference() const {
    return static_cast<uint16_t>(cid & (tmid() == 3U ? 0x0FFFU : 0x003FU));
  }
  bool packetMot() const { return tmid() == 3U && type() == 60U; }
};

struct Header {
  uint16_t version = 0;
  uint16_t components = 0;
  uint8_t services = 0;
};

// Input starts at RESP4 (SIZE), not the status word. Validate completely
// before callers mutate either station data or the retained component table.
// Same field extraction as the vendor DabServiceListParser, with exact SIZE
// and capacity checks added. No callbacks can publish provisional records.
inline bool validate(const uint8_t* list, size_t bytes, size_t serviceCapacity,
                     size_t componentCapacity, Header& out) {
  if (!list || bytes < 8U) return false;
  const uint16_t size = le16(list);
  if (size < 6U || size > 2694U || bytes != static_cast<size_t>(size) + 2U)
    return false;
  Header parsed;
  parsed.version = le16(list + 2);
  parsed.services = list[4];
  if (parsed.services >= 32U || parsed.services > serviceCapacity) return false;
  size_t offset = 8;
  for (uint8_t i = 0; i < parsed.services; ++i) {
    if (offset > bytes || bytes - offset < 24U) return false;
    const uint8_t count = list[offset + 5] & 15U;
    if (count >= 15U || parsed.components > componentCapacity ||
        count > componentCapacity - parsed.components) return false;
    offset += 24U;
    if (count > (bytes - offset) / 4U) return false;
    offset += 4U * count;
    parsed.components += count;
  }
  if (offset != bytes) return false;
  out = parsed;
  return true;
}

} // namespace dab_inventory
