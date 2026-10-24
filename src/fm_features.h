#ifndef FM_FEATURES_H
#define FM_FEATURES_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace fm_features {

static constexpr uint8_t MAX_AF_COUNT = 25;

enum class Rotary2Target : uint8_t {
  None,
  FmFineTune,
  FmScanList,
  DabGlobalList,
  DabCurrentMux
};

inline Rotary2Target rotary2Target(bool fmMode, bool autoMode,
                                   bool globalListAvailable,
                                   bool currentMuxAvailable) {
  if (fmMode) {
    if (autoMode)
      return globalListAvailable ? Rotary2Target::FmScanList
                                 : Rotary2Target::None;
    return Rotary2Target::FmFineTune;
  }
  if (globalListAvailable) return Rotary2Target::DabGlobalList;
  return currentMuxAvailable ? Rotary2Target::DabCurrentMux
                             : Rotary2Target::None;
}

inline bool shouldShowStoredFmPs(bool manualTuning, bool seekActive,
                                 uint16_t currentFrequency,
                                 uint16_t targetFrequency,
                                 bool storedPsAvailable,
                                 bool livePsAvailable) {
  return !manualTuning && !seekActive && storedPsAvailable &&
         !livePsAvailable && currentFrequency == targetFrequency;
}

inline bool rotary2ClosesDabSlideshowWait(bool fmMode, bool waitingForSlide,
                                          bool globalListAvailable) {
  return !fmMode && waitingForSlide && globalListAvailable;
}

inline bool isDabAudioServiceType(uint8_t serviceType) {
  return serviceType == 0x00U || serviceType == 0x04U ||
         serviceType == 0x05U;
}

template <typename ServiceT>
int16_t findDabServiceByIdentity(const ServiceT* services, uint8_t count,
                                 uint32_t serviceId, uint32_t componentId,
                                 bool componentValid) {
  int16_t uniqueSidMatch = -1;
  uint8_t sidMatchCount = 0U;
  for (uint8_t i = 0; i < count; ++i) {
    if (services[i].ServiceID != serviceId) continue;

    // A freshly parsed DAB service list uses type 8 as an explicit
    // "metadata not queried yet" marker. SID+component already form an exact
    // identity at that point, so do not make the fast path wait for the later
    // cooperative GET_SUBCHAN_INFO pass. Once classified, still reject an
    // exact match that is known not to be an audio component.
    if (componentValid && services[i].CompID == componentId &&
        (services[i].ServiceType == 8U ||
         isDabAudioServiceType(services[i].ServiceType)))
      return static_cast<int16_t>(i);

    if (!isDabAudioServiceType(services[i].ServiceType)) continue;
    uniqueSidMatch = static_cast<int16_t>(i);
    if (sidMatchCount != 0xFFU) ++sidMatchCount;
  }
  return sidMatchCount == 1U ? uniqueSidMatch : -1;
}

inline bool deferMissingDabIdentity(bool metadataReady, bool matchFound) {
  return !matchFound && !metadataReady;
}

struct AfList {
  uint16_t pi = 0;
  uint8_t expected = 0;
  uint8_t count = 0;
  uint16_t frequency10kHz[MAX_AF_COUNT] = {};

  void clear(uint16_t newPi = 0) {
    pi = newPi;
    expected = count = 0;
    memset(frequency10kHz, 0, sizeof(frequency10kHz));
  }

  bool addCode(uint8_t code) {
    if (code >= 224U && code <= 249U) {
      expected = static_cast<uint8_t>(code - 224U);
      return false;
    }
    // RDS Method A VHF codes: 1=87.6 MHz ... 204=107.9 MHz.
    if (code < 1U || code > 204U) return false;
    const uint16_t frequency = static_cast<uint16_t>(8750U + code * 10U);
    for (uint8_t i = 0; i < count; ++i)
      if (frequency10kHz[i] == frequency) return false;
    if (count >= MAX_AF_COUNT) return false;
    frequency10kHz[count++] = frequency;
    return true;
  }
};

struct ClockTime {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  int8_t localOffsetHalfHours = 0;
};

enum class ClockSampleResult : uint8_t {
  Candidate,
  Confirmed,
  Rejected
};

inline bool leapYear(uint16_t year) {
  return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

inline uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (month < 1U || month > 12U) return 0;
  return month == 2U && leapYear(year) ? 29U : days[month - 1U];
}

