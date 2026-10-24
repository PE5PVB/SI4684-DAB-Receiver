#pragma once

#include <stdint.h>

namespace dab_probe {

struct Endpoint {
  Endpoint() = default;
  Endpoint(uint32_t service, uint32_t component, bool enabled = true)
      : sid(service), cid(component), valid(enabled) {}
  uint32_t sid = 0;
  uint32_t cid = 0;
  bool valid = false;
  bool matches(uint32_t service, uint32_t component) const {
    return valid && sid == service && cid == component;
  }
};

enum class Route : uint8_t { Ignored, Audio, Sls, Spi };

// Real FW 6.0.9 capture: list CID 01FE002A, PAD/DLS CID 0000002A,
// same SID 00002FD9. The list's upper half contains component information.
// Accept the observed zero-extended 16-bit component field only for audio
// TMId=0 PAD/DLS; do not mask arbitrary incoming IDs or standalone data IDs.
// Endpoint::matches remains exact for command ownership and START/STOP IDs.
inline bool matchesAudioPad(const Endpoint& audio, uint32_t sid, uint32_t cid) {
  return audio.valid && audio.sid == sid && (audio.cid & 0xC000U) == 0U &&
      (cid & 0xFFFF0000UL) == 0U && (audio.cid & 0xFFFFU) == cid;
}

// FW 6.0.9, Polish MUX 11A: list CID 01F2C00B, standard-data
// DSRV CID 0000C00B. Accept only a zero-extended packet-mode component
// with the exact SID. Command ownership retains Endpoint::matches().
inline bool matchesPacketData(const Endpoint& data, uint32_t sid, uint32_t cid) {
  return data.valid && data.sid == sid && (data.cid & 0xC000U) == 0xC000U &&
      (cid & 0xFFFF0000UL) == 0U && (data.cid & 0xFFFFU) == cid;
}

// Source 0 = standard data, 1 = PAD, 2 = DLS. Application ownership comes
// before any payload interpretation; unknown sources/contexts are drained.
inline Route route(uint32_t sid, uint32_t cid, uint8_t source,
                   const Endpoint& audio, const Endpoint& sls, const Endpoint& spi) {
  if (source > 2U) return Route::Ignored;
  if (spi.matches(sid, cid) || (source == 0U && matchesPacketData(spi, sid, cid)))
    return source < 2U ? Route::Spi : Route::Ignored;
  if (audio.matches(sid, cid) ||
      ((source == 1U || source == 2U) && matchesAudioPad(audio, sid, cid)))
    return Route::Audio;
  if (sls.matches(sid, cid) || (source == 0U && matchesPacketData(sls, sid, cid)))
    return source < 2U ? Route::Sls : Route::Ignored;
  return Route::Ignored;
}

// Keep the established SLS/DLS path when no probe SPI context is tracked.
// Standalone DSRV identity representations still need hardware verification;
// an unmatched diagnostic identity alone must not break normal reception.
// Existing audio-switch/collector guards remain authoritative outside a probe.
// During a probe (including failed cleanup), strict ownership is mandatory.
inline bool feedLegacyDecoder(Route target, bool spiContextTracked,
                              bool audioContextUsable) {
  return audioContextUsable && target != Route::Spi &&
      (!spiContextTracked || target == Route::Audio || target == Route::Sls);
}

enum class State : uint8_t {
  Idle, Baseline, StartRequested, Starting, Observing, StopRequested, Stopping,
  CleanupFailed
};

struct Counts {
  uint32_t audio = 0;
  uint32_t pad = 0;
  uint32_t sls = 0;
  uint32_t spi = 0;
  uint32_t ignored = 0;
  bool slsStopped = false;
};

inline void increment(uint32_t& count) { if (count != UINT32_MAX) ++count; }
inline bool due(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

class Probe {
 public:
  State state = State::Idle;
  Endpoint audio;
  Endpoint sls;
  Endpoint spi;  // retained on cleanup failure to quarantine possible traffic
  Counts before;
  Counts after;
  uint32_t requestId = 0;
  uint32_t nextActionMs = 0;
  uint8_t stopAttempts = 0;

  bool running() const { return state != State::Idle && state != State::CleanupFailed; }

  bool request(const Endpoint& audioContext, const Endpoint& slsContext,
               const Endpoint& spiContext, uint32_t contextId, uint32_t now) {
    if (state != State::Idle || !audioContext.valid || !spiContext.valid ||
        audioContext.matches(spiContext.sid, spiContext.cid) ||
        slsContext.matches(spiContext.sid, spiContext.cid)) return false;
    audio = audioContext;
    sls = slsContext;
    spi = spiContext;
    requestId = contextId;
    before = Counts{};
    after = Counts{};
    stopAttempts = 0;
    cancel_ = false;
    state = State::Baseline;
    nextActionMs = now + 5000U;
    return true;
  }

  void stop(uint32_t now) {
    cancel_ = true;
    if (state == State::Baseline || state == State::StartRequested) finish();
    else if (state == State::Observing) {
      state = State::StopRequested;
      nextActionMs = now;
    }
  }

  void advance(uint32_t now, bool sameContext) {
    if (!sameContext && running()) {
      stop(now);
    }
    if (state == State::Baseline && due(now, nextActionMs)) state = State::StartRequested;
    if (state == State::Observing && due(now, nextActionMs)) {
      state = State::StopRequested;
      nextActionMs = now;
    }
  }

  void started(uint32_t now, bool success, bool uncertain) {
    if (success && !cancel_) {
      state = State::Observing;
      nextActionMs = now + 30000U;
    } else if (success || uncertain) {
      state = State::StopRequested;
      nextActionMs = now;
    } else finish();
  }

  void stopped(uint32_t now, bool success) {
    if (success) finish();
    else if (stopAttempts >= 3U) state = State::CleanupFailed;
    else {
      state = State::StopRequested;
      nextActionMs = now + 500U;
    }
  }

  void observe(Route target, uint8_t source, bool stoppedService, bool hasPayload = true) {
    if (state != State::Baseline && state != State::Observing) return;
    Counts& counts = state == State::Baseline ? before : after;
    if (hasPayload) switch (target) {
      case Route::Audio:
        increment(counts.audio);
        if (source == 1U) increment(counts.pad);
        break;
      case Route::Sls: increment(counts.sls); break;
      case Route::Spi: increment(counts.spi); break;
      case Route::Ignored: increment(counts.ignored); break;
    }
    if (target == Route::Sls && stoppedService) counts.slsStopped = true;
  }

 private:
  bool cancel_ = false;
  void finish() {
    state = State::Idle;
    spi.valid = false;
  }
};

}  // namespace dab_probe
