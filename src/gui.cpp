#include "epg_ui.h"
#include "epg_runtime.h"
// All TFT rendering. Each Show*() function updates one piece of the screen;
// they only redraw what has actually changed by comparing against a "*Old"
// shadow variable, so the loop can call them all every iteration cheaply.
// BuildDisplay() / BuildChannelList() / BuildMenu() do the full redraws
// when the active view switches.

#include "gui.h"
#include "ir_remote.h"
#include "dab_scheduler_policy.h"
#include "dab_station_list.h"
#include <esp_efuse.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include "esp_efuse_table.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

// Defined by the FM RDS decoder in si4684.cpp. PTY=0 is a real value, so
// this flag distinguishes "not received yet" from a valid PTY 0 (Unknown).
extern bool fmPtyValid;
extern uint8_t DabServiceLabelCharset(uint8_t serviceIndex);
extern uint8_t DabDynamicLabelCharsetValue(void);
extern uint8_t DabDynamicLabelLengthValue(void);
extern void DabDynamicLabelTextToBuffer(const char* input, char* output, size_t outputSize);

byte menuitem;                    // logical item index in Settings
static byte menuFirstItem = 0;      // first logical item shown in 9-row window
static constexpr byte MENU_VISIBLE_ROWS = 9;

// Apply the user-selected colour theme to the global PrimaryColor/etc. used
// by every draw routine. Use the linked RGB565 picker to author new themes.
void doTheme(void) {  // Use this to put your own colors in: http://www.barth-dev.de/online/rgb565-color-picker/
  switch (CurrentTheme) {
    case 0:  // Elegant
      PrimaryColor = 0x2e65;
      PrimaryColorSmooth = 0x09a1;
      SecondaryColor = 0x3d7d;
      SecondaryColorSmooth = 0x08e5;
      GreyoutColor = 0x5b0d;
      BackgroundColor = 0x0063;
      BackgroundColor2 = 0x016a;
      BackgroundColor3 = 0x0107;
      BackgroundColor4 = 0x00c6;
      ActiveColor = 0xFFFF;
      ActiveColorSmooth = 0x18E3;
      SignificantColor = 0xF800;
      SignificantColorSmooth = 0x2000;
      InsignificantColor = 0x07E0;
      InsignificantColorSmooth = 0x00C0;
      BarSignificantColor = 0xF800;
      BarInsignificantColor = 0x051f;
      break;

    case 1: // GoldenDusk
      PrimaryColor = 0x8ff1;
      PrimaryColorSmooth = 0x10c2;
      SecondaryColor = 0xFFE0;
      SecondaryColorSmooth = 0x2120;
      GreyoutColor = 0x5b0d;
      BackgroundColor = 0x016b;
      BackgroundColor2 = 0x016a;
      BackgroundColor3 = 0x0107;
      BackgroundColor4 = 0x00c6;
      ActiveColor = 0xFFFF;
      ActiveColorSmooth = 0x18E3;
      SignificantColor = 0xF800;
      SignificantColorSmooth = 0x2000;
      InsignificantColor = 0x07E0;
      InsignificantColorSmooth = 0x00C0;
      BarSignificantColor = 0xF800;
      BarInsignificantColor = 0x07E0;
      break;

    case 2: // Vibrant
      PrimaryColor = 0xF00A;
      PrimaryColorSmooth = 0x3800;
      SecondaryColor = 0xFFE0;
      SecondaryColorSmooth = 0x2120;
      GreyoutColor = 0x5b0d;
      BackgroundColor = 0x016b;
      BackgroundColor2 = 0x016a;
      BackgroundColor3 = 0x0107;
      BackgroundColor4 = 0x00c6;
      ActiveColor = 0x051F;
      ActiveColorSmooth = 0x0106;
      SignificantColor = 0xF3D5;
      SignificantColorSmooth = 0x3008;
      InsignificantColor = 0x07E0;
      InsignificantColorSmooth = 0x00C0;
      BarSignificantColor = 0xF3D5;
      BarInsignificantColor = 0x07E0;
      break;

    case 3: // Serenity
      PrimaryColor = 0xF3D5;
      PrimaryColorSmooth = 0x3008;
      SecondaryColor = 0x9C96;
      SecondaryColorSmooth = 0x41C8;
      GreyoutColor = 0x5b0d;
      BackgroundColor = 0x016b;
      BackgroundColor2 = 0x016a;
      BackgroundColor3 = 0x0107;
      BackgroundColor4 = 0x00c6;
      ActiveColor = 0x9B8D;
      ActiveColorSmooth = 0x5207;
      SignificantColor = 0x748E;
      SignificantColorSmooth = 0x3206;
      InsignificantColor = 0x9B90;
      InsignificantColorSmooth = 0x3946;
      BarSignificantColor = 0x748E;
      BarInsignificantColor = 0xF3D5;
      break;

    case 4: // Luminous
      PrimaryColor = 0x051F;
      PrimaryColorSmooth = 0x0106;
      SecondaryColor = 0xFA8D;
      SecondaryColorSmooth = 0x3083;
      GreyoutColor = 0x5b0d;
      BackgroundColor = 0x016b;
      BackgroundColor2 = 0x016a;
      BackgroundColor3 = 0x0107;
      BackgroundColor4 = 0x00c6;
      ActiveColor = 0xFC00;
      ActiveColorSmooth = 0x3165;
      SignificantColor = 0xFFE0;
      SignificantColorSmooth = 0x2120;
      InsignificantColor = 0xF8C3;
      InsignificantColorSmooth = 0x3800;
      BarSignificantColor = 0xFFE0;
      BarInsignificantColor = 0x867D;
      break;

    case 5: // Radiant
      PrimaryColor = 0x051F;
      PrimaryColorSmooth = 0x0106;
      SecondaryColor = 0xFFE0;
      SecondaryColorSmooth = 0x2120;
      GreyoutColor = 0x5b0d;
      BackgroundColor = 0x016b;
      BackgroundColor2 = 0x016a;
      BackgroundColor3 = 0x0107;
      BackgroundColor4 = 0x00c6;
      ActiveColor = 0xFC00;
      ActiveColorSmooth = 0x3165;
      SignificantColor = 0xFFE0;
      SignificantColorSmooth = 0x2120;
      InsignificantColor = 0xF8C3;
      InsignificantColorSmooth = 0x3800;
      BarSignificantColor = 0xFFE0;
      BarInsignificantColor = 0x867D;
      break;

    case 6: // Sunset
      PrimaryColor = 0x051F;
      PrimaryColorSmooth = 0x0106;
      SecondaryColor = 0xFFE0;
      SecondaryColorSmooth = 0x2120;
      GreyoutColor = 0x5b0d;
      BackgroundColor = 0x016b;
      BackgroundColor2 = 0x016a;
      BackgroundColor3 = 0x0107;
      BackgroundColor4 = 0x00c6;
      ActiveColor = 0xED20;
      ActiveColorSmooth = 0x3940;
      SignificantColor = 0xF800;
      SignificantColorSmooth = 0x2000;
      InsignificantColor = 0x07E0;
      InsignificantColorSmooth = 0x00C0;
      BarSignificantColor = 0xF800;
      BarInsignificantColor = 0x07E0;
      break;
  }
}

// Full-screen technical diagnostics. GPIO12 runtime state and its boot-time
// MTDI strap are shown separately because the pin is reused as INTB or IR after
// the reset sample.
static uint32_t systemInfoRefreshTimer = 0;
static constexpr uint32_t SYSTEM_INFO_REFRESH_MS = 1000UL;
static constexpr int16_t SYSTEM_INFO_VALUE_X = 166;
static constexpr int16_t SYSTEM_INFO_VALUE_W = 146;
static constexpr int16_t SYSTEM_INFO_ROW_H = 20;

// Coalesce main-widget writes into one final overlay repaint. No clipping
// geometry or dependency on the signal-meter timer; unchanged widgets do not
// request a repaint. Overlay rendering itself does not mark this flag.
static bool mainDisplayChanged = false;

bool TakeMainDisplayChanges(void) {
  const bool changed = mainDisplayChanged;
  mainDisplayChanged = false;
  return changed;
}

template <typename Draw>
static void DrawMainAndMarkChanged(const Draw& draw) {
  draw();
  mainDisplayChanged = true;
}

static void PushMainSprite(TFT_eSprite& sprite, int16_t x, int16_t y,
                           int16_t width, int16_t height) {
  DrawMainAndMarkChanged([&]() { sprite.pushSprite(x, y, 0, 0, width, height); });
}

static void PrintMainText(int8_t offset, const char* text, int16_t x, int16_t y,
                           int color, int smoothcolor, uint8_t fontsize) {
  DrawMainAndMarkChanged([&]() { tftPrint(offset, text, x, y, color, smoothcolor, fontsize); });
}

static void ReplaceMainText(int8_t offset, const char* previous, const char* text,
                             int16_t x, int16_t y, int color, int smoothcolor,
                             int background, uint8_t fontsize) {
  DrawMainAndMarkChanged([&]() {
    tftReplaceFixed(offset, previous, text, x, y, color, smoothcolor, background, fontsize);
  });
}

// Background[] has a fixed 320-pixel row stride. Restore one scanline at a
// time so the partial copy retains the correct source stride and gradients.
static void RestoreMainBackgroundRegion(int16_t x, int16_t y,
                                        int16_t width, int16_t height) {
  DrawMainAndMarkChanged([&]() {
    for (int16_t row = 0; row < height; ++row) {
      const uint32_t offset =
          static_cast<uint32_t>(y + row) * 320U + static_cast<uint32_t>(x);
      tft.pushImage(x, y + row, width, 1, Background + offset);
    }
  });
}

static void RestoreSystemInfoValueRow(uint8_t row) {
  const int16_t y = static_cast<int16_t>(36 + row * 20);
  for (int16_t line = 0; line < SYSTEM_INFO_ROW_H; ++line) {
    const uint32_t offset =
        static_cast<uint32_t>(y + line) * 320U +
        static_cast<uint32_t>(SYSTEM_INFO_VALUE_X);
    tft.pushImage(SYSTEM_INFO_VALUE_X, y + line,
                  SYSTEM_INFO_VALUE_W, 1,
                  serviceinfobackground + offset);
  }
}

static uint8_t SystemResetReasonIndex(void) {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return 0;
    case ESP_RST_EXT:       return 1;
    case ESP_RST_SW:        return 2;
    case ESP_RST_PANIC:     return 3;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return 4;
    case ESP_RST_DEEPSLEEP: return 5;
    case ESP_RST_BROWNOUT:  return 6;
    case ESP_RST_SDIO:      return 7;
    default:                return 8;
  }
}

// GPIO_STRAP_REG retains the strapping-pin levels sampled at reset. On the
// classic ESP32, MTDI/GPIO12 is bit 5. This is intentionally NOT a
// digitalRead(GPIO12): the live pin can later be INTB, IR, or an unused input.
static bool Gpio12BootMtdiHigh(void) {
  return (REG_READ(GPIO_STRAP_REG) & (1U << 5)) != 0U;
}

// Show the role GPIO12 is actually serving now, not only the value saved in
// Settings. AUTO can resolve to INTB, detection, or an unused input when the
// radio runs in polling mode. The final H/L is the live electrical pin level.
static void FormatGpio12RuntimeStatus(char* output, size_t outputSize) {
  const uint8_t configuredMode = sanitizeGpio12Mode(gpio12Mode);
  const RadioControlMode controlMode = radio.controlMode();
  const char level =
      digitalRead(SI4684_INTB_PIN) == HIGH ? 'H' : 'L';

  if (configuredMode == GPIO12_IR) {
    snprintf(output, outputSize, "IR / %c / %s", level,
             IrRemoteHasProfile()
                 ? irLearnedShortText[language]
                 : irEmptyShortText[language]);
    return;
  }

  const char* activeRole = "INPUT";
  if (controlMode == RADIO_CTRL_INTB)
    activeRole = "INTB";
  else if (controlMode == RADIO_CTRL_DETECT)
    activeRole = "DETECT";

  if (configuredMode == GPIO12_AUTO) {
    snprintf(output, outputSize, "AUTO>%s / %c", activeRole, level);
  } else if (configuredMode == GPIO12_INTB &&
             controlMode != RADIO_CTRL_INTB) {
    // A runtime safety fallback can leave the fixed INTB setting configured
    // while radio control has already fallen back to polling.
    snprintf(output, outputSize, "INTB>%s / %c", activeRole, level);
  } else {
    snprintf(output, outputSize, "%s / %c", activeRole, level);
  }
}