inline bool mjdToDate(uint32_t mjd, uint16_t& year, uint8_t& month,
                      uint8_t& day) {
  if (mjd < 15079U || mjd > 88068U) return false; // 1900..2099
  const int32_t j = static_cast<int32_t>(mjd) + 2400001 + 68569;
  int32_t n = (4 * j) / 146097;
  int32_t l = j - (146097 * n + 3) / 4;
  int32_t i = (4000 * (l + 1)) / 1461001;
  l = l - (1461 * i) / 4 + 31;
  int32_t k = (80 * l) / 2447;
  const int32_t d = l - (2447 * k) / 80;
  l = k / 11;
  const int32_t m = k + 2 - 12 * l;
  const int32_t y = 100 * (n - 49) + i + l;
  if (y < 1900 || y > 2099 || m < 1 || m > 12 || d < 1 ||
      d > daysInMonth(static_cast<uint16_t>(y), static_cast<uint8_t>(m)))
    return false;
  year = static_cast<uint16_t>(y);
  month = static_cast<uint8_t>(m);
  day = static_cast<uint8_t>(d);
  return true;
}

inline bool decodeClockTime(uint16_t blockB, uint16_t blockC, uint16_t blockD,
                            ClockTime& output) {
  const uint32_t mjd = (static_cast<uint32_t>(blockB & 0x0003U) << 15) |
                       (static_cast<uint32_t>(blockC) >> 1);
  const uint8_t hour = static_cast<uint8_t>(((blockC & 1U) << 4) |
                                             ((blockD >> 12) & 0x0FU));
  const uint8_t minute = static_cast<uint8_t>((blockD >> 6) & 0x3FU);
  const uint8_t magnitude = static_cast<uint8_t>(blockD & 0x1FU);
  if (hour > 23U || minute > 59U || magnitude > 24U) return false;
  ClockTime parsed;
  if (!mjdToDate(mjd, parsed.year, parsed.month, parsed.day)) return false;
  parsed.hour = hour;
  parsed.minute = minute;
  parsed.localOffsetHalfHours = static_cast<int8_t>(magnitude);
  if ((blockD & 0x0020U) != 0) parsed.localOffsetHalfHours *= -1;
  output = parsed;
  return true;
}

inline bool clockTimeToMinutes(const ClockTime& value, int32_t& minutes) {
  if (value.year < 1900U || value.year > 2099U || value.month < 1U ||
      value.month > 12U || value.day < 1U ||
      value.day > daysInMonth(value.year, value.month) || value.hour > 23U ||
      value.minute > 59U || value.localOffsetHalfHours < -24 ||
      value.localOffsetHalfHours > 24)
    return false;

  int32_t days = 0;
  for (uint16_t year = 1900U; year < value.year; ++year)
    days += leapYear(year) ? 366 : 365;
  for (uint8_t month = 1U; month < value.month; ++month)
    days += daysInMonth(value.year, month);
  days += static_cast<int32_t>(value.day) - 1;
  minutes = days * 1440L + static_cast<int32_t>(value.hour) * 60L +
            value.minute;
  return true;
}

inline bool clockSamplesConsistent(const ClockTime& previous,
                                   const ClockTime& current,
                                   uint32_t elapsedMs,
                                   uint8_t maxAdvanceMinutes = 5U) {
  if (previous.localOffsetHalfHours != current.localOffsetHalfHours ||
      elapsedMs > 10UL * 60UL * 1000UL)
    return false;
  int32_t previousMinutes = 0;
  int32_t currentMinutes = 0;
  if (!clockTimeToMinutes(previous, previousMinutes) ||
      !clockTimeToMinutes(current, currentMinutes))
    return false;
  const int32_t advance = currentMinutes - previousMinutes;
  const uint32_t elapsedMinutes = elapsedMs / 60000UL;
  const uint32_t elapsedAllowance = elapsedMinutes + 1U;
  const uint32_t allowed = elapsedAllowance < maxAdvanceMinutes
                               ? elapsedAllowance
                               : maxAdvanceMinutes;
  return advance >= 0 && static_cast<uint32_t>(advance) <= allowed;
}

struct ClockValidator {
  bool candidateValid = false;
  uint16_t candidatePi = 0U;
  uint32_t candidateTimestampMs = 0U;
  ClockTime candidate;

  void reset() {
    candidateValid = false;
    candidatePi = 0U;
    candidateTimestampMs = 0U;
    candidate = ClockTime{};
  }

