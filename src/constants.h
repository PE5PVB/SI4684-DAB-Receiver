// Project-wide constants: hardware pin assignments, legacy logical storage layout,
// UI line-spacing offsets, and the user-facing tune-mode / memory-status enums.

#ifndef CONSTANTS_H
#define CONSTANTS_H

#ifdef ARDUINO
#include "serial_monitor.h"
#define DIAG_STREAM diagSerial
#else
// Host-only decoder tests provide a minimal Serial stub and do not link the
// Arduino HardwareSerial facade.
#define DIAG_STREAM Serial
#endif

// Global UART diagnostics gate. Normal operation keeps application diagnostics
// silent; the bare serial command DEBUG toggles this flag at runtime. The
// control protocol in comms.cpp deliberately bypasses these macros.
extern bool diagnosticDebug;
#define DIAG_PRINT(...)   do { if (diagnosticDebug) DIAG_STREAM.print(__VA_ARGS__); } while (0)
#define DIAG_PRINTLN(...) do { if (diagnosticDebug) DIAG_STREAM.println(__VA_ARGS__); } while (0)
#define DIAG_PRINTF(...)  do { if (diagnosticDebug) DIAG_STREAM.printf(__VA_ARGS__); } while (0)

// ---------- Hardware pin assignments ----------
#define ROTARY_PIN_A    27
#define ROTARY_PIN_B    34
#define ROTARY_PIN_2A   33
#define ROTARY_PIN_2B   32
#define ROTARY_BUTTON   25
#define ROTARY_BUTTON2  35
#define STANDBYBUTTON   36
#define SLBUTTON        26
#define MODEBUTTON      39
#define CONTRASTPIN     2
#define SI4684_INTB_PIN 12

// ---------- GUI vertical spacing (px) for menu/list items ----------
#define ITEM_GAP        20
#define ITEM1           3
#define ITEM2           23
#define ITEM3           43
#define ITEM4           63
#define ITEM5           83
#define ITEM6           103
#define ITEM7           123
#define ITEM8           143
#define ITEM9           163
#define ITEM10          183

// ---------- GPIO12 role ----------
enum Gpio12Mode : uint8_t {
  GPIO12_AUTO = 0,
  GPIO12_INTB = 1,
  GPIO12_IR   = 2
};

static inline uint8_t sanitizeGpio12Mode(uint8_t value) {
  return value <= static_cast<uint8_t>(GPIO12_IR)
      ? value
      : static_cast<uint8_t>(GPIO12_AUTO);
}

static const char* const Gpio12ModeText[] = {"AUTO", "INTB", "IR"};

// ---------- Logical persistence layout ----------
// Runtime persistence is stored as compact direct-NVS records. These offsets
// remain only as an in-RAM compatibility view for existing call sites.
#define EE_PRESETS_CNT              99
#define EE_PRESETS_FREQUENCY        255
#define EE_CHECKBYTE_VALUE          5

#define EE_TOTAL_CNT                4096
#define EE_BYTE_CHECKBYTE           0
#define EE_BYTE_LANGUAGE            1
#define EE_BYTE_CONTRASTSET         2
#define EE_BYTE_DISPLAYFLIP         3
#define EE_BYTE_ROTARYMODE          4
#define EE_BYTE_TUNEMODE            5
#define EE_BYTE_RADIO_MODE          6
#define EE_BYTE_UNIT                7
#define EE_BYTE_DABFREQ             8
#define EE_BYTE_VOLUME              9
#define EE_BYTE_MEMORYPOS           10
#define EE_BYTE_THEME               11
#define EE_BYTE_AUTOSLIDESHOW       12
#define EE_BYTE_TOT                 13
#define EE_UINT32_SERVICEID         14
#define EE_UINT16_FM_FREQUENCY      18
#define EE_BYTE_FM_REGION           20
#define EE_BYTE_GPIO12_MODE         21
#define EE_CHAR17_SERVICENAME       22
#define EE_BYTE_FM_AF_ENABLED       37
#define EE_BYTE_FM_SEEK_SENSITIVITY 38
#define EE_PRESETS_FREQ_START       39
#define EE_PRESETS_SERVICEID_START  138
#define EE_PRESETS_NAME_START       930

#define EE_FM_PRESETS_FREQ_START    2614
#define EE_FM_PRESETS_PI_START      2812
#define EE_FM_PRESETS_NAME_START    3010
#define EE_FM_PRESET_NAME_LENGTH    9
#define EE_FM_PRESET_EMPTY_FREQUENCY 0xFFFFU
#define EE_FM_PRESETS_END           3901

// Learned IR profile occupies a 160-byte direct-NVS record. The format is
// explicitly serialized; no compiler struct padding is stored.
#define EE_IR_CONFIG_START          EE_FM_PRESETS_END
#define EE_IR_KEY_COUNT             8
#define EE_IR_KEY_RECORD_SIZE       17   // protocol + address + command + extra + bits + raw
#define EE_IR_CONFIG_SIZE           160
#define EE_IR_CONFIG_END            (EE_IR_CONFIG_START + EE_IR_CONFIG_SIZE)

#define EEPROM_COMMIT_DELAY_MS       3000UL

static_assert(EE_PRESETS_NAME_START + EE_PRESETS_CNT * 17 == 2613,
              "DAB preset layout must remain unchanged");
static_assert(EE_FM_PRESETS_PI_START ==
                  EE_FM_PRESETS_FREQ_START + EE_PRESETS_CNT * 2,
              "FM frequency and PI arrays must be contiguous");
static_assert(EE_FM_PRESETS_NAME_START ==
                  EE_FM_PRESETS_PI_START + EE_PRESETS_CNT * 2,
              "FM PI and PS arrays must be contiguous");
static_assert(EE_FM_PRESETS_END ==
                  EE_FM_PRESETS_NAME_START +
                      EE_PRESETS_CNT * EE_FM_PRESET_NAME_LENGTH,
              "FM schema 4 end offset is inconsistent");
static_assert(6 + EE_IR_KEY_COUNT * EE_IR_KEY_RECORD_SIZE + 2 <= EE_IR_CONFIG_SIZE,
              "IR profile serialization exceeds EE_IR_CONFIG_SIZE");
static_assert(EE_IR_CONFIG_END <= EE_TOTAL_CNT,
              "IR profile exceeds the legacy compatibility image");

// ---------- UI strings + enums ----------
static const char* const unitString[] = {"dBμV", "dBf", "dBm"};
static const char* const Theme[] = {"Elegant", "GoldenDusk", "Vibrant", "Serenity", "Luminous", "Radiant", "Sunset"};

enum RADIO_TUNE_MODE {
  TUNE_MAN, TUNE_AUTO, TUNE_MEM
};

enum RADIO_MEM_POS_STATUS {
  MEM_DARK, MEM_NORMAL, MEM_EXIST
};

#endif
