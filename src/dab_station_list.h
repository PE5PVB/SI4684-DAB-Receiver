#ifndef DAB_STATION_LIST_H
#define DAB_STATION_LIST_H

#include <Arduino.h>

static constexpr uint8_t DAB_STATION_LIST_CAPACITY = 64;

struct __attribute__((packed)) DabStationRecord {
  uint8_t channelIndex;
  uint32_t serviceId;
  uint32_t componentId;
  char label[17];
  uint8_t charset;
  uint8_t serviceType;
};

class DabStationList {
 public:
  bool load();
  bool save();
  void clear();
  bool add(const DabStationRecord& station);
  uint8_t count() const { return count_; }
  const DabStationRecord& operator[](uint8_t index) const { return records_[index]; }

 private:
  DabStationRecord records_[DAB_STATION_LIST_CAPACITY] = {};
  uint8_t count_ = 0;
};

extern DabStationList DabStations;
extern uint8_t DabStationListIndex;

#endif
