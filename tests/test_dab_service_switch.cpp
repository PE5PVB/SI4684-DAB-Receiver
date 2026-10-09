#include "../src/dab_service_switch_policy.h"
#include <cassert>

using dab_switch::Controller;
using dab_switch::Resume;
using dab_switch::State;

int main() {
  // Late metadata for A must not repopulate PStext after a B/C selection.
  assert(dab_switch::serviceMetadataCurrent(true, false, 1, 11, 1, 11, 1, 11));
  assert(!dab_switch::serviceMetadataCurrent(true, true, 1, 11, 1, 11, 2, 22));
  assert(!dab_switch::serviceMetadataCurrent(true, true, 1, 11, 1, 11, 3, 33));
  assert(!dab_switch::serviceMetadataCurrent(true, false, 1, 11, 3, 33, 3, 33));
  assert(!dab_switch::serviceMetadataCurrent(true, false, 2, 22, 3, 33, 3, 33));
  assert(dab_switch::serviceMetadataCurrent(true, false, 3, 33, 3, 33, 3, 33));
  assert(!dab_switch::serviceMetadataCurrent(true, false, 3, 22, 3, 33, 3, 33));
  assert(!dab_switch::serviceMetadataCurrent(true, false, 3, 33, 3, 33, 3, 22));
  assert(!dab_switch::serviceMetadataCurrent(false, false, 3, 33, 3, 33, 3, 33));
  // Returning to A while its old reply is still outstanding remains pending.
  assert(!dab_switch::serviceMetadataCurrent(true, true, 1, 11, 1, 11, 1, 11));
  // Normal switch tears down data before audio; without data it starts at audio.
  assert(dab_switch::dispatchRequest(true, true, false, true) == State::StopData);
  assert(dab_switch::dispatchRequest(false, true, false, true) == State::StopAudio);
  assert(dab_switch::dispatchRequest(false, false, false, true) == State::StartAudio);
  assert(dab_switch::dispatchRequest(false, false, true, false) == State::Idle);

  Controller sw;
  sw.requestSwitch();
  const uint32_t requestA = sw.requestId;
  sw.bindCommand();
  assert(sw.commandIsCurrent());

  // A -> B -> C supersedes the in-flight A command instead of stacking work.
  sw.requestSwitch();
  assert(sw.requestId != requestA && !sw.commandIsCurrent());
  const uint32_t requestB = sw.requestId;
  sw.requestSwitch();
  assert(sw.requestId != requestB && sw.state == State::RequestSwitch);

  // Bounded non-blocking STOP backoff, including millis() wraparound.
  sw.stopRetries = 0;
  assert(sw.backoff(Resume::StopData, sw.stopRetries,
                    dab_switch::MAX_STOP_RETRIES, 0xFFFFFF80U));
  assert(sw.state == State::Backoff && !sw.ready(0xFFFFFF90U));
  assert(sw.ready(sw.notBeforeMs));
  sw.serviceBackoff(sw.notBeforeMs);
  assert(sw.state == State::StopData);
  assert(sw.backoff(Resume::StopData, sw.stopRetries,
                    dab_switch::MAX_STOP_RETRIES, 1000U));
  assert(sw.backoff(Resume::StopData, sw.stopRetries,
                    dab_switch::MAX_STOP_RETRIES, 2000U));
  assert(!sw.backoff(Resume::StopData, sw.stopRetries,
                     dab_switch::MAX_STOP_RETRIES, 3000U));

  // Audio confirmation has a non-blocking settle before data resolution.
  sw.settle(State::AudioSettle, 500U,
            dab_switch::AUDIO_TO_DATA_SETTLE_MS);
  assert(!sw.ready(699U));
  assert(sw.ready(700U));
  assert(dab_switch::audioContextUsable(State::AudioSettle));
  assert(!dab_switch::audioContextUsable(State::WaitAudioStop));

  // Metadata callbacks only request evaluation. They cannot create a second
  // StartData command while a start is already pending.
  sw.state = State::WaitDataStart;
  sw.dataEvaluationRequested = false;
  sw.requestDataEvaluation();
  assert(sw.state == State::WaitDataStart && sw.dataEvaluationRequested);
  sw.state = State::Ready;
  sw.slsContextValid = true;
  sw.requestDataEvaluation();
  assert(sw.state == State::Ready);

  // Temporary data-service failures retain audio and schedule a later retry.
  sw.deferData(1000U);
  assert(sw.state == State::AudioOnly && !sw.slsContextValid);
  assert(!sw.ready(1000U + dab_switch::DATA_DEFER_MS - 1U));
  assert(sw.ready(1000U + dab_switch::DATA_DEFER_MS));

  // Short data retries are bounded and return through ResolveData so the
  // component is revalidated instead of blindly starting stale IDs.
  sw.dataRetries = 0U;
  for (uint8_t i = 0; i < dab_switch::MAX_DATA_RETRIES; ++i) {
    assert(sw.backoff(Resume::ResolveData, sw.dataRetries,
                      dab_switch::MAX_DATA_RETRIES, 2000U + i));
    sw.serviceBackoff(sw.notBeforeMs);
    assert(sw.state == State::ResolveData);
  }
  assert(!sw.backoff(Resume::ResolveData, sw.dataRetries,
                     dab_switch::MAX_DATA_RETRIES, 9000U));

  // SLS payload ownership is valid only after the central data start succeeds.
  sw.slsContextValid = false;
  assert(!sw.slsContextValid);
  sw.slsContextValid = true;
  sw.state = State::Ready;
  assert(sw.slsContextValid && !dab_switch::transitionActive(sw.state));

  // An ensemble may provide SLS through the confirmed audio service's PAD
  // without publishing a separately startable type-3 data component.
  assert(dab_switch::audioPadSlsContextValid(true, false));
  assert(!dab_switch::audioPadSlsContextValid(false, false));
  assert(!dab_switch::audioPadSlsContextValid(true, true));
  sw.slsContextValid = dab_switch::audioPadSlsContextValid(true, false);
  sw.state = State::Ready;
  assert(sw.slsContextValid && dab_switch::audioContextUsable(sw.state));
  return 0;
}