// VDD_SDIO is selected either by the boot-latched MTDI level or permanently
// by eFuse. XPD_SDIO_FORCE explicitly makes the ROM ignore GPIO12 for this
// purpose; XPD_SDIO_TIEH then selects 3.3 V/1.8 V when the regulator is
// enabled. The MTDI sample is still displayed even when eFuse overrides it.
static void FormatVddSdioStatus(char* output, size_t outputSize) {
  const bool mtdiHigh = Gpio12BootMtdiHigh();
  const bool force =
      esp_efuse_read_field_bit(ESP_EFUSE_SDIO_FORCE);

  if (!force) {
    snprintf(output, outputSize, "%s MTDI:%c/STRAP",
             mtdiHigh ? "1.8V" : "3.3V",
             mtdiHigh ? 'H' : 'L');
    return;
  }

  const bool regulatorEnabled =
      esp_efuse_read_field_bit(ESP_EFUSE_XPD_SDIO_REG);
  const bool tieHigh =
      esp_efuse_read_field_bit(ESP_EFUSE_SDIO_TIEH);

  if (regulatorEnabled) {
    snprintf(output, outputSize, "%s MTDI:%c/EFUSE",
             tieHigh ? "3.3V" : "1.8V",
             mtdiHigh ? 'H' : 'L');
  } else {
    // Internal VDD_SDIO regulator is forced off. The actual voltage, if any,
    // must come from the board externally and cannot be inferred in software.
    snprintf(output, outputSize, "EXT? MTDI:%c/EFUSE",
             mtdiHigh ? 'H' : 'L');
  }
}

void ShowServiceInfo(void) {
  const bool fullRedraw =
      !ShowServiceInformation || setvolume || displayreset;
  setvolume = false;

  char value[80];

  if (fullRedraw) {
    tft.pushImage(0, 0, 320, 240, serviceinfobackground);
    tftPrintFixed(0, myLanguage[language][27], 155, 4,
                  ActiveColor, ActiveColorSmooth, 28);

    // Logical order: tuner/radio and GPIO12 first. In FM mode the middle
    // rows expose live RDS/quality data; in DAB mode they retain the hardware
    // diagnostics. Heap rows remain common to both modes.
    for (uint8_t row = 0; row < 9; ++row) {
      const char* label = myLanguage[language][28 + row];
      if (radio.isFm()) {
        if (row == 3U) label = fmAfInfoText[language];
        else if (row == 4U) label = fmTrafficInfoText[language];
        else if (row == 5U) label = fmCtInfoText[language];
        else if (row == 6U) label = fmQualityInfoText[language];
      }
      tftPrintFixed(-1, label,
                    8, 36 + row * 20,
                    ActiveColor, ActiveColorSmooth, 16);
    }

    snprintf(value, sizeof(value), "%s / %s / %s",
             radio.getChipID(), radio.getFirmwareVersion(),
             radio.isFm() ? "FM" : "DAB");
    tftPrintFixed(-1, value, 166, 36,
                  PrimaryColor, PrimaryColorSmooth, 16);

    if (!radio.isFm()) {
      FormatVddSdioStatus(value, sizeof(value));
      tftPrintFixed(-1, value, 166, 96,
                    PrimaryColor, PrimaryColorSmooth, 16);

      snprintf(value, sizeof(value), "rev%u / %uM / %s",
               static_cast<unsigned>(ESP.getChipRevision()),
               static_cast<unsigned>(ESP.getCpuFreqMHz()),
               VERSION);
      tftPrintFixed(-1, value, 166, 116,
                    PrimaryColor, PrimaryColorSmooth, 16);

      tftPrintFixed(-1,
                    systemResetReasonText[language][SystemResetReasonIndex()],
                    166, 156, PrimaryColor, PrimaryColorSmooth, 16);
    }

    systemInfoRefreshTimer = 0;
  }

  const uint32_t now = millis();
  if (!fullRedraw &&
      static_cast<uint32_t>(now - systemInfoRefreshTimer) <
          SYSTEM_INFO_REFRESH_MS) {
    return;
  }
  systemInfoRefreshTimer = now;

  // Radio control and GPIO12 are live states, so refresh both once per second.
  RestoreSystemInfoValueRow(1);
  tftPrintFixed(-1, radio.controlModeName(), 166, 56,
                PrimaryColor, PrimaryColorSmooth, 16);

  RestoreSystemInfoValueRow(2);
  FormatGpio12RuntimeStatus(value, sizeof(value));
  tftPrintFixed(-1, value, 166, 76,
                PrimaryColor, PrimaryColorSmooth, 16);

  if (radio.isFm()) {
    RestoreSystemInfoValueRow(3);
    if (radio.fmAf.count == 0U) {
      snprintf(value, sizeof(value), "-");
    } else {
      const uint16_t first = radio.fmAf.frequency10kHz[0];
      if (radio.fmAf.count == 1U) {
        snprintf(value, sizeof(value), "%u: %u.%u",
                 static_cast<unsigned>(radio.fmAf.count),
                 static_cast<unsigned>(first / 100U),
                 static_cast<unsigned>((first % 100U) / 10U));
      } else {
        const uint16_t second = radio.fmAf.frequency10kHz[1];
        snprintf(value, sizeof(value), "%u: %u.%u %u.%u%s",
                 static_cast<unsigned>(radio.fmAf.count),
                 static_cast<unsigned>(first / 100U),
                 static_cast<unsigned>((first % 100U) / 10U),
                 static_cast<unsigned>(second / 100U),
                 static_cast<unsigned>((second % 100U) / 10U),
                 radio.fmAf.count > 2U ? "+" : "");
      }
    }
    tftPrintFixed(-1, value, 166, 96,
                  PrimaryColor, PrimaryColorSmooth, 16);

    RestoreSystemInfoValueRow(4);
    snprintf(value, sizeof(value), "TP:%u TA:%u %.10s",
             radio.fmTp ? 1U : 0U, radio.fmTa ? 1U : 0U,
             fmPtyValid ? fm_features::ptyName(radio.fmPty, radio.isRbds()) : "-");
    tftPrintFixed(-1, value, 166, 116,
                  PrimaryColor, PrimaryColorSmooth, 16);

    RestoreSystemInfoValueRow(5);
    if (radio.fmCtValid) {
      const int offset = static_cast<int>(radio.fmCt.localOffsetHalfHours);
      const unsigned magnitude = static_cast<unsigned>(offset < 0 ? -offset : offset);
      snprintf(value, sizeof(value), "%02u-%02u %02u:%02u %c%u.%u",
               static_cast<unsigned>(radio.fmCt.day),
               static_cast<unsigned>(radio.fmCt.month),
               static_cast<unsigned>(radio.fmCt.hour),
               static_cast<unsigned>(radio.fmCt.minute),
               offset < 0 ? '-' : '+', magnitude / 2U,
               (magnitude % 2U) * 5U);
    } else {
      snprintf(value, sizeof(value), "-");
    }
    tftPrintFixed(-1, value, 166, 136,
                  PrimaryColor, PrimaryColorSmooth, 16);

    RestoreSystemInfoValueRow(6);
    snprintf(value, sizeof(value), "%d/%d MP%u BL%u",
             static_cast<int>(radio.fmRssi), static_cast<int>(radio.fmSnr),
             static_cast<unsigned>(radio.fmMultipath),
             static_cast<unsigned>(radio.fmStereoBlend));
    tftPrintFixed(-1, value, 166, 156,
                  PrimaryColor, PrimaryColorSmooth, 16);
  } else {
    RestoreSystemInfoValueRow(5);
    const uint64_t uptimeSeconds =
        static_cast<uint64_t>(esp_timer_get_time()) / 1000000ULL;
    const uint64_t totalHours = uptimeSeconds / 3600ULL;
    const uint64_t minutes = (uptimeSeconds / 60ULL) % 60ULL;
    const uint64_t seconds = uptimeSeconds % 60ULL;
    snprintf(value, sizeof(value), "%02llu:%02llu:%02llu",
             static_cast<unsigned long long>(totalHours),
             static_cast<unsigned long long>(minutes),
             static_cast<unsigned long long>(seconds));
    tftPrintFixed(-1, value, 166, 136,
                  PrimaryColor, PrimaryColorSmooth, 16);
  }

  RestoreSystemInfoValueRow(7);
  snprintf(value, sizeof(value), "%lu / %lu kB",
           static_cast<unsigned long>(ESP.getFreeHeap() / 1024UL),
           static_cast<unsigned long>(ESP.getMinFreeHeap() / 1024UL));
  tftPrintFixed(-1, value, 166, 176,
                PrimaryColor, PrimaryColorSmooth, 16);

  RestoreSystemInfoValueRow(8);
  {
    // Use the exact same heap domain as the monitor's MEMCHK line.
    // Mixing MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT here with ESP.getFreeHeap()
    // above made the displayed largest block/fragmentation incomparable.
    const size_t freeHeap = ESP.getFreeHeap();
    const size_t largestBlock = ESP.getMaxAllocHeap();
    uint32_t fragmentation = 0;
    if (freeHeap != 0U && largestBlock <= freeHeap) {
      fragmentation = 100U -
          static_cast<uint32_t>(
              (static_cast<uint64_t>(largestBlock) * 100U) / freeHeap);
    }
    snprintf(value, sizeof(value), "%lu kB / %lu%%",
             static_cast<unsigned long>(largestBlock / 1024U),
             static_cast<unsigned long>(fragmentation));
  }
  tftPrintFixed(-1, value, 166, 196,
                PrimaryColor, PrimaryColorSmooth, 16);
}

static uint8_t ChannelListPageStart(uint8_t index) {
  if (radio.isFm() || DabGlobalListView) {
    const uint8_t count = ChannelListCount();
    if (count <= 9U || index <= 4U) return 0U;
    const uint8_t centered = static_cast<uint8_t>(index - 4U);
    const uint8_t lastStart = static_cast<uint8_t>(count - 9U);
    return centered < lastStart ? centered : lastStart;
  }
  return index < 9U ? 0U
                    : static_cast<uint8_t>(9U + ((index - 9U) / 8U) * 8U);
}

static void ShowGlobalListPosition(void) {
  if (!(radio.isFm() || DabGlobalListView) || ChannelListCount() == 0U) return;
  char value[8];
  snprintf(value, sizeof(value), "%u/%u",
           static_cast<unsigned>(ChannelListIndex()) + 1U,
           static_cast<unsigned>(ChannelListCount()));
  FullLineSprite.pushImage(-264, -5, 320, 240, servicelistbackground);
  FullLineSprite.setTextDatum(TC_DATUM);
  FullLineSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
  FullLineSprite.drawString(value, 26, 5);
  FullLineSprite.pushSprite(264, 5, 0, 0, 52, 20);
}

// Only repaint list rows when the window moves; keep the title and background
// in place to avoid a full-screen flash on every centered scrolling step.
void RedrawChannelListSelection(uint8_t oldIndex) {
  const uint8_t count = ChannelListCount();
  if (count == 0U) return;
  const uint8_t index = ChannelListIndex();
  const uint8_t oldStart = ChannelListPageStart(oldIndex);
  const uint8_t start = ChannelListPageStart(index);
  if (oldStart != start) {
    if (!(radio.isFm() || DabGlobalListView)) {
      BuildChannelList();
      return;
    }
    for (uint8_t row = 0; row < 9U && start + row < count; ++row) {
      const uint8_t item = static_cast<uint8_t>(start + row);
      ShowOneLine(20U * row, item, item == index);
    }
  } else {
    ShowOneLine(20U * (oldIndex - start), oldIndex, false);
    ShowOneLine(20U * (index - start), index, true);
  }
  ShowGlobalListPosition();
}

