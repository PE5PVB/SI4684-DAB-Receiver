#ifndef FM_STATION_LIST_H
#define FM_STATION_LIST_H

#include <Arduino.h>
#include "FmRegion.h"

static constexpr uint8_t FM_STATION_LIST_CAPACITY = 64;

struct __attribute__((packed)) FmStationRecord {
  uint16_t frequency10kHz;
  uint16_t pi;
  char ps[9];
  int8_t rssi;
  int8_t snr;
  uint8_t multipath;
  uint8_t pty;
};

class FmStationList {
 public:
  bool load(uint8_t region);
  bool save();
  void clear();
  void clear(uint8_t region) { clear(); region_ = sanitizeFmRegion(region); }
  bool add(const FmStationRecord& station);
  uint8_t count() const { return count_; }
  const FmStationRecord& operator[](uint8_t index) const { return records_[index]; }

 private:
  FmStationRecord records_[FM_STATION_LIST_CAPACITY] = {};
  uint8_t count_ = 0;
  uint8_t region_ = static_cast<uint8_t>(FmRegion::Europe);
};

extern FmStationList FmStations;
extern uint8_t FmStationListIndex;

#endif
