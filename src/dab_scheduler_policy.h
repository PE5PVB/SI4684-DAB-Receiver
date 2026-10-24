#pragma once

#include <stdint.h>

namespace dab_scheduler {

// Stable radio sampling and UI refresh rates.  Keep these values in the
// platform-neutral policy header so the host tests exercise the exact timing
// used by the firmware.
static const uint32_t DAB_SIGNAL_INTERVAL_MS = 1000U;
static const uint32_t DAB_SIGNAL_UI_INTERVAL_MS = 500U;
static const uint32_t DAB_SCAN_LIST_SETTLE_MS = 1000U;
static const uint32_t DAB_SCAN_LIST_RETRY_MS = 1500U;
static const uint32_t DAB_SCAN_NO_SIGNAL_TIMEOUT_MS = 2500U;
static const uint32_t DAB_SCAN_LIST_TIMEOUT_MS = 9000U;
static const uint8_t DAB_SCAN_LIST_MAX_REQUESTS = 4U;
static const uint32_t DAB_ENSEMBLE_INTERVAL_MS = 30000U;
static const uint32_t DAB_TIME_INTERVAL_MS = 60000U;
static const uint32_t DAB_AUDIO_INTERVAL_MS = 10000U;
static const uint32_t DAB_SERVICE_INTERVAL_MS = 10000U;
static const uint32_t DAB_SUBCHANNEL_INTERVAL_MS = 60000U;
static const uint32_t DAB_LOW_PRIORITY_GAP_MS = 100U;
static const uint32_t DAB_HOST_STARVATION_US = 2000U;
static const uint32_t DAB_AUDIO_INFO_INITIAL_DELAY_MS = 400U;
static const uint32_t DAB_AUDIO_INFO_SLOW_RETRY_MS = 10000U;
static const uint8_t DAB_AUDIO_INFO_FAST_RETRIES = 3U;
static const uint32_t DAB_TUNE_BUSY_BACKOFF_MS = 40U;
static const uint8_t DAB_TUNE_BUSY_MAX_RETRIES = 3U;

static const uint32_t DAB_AUDIO_INITIAL_PHASE_MS = 2000U;
static const uint32_t DAB_SERVICE_INITIAL_PHASE_MS = 5000U;
static const uint32_t DAB_ENSEMBLE_INITIAL_PHASE_MS = 8000U;
static const uint32_t DAB_TIME_INITIAL_PHASE_MS = 15000U;
static const uint32_t DAB_SUBCHANNEL_INITIAL_PHASE_MS = 30000U;

static const uint32_t FM_RSQ_INTERVAL_MS = 500U;
static const uint32_t FM_ACF_INTERVAL_MS = 1000U;
static const uint32_t FM_RDS_POLL_INTERVAL_MS = 80U;
static const uint32_t FM_SIGNAL_UI_INTERVAL_MS = 250U;
static const uint8_t DAB_MAX_DSRV_BURST = 4U;

inline uint32_t nextGeneration(uint32_t current) {
  ++current;
  return current == 0U ? 1U : current;
}

inline bool generationMatches(uint32_t completed, uint32_t current) {
  return completed == current;
}

inline bool retryReady(uint32_t now, uint32_t notBefore) {
  return notBefore == 0U || static_cast<int32_t>(now - notBefore) >= 0;
}

inline bool deadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

inline bool takeGeneration(uint32_t current, uint32_t& consumed) {
  if (current == 0U || current == consumed) return false;
  consumed = current;
  return true;
}

inline bool serviceListArrived(uint32_t current, uint32_t tuneGeneration) {
  return current != 0U && current != tuneGeneration;
}

inline bool refreshedServiceListArrived(uint32_t current,
                                        uint32_t firstGeneration,
                                        bool refreshRequested) {
  return refreshRequested && firstGeneration != 0U &&
         current != firstGeneration;
}

enum class CtsTimeoutClass : uint8_t {
  NotTimeout,
  HostStarved,
  Genuine
};

inline CtsTimeoutClass classifyCtsTimeout(bool timedOut,
                                          uint32_t lastServiceGapUs) {
  if (!timedOut) return CtsTimeoutClass::NotTimeout;
  return lastServiceGapUs >= DAB_HOST_STARVATION_US
             ? CtsTimeoutClass::HostStarved
             : CtsTimeoutClass::Genuine;
}

inline uint8_t nextConsecutiveCtsTimeouts(CtsTimeoutClass classification,
                                          uint8_t current) {
  if (classification != CtsTimeoutClass::Genuine) return 0U;
  return current == 0xFFU ? current : static_cast<uint8_t>(current + 1U);
}

inline bool ctsRecoveryRequired(uint8_t consecutive, uint8_t threshold) {
  return consecutive >= threshold;
}

inline uint32_t audioInfoRetryDelayMs(uint8_t retryNumber) {
  if (retryNumber == 0U) return DAB_AUDIO_INFO_INITIAL_DELAY_MS;
  if (retryNumber > DAB_AUDIO_INFO_FAST_RETRIES)
    return DAB_AUDIO_INFO_SLOW_RETRY_MS;
  return 500U << (retryNumber - 1U);
}

inline bool labelHasContent(const char* label, uint8_t length = 16U) {
  if (label == nullptr) return false;
  for (uint8_t i = 0; i < length; ++i) {
    const uint8_t value = static_cast<uint8_t>(label[i]);
    if (value != 0U && value != static_cast<uint8_t>(' ')) return true;
  }
  return false;
}

// Report a periodic deadline once and move it into the future.  Advancing from
// the previous deadline (rather than from `now`) keeps independently phased
// jobs phased after a temporarily busy bus.  The loop also prevents a backlog
// from being replayed as a command burst.
inline bool takePeriodicDeadline(uint32_t now, uint32_t& deadline,
                                 uint32_t intervalMs) {
  if (!deadlineReached(now, deadline)) return false;
  do {
    deadline += intervalMs;
  } while (deadlineReached(now, deadline));
  return true;
}

enum class BackgroundWork : uint8_t {
  None,
  Dsrv,
  Signal,
  LowPriority
};

enum class FmWork : uint8_t {
  None,
  TuneRsq,
  Rds,
  IdleRsq,
  Acf
};

inline FmWork chooseFmWork(bool tunePending, bool tuneRsqDue, bool rdsDue,
                           bool idleRsqDue, bool acfDue) {
  if (tunePending) return tuneRsqDue ? FmWork::TuneRsq : FmWork::None;
  if (rdsDue) return FmWork::Rds;
  if (idleRsqDue) return FmWork::IdleRsq;
  if (acfDue) return FmWork::Acf;
  return FmWork::None;
}

// Continuous DSRV may run in short bursts, but an overdue RF sample or one
// low-priority metadata command always gets a turn after the bounded burst.
inline BackgroundWork chooseBackgroundWork(bool dsrvPending,
                                           uint8_t dsrvBurstCount,
                                           bool signalPending,
                                           bool lowPriorityPending) {
  if (dsrvPending && dsrvBurstCount < DAB_MAX_DSRV_BURST)
    return BackgroundWork::Dsrv;
  if (signalPending) return BackgroundWork::Signal;
  if (lowPriorityPending) return BackgroundWork::LowPriority;
  if (dsrvPending) return BackgroundWork::Dsrv;
  return BackgroundWork::None;
}

inline bool shouldLogMotSegment(bool verbose, uint8_t segment, bool last,
                                uint32_t now, uint32_t& lastLogMs) {
  const bool milestone = segment == 0U || last || (segment & 0x0FU) == 0U;
  if (!verbose && (!milestone ||
      (segment != 0U && !last &&
       static_cast<uint32_t>(now - lastLogMs) < 500U)))
    return false;
  lastLogMs = now;
  return true;
}

struct RepeatedEventThrottle {
  bool valid;
  uint16_t id;
  uint32_t lastLogMs;
  uint16_t suppressed;

