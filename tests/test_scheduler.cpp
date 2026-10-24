#include "../src/dab_scheduler_policy.h"

#include <cassert>
#include <cstdint>

int main() {
  assert(dab_scheduler::classifyCtsTimeout(false, 8000U) ==
         dab_scheduler::CtsTimeoutClass::NotTimeout);
  assert(dab_scheduler::classifyCtsTimeout(true, 8000U) ==
         dab_scheduler::CtsTimeoutClass::HostStarved);
  assert(dab_scheduler::classifyCtsTimeout(true, 38U) ==
         dab_scheduler::CtsTimeoutClass::Genuine);
  uint8_t consecutiveCts = 2U;
  consecutiveCts = dab_scheduler::nextConsecutiveCtsTimeouts(
      dab_scheduler::CtsTimeoutClass::HostStarved, consecutiveCts);
  assert(consecutiveCts == 0U);
  consecutiveCts = dab_scheduler::nextConsecutiveCtsTimeouts(
      dab_scheduler::CtsTimeoutClass::Genuine, consecutiveCts);
  assert(consecutiveCts == 1U);
  assert(!dab_scheduler::ctsRecoveryRequired(consecutiveCts, 3U));
  consecutiveCts = dab_scheduler::nextConsecutiveCtsTimeouts(
      dab_scheduler::CtsTimeoutClass::Genuine, consecutiveCts);
  consecutiveCts = dab_scheduler::nextConsecutiveCtsTimeouts(
      dab_scheduler::CtsTimeoutClass::Genuine, consecutiveCts);
  assert(dab_scheduler::ctsRecoveryRequired(consecutiveCts, 3U));

  assert(dab_scheduler::audioInfoRetryDelayMs(0U) == 400U);
  assert(dab_scheduler::audioInfoRetryDelayMs(1U) == 500U);
  assert(dab_scheduler::audioInfoRetryDelayMs(2U) == 1000U);
  assert(dab_scheduler::audioInfoRetryDelayMs(3U) == 2000U);
  assert(dab_scheduler::audioInfoRetryDelayMs(4U) == 10000U);
  assert(dab_scheduler::DAB_TUNE_BUSY_BACKOFF_MS == 40U);
  assert(dab_scheduler::DAB_TUNE_BUSY_MAX_RETRIES == 3U);
  assert(dab_scheduler::DAB_SCAN_NO_SIGNAL_TIMEOUT_MS == 2500U);
  assert(dab_scheduler::DAB_SCAN_LIST_TIMEOUT_MS == 9000U);
  assert(dab_scheduler::DAB_SCAN_LIST_MAX_REQUESTS == 4U);

  assert(dab_scheduler::serviceListArrived(8U, 7U));
  assert(!dab_scheduler::serviceListArrived(7U, 7U));
  assert(dab_scheduler::refreshedServiceListArrived(9U, 8U, true));
  assert(!dab_scheduler::refreshedServiceListArrived(8U, 8U, true));
  assert(!dab_scheduler::refreshedServiceListArrived(9U, 8U, false));
  const char emptyLabel[16] = {};
  const char blankLabel[16] = {' ', ' ', ' '};
  const char realLabel[16] = {'R', 'a', 'd', 'i', 'o'};
  assert(!dab_scheduler::labelHasContent(emptyLabel));
  assert(!dab_scheduler::labelHasContent(blankLabel));
  assert(dab_scheduler::labelHasContent(realLabel));

  assert(dab_scheduler::nextGeneration(1U) == 2U);
  assert(dab_scheduler::nextGeneration(0xFFFFFFFFU) == 1U);
  assert(dab_scheduler::generationMatches(7U, 7U));
  assert(!dab_scheduler::generationMatches(6U, 7U));

  uint8_t retryCount = 0;
  uint32_t notBefore = 0;
  assert(dab_scheduler::retryReady(100U, notBefore));
  assert(dab_scheduler::scheduleRetry(retryCount, notBefore,
                                      1000U, 3U, 250U));
  assert(retryCount == 1U && notBefore == 1250U);
  assert(!dab_scheduler::retryReady(1249U, notBefore));
  assert(dab_scheduler::retryReady(1250U, notBefore));
  assert(dab_scheduler::scheduleRetry(retryCount, notBefore,
                                      1250U, 3U, 250U));
  assert(retryCount == 2U && notBefore == 1750U);
  assert(dab_scheduler::scheduleRetry(retryCount, notBefore,
                                      1750U, 3U, 250U));
  assert(retryCount == 3U && notBefore == 2500U);
  assert(!dab_scheduler::scheduleRetry(retryCount, notBefore,
                                       2500U, 3U, 250U));

  // Deadline comparison is wrap-safe for intervals below 2^31 ms.
  notBefore = 0x00000020U;
  assert(!dab_scheduler::retryReady(0xFFFFFFF0U, notBefore));
  assert(dab_scheduler::retryReady(0x00000020U, notBefore));

  dab_scheduler::resetRetry(retryCount, notBefore);
  assert(retryCount == 0U && notBefore == 0U);

  // Periodic metadata starts at distinct phases and remains separated for
  // several common multiples. No scheduler pass observes a metadata burst.
  uint32_t audioDue = dab_scheduler::DAB_AUDIO_INITIAL_PHASE_MS;
  uint32_t serviceDue = dab_scheduler::DAB_SERVICE_INITIAL_PHASE_MS;
  uint32_t ensembleDue = dab_scheduler::DAB_ENSEMBLE_INITIAL_PHASE_MS;
  uint32_t timeDue = dab_scheduler::DAB_TIME_INITIAL_PHASE_MS;
  uint32_t subchannelDue = dab_scheduler::DAB_SUBCHANNEL_INITIAL_PHASE_MS;
  uint32_t subchannelCount = 0;
  uint32_t lastSubchannelMs = 0;
  for (uint32_t now = 0; now <= 240000U; now += 10U) {
    uint8_t dueThisPass = 0;
    dueThisPass += dab_scheduler::takePeriodicDeadline(
        now, audioDue, dab_scheduler::DAB_AUDIO_INTERVAL_MS) ? 1U : 0U;
    dueThisPass += dab_scheduler::takePeriodicDeadline(
        now, serviceDue, dab_scheduler::DAB_SERVICE_INTERVAL_MS) ? 1U : 0U;
    dueThisPass += dab_scheduler::takePeriodicDeadline(
        now, ensembleDue, dab_scheduler::DAB_ENSEMBLE_INTERVAL_MS) ? 1U : 0U;
    dueThisPass += dab_scheduler::takePeriodicDeadline(
        now, timeDue, dab_scheduler::DAB_TIME_INTERVAL_MS) ? 1U : 0U;
    if (dab_scheduler::takePeriodicDeadline(
            now, subchannelDue,
            dab_scheduler::DAB_SUBCHANNEL_INTERVAL_MS)) {
      ++dueThisPass;
      if (subchannelCount != 0U)
        assert(now - lastSubchannelMs == 60000U);
      lastSubchannelMs = now;
      ++subchannelCount;
    }
    // The prescribed +5 s service and +15 s time phases can coincide once,
    // but the production scheduler still starts only one and enforces 100 ms
    // before the next low-priority command. The 60 s common multiple itself
    // must never make the full set due together.
    assert(dueThisPass <= 2U);
    if (now == 60000U || now == 120000U || now == 180000U)
      assert(dueThisPass <= 1U);
  }
  assert(subchannelCount == 4U);

  // With DSRV continuously pending, bounded bursts still leave turns for a
  // 1 Hz signal query and low-priority metadata.
  uint8_t dsrvBurst = 0;
  uint32_t signalNext = 1000U;
  uint32_t lowNext = 2000U;
  bool signalPending = false;
  bool lowPending = false;
  uint32_t signalCount = 0;
  uint32_t lowCount = 0;
  uint32_t dsrvCount = 0;
  for (uint32_t now = 0; now <= 120000U; now += 10U) {
    if (dab_scheduler::deadlineReached(now, signalNext)) {
      signalPending = true;
      signalNext += dab_scheduler::DAB_SIGNAL_INTERVAL_MS;
    }
    if (dab_scheduler::deadlineReached(now, lowNext)) {
      lowPending = true;
      lowNext += dab_scheduler::DAB_AUDIO_INTERVAL_MS;
    }
    const dab_scheduler::BackgroundWork work =
        dab_scheduler::chooseBackgroundWork(true, dsrvBurst,
                                            signalPending, lowPending);
    if (work == dab_scheduler::BackgroundWork::Dsrv) {
      if (dsrvBurst >= dab_scheduler::DAB_MAX_DSRV_BURST) dsrvBurst = 0;
      ++dsrvBurst;
      ++dsrvCount;
    } else if (work == dab_scheduler::BackgroundWork::Signal) {
      signalPending = false;
      dsrvBurst = 0;
      ++signalCount;
    } else if (work == dab_scheduler::BackgroundWork::LowPriority) {
      lowPending = false;
      dsrvBurst = 0;
      ++lowCount;
    }
  }
  assert(dsrvCount > signalCount);
  assert(signalCount >= 119U);
  assert(lowCount >= 12U);

  // DAB/FM use the same two-stage filter architecture. Reusing a generation
  // must not feed the radio IIR again, while the display may keep approaching
  // the already-filtered target.
  dab_scheduler::SignalDisplayFilter filter;
  assert(filter.acceptSample(1U, false, 100, 10, 50));
  assert(filter.targetSignal10 == 100);
  assert(!filter.acceptSample(1U, false, 700, 30, 100));
  assert(filter.targetSignal10 == 100);
  assert(filter.acceptSample(2U, false, 200, 20, 70));
  assert(filter.targetSignal10 == 130);
  const int16_t targetAfterNewSample = filter.targetSignal10;
  filter.stepDisplay(false);
  filter.stepDisplay(false);
  assert(filter.targetSignal10 == targetAfterNewSample);
  assert(filter.displayedSignal10 > 100);
  assert(dab_scheduler::DAB_SIGNAL_UI_INTERVAL_MS * 2U ==
         dab_scheduler::DAB_SIGNAL_INTERVAL_MS);
  assert(dab_scheduler::FM_SIGNAL_UI_INTERVAL_MS * 2U ==
         dab_scheduler::FM_RSQ_INTERVAL_MS);
  assert(dab_scheduler::chooseFmWork(true, true, true, true, true) ==
         dab_scheduler::FmWork::TuneRsq);
  assert(dab_scheduler::chooseFmWork(true, false, true, true, true) ==
         dab_scheduler::FmWork::None);
  assert(dab_scheduler::chooseFmWork(false, false, true, true, true) ==
         dab_scheduler::FmWork::Rds);
  assert(dab_scheduler::chooseFmWork(false, false, false, true, true) ==
         dab_scheduler::FmWork::IdleRsq);

  // A DAB time sample is consumed exactly once; the local TimeLib clock is
  // therefore free-running until a genuinely new generation arrives.
  uint32_t consumedTimeGeneration = 0;
  assert(dab_scheduler::takeGeneration(1U, consumedTimeGeneration));
  assert(!dab_scheduler::takeGeneration(1U, consumedTimeGeneration));
  assert(dab_scheduler::takeGeneration(2U, consumedTimeGeneration));

  // Normal DEBUG logs only milestones at no more than two periodic lines per
  // second; explicit verbose mode allows every segment.
  uint32_t lastMotLogMs = 0;
  assert(dab_scheduler::shouldLogMotSegment(false, 0U, false, 100U,
                                            lastMotLogMs));
  assert(!dab_scheduler::shouldLogMotSegment(false, 1U, false, 200U,
                                             lastMotLogMs));
  assert(!dab_scheduler::shouldLogMotSegment(false, 16U, false, 400U,
                                             lastMotLogMs));
  assert(dab_scheduler::shouldLogMotSegment(false, 16U, false, 600U,
                                            lastMotLogMs));
  assert(dab_scheduler::shouldLogMotSegment(false, 17U, true, 610U,
                                            lastMotLogMs));
  assert(dab_scheduler::shouldLogMotSegment(true, 3U, false, 620U,
                                            lastMotLogMs));

  dab_scheduler::RepeatedEventThrottle repeatThrottle;
  uint16_t suppressedRepeats = 0;
  assert(dab_scheduler::shouldLogRepeatedEvent(
      repeatThrottle, 49225U, 100U, false, 5000U, suppressedRepeats));
  assert(suppressedRepeats == 0U);
  assert(!dab_scheduler::shouldLogRepeatedEvent(
      repeatThrottle, 49225U, 110U, false, 5000U, suppressedRepeats));
  assert(!dab_scheduler::shouldLogRepeatedEvent(
      repeatThrottle, 49225U, 200U, false, 5000U, suppressedRepeats));
  assert(dab_scheduler::shouldLogRepeatedEvent(
      repeatThrottle, 49225U, 5100U, false, 5000U, suppressedRepeats));
  assert(suppressedRepeats == 2U);
  assert(dab_scheduler::shouldLogRepeatedEvent(
      repeatThrottle, 49226U, 5200U, false, 5000U, suppressedRepeats));
  assert(dab_scheduler::shouldLogRepeatedEvent(
      repeatThrottle, 49226U, 5210U, true, 5000U, suppressedRepeats));

  // Periodic deadline advancement remains wrap-safe as well.
  uint32_t wrappedDeadline = 0x00000020U;
  assert(!dab_scheduler::takePeriodicDeadline(
      0xFFFFFFF0U, wrappedDeadline, 1000U));
  assert(dab_scheduler::takePeriodicDeadline(
      0x00000020U, wrappedDeadline, 1000U));
  return 0;
}
