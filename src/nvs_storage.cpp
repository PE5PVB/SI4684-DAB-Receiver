#include "nvs_storage.h"

NvsStorage Storage;

void NvsStorage::setMissingPresetDefaults() {
  for (int i = 0; i < EE_PRESETS_CNT; ++i) {
    image_[EE_PRESETS_FREQ_START + i] = EE_PRESETS_FREQUENCY;
    const uint32_t zero = 0;
    memcpy(image_ + EE_PRESETS_SERVICEID_START + i * 8, &zero, sizeof(zero));
    memset(image_ + EE_PRESETS_NAME_START + i * 17, 0, 17);

    const uint16_t empty = EE_FM_PRESET_EMPTY_FREQUENCY;
    memcpy(image_ + EE_FM_PRESETS_FREQ_START + i * 2, &empty, sizeof(empty));
    memcpy(image_ + EE_FM_PRESETS_PI_START + i * 2, &zero, sizeof(uint16_t));
    memset(image_ + EE_FM_PRESETS_NAME_START +
               i * EE_FM_PRESET_NAME_LENGTH,
           0, EE_FM_PRESET_NAME_LENGTH);
  }
  memset(image_ + EE_IR_CONFIG_START, 0, EE_IR_CONFIG_SIZE);
}

bool NvsStorage::loadRecords() {
  if (!preferences_.isKey("settings") ||
      preferences_.getBytesLength("settings") != CONFIG_LENGTH) return false;
  image_[EE_BYTE_CHECKBYTE] = EE_CHECKBYTE_VALUE;
  if (preferences_.getBytes("settings", image_ + CONFIG_OFFSET,
                            CONFIG_LENGTH) != CONFIG_LENGTH)
    return false;

  uint8_t dab[DAB_PACKED_SIZE];
  if (preferences_.isKey("dab_presets") &&
      preferences_.getBytesLength("dab_presets") == sizeof(dab) &&
      preferences_.getBytes("dab_presets", dab, sizeof(dab)) == sizeof(dab)) {
    size_t p = 0;
    for (int i = 0; i < EE_PRESETS_CNT; ++i) {
      image_[EE_PRESETS_FREQ_START + i] = dab[p++];
      memcpy(image_ + EE_PRESETS_SERVICEID_START + i * 8, dab + p, 4); p += 4;
      memcpy(image_ + EE_PRESETS_NAME_START + i * 17, dab + p, 17); p += 17;
    }
  } else {
    dabDirty_ = true;
  }

  uint8_t fm[FM_PACKED_SIZE];
  if (preferences_.isKey("fm_presets") &&
      preferences_.getBytesLength("fm_presets") == sizeof(fm) &&
      preferences_.getBytes("fm_presets", fm, sizeof(fm)) == sizeof(fm)) {
    size_t p = 0;
    for (int i = 0; i < EE_PRESETS_CNT; ++i) {
      memcpy(image_ + EE_FM_PRESETS_FREQ_START + i * 2, fm + p, 2); p += 2;
      memcpy(image_ + EE_FM_PRESETS_PI_START + i * 2, fm + p, 2); p += 2;
      memcpy(image_ + EE_FM_PRESETS_NAME_START +
                 i * EE_FM_PRESET_NAME_LENGTH,
             fm + p, EE_FM_PRESET_NAME_LENGTH);
      p += EE_FM_PRESET_NAME_LENGTH;
    }
  } else {
    fmDirty_ = true;
  }

  if (preferences_.isKey("ir_profile") &&
      preferences_.getBytesLength("ir_profile") == EE_IR_CONFIG_SIZE) {
    preferences_.getBytes("ir_profile", image_ + EE_IR_CONFIG_START,
                          EE_IR_CONFIG_SIZE);
  } else {
    irDirty_ = true;
  }
  return true;
}