// Render the scrollable list of services/stations. Used as
// the alternative to "main display" when the user opens the channel list.
void BuildChannelList(void) {
  setvolume = false;
  tft.pushImage (0, 0, 320, 240, servicelistbackground);
  const char* title = radio.isFm()
      ? fmStationListText[language]
      : (DabGlobalListView ? dabStationListText[language]
                           : currentMuxListText[language]);
  tftPrintFixed(0, title, 155, 4, ActiveColor, ActiveColorSmooth, 28);

  const uint8_t count = ChannelListCount();
  const uint8_t index = ChannelListIndex();
  const uint8_t y = ChannelListPageStart(index);

  if (count == 0U) {
    const bool globalScanList = radio.isFm() || DabGlobalListView;
    if (globalScanList) {
      const bool completed = radio.isFm() ? fmScanCompleted : dabScanCompleted;
      tftPrintFixed(0, completed ? noStationsFoundText[language]
                                 : scanNotRunText[language],
                    155, 100, SecondaryColor, SecondaryColorSmooth, 16);
      tftPrintFixed(0, pressOkScanText[language],
                    155, 130, ActiveColor, ActiveColorSmooth, 16);
    } else {
      tftPrintFixed(0, noDabServiceText[language],
                    155, 112, SecondaryColor, SecondaryColorSmooth, 16);
    }
    return;
  }

  if (radio.isFm() || DabGlobalListView) {
    ShowGlobalListPosition();
  } else if (count > 9U) {
    const uint8_t page = y == 0U ? 1U : static_cast<uint8_t>(2U + (y - 9U) / 8U);
    const uint8_t pages = static_cast<uint8_t>(1U + (count - 9U + 7U) / 8U);

    char pageText[8];
    snprintf(pageText, sizeof(pageText), "%u/%u",
             static_cast<unsigned>(page), static_cast<unsigned>(pages));
    tftPrintFixed(0, pageText, 290, 10,
                  SecondaryColor, SecondaryColorSmooth, 16);
  }

  for (byte i = y; i < count; i++) {
    ShowOneLine(20 * (i - y), i, index == i);
    if (i - y == 8) i = 254;
  }
}

// Draw one row of a list/menu. `position` is the vertical line slot
// (ITEM1..ITEM10) and `item` is the data row to read from. `selected`
// switches between highlighted vs normal styling.
void ShowOneLine(byte position, byte item, bool selected) {
  char value[80];

  if (ChannelListView) {
    FullLineSprite.pushImage (-8, -position - 35, 320, 240, servicelistbackground);
    if (selected) FullLineSprite.pushImage(0, 0, 304, 20, selector);

    if (radio.isFm()) {
      const FmStationRecord& station = FmStations[item];
      const uint16_t stationFrequency = station.frequency10kHz;
      FullLineSprite.setTextDatum(TL_DATUM);
      FullLineSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
      snprintf(value, sizeof(value), "%u.%u",
               static_cast<unsigned>(stationFrequency / 100),
               static_cast<unsigned>((stationFrequency % 100) / 10));
      FullLineSprite.drawString(value, 8, 3);

      FullLineSprite.setTextDatum(TC_DATUM);
      snprintf(value, sizeof(value), "%04X",
               static_cast<unsigned>(station.pi));
      FullLineSprite.drawString(value, 92, 3);

      FullLineSprite.setTextDatum(TL_DATUM);
      FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
      if (station.ps[0] != '\0')
        radio.ASCIIToBuffer(station.ps, 0, value, sizeof(value));
      else
        snprintf(value, sizeof(value), "%u.%u MHz",
                 static_cast<unsigned>(stationFrequency / 100U),
                 static_cast<unsigned>((stationFrequency % 100U) / 10U));
      FullLineSprite.drawString(value, 122, 3);

      FullLineSprite.setTextDatum(TR_DATUM);
      FullLineSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
      snprintf(value, sizeof(value), "%d/%d",
               static_cast<int>(station.rssi), static_cast<int>(station.snr));
      FullLineSprite.drawString(value, 300, 3);
      FullLineSprite.pushSprite(8, 35 + position);
      return;
    }

    if (DabGlobalListView) {
      const DabStationRecord& station = DabStations[item];
      FullLineSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
      FullLineSprite.setTextDatum(TL_DATUM);
      FullLineSprite.drawString(radio.getChannel(station.channelIndex), 8, 3);
      FullLineSprite.setTextDatum(TC_DATUM);
      snprintf(value, sizeof(value), "%04X",
               static_cast<unsigned>(station.serviceId & 0xFFFFU));
      FullLineSprite.drawString(value, 58, 3);
      FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
      FullLineSprite.setTextDatum(TL_DATUM);
      radio.ASCIIToBuffer(station.label, station.charset, value, sizeof(value));
      FullLineSprite.drawString(value, 86, 3);
      FullLineSprite.setTextDatum(TR_DATUM);
      FullLineSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
      FullLineSprite.drawString(ServiceTypeText[station.serviceType], 300, 3);
      FullLineSprite.pushSprite(8, 35 + position);
      return;
    }

    FullLineSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
    FullLineSprite.setTextDatum(TL_DATUM);
    snprintf(value, sizeof(value), "%u",
             static_cast<unsigned>(radio.service[item].CompID & 0xFF));
    FullLineSprite.drawString(value, 12, 3);

    FullLineSprite.setTextDatum(TC_DATUM);
    snprintf(value, sizeof(value), "%04X",
             static_cast<unsigned>(radio.service[item].ServiceID & 0xFFFF));
    FullLineSprite.drawString(value, 56, 3);

    FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
    FullLineSprite.setTextDatum(TL_DATUM);
    radio.ASCIIToBuffer(radio.service[item].Label,
                        DabServiceLabelCharset(item), value, sizeof(value));
    FullLineSprite.drawString(value, 84, 3);

    FullLineSprite.setTextDatum(TC_DATUM);
    FullLineSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
    FullLineSprite.drawString(ServiceTypeText[radio.service[item].ServiceType], 282, 3);
    FullLineSprite.pushSprite(8, 35 + position);
  } else if (menu) {
    FullLineSprite.pushImage (-8, -position - 32, 320, 240, configurationbackground);
    if (selected) FullLineSprite.pushImage(0, 0, 304, 20, selector);

    FullLineSprite.setTextDatum(TL_DATUM);
    FullLineSprite.setTextColor(ActiveColor, ActiveColorSmooth, false);
    switch (item) {
      case MENU_LANGUAGE:
        FullLineSprite.drawString(myLanguage[language][12], 6, 3);
        FullLineSprite.setTextDatum(TR_DATUM);
        FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        FullLineSprite.drawString(myLanguage[language][0], 300, 3);
        break;

      case MENU_BRIGHTNESS:
        FullLineSprite.drawString(myLanguage[language][13], 6, 3);
        FullLineSprite.setTextDatum(TR_DATUM);
        FullLineSprite.drawString("%", 300, 3);
        FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(ContrastSet));
        FullLineSprite.drawString(value, 270, 3);
        break;

      case MENU_THEME:
        FullLineSprite.drawString(myLanguage[language][14], 6, 3);
        FullLineSprite.setTextDatum(TR_DATUM);
        FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        FullLineSprite.drawString(Theme[CurrentTheme], 300, 3);
        break;

      case MENU_AUTO_SLIDESHOW:
        FullLineSprite.drawString(myLanguage[language][15], 6, 3);
        FullLineSprite.setTextDatum(TR_DATUM);
        FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        FullLineSprite.drawString((autoslideshow ? myLanguage[language][23] : myLanguage[language][24]), 300, 3);
        break;

      case MENU_SIGNAL_UNIT:
        FullLineSprite.drawString(myLanguage[language][16], 6, 3);
        FullLineSprite.setTextDatum(TR_DATUM);
        FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        FullLineSprite.drawString(unitString[unit], 300, 3);
        break;

      case MENU_SLEEP_TIMER:
        FullLineSprite.drawString(myLanguage[language][25], 6, 3);
        FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        FullLineSprite.setTextDatum(TR_DATUM);
        if (tot != 0) {
          snprintf(value, sizeof(value), "%u", static_cast<unsigned>(tot));
          FullLineSprite.drawString(value, 270, 3);
        }
        if (tot != 0) FullLineSprite.setTextColor(ActiveColor, ActiveColorSmooth, false);
        FullLineSprite.drawString((tot != 0 ? myLanguage[language][26] : myLanguage[language][24]), 300, 3);
        break;

      case MENU_RADIO_MODE:
        FullLineSprite.drawString(myLanguage[language][17], 6, 3);
        FullLineSprite.setTextDatum(TR_DATUM);
        FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        FullLineSprite.drawString(radioModeValueText[requestedRadioMode], 300, 3);
        break;

      case MENU_FM_REGION:
        FullLineSprite.drawString(fmRegionMenuText[language], 6, 3);
        FullLineSprite.setTextDatum(TR_DATUM);
        FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        FullLineSprite.drawString(
            fmRegionValueText[language][sanitizeFmRegion(requestedFmRegion)],
            300, 3);
        break;

      case MENU_GPIO12:
        FullLineSprite.drawString("GPIO12", 6, 3);
        FullLineSprite.setTextDatum(TR_DATUM);
        FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        FullLineSprite.drawString(
            Gpio12ModeText[sanitizeGpio12Mode(requestedGpio12Mode)], 300, 3);
        break;

      case MENU_I2S_AUDIO:
        FullLineSprite.drawString(i2sAudioMenuText[language], 6, 3);
        FullLineSprite.setTextDatum(TR_DATUM);
        FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        FullLineSprite.drawString(i2sAudioModeText(requestedI2sAudioMode), 300, 3);
        break;

      case MENU_IR_REMOTE:
        FullLineSprite.drawString(irRemoteText[language], 6, 3);
        FullLineSprite.setTextDatum(TR_DATUM);
        FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        if (IrRemoteHasProfile()) {
          snprintf(value, sizeof(value), "%s >", irLearnedShortText[language]);
        } else {
          snprintf(value, sizeof(value), ">");
        }
        FullLineSprite.drawString(value, 300, 3);
        break;

      case MENU_FM_SEEK:
        FullLineSprite.drawString(fmSeekMenuText[language], 6, 3);
        FullLineSprite.setTextDatum(TR_DATUM);
        FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        FullLineSprite.drawString(
            fmSeekValueText[language][fmSeekSensitivity], 300, 3);
        break;

      case MENU_FM_AF:
        FullLineSprite.drawString(fmAfMenuText[language], 6, 3);
        FullLineSprite.setTextDatum(TR_DATUM);
        FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
        FullLineSprite.drawString(
            fmAfEnabled ? myLanguage[language][23] : myLanguage[language][24],
            300, 3);
        break;

      case MENU_ABOUT:
        FullLineSprite.drawString(myLanguage[language][81], 6, 3);
        FullLineSprite.setTextDatum(TR_DATUM);
        break;
    }
    FullLineSprite.pushSprite(8, 32 + position);
  }
}

static void DrawMenuRows(void) {
  for (byte row = 0; row < MENU_VISIBLE_ROWS; row++) {
    const byte logicalItem = menuFirstItem + row;
    if (logicalItem >= MENU_ITEM_COUNT) break;
    ShowOneLine(ITEM1 + row * ITEM_GAP, logicalItem, logicalItem == menuitem);
  }
}

static void RedrawMenuSelection(byte oldItem) {
  // If the visible window did not move, only two 20-pixel rows need updating:
  // remove the selector from the old row and draw it on the new row. This
  // avoids the distracting full-menu text repaint on every encoder detent.
  if (oldItem >= menuFirstItem && oldItem < menuFirstItem + MENU_VISIBLE_ROWS)
    ShowOneLine(ITEM1 + (oldItem - menuFirstItem) * ITEM_GAP, oldItem, false);

  if (menuitem >= menuFirstItem && menuitem < menuFirstItem + MENU_VISIBLE_ROWS)
    ShowOneLine(ITEM1 + (menuitem - menuFirstItem) * ITEM_GAP, menuitem, true);

  menuoption = ITEM1 + (menuitem - menuFirstItem) * ITEM_GAP;
}

// Full redraw of the settings menu (entered by long-pressing MODE).
void BuildMenu(void) {
  EpgSuspend();
  // Settings has more logical entries than the display's nine-row window.
  // Scroll the existing 9-row window instead of shrinking the proven UI.
  menuopen = false;
  IrRemoteUiAbort();
  SlideShowView = false;
  slsWaitingView = false;
  slsWaitingOverlayVisible = false;
  ShowServiceInformation = false;
  ChannelListView = false;
  DabGlobalListView = false;

  if (menuitem >= MENU_ITEM_COUNT) menuitem = MENU_LANGUAGE;
  if (menuitem < menuFirstItem) menuFirstItem = menuitem;
  if (menuitem >= menuFirstItem + MENU_VISIBLE_ROWS)
    menuFirstItem = menuitem - (MENU_VISIBLE_ROWS - 1);
  if (menuFirstItem > MENU_ITEM_COUNT - MENU_VISIBLE_ROWS)
    menuFirstItem = MENU_ITEM_COUNT - MENU_VISIBLE_ROWS;

  menuoption = ITEM1 + (menuitem - menuFirstItem) * ITEM_GAP;

  tft.pushImage (0, 0, 320, 240, configurationbackground);
  tftPrintFixed(0, myLanguage[language][20], 155, 4,
                PrimaryColor, PrimaryColorSmooth, 28);
  tftPrintFixed(0, myLanguage[language][19], 155, 222,
                SecondaryColor, SecondaryColorSmooth, 16);
  DrawMenuRows();
}

