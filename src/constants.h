// Project-wide constants: hardware pin assignments, EEPROM layout,
// UI line-spacing offsets, and the user-facing tune-mode / memory-status enums.
//
// Pin numbers map to the ESP32 dev-board GPIOs used by the schematic.
// EEPROM layout defines BOTH the size (EE_TOTAL_CNT) and the offsets of each
// stored value; the EE_CHECKBYTE_VALUE doubles as a schema version so older
// layouts get re-initialised via DefaultSettings() on first boot.

#ifndef CONSTANTS_H
#define CONSTANTS_H

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
#define CONTRASTPIN     2          // PWM pin driving the LCD backlight

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

// ---------- EEPROM layout ----------
#define EE_PRESETS_CNT              99   // number of memory presets supported
#define EE_PRESETS_FREQUENCY        255  // sentinel marking an empty preset slot
#define EE_CHECKBYTE_VALUE          3
#define EE_CHECKBYTE_DAB_ONLY       2    // migrates in place and preserves DAB presets

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
#define EE_UINT32_SERVICEID         14   // 4 bytes: last-used service ID
#define EE_CHAR17_SERVICENAME       22   // 17 bytes: last-used service label (16 chars + NUL)
#define EE_PRESETS_FREQ_START       39   // 1 byte * EE_PRESETS_CNT: per-preset channel index
#define EE_PRESETS_SERVICEID_START  138  // 8 bytes * EE_PRESETS_CNT: per-preset 64-bit service ID
#define EE_PRESETS_NAME_START       930  // 17 bytes * EE_PRESETS_CNT: per-preset label

// Bytes 18..21 were unused by schema 2. FM presets fill the former free tail:
// 99 one-byte frequency indices + 99 PI codes + 99 eight-char PS names.
#define EE_UINT16_FM_FREQUENCY      18
#define EE_FM_PRESETS_FREQ_START    2614
#define EE_FM_PRESETS_PI_START      2713
#define EE_FM_PRESETS_NAME_START    3109
#define EE_FM_PRESET_NAME_LENGTH    9
#define EEPROM_COMMIT_DELAY_MS       3000UL

// ---------- UI strings + enums ----------
static const char* const unitString[] = {"dBμV", "dBf", "dBm"};
static const char* const Theme[] = {"Elegant", "GoldenDusk", "Vibrant", "Serenity", "Luminous", "Radiant", "Sunset"};

// Manual = rotary changes channel; Auto = seek; Mem = step through presets.
enum RADIO_TUNE_MODE {
  TUNE_MAN, TUNE_AUTO, TUNE_MEM
};

// Visual state of the memory-position indicator.
enum RADIO_MEM_POS_STATUS {
  MEM_DARK, MEM_NORMAL, MEM_EXIST
};

#endif