  ClockSampleResult ingest(const ClockTime& decoded, uint16_t pi,
                           uint32_t now, ClockTime& confirmed) {
    if (pi == 0U) {
      reset();
      return ClockSampleResult::Rejected;
    }
    if (!candidateValid || candidatePi != pi) {
      candidate = decoded;
      candidatePi = pi;
      candidateTimestampMs = now;
      candidateValid = true;
      return ClockSampleResult::Candidate;
    }
    const uint32_t elapsed = now - candidateTimestampMs;
    if (!clockSamplesConsistent(candidate, decoded, elapsed)) {
      candidate = decoded;
      candidateTimestampMs = now;
      return ClockSampleResult::Rejected;
    }
    candidate = decoded;
    candidateTimestampMs = now;
    confirmed = decoded;
    return ClockSampleResult::Confirmed;
  }
};

inline bool shouldShowPendingDabTarget(bool selectionDebounce,
                                       bool restorePending,
                                       bool driverStartPending,
                                       bool labelAvailable,
                                       bool currentLabelReady = true) {
  return labelAvailable &&
         (selectionDebounce || restorePending || driverStartPending || !currentLabelReady);
}

inline const char* ptyName(uint8_t pty, bool rbds) {
  static const char* const rds[32] = {
    "None", "News", "Current Affairs", "Information", "Sport", "Education",
    "Drama", "Culture", "Science", "Varied", "Pop Music", "Rock Music",
    "Easy Listening", "Light Classics", "Serious Classics", "Other Music",
    "Weather", "Finance", "Children", "Social Affairs", "Religion",
    "Phone In", "Travel", "Leisure", "Jazz", "Country", "National Music",
    "Oldies", "Folk Music", "Documentary", "Alarm Test", "Alarm"};
  static const char* const rbdsNames[32] = {
    "None", "News", "Information", "Sports", "Talk", "Rock", "Classic Rock",
    "Adult Hits", "Soft Rock", "Top 40", "Country", "Oldies", "Soft",
    "Nostalgia", "Jazz", "Classical", "Rhythm and Blues", "Soft R&B",
    "Foreign Language", "Religious Music", "Religious Talk", "Personality",
    "Public", "College", "Spanish Talk", "Spanish Music", "Hip Hop",
    "Unassigned", "Unassigned", "Weather", "Emergency Test", "Emergency"};
  return pty < 32U ? (rbds ? rbdsNames[pty] : rds[pty]) : "";
}

inline bool isAudioServiceType(uint8_t type) {
  return type == 0x00U || type == 0x04U || type == 0x05U;
}

inline uint8_t nextDabChannel(uint8_t current, bool forward) {
  current = static_cast<uint8_t>(current % 38U);
  return forward ? static_cast<uint8_t>((current + 1U) % 38U)
                 : static_cast<uint8_t>(current == 0U ? 37U : current - 1U);
}

inline bool sameStation(uint16_t firstPi, uint16_t firstFrequency,
                        uint16_t secondPi, uint16_t secondFrequency) {
  return firstPi != 0U && secondPi != 0U
             ? firstPi == secondPi
             : firstFrequency == secondFrequency;
}

template <typename Service>
int16_t adjacentAudioService(const Service* services, uint8_t count,
                             uint8_t current, bool forward) {
  if (!services || count == 0U || current >= count) return -1;
  if (forward) {
    for (uint8_t i = static_cast<uint8_t>(current + 1U); i < count; ++i)
      if (isAudioServiceType(services[i].ServiceType)) return i;
  } else {
    for (int16_t i = static_cast<int16_t>(current) - 1; i >= 0; --i)
      if (isAudioServiceType(services[i].ServiceType)) return i;
  }
  return -1;
}

template <typename Service>
int16_t edgeAudioService(const Service* services, uint8_t count, bool first) {
  if (!services || count == 0U) return -1;
  if (first) {
    for (uint8_t i = 0; i < count; ++i)
      if (isAudioServiceType(services[i].ServiceType)) return i;
  } else {
    for (int16_t i = static_cast<int16_t>(count) - 1; i >= 0; --i)
      if (isAudioServiceType(services[i].ServiceType)) return i;
  }
  return -1;
}

struct PressTracker {
  bool down = false;
  bool longFired = false;
  uint32_t started = 0;

  // 0=no action, 1=short release, 2=long threshold crossed.
  uint8_t update(bool pressed, uint32_t now, uint32_t longPressMs) {
    if (pressed && !down) {
      down = true; longFired = false; started = now; return 0;
    }
    if (pressed && down && !longFired &&
        static_cast<uint32_t>(now - started) >= longPressMs) {
      longFired = true; return 2;
    }
    if (!pressed && down) {
      down = false;
      if (!longFired) return 1;
      longFired = false;
    }
    return 0;
  }
};

} // namespace fm_features

#endif