// Full redraw of the main radio screen (frequency, PS, RT, signal bars,
// memory slot, clock). Called when switching back from another view or
// after a theme/display-orientation change.
void BuildDisplay(void) {
  EpgResume();
  if (EpgVisible()) { EpgDraw(true); return; }
  SlideShowView = false;
  slsWaitingView = false;
  slsWaitingOverlayVisible = false;
  ShowServiceInformation = false;
  ChannelListView = false;
  displayreset = true;
  setvolume = false;
  rotary = 0;
  rotary2 = 0;

  tft.pushImage (0, 0, 320, 240, Background);
  tftPrint(1, "PR:", 84, 65, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, radio.isFm() ? fmPiText[language] : "EID", 10, 105, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, radio.isFm() ? fmPtyText[language] : "SID", 10, 120, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(1, "MHz", 310, 55, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "SIG:", 123, 109, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, unitString[unit], 183, 109, ActiveColor, ActiveColorSmooth, 16);
  char snrLabel[24];
  if (radio.isFm())
    snprintf(snrLabel, sizeof(snrLabel), "%s:", fmSnrText[language]);
  else
    snprintf(snrLabel, sizeof(snrLabel), "MER:");
  tftPrintFixed(-1, snrLabel, 237, 109, ActiveColor, ActiveColorSmooth, 16);
  tftPrint(1, "dB", 309, 109, ActiveColor, ActiveColorSmooth, 16);
  // The compact meter uses a single-letter M. A two-letter "MP" reaches the
  // bar border and its clipped P looks like a stray white pixel.
  tftPrint(-1, radio.isFm() ? "M" : "Q", 122, 90,
           ActiveColor, ActiveColorSmooth, 16);
  tftPrint(-1, "S", 122, 127, SecondaryColor, SecondaryColorSmooth, 16);
  if (!radio.isFm()) tftPrint(1, "ECC", 110, 90, ActiveColor, ActiveColorSmooth, 16);

  for (byte segments = 0; segments < 13; segments++) tft.fillRect(134 + (segments * 14), 135, 2, 3, (segments < 8 ? BarInsignificantColor : BarSignificantColor));
  tft.drawLine(134, 138, 302, 138, ActiveColor);
  tftPrint(-1, "1   3   5   7   9  +10 +30", 134, 140, ActiveColor, ActiveColorSmooth, 16);
  tft.drawRect(134, 90, 141, 12, GreyoutColor);

  ShowFreq();
  ShowTuneMode();
  ShowMemoryPos();
}

// Menu navigation: handle the rotary-up event while the settings menu is
// open. Either steps a value of the current row or moves to the next row.
void MenuUp(void) {
  if (!menuopen) {
    const byte oldItem = menuitem;
    const byte oldFirstItem = menuFirstItem;

    menuitem++;
    if (menuitem >= MENU_ITEM_COUNT) {
      menuitem = MENU_LANGUAGE;
      menuFirstItem = 0;
    } else if (menuitem >= menuFirstItem + MENU_VISIBLE_ROWS) {
      menuFirstItem = menuitem - (MENU_VISIBLE_ROWS - 1);
    }

    if (menuFirstItem != oldFirstItem) {
      // Only the scroll-window boundary needs all menu rows repainted. The
      // header/footer/background remain untouched.
      menuoption = ITEM1 + (menuitem - menuFirstItem) * ITEM_GAP;
      DrawMenuRows();
    } else {
      RedrawMenuSelection(oldItem);
    }
    return;
  }

  if (menuitem == MENU_IR_REMOTE) {
    IrRemoteUiRotate(+1);
    return;
  }
  if (menuitem == MENU_ABOUT) return;  // About is read-only.

  OneBigLineSprite.pushImage(-11, -88, 292, 170, popupbackground);
  OneBigLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
  OneBigLineSprite.setTextDatum(TC_DATUM);
  char value[8];

  switch (menuitem) {
    case MENU_LANGUAGE:
      language ++;
      if (language == (sizeof (myLanguage) / sizeof (myLanguage[0]))) language = 0;
      OneBigLineSprite.drawString(myLanguage[language][0], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_BRIGHTNESS:
      ContrastSet ++;
      if (ContrastSet > 100) ContrastSet = 1;
      OneBigLineSprite.setTextDatum(TL_DATUM);
      OneBigLineSprite.setTextColor(ActiveColor, ActiveColorSmooth, false);
      OneBigLineSprite.drawString("%", 155, 2);
      OneBigLineSprite.setTextDatum(TR_DATUM);
      OneBigLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
      snprintf(value, sizeof(value), "%u", static_cast<unsigned>(ContrastSet));
      OneBigLineSprite.drawString(value, 135, 2);
      analogWrite(CONTRASTPIN, ContrastSet * 2 + 27);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_THEME:
      CurrentTheme ++;
      if (CurrentTheme > sizeof(Theme) / sizeof(Theme[0]) - 1) CurrentTheme = 0;
      doTheme();
      tft.pushImage (13, 30, 292, 170, popupbackground);
      Infoboxprint(myLanguage[language][14]);
      OneBigLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
      OneBigLineSprite.drawString(Theme[CurrentTheme], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_AUTO_SLIDESHOW:
      autoslideshow = !autoslideshow;
      OneBigLineSprite.drawString(
          autoslideshow ? myLanguage[language][23] : myLanguage[language][24],
          135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_SIGNAL_UNIT:
      unit ++;
      if (unit > sizeof(unitString) / sizeof(unitString[0]) - 1) unit = 0;
      OneBigLineSprite.drawString(unitString[unit], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_SLEEP_TIMER:
      switch (tot) {
        case 0: tot = 15; break;
        case 15: tot = 30; break;
        case 30: tot = 60; break;
        case 60: tot = 90; break;
        default: tot = 0; break;
      }
      if (tot != 0) {
        OneBigLineSprite.setTextDatum(TR_DATUM);
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(tot));
        OneBigLineSprite.drawString(value, 135, 2);
        OneBigLineSprite.setTextColor(ActiveColor, ActiveColorSmooth, false);
        OneBigLineSprite.setTextDatum(TL_DATUM);
        OneBigLineSprite.drawString(myLanguage[language][26], 155, 2);
      } else {
        OneBigLineSprite.drawString(myLanguage[language][24], 135, 2);
      }
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_RADIO_MODE:
      requestedRadioMode = requestedRadioMode == RADIO_MODE_DAB
                               ? RADIO_MODE_FM : RADIO_MODE_DAB;
      OneBigLineSprite.drawString(radioModeValueText[requestedRadioMode], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_FM_REGION:
      requestedFmRegion = static_cast<uint8_t>(
          (sanitizeFmRegion(requestedFmRegion) + 1U) % FM_REGION_COUNT);
      OneBigLineSprite.drawString(
          fmRegionValueText[language][requestedFmRegion], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_GPIO12:
      requestedGpio12Mode = static_cast<uint8_t>(
          (sanitizeGpio12Mode(requestedGpio12Mode) + 1U) % 3U);
      OneBigLineSprite.drawString(
          Gpio12ModeText[requestedGpio12Mode], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_I2S_AUDIO:
      requestedI2sAudioMode = cycleI2sAudioMode(requestedI2sAudioMode, true);
      OneBigLineSprite.drawString(i2sAudioModeText(requestedI2sAudioMode), 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_FM_SEEK:
      fmSeekSensitivity = static_cast<uint8_t>((fmSeekSensitivity + 1U) % 3U);
      OneBigLineSprite.drawString(
          fmSeekValueText[language][fmSeekSensitivity], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;
    case MENU_FM_AF:
      fmAfEnabled = !fmAfEnabled;
      OneBigLineSprite.drawString(
          fmAfEnabled ? myLanguage[language][23] : myLanguage[language][24],
          135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;
  }
}

// Menu navigation: rotary-down counterpart of MenuUp().
void MenuDown(void) {
  if (!menuopen) {
    const byte oldItem = menuitem;
    const byte oldFirstItem = menuFirstItem;

    if (menuitem == MENU_LANGUAGE) {
      menuitem = MENU_ITEM_COUNT - 1;
      menuFirstItem = MENU_ITEM_COUNT - MENU_VISIBLE_ROWS;
    } else {
      menuitem--;
      if (menuitem < menuFirstItem) menuFirstItem = menuitem;
    }

    if (menuFirstItem != oldFirstItem) {
      menuoption = ITEM1 + (menuitem - menuFirstItem) * ITEM_GAP;
      DrawMenuRows();
    } else {
      RedrawMenuSelection(oldItem);
    }
    return;
  }

  if (menuitem == MENU_IR_REMOTE) {
    IrRemoteUiRotate(-1);
    return;
  }
  if (menuitem == MENU_ABOUT) return;  // About is read-only.

  OneBigLineSprite.pushImage(-11, -88, 292, 170, popupbackground);
  OneBigLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
  OneBigLineSprite.setTextDatum(TC_DATUM);
  char value[8];

  switch (menuitem) {
    case MENU_LANGUAGE:
      language --;
      if (language > (sizeof (myLanguage) / sizeof (myLanguage[0])))
        language = (sizeof (myLanguage) / sizeof (myLanguage[0])) - 1;
      OneBigLineSprite.drawString(myLanguage[language][0], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_BRIGHTNESS:
      ContrastSet --;
      if (ContrastSet < 1) ContrastSet = 100;
      OneBigLineSprite.setTextDatum(TL_DATUM);
      OneBigLineSprite.setTextColor(ActiveColor, ActiveColorSmooth, false);
      OneBigLineSprite.drawString("%", 155, 2);
      OneBigLineSprite.setTextDatum(TR_DATUM);
      OneBigLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
      snprintf(value, sizeof(value), "%u", static_cast<unsigned>(ContrastSet));
      OneBigLineSprite.drawString(value, 135, 2);
      analogWrite(CONTRASTPIN, ContrastSet * 2 + 27);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_THEME:
      CurrentTheme --;
      if (CurrentTheme > sizeof(Theme) / sizeof(Theme[0]) - 1)
        CurrentTheme = sizeof(Theme) / sizeof(Theme[0]) - 1;
      doTheme();
      tft.pushImage (13, 30, 292, 170, popupbackground);
      Infoboxprint(myLanguage[language][14]);
      OneBigLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
      OneBigLineSprite.drawString(Theme[CurrentTheme], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_AUTO_SLIDESHOW:
      autoslideshow = !autoslideshow;
      OneBigLineSprite.drawString(
          autoslideshow ? myLanguage[language][23] : myLanguage[language][24],
          135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_SIGNAL_UNIT:
      unit --;
      if (unit > sizeof(unitString) / sizeof(unitString[0]) - 1)
        unit = sizeof(unitString) / sizeof(unitString[0]) - 1;
      OneBigLineSprite.drawString(unitString[unit], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_SLEEP_TIMER:
      switch (tot) {
        case 15: tot = 0; break;
        case 30: tot = 15; break;
        case 60: tot = 30; break;
        case 90: tot = 60; break;
        default: tot = 90; break;
      }
      if (tot != 0) {
        OneBigLineSprite.setTextDatum(TR_DATUM);
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(tot));
        OneBigLineSprite.drawString(value, 135, 2);
        OneBigLineSprite.setTextColor(ActiveColor, ActiveColorSmooth, false);
        OneBigLineSprite.setTextDatum(TL_DATUM);
        OneBigLineSprite.drawString(myLanguage[language][26], 155, 2);
      } else {
        OneBigLineSprite.drawString(myLanguage[language][24], 135, 2);
      }
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_RADIO_MODE:
      requestedRadioMode = requestedRadioMode == RADIO_MODE_DAB
                               ? RADIO_MODE_FM : RADIO_MODE_DAB;
      OneBigLineSprite.drawString(radioModeValueText[requestedRadioMode], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_FM_REGION:
      requestedFmRegion = sanitizeFmRegion(requestedFmRegion) == 0
                              ? FM_REGION_COUNT - 1
                              : sanitizeFmRegion(requestedFmRegion) - 1;
      OneBigLineSprite.drawString(
          fmRegionValueText[language][requestedFmRegion], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_GPIO12:
      requestedGpio12Mode = sanitizeGpio12Mode(requestedGpio12Mode) == GPIO12_AUTO
                                ? GPIO12_IR
                                : requestedGpio12Mode - 1;
      OneBigLineSprite.drawString(
          Gpio12ModeText[requestedGpio12Mode], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_I2S_AUDIO:
      requestedI2sAudioMode = cycleI2sAudioMode(requestedI2sAudioMode, false);
      OneBigLineSprite.drawString(i2sAudioModeText(requestedI2sAudioMode), 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_FM_SEEK:
      fmSeekSensitivity = fmSeekSensitivity == 0U
                              ? 2U : fmSeekSensitivity - 1U;
      OneBigLineSprite.drawString(
          fmSeekValueText[language][fmSeekSensitivity], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;
    case MENU_FM_AF:
      fmAfEnabled = !fmAfEnabled;
      OneBigLineSprite.drawString(
          fmAfEnabled ? myLanguage[language][23] : myLanguage[language][24],
          135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;
  }
}

// Menu confirm: rotary-button click while in the menu. Either enters a sub-
// menu, applies the current change, or commits the value to NVS.
void DoMenu(void) {
  if (menuopen) {
    if (menuitem == MENU_IR_REMOTE) {
      if (IrRemoteUiPress()) {
        menuopen = false;
        BuildMenu();
      }
      return;
    }
    menuopen = false;
    BuildMenu();
    return;
  }

  if (menuitem == MENU_IR_REMOTE) {
    menuopen = true;
    IrRemoteUiOpen();
    return;
  }

  tft.pushImage (13, 30, 292, 170, popupbackground);
  OneBigLineSprite.pushImage(-11, -88, 292, 170, popupbackground);
  OneBigLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
  OneBigLineSprite.setTextDatum(TC_DATUM);
  menuopen = true;
  char value[80];

  switch (menuitem) {
    case MENU_LANGUAGE:
      Infoboxprint(myLanguage[language][12]);
      OneBigLineSprite.drawString(myLanguage[language][0], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_BRIGHTNESS:
      Infoboxprint(myLanguage[language][13]);
      OneBigLineSprite.setTextDatum(TL_DATUM);
      OneBigLineSprite.setTextColor(ActiveColor, ActiveColorSmooth, false);
      OneBigLineSprite.drawString("%", 155, 2);
      OneBigLineSprite.setTextDatum(TR_DATUM);
      OneBigLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
      snprintf(value, sizeof(value), "%u", static_cast<unsigned>(ContrastSet));
      OneBigLineSprite.drawString(value, 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_THEME:
      Infoboxprint(myLanguage[language][14]);
      OneBigLineSprite.drawString(Theme[CurrentTheme], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_AUTO_SLIDESHOW:
      Infoboxprint(myLanguage[language][15]);
      OneBigLineSprite.drawString(
          autoslideshow ? myLanguage[language][23] : myLanguage[language][24],
          135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_SIGNAL_UNIT:
      Infoboxprint(myLanguage[language][16]);
      OneBigLineSprite.drawString(unitString[unit], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_SLEEP_TIMER:
      Infoboxprint(myLanguage[language][25]);
      if (tot != 0) {
        OneBigLineSprite.setTextDatum(TR_DATUM);
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(tot));
        OneBigLineSprite.drawString(value, 135, 2);
        OneBigLineSprite.setTextColor(ActiveColor, ActiveColorSmooth, false);
        OneBigLineSprite.setTextDatum(TL_DATUM);
        OneBigLineSprite.drawString(myLanguage[language][26], 155, 2);
      } else {
        OneBigLineSprite.drawString(myLanguage[language][24], 135, 2);
      }
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_RADIO_MODE:
      Infoboxprint(myLanguage[language][17]);
      OneBigLineSprite.drawString(radioModeValueText[requestedRadioMode], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_FM_REGION:
      Infoboxprint(fmRegionMenuText[language]);
      OneBigLineSprite.drawString(
          fmRegionValueText[language][sanitizeFmRegion(requestedFmRegion)],
          135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_GPIO12:
      Infoboxprint("GPIO12");
      OneBigLineSprite.drawString(
          Gpio12ModeText[sanitizeGpio12Mode(requestedGpio12Mode)], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_I2S_AUDIO:
      Infoboxprint(i2sAudioMenuText[language]);
      OneBigLineSprite.drawString(i2sAudioModeText(requestedI2sAudioMode), 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_FM_SEEK:
      Infoboxprint(fmSeekMenuText[language]);
      OneBigLineSprite.drawString(
          fmSeekValueText[language][fmSeekSensitivity], 135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_FM_AF:
      Infoboxprint(fmAfMenuText[language]);
      OneBigLineSprite.drawString(
          fmAfEnabled ? myLanguage[language][23] : myLanguage[language][24],
          135, 2);
      OneBigLineSprite.pushSprite(24, 118);
      break;

    case MENU_ABOUT:
      tftPrintFixed(0, myLanguage[language][79], 155, 40,
                    ActiveColor, ActiveColorSmooth, 28);
      tftPrintFixed(0, "PE5PVB, bales", 155, 72,
                    PrimaryColor, PrimaryColorSmooth, 28);
      tftPrintFixed(0, myLanguage[language][80], 155, 104,
                    ActiveColor, ActiveColorSmooth, 28);
      tftPrintFixed(0, "mcelliotg", 155, 132,
                    PrimaryColor, PrimaryColorSmooth, 28);
      tftPrintFixed(0, "github.com/PE5PVB/SI4684-DAB-Receiver", 155, 161,
                    SecondaryColor, SecondaryColorSmooth, 16);
      snprintf(value, sizeof(value), "GPIO12: %s / %s",
               Gpio12ModeText[sanitizeGpio12Mode(gpio12Mode)],
               radio.controlModeName());
      tftPrintFixed(0, value, 155, 182,
                    SecondaryColor, SecondaryColorSmooth, 16);
      break;
  }
}

// Render a centered text line in the small "info" box at the top of the
// screen (used for transient messages like "Saved" / "No signal").
void Infoboxprint(const char* input) {
  if (!input) return;
  // Use a fixed stack copy so two-line messages need no heap allocation.
  char text[256];
  snprintf(text, sizeof(text), "%s", input);
  char* newline = strchr(text, '\n');
  if (newline) {
    *newline = '\0';
    tftPrintFixed(0, text, 155, 48,
                  ActiveColor, ActiveColorSmooth, 28);
    tftPrintFixed(0, newline + 1, 155, 78,
                  ActiveColor, ActiveColorSmooth, 28);
  } else {
    tftPrintFixed(0, text, 155, 78,
                  ActiveColor, ActiveColorSmooth, 28);
  }
}

// The Show*() routines below are individually called from ProcessDAB() every
// loop. Each one diffs the current radio.* / live state against a `*Old` shadow
// variable and only pushes its sprite/region to the TFT when something changed.

void ShowFreq(void) {
  if (EpgVisible()) return;
  // Frequency belongs exclusively to the normal radio screen. AUTO seek or a
  // tune already running before System info was opened may still update the
  // radio state in the background, but must never paint through this overlay.
  if (ShowServiceInformation) return;

  char value[12];

  if (radio.isFm() || radioMode == RADIO_MODE_FM) {
    snprintf(value, sizeof(value), "%u.%u",
             static_cast<unsigned>(fmfreq / 100),
             static_cast<unsigned>((fmfreq % 100) / 10));
    ReplaceMainText(0, fmModeText[language], fmModeText[language],
                    145, 45, PrimaryColor, PrimaryColorSmooth,
                    BackgroundColor2, 28);
    ReplaceMainText(-1, dabfreqStringOld, value,
                    184, 43, SecondaryColor, SecondaryColorSmooth,
                    BackgroundColor2, 52);
    snprintf(dabfreqStringOld, sizeof(dabfreqStringOld), "%s", value);
    return;
  }

  ReplaceMainText(0, radio.getChannel(dabfreqold), radio.getChannel(dabfreq),
                  145, 45, PrimaryColor, PrimaryColorSmooth,
                  BackgroundColor2, 28);

  const uint32_t frequency = radio.getFreq(dabfreq);
  const uint32_t fraction = frequency % 1000U;
  // Preserve the established display formatting: add one leading
  // zero only when the kHz remainder was below 100.
  if (fraction < 100U) {
    snprintf(value, sizeof(value), "%lu.0%lu",
             static_cast<unsigned long>(frequency / 1000U),
             static_cast<unsigned long>(fraction));
  } else {
    snprintf(value, sizeof(value), "%lu.%lu",
             static_cast<unsigned long>(frequency / 1000U),
             static_cast<unsigned long>(fraction));
  }

  ReplaceMainText(-1, dabfreqStringOld, value,
                  184, 43, SecondaryColor, SecondaryColorSmooth,
                  BackgroundColor2, 52);
  snprintf(dabfreqStringOld, sizeof(dabfreqStringOld), "%s", value);
  dabfreqold = dabfreq;
}

void ShowPTY(void) {
  const uint8_t ptyValue = radio.isFm() ? radio.fmPty : radio.pty;
  const bool ptyVisible =
      radio.isFm()
          ? (!tuning && fmPtyValid && ptyValue <= 31)
          : (radio.ServiceStart && ptyValue > 0 && ptyValue <= 29);
  const uint8_t displayPty = ptyVisible ? ptyValue : 0xFF;
  static uint8_t fmIndicatorsOld = 0xFF;
  const uint8_t fmIndicators = radio.isFm()
      ? static_cast<uint8_t>((radio.fmAf.count != 0U ? 1U : 0U) |
                             (radio.fmTp ? 2U : 0U) |
                             (radio.fmTa ? 4U : 0U))
      : 0U;

  char fmValue[64] = "";
  const char* value = "";
  if (radio.isFm()) {
    char indicators[12];
    snprintf(indicators, sizeof(indicators), "%s%s%s",
             radio.fmAf.count != 0U ? " AF" : "",
             radio.fmTp ? " TP" : "", radio.fmTa ? " TA" : "");
    const int maxPtyChars = 18 - static_cast<int>(strlen(indicators));
    snprintf(fmValue, sizeof(fmValue), "%.*s%s",
             maxPtyChars > 0 ? maxPtyChars : 0,
             ptyVisible ? fm_features::ptyName(ptyValue, radio.isRbds()) : "",
             indicators);
    value = fmValue;
  } else if (ptyVisible) {
    value = myLanguage[language][37 + ptyValue];
  }

  if (displayPty != ptyold || fmIndicators != fmIndicatorsOld || displayreset) {
    FullLineSprite.pushImage(-8, -162, 320, 240, Background);
    FullLineSprite.setTextDatum(TC_DATUM);
    FullLineSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
    FullLineSprite.drawString(value, 75, 0);
    FullLineSprite.pushSprite(8, 162, 0, 0, 150, 17);
    ptyold = displayPty;
    fmIndicatorsOld = fmIndicators;
  }
}

void ShowRT(void) {
  // System info is a pure diagnostic page. Never paint normal-screen
  // radiotext/DLS over it.
  if (ShowServiceInformation) return;

  // The 20-pixel radiotext interior is y=219..238; row 239 is the black edge
  // of every background. Always sample and push that same region so clearing
  // an empty label cannot leave a stale row behind.
  constexpr int16_t rtStripY = 219;
  // FONT16's basic ascent is 13 pixels, while accented capitals such as Zcaron
  // and Dcaron extend three pixels above it. Local y=3 keeps those accents at
  // row 0 and keeps j/p/g descenders on the final valid row (19).
  constexpr int16_t rtTextY = 3;
  if (ShowServiceInformation)  {
    FullLineSprite.pushImage(-6, -rtStripY, 320, 240, serviceinfobackground);
  } else if (ChannelListView) {
    FullLineSprite.pushImage(-6, -rtStripY, 320, 240, servicelistbackground);
  } else {
    FullLineSprite.pushImage(-6, -rtStripY, 320, 240, Background);
  }

  // Radiotext is queried from the GUI on every fast main-loop pass, while the
  // broadcast text itself changes only occasionally. Keep the decoded UTF-8
  // text in the fixed RTold[] buffer and rebuild it only when the actual source
  // bytes (or DLS charset/length / FM tuning state) change. No radio, scheduler,
  // slideshow or TFT update timing is changed here.
  static char rtSourceSnapshot[sizeof(radio.ServiceData)] = {0};
  static bool rtSourceValid = false;
  static bool rtFmSnapshot = false;
  static bool rtTuningSnapshot = false;
  static uint8_t rtDabCharsetSnapshot = 0xFF;
  static uint8_t rtDabLengthSnapshot = 0xFF;

  const bool fm = radio.isFm();
  const uint8_t dabCharset = fm ? 0U : DabDynamicLabelCharsetValue();
  const uint8_t dabLength = fm ? 0U : DabDynamicLabelLengthValue();
  const bool sourceChanged =
      !rtSourceValid ||
      fm != rtFmSnapshot ||
      (fm && tuning != rtTuningSnapshot) ||
      (!fm && (dabCharset != rtDabCharsetSnapshot ||
               dabLength != rtDabLengthSnapshot)) ||
      memcmp(rtSourceSnapshot, radio.ServiceData,
             sizeof(rtSourceSnapshot)) != 0;

  if (sourceChanged) {
    char value[RT_TEXT_BUFFER_SIZE];
    if (fm) {
      radio.ASCIIToBuffer(radio.ServiceData, 0, value, sizeof(value));
      if (tuning) value[0] = '\0';
    } else {
      DabDynamicLabelTextToBuffer(radio.ServiceData, value, sizeof(value));
    }

    // A new string must start from x=0 before its first frame is drawn.
    if (strcmp(RTold, value) != 0) xPos = 0;
    memcpy(RTold, value, strlen(value) + 1U);

    memcpy(rtSourceSnapshot, radio.ServiceData, sizeof(rtSourceSnapshot));
    rtSourceValid = true;
    rtFmSnapshot = fm;
    rtTuningSnapshot = tuning;
    rtDabCharsetSnapshot = dabCharset;
    rtDabLengthSnapshot = dabLength;
  }

  const char* value = RTold;
  FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
  if (value[0] != '\0') {
    // Measure with the exact sprite/font that renders the text. tft.textWidth()
    // could use whichever font the preceding main-screen field left loaded.
    RTWidth = FullLineSprite.textWidth(value);
    if (RTWidth < 300) {
      xPos = 0;
      FullLineSprite.setTextDatum(TC_DATUM);
      FullLineSprite.drawString(value, 154, rtTextY);
      FullLineSprite.pushSprite(6, rtStripY);
    } else if (millis() - rtticker >= 50) {
      --xPos;
      rttickerhold = millis();

      if (xPos < -RTWidth - 50) xPos = 0;
      FullLineSprite.setTextDatum(TL_DATUM);
      FullLineSprite.drawString(value, xPos, rtTextY);
      FullLineSprite.drawString(value, xPos + RTWidth + 50, rtTextY);
      FullLineSprite.pushSprite(6, rtStripY);
      rtticker = millis();
    }
  } else {
    FullLineSprite.pushSprite(6, rtStripY);
  }
}

void ShowSID(void) {
  char value[5] = "";

  if (radio.isFm()) {
    if (!tuning && fmPtyValid && radio.fmPty > 0 && radio.fmPty <= 31)
      snprintf(value, sizeof(value), "%u", static_cast<unsigned>(radio.fmPty));
  } else {
    // Service start is asynchronous. During the short interval before
    // START_DIGITAL_SERVICE completes, hide SID in the UI without modifying
    // the driver's SID buffer. Clearing radio.SID here used to destroy the
    // value prepared by setService(), so it never reappeared after ServiceStart
    // became true.
    if (radio.ServiceStart)
      snprintf(value, sizeof(value), "%s", radio.SID);
  }

  if (strcmp(value, SIDold) != 0 || displayreset) {
    // FONT16 hex glyphs occupy 14 rows. Leave a row above/below and room for
    // four wide letters; the EID crop ends at y=119, before this SID region.
    FullLineSprite.pushImage(-38, -119, 320, 240, Background);
    FullLineSprite.setTextDatum(TL_DATUM);
    FullLineSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
    FullLineSprite.drawString(value, 1, 1);
    FullLineSprite.pushSprite(38, 119, 0, 0, 40, 17);
    snprintf(SIDold, sizeof(SIDold), "%s", value);
  }
}

void ShowEID(void) {
  char value[5] = "";

  if (radio.isFm()) {
    if (!tuning && radio.fmPi)
      snprintf(value, sizeof(value), "%04X", static_cast<unsigned>(radio.fmPi));
  } else {
    if (tuning) radio.EID[0] = '\0';
    snprintf(value, sizeof(value), "%s", radio.EID);
  }

  if (strcmp(value, EIDold) != 0 || displayreset) {
    // Keep the EID repaint strictly above SID, including its background.
    FullLineSprite.pushImage(-38, -104, 320, 240, Background);
    FullLineSprite.setTextDatum(TL_DATUM);
    FullLineSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
    FullLineSprite.drawString(value, 1, 1);
    FullLineSprite.pushSprite(38, 104, 0, 0, 40, 15);
    snprintf(EIDold, sizeof(EIDold), "%s", value);
  }
}

static void trimFixed(char* text) {
  if (!text || !text[0]) return;

  char* first = text;
  while (*first && static_cast<uint8_t>(*first) <= 0x20U) ++first;

  if (first != text) memmove(text, first, strlen(first) + 1U);

  size_t len = strlen(text);
  while (len > 0 && static_cast<uint8_t>(text[len - 1]) <= 0x20U)
    text[--len] = '\0';
}

void ShowPS(void) {
  char value[65] = "";

  if (radio.isFm()) {
    if (!tuning) radio.ASCIIToBuffer(radio.fmPs, 0, value, sizeof(value));
    trimFixed(value);

    const bool livePsAvailable = value[0] != '\0';
    if (livePsAvailable) {
      // The decoder-confirmed live PS is authoritative from this point on.
      fmPendingStationPs[0] = '\0';
      fmPendingStationFrequency = 0U;
    } else if (fm_features::shouldShowStoredFmPs(
                   tuning, seek, fmfreq, fmPendingStationFrequency,
                   fmPendingStationPs[0] != '\0', livePsAvailable)) {
      radio.ASCIIToBuffer(fmPendingStationPs, 0, value, sizeof(value));
      trimFixed(value);
    }

    // A scan-list selection may supply its saved PS during acquisition. For
    // every other tune the placeholder remains until live RDS is confirmed.
    if (value[0] == '\0') snprintf(value, sizeof(value), "--------");

    if (strcmp(value, PSold) != 0 || displayreset) {
      OneBigLineSprite.pushImage(-44, -185, 320, 240, Background);
      OneBigLineSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
      OneBigLineSprite.setTextDatum(TC_DATUM);
      OneBigLineSprite.drawString(value, 130, 4);
      OneBigLineSprite.pushSprite(44, 185);
      snprintf(PSold, sizeof(PSold), "%s", value);
    }
    return;
  }

  const bool showPendingTarget = fm_features::shouldShowPendingDabTarget(
      dabServiceSelectionPending, trysetservice,
      radio.isDabServiceStartPending(), _serviceName[0] != '\0',
      radio.isDabServiceLabelCurrent());
  if (showPendingTarget) {
    radio.ASCIIToBuffer(_serviceName, _serviceNameCharset,
                        value, sizeof(value));
  } else if (radio.ServiceStart) {
    radio.ASCIIToBuffer(radio.PStext, radio.ServiceLabelCharset,
                        value, sizeof(value));

    // CurrentServiceInfo can arrive shortly after START_DIGITAL_SERVICE.
    // Until then keep the already selected service-list label visible.
    if (value[0] == '\0' &&
        radio.numberofservices > 0 &&
        radio.ServiceIndex < radio.numberofservices) {
      radio.ASCIIToBuffer(
          radio.service[radio.ServiceIndex].Label,
          DabServiceLabelCharset(radio.ServiceIndex),
          value, sizeof(value));
    }
  } else if (tuning || seek) {
    value[0] = '\0';
  } else if (radio.signallock) {
    if (radio.numberofservices == 0) {
      snprintf(value, sizeof(value), "%s", myLanguage[language][73]);  // Waiting for list
    } else {
      // During asynchronous service selection/restoration _serviceName equals
      // the target service-list label. Otherwise no service has been selected.
      uint8_t pendingCharset = radio.ServiceLabelCharset;
      if (radio.ServiceIndex < radio.numberofservices)
        pendingCharset = DabServiceLabelCharset(radio.ServiceIndex);

      char pendingName[65] = "";
      char indexedName[65] = "";
      radio.ASCIIToBuffer(_serviceName, pendingCharset,
                          pendingName, sizeof(pendingName));
      if (radio.ServiceIndex < radio.numberofservices) {
        radio.ASCIIToBuffer(radio.service[radio.ServiceIndex].Label,
                            pendingCharset, indexedName, sizeof(indexedName));
      }

      if (pendingName[0] != '\0' && strcmp(pendingName, indexedName) == 0)
        snprintf(value, sizeof(value), "%s", pendingName);
      else
        snprintf(value, sizeof(value), "%s", myLanguage[language][74]);  // Select service
    }
  } else if (trysetservice) {
    // The stored preset name belongs to the previously selected service. Do not paint it
    // until the current multiplex/service list confirms that service ID; this
    // avoids a brief stale/incorrect station-name flash after restart.
    value[0] = '\0';
  } else {
    value[0] = '\0';
  }

  if (strcmp(value, PSold) != 0 || displayreset) {
    if (tunemode != TUNE_MEM || value[0] != '\0') {
      OneBigLineSprite.pushImage(-44, -185, 320, 240, Background);
      OneBigLineSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
      OneBigLineSprite.setTextDatum(TC_DATUM);
      OneBigLineSprite.drawString(value, 130, 4);
      OneBigLineSprite.pushSprite(44, 185);
    }
    snprintf(PSold, sizeof(PSold), "%s", value);
  }
}

void ShowEN(void) {
  char value[65] = "";

  const auto drawEnsembleField = [](const char* text) {
    // Match the adjacent PTY/program-info baseline exactly. Restore only the
    // interior so neither the upper highlight nor the PS border below can be
    // touched by subsequent ensemble-label updates.
    constexpr int16_t fieldX = 167;
    constexpr int16_t fieldY = 162;
    constexpr int16_t fieldW = 145;
    constexpr int16_t fieldH = 17;
    constexpr int16_t textCenterX = 71;
    FullLineSprite.pushImage(-fieldX, -fieldY, 320, 240, Background);
    FullLineSprite.setTextDatum(TC_DATUM);
    FullLineSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
    FullLineSprite.drawString(text != nullptr ? text : "", textCenterX, 0);
    FullLineSprite.pushSprite(fieldX, fieldY, 0, 0, fieldW, fieldH);
  };

  if (radio.isFm()) {
    const char* source = tuning || radio.isTunePending() ? myLanguage[language][75]
                         : (!radio.signallock ? myLanguage[language][76]
                            : (radio.fmPi ? (radio.isRbds() ? fmRbdsText[language]
                                                           : fmRdsText[language])
                                          : (radio.fmPilot ? fmStereoText[language]
                                                           : fmMonoText[language])));
    snprintf(value, sizeof(value), "%s", source);

    if (strcmp(value, EnsembleNameOld) != 0 || displayreset) {
      drawEnsembleField(value);
      snprintf(EnsembleNameOld, sizeof(EnsembleNameOld), "%s", value);
    }
    return;
  }

  if (tuning) {
    snprintf(value, sizeof(value), "%s", myLanguage[language][75]);
  } else if (!radio.signallock) {
    snprintf(value, sizeof(value), "%s", myLanguage[language][76]);
  } else {
    radio.ASCIIToBuffer(radio.EnsembleLabel, radio.EnsembleLabelCharset,
                        value, sizeof(value));
  }

  if (strcmp(value, EnsembleNameOld) != 0 || displayreset) {
    drawEnsembleField(value);
    snprintf(EnsembleNameOld, sizeof(EnsembleNameOld), "%s", value);
  }
  // Preserve the display state semantics: tuning/no-lock never leaves a
  // placeholder string in the actual ensemble-label receive buffer.
  if (!radio.signallock || tuning) radio.EnsembleLabel[0] = '\0';
}

void ShowProtectionlevel(void) {
  char value[9] = "";

  if (radio.isFm()) {
    snprintf(value, sizeof(value), "ST %u%%",
             static_cast<unsigned>(radio.fmStereoBlend));
  } else {
    if (!radio.ServiceStart) radio.protectionlevel = 0;
    const char* protection =
        radio.protectionlevel < 14 ? ProtectionText[radio.protectionlevel] : "";
    snprintf(value, sizeof(value), "%s", protection);
  }

  if (strcmp(value, PLold) != 0 || displayreset) {
    FullLineSprite.pushImage(-9, -90, 320, 240, Background);
    FullLineSprite.setTextDatum(TC_DATUM);
    FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
    FullLineSprite.drawString(value, 30, 0);
    PushMainSprite(FullLineSprite, 9, 90, 70, 16);
    snprintf(PLold, sizeof(PLold), "%s", value);
  }
}

static void ShowEpgIcon(void) {
  static bool visibleOld = false;
  static epg::IndicatorState stateOld = epg::IndicatorState::Absent;
  const bool visible = !radio.isFm();
  const epg::IndicatorState state = epg::indicatorState();
  if (!displayreset && visible == visibleOld && state == stateOld) return;

  // Use the same loaded FONT16 as the codec. E/P/G glyphs fit all their
  // antialiasing rows in this 14-pixel crop, above the PR row at y=65.
  RestoreMainBackgroundRegion(58, 50, 58, 14);
  if (visible) {
    // Grey/cyan match slideshow artwork; green means this selection is cached.
    const uint16_t color = state == epg::IndicatorState::Ready ? 0x07E0U :
                           state == epg::IndicatorState::Present ? 0x06DFU : 0x3A4AU;
    FullLineSprite.pushImage(-58, -50, 320, 240, Background);
    FullLineSprite.setTextDatum(TC_DATUM);
    FullLineSprite.setTextColor(color, BackgroundColor2, false);
    FullLineSprite.drawString("EPG", 29, 0);
    PushMainSprite(FullLineSprite, 58, 50, 58, 14);
  }
  visibleOld = visible;
  stateOld = state;
}

void ShowAudioMode(void) {
  ShowEpgIcon();
  constexpr int16_t codecCellX = 58;
  constexpr int16_t codecCellY = 30;
  constexpr int16_t codecCellW = 58;
  constexpr int16_t codecCellH = 20;
  constexpr int16_t codecTextCenterX = 87;
  static uint8_t codecPage = 0U;
  static uint32_t codecPageTimer = 0U;

  if (radio.isFm()) {
    if (displayreset || audiomodeold != radio.audiomode) {
      RestoreMainBackgroundRegion(codecCellX, codecCellY,
                                  codecCellW, codecCellH);
      PrintMainText(0, fmModeText[language], codecTextCenterX, 33,
               SecondaryColor, SecondaryColorSmooth, 16);
      tft.pushImage(10, 4, 28, 19, radio.fmPilot ? stereoon : mono);
      audiomodeold = radio.audiomode;
      codecPage = 0U;
      codecPageTimer = millis();
    }
    return;
  }
  if (!radio.ServiceStart) radio.servicetype = 9;
  const bool audioInfoReady = radio.ServiceStart &&
                              radio.isDabAudioInfoValid() &&
                              fm_features::isAudioServiceType(
                                  radio.servicetype);
  static bool audioInfoReadyOld = false;
  const bool codecChanged = servicetypeold != radio.servicetype ||
                            audioInfoReadyOld != audioInfoReady;
  bool codecPageChanged = false;
  const uint32_t codecNow = millis();
  if (codecChanged || displayreset) {
    codecPage = 0U;
    codecPageTimer = codecNow;
  } else if (audioInfoReady && codecNow - codecPageTimer >= 2000UL) {
    codecPage ^= 1U;
    codecPageTimer = codecNow;
    codecPageChanged = true;
  }
  if (codecChanged || codecPageChanged || displayreset) {
    RestoreMainBackgroundRegion(codecCellX, codecCellY,
                                codecCellW, codecCellH);
    if (radio.ServiceStart && !audioInfoReady) {
      PrintMainText(0, "...", codecTextCenterX, 33,
               GreyoutColor, BackgroundColor, 16);
    } else if (radio.servicetype == 4) {
      PrintMainText(0, codecPage == 0U ? "DAB+" : "HE-AAC",
               codecTextCenterX, 33,
               SecondaryColor, SecondaryColorSmooth, 16);
    } else if (radio.servicetype == 0 || radio.servicetype == 5) {
      PrintMainText(0, codecPage == 0U ? "DAB" : "MP2",
               codecTextCenterX, 33,
               SecondaryColor, SecondaryColorSmooth, 16);
    }
    servicetypeold = radio.servicetype;
    audioInfoReadyOld = audioInfoReady;
  }

  if (!radio.ServiceStart) radio.audiomode = 4;
  if (audiomodeold != radio.audiomode || displayreset) {
    switch (radio.audiomode) {
      case 0: tft.pushImage(10, 4, 28, 19, mono); break;
      case 1: tft.pushImage(10, 4, 28, 19, mono); break;
      case 2:
      case 3: tft.pushImage(10, 4, 28, 19, stereoon); break;
      case 4: tft.pushImage(10, 4, 28, 19, stereooff); break;
    }
    audiomodeold = radio.audiomode;
  }
}

static void ClearDabFlagArea(void) {
  // Leave the flag slot empty when no valid country flag is available.
  tft.fillRect(80, 110, 36, 23, BackgroundColor3);
}

void ShowECC(void) {
  if (radio.isFm()) return;
  if (eccold != radio.ecc || displayreset) {
    const char* ITU = "";
    switch (radio.serviceHasOwnEcc ? radio.SID[0] : radio.EID[0]) {
      case '1':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, de); ITU = "D"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, gr); ITU = "GRC"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, ma); ITU = "MRC"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, me); ITU = "MNE"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, md); ITU = "MDA"; break;
          default: ClearDabFlagArea(); ITU = ""; break;
        }
        break;

      case '2':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, dz); ITU = "ALG"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, cy); ITU = "CYP"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, cz); ITU = "CZE"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, ie); ITU = "IRL"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, ee); ITU = "EST"; break;
          default: ClearDabFlagArea(); ITU = ""; break;
        }
        break;

      case '3':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, ad); ITU = "AND"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, sm); ITU = "SM"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, pl); ITU = "POL"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, tr); ITU = "TUR"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, mk); ITU = "MKD"; break;
          default: ClearDabFlagArea(); ITU = ""; break;
        }
        break;

      case '4':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, il); ITU = "ISR"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, ch); ITU = "SUI"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, va); ITU = "CVA"; break;
          default: ClearDabFlagArea(); ITU = ""; break;
        }
        break;

      case '5':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, it); ITU = "I"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, jo); ITU = "JOR"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, sk); ITU = "SVK"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, tj); ITU = "TJK"; break;
          default: ClearDabFlagArea(); ITU = ""; break;
        }
        break;

      case '6':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, be); ITU = "BEL"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, fi); ITU = "FNL"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, sy); ITU = "SYR"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, ua); ITU = "UKR"; break;
          default: ClearDabFlagArea(); ITU = ""; break;
        }
        break;

      case '7':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, ru); ITU = "RUS"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, lu); ITU = "LUX"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, tn); ITU = "TUN"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, kz); ITU = "XXK"; break;
          default: ClearDabFlagArea(); ITU = ""; break;
        }
        break;

      case '8':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, ra); ITU = "AZR"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, bg); ITU = "BUL"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, m1); ITU = "MDR"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, nl); ITU = "HOL"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, pt); ITU = "POR"; break;
          default: ClearDabFlagArea(); ITU = ""; break;
        }
        break;

      case '9':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, al); ITU = "ALB"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, dk); ITU = "DNK"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, li); ITU = "LIE"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, lv); ITU = "LVA"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, si); ITU = "SVN"; break;
          default: ClearDabFlagArea(); ITU = ""; break;
        }
        break;

      case 'A':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, at); ITU = "AUT"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, gi); ITU = "GIB"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, is); ITU = "ISL"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, lb); ITU = "LBN"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, am); ITU = "ARM"; break;
          default: ClearDabFlagArea(); ITU = ""; break;
        }
        break;

      case 'B':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, hu); ITU = "HNG"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, iq); ITU = "IRQ"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, mc); ITU = "MCO"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, az); ITU = "AZE"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, uz); ITU = "UZB"; break;
          default: ClearDabFlagArea(); ITU = ""; break;
        }
        break;

      case 'C':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, mt); ITU = "MLT"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, gb); ITU = "G"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, lt); ITU = "LTU"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, hr); ITU = "HRV"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, ge); ITU = "GEO"; break;
          default: ClearDabFlagArea(); ITU = ""; break;
        }
        break;

      case 'D':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, de); ITU = "D"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, ly); ITU = "LBY"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, rs); ITU = "SRB"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, kz); ITU = "KAZ"; break;
          default: ClearDabFlagArea(); ITU = ""; break;
        }
        break;

      case 'E':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, c1); ITU = "CNR"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, ro); ITU = "ROU"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, es); ITU = "E"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, se); ITU = "S"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, tm); ITU = "TKM"; break;
          default: ClearDabFlagArea(); ITU = ""; break;
        }
        break;

      case 'F':
        switch (radio.ecc) {
          case 0xe0: tft.pushImage(80, 110, 36, 23, eg); ITU = "EGY"; break;
          case 0xe1: tft.pushImage(80, 110, 36, 23, fr); ITU = "F"; break;
          case 0xe2: tft.pushImage(80, 110, 36, 23, no); ITU = "NOR"; break;
          case 0xe3: tft.pushImage(80, 110, 36, 23, by); ITU = "BLR"; break;
          case 0xe4: tft.pushImage(80, 110, 36, 23, ba); ITU = "BIH"; break;
          default: ClearDabFlagArea(); ITU = ""; break;
        }
        break;
      default: ClearDabFlagArea(); ITU = ""; break;
    }
    tftReplaceFixed(0, ITUold, ITU, 97, 140,
                    SecondaryColor, SecondaryColorSmooth,
                    BackgroundColor3, 16);
    eccold = radio.ecc;
    snprintf(ITUold, sizeof(ITUold), "%s", ITU);
  }
}

