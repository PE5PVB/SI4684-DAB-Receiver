#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include <Arduino.h>
#include <Preferences.h>
#include <cstring>
#include "constants.h"
#include "i2s_audio.h"

// Direct NVS persistence with a compatibility-shaped RAM view. The firmware
// keeps its proven address-based call sites while flash is stored as compact
// named records rather than an EEPROM-emulation blob.
class NvsStorage {
 public:
  bool begin();
  bool commit();
  uint8_t getI2sAudioMode() const { return i2sAudioMode_; }
  void setI2sAudioMode(uint8_t value);

  uint8_t readByte(int address) const;
  void writeByte(int address, uint8_t value);

  template <typename T> void get(int address, T& value) const {
    if (!validRange(address, sizeof(T))) {
      memset(&value, 0, sizeof(T));
      return;
    }
    memcpy(&value, image_ + address, sizeof(T));
  }

  template <typename T> void put(int address, const T& value) {
    if (!validRange(address, sizeof(T))) return;
    if (memcmp(image_ + address, &value, sizeof(T)) == 0) return;
    memcpy(image_ + address, &value, sizeof(T));
    markRangeDirty(address, sizeof(T));
  }

  size_t getScan(void* data, size_t capacity, uint8_t expectedRegion);
  bool putScan(const void* data, size_t length, uint8_t region);
  bool putEmptyScan(uint8_t region);
  bool fmScanCompleted(uint8_t expectedRegion);
  bool clearScan();
  size_t getDabScan(void* data, size_t capacity);
  bool putDabScan(const void* data, size_t length);
  bool putEmptyDabScan();
  bool dabScanCompleted();
  bool clearDabScan();

 private:
  static constexpr size_t IMAGE_SIZE = EE_TOTAL_CNT;
  static constexpr size_t CONFIG_OFFSET = 1;
  static constexpr size_t CONFIG_LENGTH = EE_PRESETS_FREQ_START - 1;
  static constexpr size_t DAB_PACKED_SIZE = EE_PRESETS_CNT * (1 + 4 + 17);
  static constexpr size_t FM_PACKED_SIZE =
      EE_PRESETS_CNT * (2 + 2 + EE_FM_PRESET_NAME_LENGTH);

  Preferences preferences_;
  uint8_t image_[IMAGE_SIZE];
  bool opened_ = false;
  bool configDirty_ = false;
  bool dabDirty_ = false;
  bool fmDirty_ = false;
  bool irDirty_ = false;
  uint8_t i2sAudioMode_ = I2S_AUDIO_48K;
  bool i2sAudioDirty_ = false;

  static bool validRange(int address, size_t length) {
    return address >= 0 && static_cast<size_t>(address) + length <= IMAGE_SIZE;
  }
  void markRangeDirty(int address, size_t length);
  void setMissingPresetDefaults();
  bool loadRecords();
  bool saveConfig();
  bool saveDabPresets();
  bool saveFmPresets();
  bool saveIrProfile();
};

extern NvsStorage Storage;

#endif
