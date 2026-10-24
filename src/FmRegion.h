#ifndef FM_REGION_H
#define FM_REGION_H

#include <stdint.h>

enum class FmRegion : uint8_t {
  Europe = 0,
  NorthAmerica = 1,
  Japan = 2
};

constexpr uint8_t FM_REGION_COUNT = 3;

struct FmRegionProfile {
  const char* menuName;
  uint16_t minFrequency10kHz;
  uint16_t maxFrequency10kHz;
  uint8_t seekSpacing10kHz;
  uint8_t deEmphasis;
  bool rbds;
};

// DAB is intentionally not part of the FM profile. Values mirror the
// bales0/FMDABRadio FmRegion profile and Si468x property encodings.
constexpr FmRegionProfile FM_REGION_PROFILES[FM_REGION_COUNT] = {
    {"Europe",    8750, 10800, 10, 1, false}, // 87.5-108.0 MHz, 100 kHz, 50 us RDS
    {"N.America", 8790, 10790, 20, 0, true }, // 87.9-107.9 MHz, 200 kHz, 75 us RBDS
    {"Japan",     7600,  9500, 10, 1, false}  // 76.0-95.0 MHz, 100 kHz, 50 us RDS
};

inline uint8_t sanitizeFmRegion(uint8_t value) {
  return value < FM_REGION_COUNT ? value
                                 : static_cast<uint8_t>(FmRegion::Europe);
}

inline const FmRegionProfile& fmRegionProfile(uint8_t value) {
  return FM_REGION_PROFILES[sanitizeFmRegion(value)];
}

inline bool isFmFrequencyValid(uint16_t frequency10kHz, uint8_t region) {
  const FmRegionProfile& profile = fmRegionProfile(region);
  return frequency10kHz >= profile.minFrequency10kHz &&
         frequency10kHz <= profile.maxFrequency10kHz &&
         ((frequency10kHz - profile.minFrequency10kHz) %
          profile.seekSpacing10kHz) == 0;
}

inline uint16_t normalizeFmFrequency(uint16_t frequency10kHz, uint8_t region) {
  const FmRegionProfile& profile = fmRegionProfile(region);
  if (frequency10kHz <= profile.minFrequency10kHz)
    return profile.minFrequency10kHz;
  if (frequency10kHz >= profile.maxFrequency10kHz)
    return profile.maxFrequency10kHz;

  const uint16_t delta = frequency10kHz - profile.minFrequency10kHz;
  uint16_t channel = static_cast<uint16_t>(
      (delta + profile.seekSpacing10kHz / 2U) / profile.seekSpacing10kHz);
  uint16_t normalized = profile.minFrequency10kHz +
                        channel * profile.seekSpacing10kHz;
  return normalized > profile.maxFrequency10kHz
             ? profile.maxFrequency10kHz
             : normalized;
}

// Circular stepping preserves the selected region's raster. A +/-100 value is
// exactly +/-1.0 MHz for rotary 1; +/-seekSpacing10kHz is rotary 2.
inline uint16_t stepFmFrequency(uint16_t frequency10kHz, int16_t delta10kHz,
                                uint8_t region) {
  const FmRegionProfile& profile = fmRegionProfile(region);
  frequency10kHz = normalizeFmFrequency(frequency10kHz, region);
  const int32_t channelCount =
      (profile.maxFrequency10kHz - profile.minFrequency10kHz) /
          profile.seekSpacing10kHz +
      1;
  int32_t channel =
      (frequency10kHz - profile.minFrequency10kHz) /
      profile.seekSpacing10kHz;
  channel += delta10kHz / static_cast<int16_t>(profile.seekSpacing10kHz);
  channel %= channelCount;
  if (channel < 0) channel += channelCount;
  return profile.minFrequency10kHz +
         static_cast<uint16_t>(channel) * profile.seekSpacing10kHz;
}

#endif