void ShowMemoryPos(void) {
  int memposcolor = 0;
  int memposcolorsmooth = 0;
  switch (memoryposstatus) {
    case MEM_DARK:
      memposcolor = InsignificantColor;
      memposcolorsmooth = InsignificantColorSmooth;
      break;

    case MEM_NORMAL:
      memposcolor = PrimaryColor;
      memposcolorsmooth = PrimaryColorSmooth;
      break;

    case MEM_EXIST:
      memposcolor = SignificantColor;
      memposcolorsmooth = SignificantColorSmooth;
      break;
  }

  char oldValue[5];
  char value[5];
  snprintf(oldValue, sizeof(oldValue), "%u",
           static_cast<unsigned>(memoryposold + 1U));
  snprintf(value, sizeof(value), "%u",
           static_cast<unsigned>(memorypos + 1U));
  ReplaceMainText(-1, oldValue, value, 93, 65,
                  memposcolor, memposcolorsmooth, BackgroundColor2, 16);
  memoryposold = memorypos;
}

void ShowVolume(void) {
  uint8_t segments = map(volume, 0, 63, 0, 100);
  tft.pushImage(25, 46, 270, 50, volumebackground);
  OneBigLineSprite.pushImage(0, 0, 270, 50, volumebackground);
  if (segments > 100) segments = 100;
  OneBigLineSprite.fillRect(60, 9, 2 * segments, 9, BarInsignificantColor);
  OneBigLineSprite.pushSprite(25, 46);

  char value[5];
  snprintf(value, sizeof(value), "%ld",
           static_cast<long>(map(volume, 0, 62, 0, 100)));
  tftPrintFixed(0, value, 190, 68,
                ActiveColor, ActiveColorSmooth, 28);
  Headphones.SetVolume(volume);
  VolumeTimer = millis();
}


