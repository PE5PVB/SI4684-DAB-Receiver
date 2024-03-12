#ifndef CONSTANTS_H
#define CONSTANTS_H

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

// EEPROM index defines
#define EE_PRESETS_CNT              99
#define EE_PRESETS_FREQUENCY        255
#define EE_CHECKBYTE_VALUE          2 // 0 ~ 255,add new entry, change for new value

#define EE_TOTAL_CNT                2614
#define EE_BYTE_CHECKBYTE           0
#define EE_BYTE_LANGUAGE            1
#define EE_BYTE_CONTRASTSET         2
#define EE_BYTE_DISPLAYFLIP         3
#define EE_BYTE_ROTARYMODE          4
#define EE_BYTE_TUNEMODE            5
#define EE_BYTE_WIFI                6
#define EE_BYTE_UNIT                7
#define EE_BYTE_DABFREQ             8
#define EE_BYTE_VOLUME              9
#define EE_BYTE_MEMORYPOS           10
#define EE_BYTE_THEME               11
#define EE_BYTE_AUTOSLIDESHOW       12
#define EE_BYTE_TOT                 13
#define EE_UINT32_SERVICEID         14
#define EE_CHAR17_SERVICENAME       22 // 17 bytes!
#define EE_PRESETS_FREQ_START       39
#define EE_PRESETS_SERVICEID_START  138
#define EE_PRESETS_NAME_START       930
// End of EEPROM index defines

static const char* const unitString[] = {"dBμV", "dBf", "dBm"};
static const char* const Theme[] = {"Elegant", "GoldenDusk", "Vibrant", "Serenity", "Luminous", "Radiant", "Sunset"};

enum RADIO_TUNE_MODE {
  TUNE_MAN, TUNE_AUTO, TUNE_MEM
};

enum RADIO_MEM_POS_STATUS {
  MEM_DARK, MEM_NORMAL, MEM_EXIST
};

#endif