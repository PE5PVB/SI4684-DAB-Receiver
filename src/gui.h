#ifndef GUI_H
#define GUI_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TimeLib.h>
#include "si4684.h"
#include "TPA6130A2.h"
#include "language.h"
#include "constants.h"
#include "graphics.h"
#include <EEPROM.h>
#include <cstring>

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
extern bool trysetservice;
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
extern char _serviceName[17];
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
extern int rotary;
extern int rotary2;
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
extern String clockstringOld;
extern String dabfreqStringOld;
extern String datestringOld;
extern String EnsembleNameOld;
extern String EIDold;
extern String ITUold;
extern String PLold;
extern String PSold;
extern String RTold;
extern String SIDold;
extern String SignalLeveloldString;
extern uint16_t BitrateOld;
extern unsigned long rssiTimer;
extern unsigned long rtticker;
extern unsigned long rttickerhold;
extern unsigned long VolumeTimer;

extern TFT_eSPI tft;
extern TFT_eSprite FullLineSprite;
extern TFT_eSprite OneBigLineSprite;
extern TFT_eSprite LongSprite;
extern TFT_eSprite MediumSprite;
extern TFT_eSprite ModeSprite;
extern TFT_eSprite ShortSprite;
extern DAB radio;
extern TPA6130A2 Headphones;

void BuildChannelList(void);
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
void ShowRSSI(void);
void ShowOneLine(byte position, byte item, bool selected);

extern void ShowTuneMode(void);
extern void tftPrint(int8_t offset, const String & text, int16_t x, int16_t y, int color, int smoothcolor, uint8_t fontsize);
extern void tftReplace(int8_t offset, const String & textold, const String & text, int16_t x, int16_t y, int color, int smoothcolor, int backcolor, uint8_t fontsize);
extern void ShowMemoryPos(void);
extern void loadFonts(bool option);
extern bool IsStationEmpty(void);

#endif