// RF samples and display animation are deliberately separate. The radio-side
// IIR accepts each driver sample generation once; the UI then approaches that
// target at 500 ms (DAB) or 250 ms (FM) without re-filtering stale RF input.
void ShowSignalLevel(void) {
  // ProcessDAB() already calls ShowSignalLevel() while System info is open.
  // Reuse that existing call as the lightweight refresh hook and do not draw
  // main-screen signal widgets over the diagnostics page.
  if (ShowServiceInformation) {
    ShowServiceInfo();
    return;
  }

  const bool fm = radio.isFm();
  const int16_t rawSignal = radio.getRSSI();
  const int16_t rawCnr = radio.cnr;
  uint8_t rawQuality = radio.fic > 100U ? 100U : radio.fic;
  if (!fm) {
    const uint8_t cnrQuality =
        radio.cnr >= 20U ? 100U : static_cast<uint8_t>(radio.cnr * 5U);
    if (cnrQuality < rawQuality) rawQuality = cnrQuality;
    if (!radio.signallock) rawQuality = 0;
  }

  static dab_scheduler::SignalDisplayFilter signalFilter;
  signalFilter.acceptSample(radio.signalSampleGeneration(), fm, rawSignal,
                            rawCnr, rawQuality);

  const uint32_t uiInterval = fm
      ? dab_scheduler::FM_SIGNAL_UI_INTERVAL_MS
      : dab_scheduler::DAB_SIGNAL_UI_INTERVAL_MS;
  if (!displayreset && millis() - rssiTimer < uiInterval) return;
  rssiTimer = millis();
  signalFilter.stepDisplay(displayreset);

  SAvg = signalFilter.displayedSignal10;
  SAvg2 = static_cast<int16_t>((signalFilter.displayedCnr10 + 5) / 10);
  SignalLevel = SAvg;
  CNR = static_cast<int8_t>(SAvg2 < 0 ? 0 : (SAvg2 > 127 ? 127 : SAvg2));
  const uint8_t qualityValue = static_cast<uint8_t>(
      (signalFilter.displayedQuality10 + 5U) / 10U);

  int SignalLevelprint = 0;
  if (unit == 0) SignalLevelprint = SignalLevel;
  if (unit == 1) SignalLevelprint = ((SignalLevel * 100) + 10875) / 100;
  if (unit == 2)
    SignalLevelprint = round((float(SignalLevel) / 10.0 -
                              10.0 * log10(75) - 90.0) * 10.0);

  char signalText[16];
  snprintf(signalText, sizeof(signalText), "%d.%d",
           SignalLevelprint / 10, abs(SignalLevelprint % 10));

  if (!ShowServiceInformation) {
    if (SignalLevelprint > SignalLevelold + 3 ||
        SignalLevelprint < SignalLevelold - 3 || displayreset) {
      FullLineSprite.fillSprite(BackgroundColor3);
      FullLineSprite.setTextDatum(TR_DATUM);
      FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
      FullLineSprite.drawString(signalText, 35, 0);
      FullLineSprite.pushSprite(146, 109, 0, 0, 36, 16);

      int segments = 0;
      if (SignalLevel > 120)
        segments = map(SignalLevel, 100, 700, 0, 85);
      if (segments < 0) segments = 0;
      if (segments > 85) segments = 85;

      // Always clear the complete bar first and draw only positive widths so
      // values below segment 56 cannot produce an invalid fillRect width.
      tft.fillRect(134, 129, 170, 6, GreyoutColor);
      const int insignificantSegments = segments < 56 ? segments : 56;
      const int significantSegments = segments > 56 ? segments - 56 : 0;
      if (insignificantSegments > 0)
        tft.fillRect(134, 129, 2 * insignificantSegments, 6,
                     BarInsignificantColor);
      if (significantSegments > 0)
        tft.fillRect(134 + 2 * 56, 129, 2 * significantSegments, 6,
                     BarSignificantColor);

      SignalLevelold = SignalLevelprint;
    }

    if (CNRold != CNR || displayreset) {
      char cnrOldText[8];
      char cnrText[8];
      snprintf(cnrOldText, sizeof(cnrOldText), "%d", static_cast<int>(CNRold));
      snprintf(cnrText, sizeof(cnrText), "%d", static_cast<int>(CNR));

      if (radio.signallock) {
        // If the previous state was unlocked, erase the "--" placeholder before
        // replacing the previous numeric CNR.
        tftPrintFixed(1, "--", 289, 109,
                      BackgroundColor, BackgroundColor3, 16);
        tftReplaceFixed(1, cnrOldText, cnrText, 289, 109,
                        PrimaryColor, PrimaryColorSmooth, BackgroundColor3, 16);
      } else {
        tftReplaceFixed(1, cnrOldText, "--", 289, 109,
                        PrimaryColor, PrimaryColorSmooth, BackgroundColor3, 16);
      }
      CNRold = CNR;
    }

    if (ficold != qualityValue || displayreset) {
      const int filledWidth = map(qualityValue, 0, 100, 0, 139);
      // Build the final frame in RAM. Drawing the full gradient directly to TFT
      // and clearing its unused tail afterwards exposed a brief 100% frame.
      for (byte x = 0; x < 10; x++)
        QualityBarSprite.pushImage(0, x, 139, 1, QualLine);
      if (filledWidth < 139)
        QualityBarSprite.fillRect(filledWidth, 0, 139 - filledWidth, 10,
                                  BackgroundColor3);
      PushMainSprite(QualityBarSprite, 135, 91, 139, 10);

      char ficOldText[8];
      char qualityText[8];
      snprintf(ficOldText, sizeof(ficOldText), "%u%%",
               static_cast<unsigned>(ficold));
      snprintf(qualityText, sizeof(qualityText), "%u%%",
               static_cast<unsigned>(qualityValue));
      ReplaceMainText(1, ficOldText, qualityText, 315, 90,
                      PrimaryColor, PrimaryColorSmooth,
                      BackgroundColor3, 16);
      ficold = qualityValue;
    }
  } else {
    static char signalLevelOldText[16] = "";

    if (strcmp(signalText, signalLevelOldText) != 0 || displayreset) {
      ReplaceMainText(1, signalLevelOldText, signalText, 191, 56,
                      PrimaryColor, PrimaryColorSmooth, BackgroundColor3, 16);
      snprintf(signalLevelOldText, sizeof(signalLevelOldText), "%s", signalText);
    }

    if (CNRold != CNR || displayreset) {
      char cnrOldText[8];
      char cnrText[8];
      snprintf(cnrOldText, sizeof(cnrOldText), "%d", static_cast<int>(CNRold));
      snprintf(cnrText, sizeof(cnrText), "%d", static_cast<int>(CNR));
      ReplaceMainText(1, cnrOldText, cnrText, 284, 56,
                      PrimaryColor, PrimaryColorSmooth, BackgroundColor3, 16);
      CNRold = CNR;
    }
  }
}

