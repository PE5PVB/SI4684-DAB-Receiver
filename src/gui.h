// GUI module: all TFT rendering for the radio.
// Renders directly to the 320x240 ILI9341 panel via TFT_eSPI, plus uses
// TFT_eSprite objects (defined in the .ino) for flicker-free partial updates.

#ifndef GUI_H
#define GUI_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TimeLib.h>
#include "si4684.h"
#include "TPA6130A2.h"
#include "language.h"
#include "constants.h"
#include "fm_station_list.h"
#include "graphics.h"
#include <cstring>

// Logical Settings items in display/navigation order.
enum SettingsMenuItem : uint8_t {
  MENU_LANGUAGE,
  MENU_BRIGHTNESS,
  MENU_THEME,
  MENU_AUTO_SLIDESHOW,
  MENU_SIGNAL_UNIT,
  MENU_SLEEP_TIMER,
  MENU_RADIO_MODE,
  MENU_FM_REGION,
  MENU_GPIO12,
  MENU_I2S_AUDIO,
  MENU_IR_REMOTE,
  MENU_FM_SEEK,
  MENU_FM_AF,
  MENU_ABOUT,
  MENU_ITEM_COUNT
};

extern bool autoslideshow;
extern bool change;
extern bool ChannelListView;
extern bool displayreset;
extern bool highz;
extern bool memorystore;
extern bool menu;
extern bool menuopen;
extern bool seek;
extern bool setvolume;
extern bool ShowServiceInformation;
extern bool SlideShowAvailableOld;
extern bool SlideShowView;
extern bool slsWaitingView;
extern bool slsWaitingOverlayVisible;
extern bool trysetservice;
extern bool dabServiceSelectionPending;
extern bool tuning;
extern byte audiomodeold;
extern byte ContrastSet;
extern byte CurrentTheme;
extern byte dabfreq;
extern byte dabfreqold;
extern byte eccold;
extern byte ficold;
extern byte language;
extern byte memorypos;
extern byte memoryposold;
extern byte memoryposstatus;
extern byte menuitem;
extern byte ptyold;
extern byte servicetypeold;
extern byte tot;
extern byte tunemode;
extern byte unit;
extern byte volume;
extern RadioMode radioMode;
extern RadioMode requestedRadioMode;
extern uint8_t fmRegion;
extern uint8_t requestedFmRegion;
extern uint8_t fmSeekSensitivity;
extern bool fmAfEnabled;
extern uint8_t gpio12Mode;
extern uint8_t requestedGpio12Mode;
extern I2sAudioMode i2sAudioMode;
extern I2sAudioMode requestedI2sAudioMode;
extern uint16_t fmfreq;
extern uint16_t fmPendingStationFrequency;
extern char fmPendingStationPs[9];
extern char _serviceName[17];
extern uint8_t _serviceNameCharset;
extern int ActiveColor;
extern int ActiveColorSmooth;
extern int BackgroundColor;
extern int BackgroundColor2;
extern int BackgroundColor3;
extern int BackgroundColor4;
extern int BarInsignificantColor;
extern int BarSignificantColor;
extern int BitrateAutoColor;
extern int BitrateAutoColorSmooth;
extern int GreyoutColor;
extern int InsignificantColor;
extern int InsignificantColorSmooth;
extern int menuoption;
extern int PrimaryColor;
extern int PrimaryColorSmooth;
extern volatile int rotary;
extern volatile int rotary2;
extern int rssi;
extern int rssiold;
extern int RTWidth;
extern int SecondaryColor;
extern int SecondaryColorSmooth;
extern int SignalLevelold;
extern int SignificantColor;
extern int SignificantColorSmooth;
extern int xPos;
extern int16_t SignalLevel;
extern int16_t SAvg;
extern int16_t SAvg2;
extern int8_t CNR;
extern int8_t CNRold;
extern char clockstringOld[6];
extern char dabfreqStringOld[12];
extern char datestringOld[11];
extern char EnsembleNameOld[65];
extern char EIDold[5];
extern char ITUold[4];
extern char PLold[9];
extern char PSold[65];
static constexpr size_t RT_TEXT_BUFFER_SIZE = 385;  // 128 EBU bytes * max 3 UTF-8 bytes + NUL
extern char RTold[RT_TEXT_BUFFER_SIZE];
extern char SIDold[5];
extern uint16_t BitrateOld;
extern unsigned long rssiTimer;
extern unsigned long rtticker;
extern unsigned long rttickerhold;
extern unsigned long VolumeTimer;

extern TFT_eSPI tft;
extern TFT_eSprite FullLineSprite;
extern TFT_eSprite OneBigLineSprite;
extern TFT_eSprite ModeSprite;
extern TFT_eSprite QualityBarSprite;
extern DAB radio;
extern bool DabGlobalListView;
extern bool fmScanCompleted;
extern bool dabScanCompleted;
extern TPA6130A2 Headphones;

void BuildChannelList(void);
void RedrawChannelListSelection(uint8_t oldIndex);
void BuildMenu(void);
void BuildDisplay(void);
void MenuUp(void);
void MenuDown(void);
void DoMenu(void);
void doTheme(void);
void Infoboxprint(const char* input);
void ShowServiceInfo(void);
void ShowFreq(void);
void ShowPTY(void);
void ShowRT(void);
void ShowSID(void);
void ShowEID(void);
void ShowPS(void);
void ShowEN(void);
void ShowProtectionlevel(void);
void ShowAudioMode(void);
void ShowECC(void);
void ShowMemoryPos(void);
void ShowVolume(void);
void ShowSignalLevel(void);
void ShowBitrate(void);
void ShowClock(void);
void ShowSlideShowIcon(void);
void ShowTuneMode(void);
bool TakeMainDisplayChanges(void);
void ShowRSSI(void);
void ShowOneLine(byte position, byte item, bool selected);

extern void tftPrint(int8_t offset, const char *text, int16_t x, int16_t y,
                     int color, int smoothcolor, uint8_t fontsize);
extern void tftReplaceFixed(int8_t offset, const char *textold, const char *text,
                            int16_t x, int16_t y, int color, int smoothcolor,
                            int backcolor, uint8_t fontsize);
extern void tftPrintFixed(int8_t offset, const char *text, int16_t x, int16_t y,
                          int color, int smoothcolor, uint8_t fontsize);
extern void loadFonts(bool option);
extern bool IsStationEmpty(void);
extern void MarkEepromDirty(void);
extern uint8_t ChannelListCount(void);
extern uint8_t ChannelListIndex(void);

#endif
