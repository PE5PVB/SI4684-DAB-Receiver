#ifndef DAB_SERVICE_SWITCH_POLICY_H
#define DAB_SERVICE_SWITCH_POLICY_H

#include <stdint.h>

namespace dab_switch {

// A reply for the previously active service may finish after a new selection
// cleared PStext. It must not restore the old label during the switch.
inline bool serviceMetadataCurrent(bool active, bool switchPending,
                                   uint32_t replySid, uint32_t replyCid,
                                   uint32_t activeSid, uint32_t activeCid,
                                   uint32_t requestedSid, uint32_t requestedCid) {
  return active && !switchPending && replySid == activeSid && replyCid == activeCid &&
         replySid == requestedSid && replyCid == requestedCid;
}

static constexpr uint32_t STOP_SETTLE_MS = 200U;
static constexpr uint32_t AUDIO_TO_DATA_SETTLE_MS = 200U;
static constexpr uint32_t RETRY_BACKOFF_MS = 250U;
static constexpr uint32_t DATA_DEFER_MS = 5000U;
static constexpr uint8_t MAX_STOP_RETRIES = 3U;
static constexpr uint8_t MAX_AUDIO_RETRIES = 3U;
static constexpr uint8_t MAX_DATA_RETRIES = 3U;

enum class State : uint8_t {
  Idle,
  RequestSwitch,
  StopData,
  WaitDataStop,
  StopAudio,
  WaitAudioStop,
  ServiceSettle,
  WaitTune,
  StartAudio,
  WaitAudioStart,
  AudioSettle,
  ResolveData,
  StartData,
  WaitDataStart,
  Backoff,
  Ready,
  AudioOnly,
  Failed
};

enum class Resume : uint8_t {
  None,
  StopData,
  StopAudio,
  StartAudio,
  ResolveData
};

struct Controller {
  State state = State::Idle;
  Resume resume = Resume::None;
  uint32_t requestId = 1U;
  uint32_t commandRequestId = 0U;
  uint32_t notBeforeMs = 0U;
  uint8_t stopRetries = 0U;
  uint8_t audioRetries = 0U;
  uint8_t dataRetries = 0U;
  bool dataEvaluationRequested = false;
  bool slsContextValid = false;

  void reset() {
    state = State::Idle;
    resume = Resume::None;
    commandRequestId = 0U;
    notBeforeMs = 0U;
    stopRetries = audioRetries = dataRetries = 0U;
    dataEvaluationRequested = false;
    slsContextValid = false;
  }

  void requestSwitch() {
    ++requestId;
    if (requestId == 0U) ++requestId;
    state = State::RequestSwitch;
    resume = Resume::None;
    notBeforeMs = 0U;
    stopRetries = audioRetries = dataRetries = 0U;
    dataEvaluationRequested = false;
    slsContextValid = false;
  }

  void bindCommand() { commandRequestId = requestId; }
  bool commandIsCurrent() const { return commandRequestId == requestId; }

  bool ready(uint32_t now) const {
    return static_cast<int32_t>(now - notBeforeMs) >= 0;
  }

  void settle(State next, uint32_t now, uint32_t delayMs) {
    state = next;
    notBeforeMs = now + delayMs;
  }

  bool backoff(Resume next, uint8_t& retries, uint8_t maximum,
               uint32_t now) {
    if (retries >= maximum) return false;
    ++retries;
    resume = next;
    state = State::Backoff;
    notBeforeMs = now + RETRY_BACKOFF_MS * retries;
    return true;
  }

  void deferData(uint32_t now) {
    state = State::AudioOnly;
    notBeforeMs = now + DATA_DEFER_MS;
    dataRetries = 0U;
    slsContextValid = false;
  }

  void requestDataEvaluation() {
    dataEvaluationRequested = true;
    if (state == State::Ready && !slsContextValid)
      state = State::ResolveData;
  }

  void serviceBackoff(uint32_t now) {
    if (state != State::Backoff || !ready(now)) return;
    switch (resume) {
      case Resume::StopData: state = State::StopData; break;
      case Resume::StopAudio: state = State::StopAudio; break;
      case Resume::StartAudio: state = State::StartAudio; break;
      case Resume::ResolveData: state = State::ResolveData; break;
      case Resume::None: state = State::Failed; break;
    }
    resume = Resume::None;
  }
};

inline bool transitionActive(State state) {
  return state != State::Idle && state != State::Ready &&
         state != State::AudioOnly && state != State::Failed;
}

inline bool audioContextUsable(State state) {
  return state == State::AudioSettle || state == State::ResolveData ||
         state == State::StartData || state == State::WaitDataStart ||
         state == State::Ready || state == State::AudioOnly;
}

inline bool audioPadSlsContextValid(bool activeAudio,
                                    bool separateDataTargetFound) {
  return activeAudio && !separateDataTargetFound;
}

inline State dispatchRequest(bool activeData, bool activeAudio,
                             bool tuneRequested, bool serviceRequested) {
  if (activeData) return State::StopData;
  if (activeAudio) return State::StopAudio;
  if (tuneRequested) return State::Idle;
  if (serviceRequested) return State::StartAudio;
  return State::Idle;
}

inline const char* stateName(State state) {
  switch (state) {
    case State::Idle: return "Idle";
    case State::RequestSwitch: return "RequestSwitch";
    case State::StopData: return "StopData";
    case State::WaitDataStop: return "WaitDataStop";
    case State::StopAudio: return "StopAudio";
    case State::WaitAudioStop: return "WaitAudioStop";
    case State::ServiceSettle: return "ServiceSettle";
    case State::WaitTune: return "WaitTune";
    case State::StartAudio: return "StartAudio";
    case State::WaitAudioStart: return "WaitAudioStart";
    case State::AudioSettle: return "AudioSettle";
    case State::ResolveData: return "ResolveData";
    case State::StartData: return "StartData";
    case State::WaitDataStart: return "WaitDataStart";
    case State::Backoff: return "Backoff";
    case State::Ready: return "Ready";
    case State::AudioOnly: return "AudioOnly";
    case State::Failed: return "Failed";
  }
  return "?";
}

}  // namespace dab_switch

#endif