void ShowBitrate(void) {
  if (radio.isFm()) return;
  if (tuning) radio.bitrate = 0;
  static uint8_t bitratePage = 0U;
  static uint32_t bitratePageTimer = 0U;
  static uint32_t sampleRateOld = 0;
  const bool bitrateChanged = radio.bitrate != BitrateOld ||
                              radio.samplerate != sampleRateOld;
  bool bitratePageChanged = false;
  const uint32_t bitrateNow = millis();
  if (bitrateChanged || displayreset) {
    bitratePage = 0U;
    bitratePageTimer = bitrateNow;
  } else if (radio.ServiceStart && radio.bitrate != 0U &&
             radio.samplerate != 0U &&
             bitrateNow - bitratePageTimer >= 2000UL) {
    bitratePage ^= 1U;
    bitratePageTimer = bitrateNow;
    bitratePageChanged = true;
  }
  if (bitrateChanged || bitratePageChanged || displayreset) {
    char value[16] = "";
    if (radio.ServiceStart && !tuning && bitratePage == 0U &&
        radio.bitrate != 0) {
      snprintf(value, sizeof(value), "%u kbit/s",
               static_cast<unsigned>(radio.bitrate));
    } else if (radio.ServiceStart && !tuning && radio.samplerate != 0U) {
      if (radio.samplerate % 1000U == 0U) {
        snprintf(value, sizeof(value), "%lu kHz",
                 static_cast<unsigned long>(radio.samplerate / 1000U));
      } else {
        snprintf(value, sizeof(value), "%lu.%lu kHz",
                 static_cast<unsigned long>(radio.samplerate / 1000U),
                 static_cast<unsigned long>((radio.samplerate % 1000U) / 100U));
      }
    }

    FullLineSprite.pushImage(-9, -140, 320, 240, Background);
    FullLineSprite.setTextDatum(TC_DATUM);
    FullLineSprite.setTextColor(PrimaryColor, PrimaryColorSmooth, false);
    FullLineSprite.drawString(value, 30, 0);
    FullLineSprite.pushSprite(9, 140, 0, 0, 70, 16);
    BitrateOld = radio.bitrate;
    sampleRateOld = radio.samplerate;
  }
}

