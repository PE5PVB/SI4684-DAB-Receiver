#pragma once

#include "dab_user_application.h"
#include "dab_component_inventory.h"

namespace dab_spi {

enum class Identity : uint8_t { Unqueried, NotSpi, Spi, Failed, Slideshow, SpiSlideshow };
inline bool isSpi(Identity identity) {
  return identity == Identity::Spi || identity == Identity::SpiSlideshow;
}
inline bool isSlideshow(Identity identity) {
  return identity == Identity::Slideshow || identity == Identity::SpiSlideshow;
}

// Re-arm after every completed pass, including failed passes. No startup-only
// detection window. Signed differences keep the deadline safe across millis().
struct RefreshCycle {
  static constexpr uint32_t INTERVAL_MS = 60000U;
  uint32_t deadline = 0;
  bool armed = false;
  void cancel() { armed = false; }
  void completed(uint32_t now) { deadline = now + INTERVAL_MS; armed = true; }
  bool ready(uint32_t now, bool eligible) const {
    return armed && eligible && static_cast<int32_t>(now - deadline) >= 0;
  }
};

// Called only after the whole service-list structure has been validated.
// Retain every wire CID, including the upper bits used by tuner commands.
inline bool retainComponents(uint32_t* table, size_t capacity, size_t first,
                             const uint8_t* raw, size_t bytes, size_t count,
                             bool& changed) {
  if (first > capacity || count > capacity - first || count > bytes / 4U ||
      (count != 0U && (!table || !raw))) return false;
  for (size_t j = 0; j < count; ++j) {
    const uint8_t* p = raw + 4U * j;
    const uint32_t cid = static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
    changed = changed || table[first + j] != cid;
    table[first + j] = cid;
  }
  return true;
}

// One cooperative command at a time. Completion means committed answers,
// never merely that the final request has been issued. A new pass invalidates
// even an in-flight request for an otherwise identical component inventory.
struct Scan {
  uint32_t generation = 0;
  uint16_t total = 0;
  uint16_t scheduled = 0;
  uint16_t processed = 0;
  uint16_t failed = 0;
  bool active = false;
  bool enumerated = false;

  void begin(uint16_t count, bool validInventory) {
    if (++generation == 0U) ++generation;
    total = count;
    scheduled = processed = failed = 0;
    active = false;
    enumerated = validInventory;
  }
  bool issue() {
    if (!enumerated || active || scheduled >= total) return false;
    ++scheduled;
    active = true;
    return true;
  }
  bool commit(uint32_t token, bool success) {
    if (token != generation || !active) return false;
    if (success) ++processed;
    else ++failed;
    active = false;
    return true;
  }
  bool functionalComplete(bool commandActive) const {
    return enumerated && scheduled == total && processed + failed == total &&
           !active && !commandActive;
  }
};
enum class Status : uint8_t {
  Pending, NotSignalled, SelectedService, SelectedEnsemble, Ambiguous, Incomplete,
  PacketMotUnverified
};

inline Identity identify(const uint8_t* raw, size_t bytes, uint8_t count) {
  if (!dab_ua::valid(raw, bytes, count)) return Identity::Failed;
  dab_ua::Reader reader(raw, bytes, count);
  dab_ua::Application application;
  bool slideshow = false;
  bool spi = false;
  while (reader.next(application)) {
    if (application.type == dab_ua::SPI) spi = true;
    if (application.type == dab_ua::SLIDESHOW) slideshow = true;
  }
  if (spi) return slideshow ? Identity::SpiSlideshow : Identity::Spi;
  return slideshow ? Identity::Slideshow : Identity::NotSpi;
}

struct Result {
  Status status = Status::Pending;
  uint32_t serviceId = 0;
  uint32_t componentId = 0;
  uint16_t confirmed = 0;
  uint16_t packetMotUnverified = 0;
  bool confirmedPresent() const { return confirmed != 0U; }
  bool available() const {
    return status == Status::SelectedService || status == Status::SelectedEnsemble;
  }
};

inline const char* statusName(Status status) {
  switch (status) {
    case Status::Pending: return "pending";
    case Status::NotSignalled: return "not signalled";
    case Status::SelectedService: return "confirmed/service";
    case Status::SelectedEnsemble: return "confirmed/ensemble";
    case Status::Ambiguous: return "ambiguous";
    case Status::Incomplete: return "discovery incomplete";
    case Status::PacketMotUnverified: return "packet MOT unverified";
  }
  return "unknown";
}

// Constant storage, independent of inventory size. Finish only after every
// component has a validated UA answer; a failed/unknown answer could hide a
// higher-priority candidate. Labels, component order and UA data are not used
// as identity/profile heuristics. A unique ensemble candidate is a selection
// policy, not proof that its schedule covers every audio service.
class Selector {
 public:
  explicit Selector(uint32_t currentService) : currentService_(currentService) {}