  RepeatedEventThrottle()
      : valid(false), id(0), lastLogMs(0), suppressed(0) {}
};

// Coalesce duplicate diagnostics produced by multiple representations of the
// same repeated MOT object (for example its header followed by segment zero).
// `suppressedBeforeLog` lets the caller emit one compact summary line.
inline bool shouldLogRepeatedEvent(RepeatedEventThrottle& state, uint16_t id,
                                   uint32_t now, bool verbose,
                                   uint32_t summaryIntervalMs,
                                   uint16_t& suppressedBeforeLog) {
  suppressedBeforeLog = 0;
  if (verbose) return true;
  if (!state.valid || state.id != id ||
      static_cast<uint32_t>(now - state.lastLogMs) >= summaryIntervalMs) {
    suppressedBeforeLog = state.suppressed;
    state.valid = true;
    state.id = id;
    state.lastLogMs = now;
    state.suppressed = 0;
    return true;
  }
  if (state.suppressed != 0xFFFFU) ++state.suppressed;
  return false;
}

// Two-stage filter used by both DAB and FM UI.  acceptSample() performs the
// radio-sample IIR exactly once for each new generation.  stepDisplay() is a
// separate visual interpolation and is allowed to run twice per RF sample.
struct SignalDisplayFilter {
  bool valid;
  bool fm;
  uint32_t sampleGeneration;
  int16_t targetSignal10;
  int16_t targetCnr10;
  uint16_t targetQuality10;
  int16_t displayedSignal10;
  int16_t displayedCnr10;
  uint16_t displayedQuality10;