void ShowClock(void) {
  static uint32_t appliedDabTimeGeneration = 0;
  static bool appliedFmCt = false;
  static fm_features::ClockTime previousFmCt;
  const uint32_t timeGeneration = radio.timeSampleGeneration();
  if (!radio.isFm() && radio.signallock &&
      dab_scheduler::takeGeneration(timeGeneration,
                                    appliedDabTimeGeneration)) {
    setTime(radio.Hours, radio.Minutes, radio.Seconds,
            radio.Days, radio.Months, radio.Year);
    appliedDabTimeGeneration = timeGeneration;
  }

  // RDS CT carries UTC plus a local offset. Apply a confirmed sample only
  // when it changes; repeated RDS groups must not reset seconds to zero.
  if (!radio.isFm() || !radio.fmCtValid) {
    appliedFmCt = false;
  } else {
    const fm_features::ClockTime& ct = radio.fmCt;
    const bool changed = !appliedFmCt ||
        ct.year != previousFmCt.year || ct.month != previousFmCt.month ||
        ct.day != previousFmCt.day || ct.hour != previousFmCt.hour ||
        ct.minute != previousFmCt.minute ||
        ct.localOffsetHalfHours != previousFmCt.localOffsetHalfHours;
    if (changed) {
      setTime(ct.hour, ct.minute, 0, ct.day, ct.month, ct.year);
      adjustTime(static_cast<long>(ct.localOffsetHalfHours) * 1800L);
      previousFmCt = ct;
      appliedFmCt = true;
    }
  }

  // This function runs in the normal UI loop. Keep the two displayed strings in
  // fixed buffers so checking an unchanged clock/date never allocates on heap.
  char clockstring[6];
  char datestring[11];
  snprintf(clockstring, sizeof(clockstring), "%02d:%02d", hour(), minute());
  snprintf(datestring, sizeof(datestring), "%02d-%02d-%04d", day(), month(), year());

  if (strcmp(clockstringOld, clockstring) != 0 || displayreset) {
    FullLineSprite.pushImage(-105, -7, 320, 240, Background);
    FullLineSprite.setTextDatum(TL_DATUM);
    FullLineSprite.setTextColor(ActiveColor, ActiveColorSmooth, false);
    FullLineSprite.drawString(clockstring, 0, 0);
    FullLineSprite.pushSprite(105, 7, 0, 0, 36, 16);
    snprintf(clockstringOld, sizeof(clockstringOld), "%s", clockstring);
  }

  if (strcmp(datestringOld, datestring) != 0 || displayreset) {
    FullLineSprite.pushImage(-177, -7, 320, 240, Background);
    FullLineSprite.setTextDatum(TL_DATUM);
    FullLineSprite.setTextColor(ActiveColor, ActiveColorSmooth, false);
    FullLineSprite.drawString(datestring, 0, 0);
    FullLineSprite.pushSprite(177, 7, 0, 0, 70, 16);
    snprintf(datestringOld, sizeof(datestringOld), "%s", datestring);
  }
}

void ShowSlideShowIcon(void) {
  if (radio.isFm()) {
    if (SlideShowAvailableOld || displayreset) tft.pushImage (10, 187, 30, 22, slideshowoff);
    SlideShowAvailableOld = false;
    return;
  }
  if (SlideShowAvailableOld != radio.SlideShowAvailable || displayreset) {
    if (radio.SlideShowAvailable) {
      tft.pushImage (10, 187, 30, 22, slideshowon);
    } else {
      tft.pushImage (10, 187, 30, 22, slideshowoff);
    }
    SlideShowAvailableOld = radio.SlideShowAvailable;
  }
}

void ShowTuneMode(void) {
  ModeSprite.pushImage(-6, -33, 320, 240, Background);

  switch (tunemode) {
    case TUNE_MAN:
      ModeSprite.setTextColor(ActiveColor, ActiveColorSmooth, false);
      ModeSprite.drawString("MAN", 23, 0);
      ModeSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
      ModeSprite.drawString("AUTO", 23, 16);
      ModeSprite.drawString("MEM", 23, 32);
      break;

    case TUNE_AUTO:
      ModeSprite.setTextColor(ActiveColor, ActiveColorSmooth, false);
      ModeSprite.drawString("AUTO", 23, 16);
      ModeSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
      ModeSprite.drawString("MAN", 23, 0);
      ModeSprite.drawString("MEM", 23, 32);
      break;

    case TUNE_MEM:
      ModeSprite.setTextColor(SecondaryColor, SecondaryColorSmooth, false);
      ModeSprite.drawString("MAN", 23, 0);
      ModeSprite.drawString("AUTO", 23, 16);

      if (memorystore) {
        ModeSprite.setTextColor(SignificantColor, SignificantColorSmooth, false);
      } else {
        ModeSprite.setTextColor(ActiveColor, ActiveColorSmooth, false);
      }
      ModeSprite.drawString("MEM", 23, 32);
      break;
  }
  PushMainSprite(ModeSprite, 6, 33, 46, 47);
}

// Share existing flash assets with the EPG renderer; no duplicate bitmap copies.
// EPG uses a single-panel frame and aligns time/name columns with spacing.
// The station list bitmap has extra ID/type columns that do not belong here.
const uint16_t* EpgBackground(bool) { return configurationbackground; }
const uint16_t* EpgSelector() { return selector; }
