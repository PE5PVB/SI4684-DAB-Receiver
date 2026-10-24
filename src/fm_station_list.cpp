#include "fm_station_list.h"
#include "nvs_storage.h"
#include "fm_features.h"
#include <cstring>

FmStationList FmStations;
uint8_t FmStationListIndex = 0;

void FmStationList::clear() {
  memset(records_, 0, sizeof(records_));
  count_ = 0;
}

bool FmStationList::load(uint8_t region) {
  clear();
  region_ = sanitizeFmRegion(region);
  FmStationListIndex = 0;
  const size_t bytes = Storage.getScan(records_, sizeof(records_), region_);
  if (bytes == 0U) return false;
  if (bytes % sizeof(FmStationRecord) != 0U) {
    clear();
    return false;
  }
  count_ = static_cast<uint8_t>(bytes / sizeof(FmStationRecord));
  if (count_ > FM_STATION_LIST_CAPACITY) {
    clear();
    return false;
  }
  for (uint8_t i = 0; i < count_; ++i) {
    if (!isFmFrequencyValid(records_[i].frequency10kHz, region)) {
      clear();
      return false;
    }
    records_[i].ps[8] = '\0';
  }
  return true;
}

bool FmStationList::save() {
  if (count_ == 0U) return Storage.putEmptyScan(region_);
  return Storage.putScan(records_, count_ * sizeof(FmStationRecord), region_);
}

bool FmStationList::add(const FmStationRecord& station) {
  int16_t duplicate = -1;
  for (uint8_t i = 0; i < count_; ++i) {
    if (fm_features::sameStation(
            records_[i].pi, records_[i].frequency10kHz,
            station.pi, station.frequency10kHz)) {
      duplicate = i;
      break;
    }
  }
  if (duplicate >= 0) {
    FmStationRecord& old = records_[duplicate];
    if (station.rssi > old.rssi || (old.ps[0] == '\0' && station.ps[0] != '\0'))
      old = station;
    return false;
  }
  if (count_ >= FM_STATION_LIST_CAPACITY) return false;
  uint8_t insert = count_;
  while (insert > 0U &&
         records_[insert - 1U].frequency10kHz > station.frequency10kHz) {
    records_[insert] = records_[insert - 1U];
    --insert;
  }
  records_[insert] = station;
  ++count_;
  return true;
}
