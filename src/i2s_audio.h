#pragma once
#include <stdint.h>

enum I2sAudioMode : uint8_t {
  I2S_AUDIO_OFF, I2S_AUDIO_32K, I2S_AUDIO_44K1,
  I2S_AUDIO_48K, I2S_AUDIO_MODE_COUNT
};
inline I2sAudioMode sanitizeI2sAudioMode(uint8_t value) {
  return value < I2S_AUDIO_MODE_COUNT ? static_cast<I2sAudioMode>(value) : I2S_AUDIO_48K;
}
inline uint16_t i2sAudioSampleRate(I2sAudioMode value) {
  switch (sanitizeI2sAudioMode(value)) {
    case I2S_AUDIO_OFF: return 0;
    case I2S_AUDIO_32K: return 32000;
    case I2S_AUDIO_44K1: return 44100;
    default: return 48000;
  }
}
inline const char* i2sAudioModeText(I2sAudioMode value) {
  switch (sanitizeI2sAudioMode(value)) {
    case I2S_AUDIO_OFF: return "OFF";
    case I2S_AUDIO_32K: return "32 kHz";
    case I2S_AUDIO_44K1: return "44.1 kHz";
    default: return "48 kHz";
  }
}
inline I2sAudioMode cycleI2sAudioMode(I2sAudioMode value, bool forward) {
  const uint8_t mode = sanitizeI2sAudioMode(value);
  return static_cast<I2sAudioMode>((mode + (forward ? 1U : I2S_AUDIO_MODE_COUNT - 1U)) %
                                  I2S_AUDIO_MODE_COUNT);
}
// AN649: keep analog DAC (bit 0) enabled independently of digital I²S (bit 1)
// and INTB (bit 15). MASTER is bit 15 of DIGITAL_IO_OUTPUT_SELECT, not PIN_CONFIG.
static constexpr uint16_t I2S_AUDIO_MASTER = 0x8000U;
// Philips I2S: 24-bit samples in 32-bit channel slots, BCK = 64 * Fs.
static constexpr uint16_t I2S_AUDIO_FORMAT_STANDARD = 0x1870U;
inline uint16_t i2sAudioPinConfig(I2sAudioMode mode, bool intb) {
  return 1U | (i2sAudioSampleRate(mode) != 0U ? 2U : 0U) | (intb ? 0x8000U : 0U);
}
