#ifndef FM_AF_POLICY_H
#define FM_AF_POLICY_H

#include <stdint.h>

namespace fm_af {

static constexpr uint32_t STATION_STABLE_MS = 10000UL;
static constexpr uint32_t WEAK_HOLD_MS = 4000UL;
static constexpr uint32_t PI_TIMEOUT_MS = 2000UL;
static constexpr uint32_t SWEEP_TIMEOUT_MS = 15000UL;
static constexpr uint32_t SUCCESS_COOLDOWN_MS = 30000UL;
static constexpr uint32_t FAILURE_COOLDOWN_MS = 10000UL;
static constexpr int8_t MIN_RSSI_GAIN_DB = 5;
static constexpr int8_t MAX_SNR_LOSS_DB = 1;

inline bool weakSignal(bool valid, int8_t rssi, int8_t snr,
                       uint8_t rssiThreshold, uint8_t snrThreshold) {
  return !valid || rssi < static_cast<int8_t>(rssiThreshold) ||
         snr < static_cast<int8_t>(snrThreshold);
}

inline bool candidateIsBetter(bool valid, uint16_t expectedPi,
                              uint16_t candidatePi, int8_t originalRssi,
                              int8_t originalSnr, int8_t candidateRssi,
                              int8_t candidateSnr) {
  return valid && expectedPi != 0U && candidatePi == expectedPi &&
         candidateRssi >= originalRssi + MIN_RSSI_GAIN_DB &&
         candidateSnr >= originalSnr - MAX_SNR_LOSS_DB;
}

inline bool sweepExpired(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

}  // namespace fm_af

#endif
