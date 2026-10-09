#include "../src/fm_features.h"
#include "../src/fm_af_policy.h"
#include "../src/FmRegion.h"
#include <cassert>
#include <cstring>

struct Service {
  uint8_t ServiceType;
  uint32_t ServiceID;
  uint32_t CompID;
};

int main() {
  assert(stepFmFrequency(10800, 10, static_cast<uint8_t>(FmRegion::Europe)) == 8750);
  assert(stepFmFrequency(10790, 20, static_cast<uint8_t>(FmRegion::NorthAmerica)) == 8790);
  assert(stepFmFrequency(9500, 10, static_cast<uint8_t>(FmRegion::Japan)) == 7600);
  fm_features::AfList af;
  af.clear(0x1234);
  assert(af.addCode(1) && af.frequency10kHz[0] == 8760);
  assert(af.addCode(204) && af.frequency10kHz[1] == 10790);
  assert(!af.addCode(204));
  assert(!af.addCode(205));
  assert(!af.addCode(0));
  assert(!af.addCode(226) && af.expected == 2);

  // MJD 60000 = 2023-02-25, 13:45 UTC, +1 hour local offset.
  fm_features::ClockTime ct;
  const uint32_t mjd = 60000;
  const uint16_t b = static_cast<uint16_t>((mjd >> 15) & 3U);
  const uint16_t c = static_cast<uint16_t>((mjd << 1) | (13U >> 4));
  const uint16_t d = static_cast<uint16_t>(((13U & 15U) << 12) |
                                           (45U << 6) | 2U);
  assert(fm_features::decodeClockTime(b, c, d, ct));
  assert(ct.year == 2023 && ct.month == 2 && ct.day == 25);
  assert(ct.hour == 13 && ct.minute == 45 && ct.localOffsetHalfHours == 2);
  assert(!fm_features::decodeClockTime(
      b, c, static_cast<uint16_t>(63U << 6), ct));
  assert(std::strcmp(fm_features::ptyName(4, false), "Sport") == 0);
  assert(std::strcmp(fm_features::ptyName(4, true), "Talk") == 0);

  fm_features::ClockValidator clockValidator;
  fm_features::ClockTime confirmed;
  assert(clockValidator.ingest(ct, 0x1234, 1000, confirmed) ==
         fm_features::ClockSampleResult::Candidate);
  fm_features::ClockTime nextMinute = ct;
  nextMinute.minute = 46;
  assert(clockValidator.ingest(nextMinute, 0x1234, 61000, confirmed) ==
         fm_features::ClockSampleResult::Confirmed);
  assert(confirmed.minute == 46);
  fm_features::ClockTime implausible = nextMinute;
  implausible.hour = 17;
  assert(clockValidator.ingest(implausible, 0x1234, 62000, confirmed) ==
         fm_features::ClockSampleResult::Rejected);
  assert(clockValidator.ingest(implausible, 0x5678, 63000, confirmed) ==
         fm_features::ClockSampleResult::Candidate);
  fm_features::ClockTime dayEnd;
  dayEnd.year = 2023; dayEnd.month = 2; dayEnd.day = 28;
  dayEnd.hour = 23; dayEnd.minute = 59; dayEnd.localOffsetHalfHours = 2;
  fm_features::ClockTime nextDay;
  nextDay.year = 2023; nextDay.month = 3; nextDay.day = 1;
  nextDay.hour = 0; nextDay.minute = 0; nextDay.localOffsetHalfHours = 2;
  fm_features::ClockTime yearEnd;
  yearEnd.year = 2023; yearEnd.month = 12; yearEnd.day = 31;
  yearEnd.hour = 23; yearEnd.minute = 59; yearEnd.localOffsetHalfHours = -7;
  fm_features::ClockTime nextYear;
  nextYear.year = 2024; nextYear.month = 1; nextYear.day = 1;
  nextYear.hour = 0; nextYear.minute = 0; nextYear.localOffsetHalfHours = -7;
  assert(fm_features::clockSamplesConsistent(dayEnd, nextDay, 60000));
  assert(fm_features::clockSamplesConsistent(yearEnd, nextYear, 60000));
  fm_features::ClockTime negativeOffset;
  const uint16_t negativeD = static_cast<uint16_t>(
      ((13U & 15U) << 12) | (45U << 6) | 0x20U | 7U);
  assert(fm_features::decodeClockTime(b, c, negativeD, negativeOffset));
  assert(negativeOffset.localOffsetHalfHours == -7);

  const Service services[] = {
      {8, 0, 0}, {0, 0, 0}, {4, 0, 0}, {8, 0, 0}, {5, 0, 0}};
  assert(fm_features::adjacentAudioService(services, 5, 1, true) == 2);
  assert(fm_features::adjacentAudioService(services, 5, 4, true) == -1);
  assert(fm_features::adjacentAudioService(services, 5, 1, false) == -1);
  assert(fm_features::edgeAudioService(services, 5, true) == 1);
  assert(fm_features::edgeAudioService(services, 5, false) == 4);
  assert(fm_features::nextDabChannel(37, true) == 0);
  assert(fm_features::nextDabChannel(0, false) == 37);
  assert(fm_features::sameStation(0x1234, 9000, 0x1234, 9500));
  assert(!fm_features::sameStation(0x1234, 9000, 0x5678, 9000));
  assert(fm_features::sameStation(0, 9000, 0, 9000));

  using fm_features::Rotary2Target;
  assert(fm_features::rotary2Target(true, true, true, false) ==
         Rotary2Target::FmScanList);
  assert(fm_features::rotary2Target(true, true, false, false) ==
         Rotary2Target::None);
  assert(fm_features::rotary2Target(true, false, false, false) ==
         Rotary2Target::FmFineTune);
  assert(fm_features::rotary2Target(false, true, true, true) ==
         Rotary2Target::DabGlobalList);
  assert(fm_features::rotary2Target(false, false, false, true) ==
         Rotary2Target::DabCurrentMux);
  assert(fm_features::rotary2Target(false, false, false, false) ==
         Rotary2Target::None);
  assert(fm_features::shouldShowStoredFmPs(
      false, false, 9500U, 9500U, true, false));
  assert(!fm_features::shouldShowStoredFmPs(
      true, false, 9500U, 9500U, true, false));
  assert(!fm_features::shouldShowStoredFmPs(
      false, true, 9500U, 9500U, true, false));
  assert(!fm_features::shouldShowStoredFmPs(
      false, false, 9510U, 9500U, true, false));
  assert(!fm_features::shouldShowStoredFmPs(
      false, false, 9500U, 9500U, true, true));
  assert(fm_features::rotary2ClosesDabSlideshowWait(false, true, true));
  assert(!fm_features::rotary2ClosesDabSlideshowWait(false, false, true));
  assert(!fm_features::rotary2ClosesDabSlideshowWait(false, true, false));
  assert(!fm_features::rotary2ClosesDabSlideshowWait(true, true, true));

  const Service identities[] = {
      {0, 0x100U, 0x10U}, {4, 0x200U, 0x20U},
      {5, 0x200U, 0x21U}, {8, 0x300U, 0x30U},
      {0, 0x400U, 0x40U}, {3, 0x500U, 0x50U}};
  assert(fm_features::findDabServiceByIdentity(
             identities, 5, 0x200U, 0x21U, true) == 2);
  assert(fm_features::findDabServiceByIdentity(
             identities, 5, 0x400U, 0x99U, true) == 4);
  assert(fm_features::findDabServiceByIdentity(
             identities, 5, 0x200U, 0x99U, true) == -1);
  assert(fm_features::findDabServiceByIdentity(
             identities, 5, 0x300U, 0x30U, true) == 3);
  assert(fm_features::findDabServiceByIdentity(
             identities, 6, 0x300U, 0x30U, true) == 3);
  assert(fm_features::findDabServiceByIdentity(
             identities, 6, 0x300U, 0x99U, true) == -1);
  assert(fm_features::findDabServiceByIdentity(
             identities, 6, 0x500U, 0x50U, true) == -1);
  assert(fm_features::deferMissingDabIdentity(false, false));
  assert(!fm_features::deferMissingDabIdentity(true, false));
  assert(!fm_features::deferMissingDabIdentity(false, true));
  assert(fm_features::shouldShowPendingDabTarget(true, false, false, true));
  assert(fm_features::shouldShowPendingDabTarget(false, true, false, true));
  assert(!fm_features::shouldShowPendingDabTarget(false, false, false, true));
  assert(!fm_features::shouldShowPendingDabTarget(true, false, false, false));
  // The selected name survives debounce -> driver start -> metadata arrival.
  // Exercise both UI selection routes (local/rotary1 and global/rotary2).
  for (unsigned route = 0; route < 2; ++route) {
    assert(fm_features::shouldShowPendingDabTarget(true, route != 0, false, true, true));
    assert(fm_features::shouldShowPendingDabTarget(false, false, true, true, false));
    assert(fm_features::shouldShowPendingDabTarget(false, false, false, true, false));
    assert(!fm_features::shouldShowPendingDabTarget(false, false, false, true, true));
    assert(!fm_features::shouldShowPendingDabTarget(false, false, false, false, false));
  }

  assert(fm_af::weakSignal(true, 10, 8, 18, 4));
  assert(!fm_af::weakSignal(true, 20, 8, 18, 4));
  assert(fm_af::candidateIsBetter(true, 0x1234, 0x1234,
                                  10, 5, 15, 4));
  assert(!fm_af::candidateIsBetter(true, 0x1234, 0x5678,
                                   10, 5, 20, 8));
  assert(!fm_af::candidateIsBetter(true, 0x1234, 0x1234,
                                   10, 5, 14, 8));
  assert(!fm_af::sweepExpired(999U, 1000U));
  assert(fm_af::sweepExpired(1000U, 1000U));
  assert(fm_af::sweepExpired(5U, 0xFFFFFFF0UL));

  fm_features::PressTracker press;
  assert(press.update(true, 100, 1000) == 0);
  assert(press.update(false, 900, 1000) == 1);
  assert(press.update(true, 2000, 1000) == 0);
  assert(press.update(true, 3000, 1000) == 2);
  assert(press.update(false, 3100, 1000) == 0);
  return 0;
}