  void consider(uint32_t sid, uint32_t cid, Identity identity, bool packetMot = false) {
    if (identity == Identity::Unqueried) pending_ = true;
    if (identity == Identity::Failed) failed_ = true;
    if (!isSpi(identity)) {
      if (packetMot && packetMotUnverified_ != UINT16_MAX) ++packetMotUnverified_;
      return;
    }
    if (confirmed_ != UINT16_MAX) ++confirmed_;
    add(all_, allCount_, sid, cid);
    if (sid == currentService_) add(local_, localCount_, sid, cid);
  }

  Result finish() const {
    Result result;
    result.confirmed = confirmed_;
    result.packetMotUnverified = packetMotUnverified_;
    if (pending_) return result;
    if (failed_) { result.status = Status::Incomplete; return result; }
    if (localCount_ == 1U) {
      result.serviceId = local_.serviceId;
      result.componentId = local_.componentId;
      result.status = Status::SelectedService;
    } else if (localCount_ > 1U || allCount_ > 1U) {
      result.status = Status::Ambiguous;
    } else if (allCount_ == 1U) {
      result.serviceId = all_.serviceId;
      result.componentId = all_.componentId;
      result.status = Status::SelectedEnsemble;
    } else {
      result.status = packetMotUnverified_ ? Status::PacketMotUnverified : Status::NotSignalled;
    }
    return result;
  }

 private:
  static void add(Result& first, uint8_t& count, uint32_t sid, uint32_t cid) {
    if (count == 0U) {
      first.serviceId = sid;
      first.componentId = cid;
      count = 1;
    } else if (first.serviceId != sid || first.componentId != cid) {
      count = 2;  // saturate: only zero/one/multiple matter
    }
  }
  uint32_t currentService_;
  Result all_;
  Result local_;
  uint8_t allCount_ = 0;
  uint8_t localCount_ = 0;
  bool pending_ = false;
  bool failed_ = false;
  uint16_t confirmed_ = 0;
  uint16_t packetMotUnverified_ = 0;
};

// A separate, conservative policy for the legacy slideshow START path.
// Never infer slideshow from MOT transport, labels or ensemble component order.
// An advertised slideshow on the active audio component already uses X-PAD.
class SlideshowSelector {
 public:
  SlideshowSelector(uint32_t audioSid, uint32_t audioCid)
      : audioSid_(audioSid), audioCid_(audioCid) {}
  void consider(uint32_t sid, uint32_t cid, Identity identity) {
    if (sid != audioSid_) return;
    if (cid == audioCid_) {
      if (isSlideshow(identity)) audioPad_ = true;
      return;
    }
    const dab_inventory::Component component{cid};
    if (identity != Identity::Slideshow || !component.packetMot() || component.ca()) return;
    if (!selected_) { componentId_ = cid; selected_ = true; }
    else if (componentId_ != cid) ambiguous_ = true;
  }
  bool selected() const { return selected_ && !ambiguous_ && !audioPad_; }
  uint32_t componentId() const { return selected() ? componentId_ : 0; }
 private:
  uint32_t audioSid_, audioCid_;
  uint32_t componentId_ = 0;
  bool selected_ = false;
  bool ambiguous_ = false;
  bool audioPad_ = false;
};

}  // namespace dab_spi