bool NvsStorage::begin() {
  memset(image_, 0xFF, sizeof(image_));
  setMissingPresetDefaults();
  if (!preferences_.begin("si4684", false)) return false;
  opened_ = true;
  const uint8_t storedI2s = preferences_.getUChar("i2s_audio", I2S_AUDIO_48K);
  i2sAudioMode_ = sanitizeI2sAudioMode(storedI2s);
  i2sAudioDirty_ = storedI2s != i2sAudioMode_;
  if (loadRecords()) {
    // Missing optional records were replaced with safe defaults above.
    return commit();
  }
  // Missing or malformed records deliberately fall back to application
  // defaults. No EEPROM-emulation data is imported.
  image_[EE_BYTE_CHECKBYTE] = 0xFF;
  configDirty_ = dabDirty_ = fmDirty_ = irDirty_ = true;
  return true;
}

void NvsStorage::setI2sAudioMode(uint8_t value) {
  const uint8_t mode = sanitizeI2sAudioMode(value);
  if (mode != i2sAudioMode_ || !opened_ || !preferences_.isKey("i2s_audio")) {
    i2sAudioMode_ = mode;
    i2sAudioDirty_ = true;
  }
}

uint8_t NvsStorage::readByte(int address) const {
  return validRange(address, 1) ? image_[address] : 0xFF;
}

void NvsStorage::writeByte(int address, uint8_t value) {
  if (!validRange(address, 1) || image_[address] == value) return;
  image_[address] = value;
  markRangeDirty(address, 1);
}

void NvsStorage::markRangeDirty(int address, size_t length) {
  const size_t first = static_cast<size_t>(address);
  const size_t last = first + length;
  if (first < EE_PRESETS_FREQ_START) configDirty_ = true;
  if (first < EE_PRESETS_NAME_START + EE_PRESETS_CNT * 17 &&
      last > EE_PRESETS_FREQ_START) dabDirty_ = true;
  if (first < EE_FM_PRESETS_END && last > EE_FM_PRESETS_FREQ_START)
    fmDirty_ = true;
  if (first < EE_IR_CONFIG_END && last > EE_IR_CONFIG_START) irDirty_ = true;
}

bool NvsStorage::saveConfig() {
  return preferences_.putBytes("settings", image_ + CONFIG_OFFSET,
                               CONFIG_LENGTH) == CONFIG_LENGTH;
}

bool NvsStorage::saveDabPresets() {
  uint8_t packed[DAB_PACKED_SIZE];
  size_t p = 0;
  for (int i = 0; i < EE_PRESETS_CNT; ++i) {
    packed[p++] = image_[EE_PRESETS_FREQ_START + i];
    memcpy(packed + p, image_ + EE_PRESETS_SERVICEID_START + i * 8, 4); p += 4;
    memcpy(packed + p, image_ + EE_PRESETS_NAME_START + i * 17, 17); p += 17;
  }
  return preferences_.putBytes("dab_presets", packed, sizeof(packed)) ==
         sizeof(packed);
}

bool NvsStorage::saveFmPresets() {
  uint8_t packed[FM_PACKED_SIZE];
  size_t p = 0;
  for (int i = 0; i < EE_PRESETS_CNT; ++i) {
    memcpy(packed + p, image_ + EE_FM_PRESETS_FREQ_START + i * 2, 2); p += 2;
    memcpy(packed + p, image_ + EE_FM_PRESETS_PI_START + i * 2, 2); p += 2;
    memcpy(packed + p, image_ + EE_FM_PRESETS_NAME_START +
               i * EE_FM_PRESET_NAME_LENGTH,
           EE_FM_PRESET_NAME_LENGTH);
    p += EE_FM_PRESET_NAME_LENGTH;
  }
  return preferences_.putBytes("fm_presets", packed, sizeof(packed)) ==
         sizeof(packed);
}

bool NvsStorage::saveIrProfile() {
  return preferences_.putBytes("ir_profile", image_ + EE_IR_CONFIG_START,
                               EE_IR_CONFIG_SIZE) == EE_IR_CONFIG_SIZE;
}