  SignalDisplayFilter()
      : valid(false), fm(false), sampleGeneration(0), targetSignal10(0),
        targetCnr10(0), targetQuality10(0), displayedSignal10(0),
        displayedCnr10(0), displayedQuality10(0) {}

  bool acceptSample(uint32_t generation, bool isFm, int16_t rawSignal10,
                    int16_t rawCnr, uint8_t rawQuality) {
    if (valid && fm == isFm && sampleGeneration == generation) return false;
    const int16_t rawCnr10 = static_cast<int16_t>(rawCnr * 10);
    const uint16_t rawQuality10 = static_cast<uint16_t>(rawQuality) * 10U;
    if (!valid || fm != isFm) {
      targetSignal10 = rawSignal10;
      targetCnr10 = rawCnr10;
      targetQuality10 = rawQuality10;
      displayedSignal10 = targetSignal10;
      displayedCnr10 = targetCnr10;
      displayedQuality10 = targetQuality10;
    } else {
      targetSignal10 = static_cast<int16_t>(
          (static_cast<int32_t>(targetSignal10) * 7 +
           static_cast<int32_t>(rawSignal10) * 3) / 10);
      targetCnr10 = static_cast<int16_t>(
          (static_cast<int32_t>(targetCnr10) * 7 +
           static_cast<int32_t>(rawCnr10) * 3) / 10);
      targetQuality10 = static_cast<uint16_t>(
          (static_cast<uint32_t>(targetQuality10) * 7U +
           static_cast<uint32_t>(rawQuality10) * 3U + 5U) / 10U);
    }
    valid = true;
    fm = isFm;
    sampleGeneration = generation;
    return true;
  }

  void stepDisplay(bool snap) {
    if (!valid) return;
    if (snap) {
      displayedSignal10 = targetSignal10;
      displayedCnr10 = targetCnr10;
      displayedQuality10 = targetQuality10;
      return;
    }
    int32_t delta = static_cast<int32_t>(targetSignal10) - displayedSignal10;
    displayedSignal10 = static_cast<int16_t>(
        displayedSignal10 + (delta == 0 ? 0 : (delta / 2 != 0 ? delta / 2
                                                               : (delta > 0 ? 1 : -1))));
    delta = static_cast<int32_t>(targetCnr10) - displayedCnr10;
    displayedCnr10 = static_cast<int16_t>(
        displayedCnr10 + (delta == 0 ? 0 : (delta / 2 != 0 ? delta / 2
                                                            : (delta > 0 ? 1 : -1))));
    delta = static_cast<int32_t>(targetQuality10) - displayedQuality10;
    displayedQuality10 = static_cast<uint16_t>(
        displayedQuality10 + (delta == 0 ? 0 : (delta / 2 != 0 ? delta / 2
                                                                : (delta > 0 ? 1 : -1))));
  }
};

// Returns true when another retry is allowed. The first failure schedules
// retry #1; after maxRetries have been scheduled the following failure is
// terminal. Unsigned time arithmetic remains valid across millis() wraparound.
inline bool scheduleRetry(uint8_t& retryCount, uint32_t& notBefore,
                          uint32_t now, uint8_t maxRetries,
                          uint32_t baseBackoffMs) {
  if (retryCount >= maxRetries) return false;
  ++retryCount;
  notBefore = now + baseBackoffMs * retryCount;
  return true;
}

inline void resetRetry(uint8_t& retryCount, uint32_t& notBefore) {
  retryCount = 0;
  notBefore = 0;
}

}  // namespace dab_scheduler
