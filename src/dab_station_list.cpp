#include "dab_station_list.h"
#include "nvs_storage.h"
#include "fm_features.h"
#include "dab_scheduler_policy.h"
#include <cstring>

DabStationList DabStations;
uint8_t DabStationListIndex = 0;

void DabStationList::clear() {
  memset(records_, 0, sizeof(records_));
  count_ = 0;
}

bool DabStationList::load() {
  clear();
  DabStationListIndex = 0;
  const size_t bytes = Storage.getDabScan(records_, sizeof(records_));
  if (bytes == 0U || bytes % sizeof(DabStationRecord) != 0U) return false;
  count_ = static_cast<uint8_t>(bytes / sizeof(DabStationRecord));
  if (count_ > DAB_STATION_LIST_CAPACITY) {
    clear();
    return false;
  }
  for (uint8_t i = 0; i < count_; ++i) {
    if (records_[i].channelIndex >= 38U || records_[i].serviceId == 0U ||
        !fm_features::isAudioServiceType(records_[i].serviceType)) {
      clear();
      return false;
    }
    records_[i].label[16] = '\0';
  }
  return true;
}

bool DabStationList::save() {
  if (count_ == 0U) return Storage.putEmptyDabScan();
  return Storage.putDabScan(records_, count_ * sizeof(DabStationRecord));
}

bool DabStationList::add(const DabStationRecord& station) {
  for (uint8_t i = 0; i < count_; ++i) {
    if (records_[i].channelIndex == station.channelIndex &&
        records_[i].serviceId == station.serviceId &&
        records_[i].componentId == station.componentId) {
      // A provisional scan list may contain the correct identity with an
      // empty label. Upgrade it when a later generation supplies a real name,
      // but never erase an already useful label with empty data.
      if (!dab_scheduler::labelHasContent(records_[i].label) &&
          dab_scheduler::labelHasContent(station.label)) {
        memcpy(records_[i].label, station.label, sizeof(records_[i].label));
        records_[i].label[16] = '\0';
        records_[i].charset = station.charset;
      }
      if (fm_features::isAudioServiceType(station.serviceType))
        records_[i].serviceType = station.serviceType;
      return false;
    }
  }
  if (count_ >= DAB_STATION_LIST_CAPACITY) return false;
  records_[count_++] = station;
  return true;
}