bool NvsStorage::commit() {
  if (!opened_) return false;
  bool ok = true;
  if (configDirty_) ok = saveConfig() && ok;
  if (dabDirty_) ok = saveDabPresets() && ok;
  if (fmDirty_) ok = saveFmPresets() && ok;
  if (irDirty_) ok = saveIrProfile() && ok;
  if (i2sAudioDirty_)
    ok = (preferences_.putUChar("i2s_audio", i2sAudioMode_) == sizeof(uint8_t)) && ok;
  if (ok) {
    configDirty_ = dabDirty_ = fmDirty_ = irDirty_ = false;
    i2sAudioDirty_ = false;
  }
  return ok;
}

size_t NvsStorage::getScan(void* data, size_t capacity,
                           uint8_t expectedRegion) {
  if (!opened_ || !data) return 0;
  if (!preferences_.isKey("fm_scan") ||
      !preferences_.isKey("fm_scan_reg")) return 0;
  if (preferences_.getUChar("fm_scan_reg", 0xFFU) != expectedRegion) return 0;
  const size_t length = preferences_.getBytesLength("fm_scan");
  if (length == 0 || length > capacity) return 0;
  return preferences_.getBytes("fm_scan", data, length);
}

bool NvsStorage::putScan(const void* data, size_t length, uint8_t region) {
  if (!opened_ || !data || length == 0U) return false;
  if (preferences_.putBytes("fm_scan", data, length) != length) return false;
  if (preferences_.putUChar("fm_scan_reg", region) != sizeof(uint8_t))
    return false;
  return preferences_.putBool("fm_scan_done", true) == sizeof(bool);
}

bool NvsStorage::putEmptyScan(uint8_t region) {
  if (!opened_) return false;
  if (preferences_.isKey("fm_scan") && !preferences_.remove("fm_scan"))
    return false;
  if (preferences_.putUChar("fm_scan_reg", region) != sizeof(uint8_t))
    return false;
  return preferences_.putBool("fm_scan_done", true) == sizeof(bool);
}

bool NvsStorage::fmScanCompleted(uint8_t expectedRegion) {
  return opened_ && preferences_.isKey("fm_scan_done") &&
         preferences_.isKey("fm_scan_reg") &&
         preferences_.getBool("fm_scan_done", false) &&
         preferences_.getUChar("fm_scan_reg", 0xFFU) == expectedRegion;
}

bool NvsStorage::clearScan() {
  if (!opened_) return false;
  const bool scanRemoved = !preferences_.isKey("fm_scan") ||
                           preferences_.remove("fm_scan");
  const bool regionRemoved = !preferences_.isKey("fm_scan_reg") ||
                             preferences_.remove("fm_scan_reg");
  const bool doneRemoved = !preferences_.isKey("fm_scan_done") ||
                           preferences_.remove("fm_scan_done");
  return scanRemoved && regionRemoved && doneRemoved;
}

size_t NvsStorage::getDabScan(void* data, size_t capacity) {
  if (!opened_ || !data) return 0;
  if (!preferences_.isKey("dab_scan")) return 0;
  const size_t length = preferences_.getBytesLength("dab_scan");
  if (length == 0U || length > capacity) return 0;
  return preferences_.getBytes("dab_scan", data, length);
}

bool NvsStorage::putDabScan(const void* data, size_t length) {
  if (!opened_ || !data || length == 0U) return false;
  if (preferences_.putBytes("dab_scan", data, length) != length) return false;
  return preferences_.putBool("dab_scan_done", true) == sizeof(bool);
}

bool NvsStorage::putEmptyDabScan() {
  if (!opened_) return false;
  if (preferences_.isKey("dab_scan") && !preferences_.remove("dab_scan"))
    return false;
  return preferences_.putBool("dab_scan_done", true) == sizeof(bool);
}

bool NvsStorage::dabScanCompleted() {
  return opened_ && preferences_.isKey("dab_scan_done") &&
         preferences_.getBool("dab_scan_done", false);
}

bool NvsStorage::clearDabScan() {
  if (!opened_) return false;
  const bool scanRemoved = !preferences_.isKey("dab_scan") ||
                           preferences_.remove("dab_scan");
  const bool doneRemoved = !preferences_.isKey("dab_scan_done") ||
                           preferences_.remove("dab_scan_done");
  return scanRemoved && doneRemoved;
}
