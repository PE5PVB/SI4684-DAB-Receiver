// SI4684 DAB chip driver. See si4684.h for the high-level summary.
//
// Implementation notes:
//   - The supplied Si468x library owns the common CTS/error state machine.
//     Legacy DAB parsers below still use SPIbuffer, but all their commands are
//     routed through that one common transport.
//   - Commands and reply layouts mirror the Si468x programming guide (AN649).
//   - Slideshow segments and the assembled current image stay in RAM.

#include "si4684.h"
#include "Si468xROM.h"
// Arduino defines interrupts() as a function-like macro; the standalone
// driver also has a reply field named `interrupts`.
#ifdef interrupts
#undef interrupts
#endif
#include "vendor/si468x/Si468x.h"
#include "vendor/si468x/dab_6_0_9.h"
#include "vendor/si468x/fmhd_5_3_3.h"

unsigned char SPIbuffer[4096];      // shared SPI tx/rx buffer (commands and replies)
bool once = false;

unsigned long DataUpdate = 0;       // millis() of last EnsembleInfo/ServiceInfo refresh (500 ms throttle)
bool EnsembleInfoSet;
uint8_t slaveSelectPin;

static si468x::Si468x chip;
// 512 bytes is enough for the 38-entry DAB plan and keeps scarce internal
// DRAM available for the 40 KiB MOT buffer. Firmware upload is simply chunked.
static uint8_t chipWorkspace[512];
static volatile uint8_t lastStatus0;

static bool hostWriteCommand(void*, uint8_t command, const uint8_t* args, uint16_t length) {
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(slaveSelectPin, LOW);
  SPI.transfer(command);
  for (uint16_t i = 0; i < length; ++i) SPI.transfer(args[i]);
  digitalWrite(slaveSelectPin, HIGH);
  SPI.endTransaction();
  return true;
}

static bool hostReadReply(void*, uint8_t* destination, uint16_t length) {
  if (!destination || !length) return false;
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(slaveSelectPin, LOW);
  SPI.transfer(0);  // SPI framing byte; hidden from the common driver
  for (uint16_t i = 0; i < length; ++i) destination[i] = SPI.transfer(0);
  digitalWrite(slaveSelectPin, HIGH);
  SPI.endTransaction();
  return true;
}

static uint32_t hostTimeUs(void*) { return micros(); }
static void hostIdle(void*) { yield(); }
static void statusChanged(void*, const si468x::Status& status) { lastStatus0 = status.status0; }

static size_t progmemImageReader(void* context, uint32_t offset, uint8_t* destination, size_t length) {
  const uint8_t* source = static_cast<const uint8_t*>(context);
  for (size_t i = 0; i < length; ++i) destination[i] = pgm_read_byte(source + offset + i);
  return length;
}

static void SPIwrite(unsigned char* data, uint32_t length);
static void SPIread(uint16_t length);
static void cts(void);
static void Set_Property(uint16_t property, uint16_t value);
static String convertToUTF8(const wchar_t* input);
static String extractUTF8Substring(const String& utf8String, size_t start, size_t length);
static void charConverter(const char* input, wchar_t* output, size_t size);
static int compareCompID(const void* a, const void* b);

// Read back the chip identifier string (e.g. "Si4684").
char* DAB::getChipID(void) {
  return ChipType;
}

// Return the loaded firmware version string ("major.minor.build").
char* DAB::getFirmwareVersion(void) {
  return FirmwVersion;
}

// Sanity check: query the chip status. Returns true if it looks hung
// (caller responds by reinitialising the chip via doRecovery()).
bool DAB::panic(void) {
  si468x::SystemState state;
  if (chip.getSystemState(state, 100000UL) != si468x::Result::Ok) return true;
  return state.image != (isFm() ? si468x::Image::FMHD : si468x::Image::DAB);
}

int16_t DAB::getRSSI(void) {
  if (isFm()) return static_cast<int16_t>(fmRssi) * 10;
  return dabRssi10;
}

uint32_t DAB::getFreq(uint8_t freq) {
  if (isFm()) return static_cast<uint32_t>(fmFrequency10kHz) * 10UL;
  return DABfrequencyTable_DAB[freq].frequency;
}

const char* DAB::getChannel(uint8_t freq) {
  if (isFm()) return "FM";
  return DABfrequencyTable_DAB[freq].label;
}

// Set the chip's internal audio attenuator (the headphone amp does the
// fine volume; this is mostly a coarse control).
void DAB::vol(uint8_t vol) {
  Set_Property(0x0300, (vol & 0x3F));
}

// Low-level SPI command write: pulls CS low, transfers `length` bytes,
// then releases CS. No reply data is captured here — use SPIread() after cts()
// when the chip needs to return data.
static void SPIwrite(unsigned char* data, uint32_t length) {
  if (!data || !length || data[0] == 0) return;
  chip.executeRaw(data[0], length > 1 ? data + 1 : nullptr,
                  length > 1 ? static_cast<uint16_t>(length - 1) : 0,
                  nullptr, 0, 1000000UL);
}

// Low-level SPI reply read: clocks `length` dummy bytes out and stores the
// returned bytes back into SPIbuffer. Expects cts() to have already been
// called so the chip is ready to respond.
static void SPIread(uint16_t length) {
  memset(SPIbuffer, 0, static_cast<size_t>(length) + 1U);
  chip.readCurrentReply(SPIbuffer + 1, length);
}

// SET_PROPERTY (cmd 0x13): write one of the chip's internal properties such
// as sample rate, audio output config, FIC interrupt source. See AN649 §6.
static void Set_Property(uint16_t property, uint16_t value) {
  SPIbuffer[0] = 0x13;
  SPIbuffer[1] = 0x00;
  SPIbuffer[2] = property & 0xFF;
  SPIbuffer[3] = (property >> 8) & 0xFF;
  SPIbuffer[4] = value & 0xFF;
  SPIbuffer[5] = (value >> 8) & 0xFF;
  SPIwrite(SPIbuffer, 6);
  cts();
}

static void cts(void) {
  // SPIwrite() above already completed CTS through Si468x::executeRaw().
}

// Cold-start sequence per AN649:
//   1. POWER_UP - configure clock + crystal
//   2. LOAD_INIT + HOST_LOAD - upload the patch + firmware blobs from flash
//   3. BOOT - jump to firmware
//   4. Configure DAB-specific properties (sample rate, audio output, FIC etc.)
// Returns true once the chip reports the DAB image is running.
bool DAB::begin(uint8_t SSpin, RadioMode requestedMode) {
  memset(SPIbuffer, 0, sizeof(SPIbuffer));
  slaveSelectPin = SSpin;
  tunePending = false;
  seekPending = false;
  ServiceStart = false;
  ServiceIndex = 0;
  numberofservices = 0;
  dabRssi10 = 0;
  dabSignalTimer = 0;
  lastStatus0 = 0;

  pinMode(slaveSelectPin, OUTPUT);
  digitalWrite(slaveSelectPin, HIGH);
  SPI.begin(14, 16, 13, SSpin);

  si468x::HostInterface host;
  host.writeCommand = hostWriteCommand;
  host.readReply = hostReadReply;
  host.timeUs = hostTimeUs;
  host.idle = hostIdle;
  chip.setHost(host);
  chip.setWorkspace(chipWorkspace, sizeof(chipWorkspace));
  chip.setStatusCallback(statusChanged);
  chip.setCtsPollIntervalUs(1000);
  // INTB is not routed to an ESP32 GPIO on this PCB, so use bounded polling.
  chip.setIdleStatusPollIntervalUs(20000);

  si468x::PowerUpConfig power;
  power.ctsInterruptEnable = false;
  power.clockMode = 1;
  power.trSize = 7;
  power.iBias = 0x48;
  power.crystalFrequencyHz = 19200000UL;
  power.cTune = 0x1F;
  power.iBiasRun = 0x18;

  const uint8_t* image = requestedMode == RADIO_MODE_FM ? si468x_fmhd_5_3_3 : si468x_dab_6_0_9;
  const uint32_t imageSize = requestedMode == RADIO_MODE_FM ? si468x_fmhd_5_3_3_size : si468x_dab_6_0_9_size;
  si468x::Result result = chip.bootHostImage(
      power,
      progmemImageReader, const_cast<uint8_t*>(rom_patch_016), sizeof(rom_patch_016),
      progmemImageReader, const_cast<uint8_t*>(image), imageSize,
      1000000UL);
  if (result != si468x::Result::Ok) return false;

  si468x::PartInfo part;
  si468x::SystemState state;
  if (chip.getPartInfo(part) != si468x::Result::Ok || part.partNumber != 4684) return false;
  snprintf(ChipType, sizeof(ChipType), "SI%u", part.partNumber);
  if (chip.getSystemState(state) != si468x::Result::Ok) return false;
  const si468x::Image expected = requestedMode == RADIO_MODE_FM ? si468x::Image::FMHD : si468x::Image::DAB;
  if (state.image != expected) return false;
  activeMode = requestedMode;

  si468x::FunctionInfo functionInfo;
  if (chip.getFunctionInfo(functionInfo) != si468x::Result::Ok) return false;
  snprintf(FirmwVersion, sizeof(FirmwVersion), "%u.%u.%u",
           functionInfo.major, functionInfo.minor, functionInfo.build);
  Serial.printf("[RADIO] Si%u image=%s firmware=%s\n", part.partNumber,
                requestedMode == RADIO_MODE_FM ? "FMHD" : "DAB", FirmwVersion);

  // Shared audio/front-end setup retained from the proven DAB configuration.
  Set_Property(0x0200, 0x8000);
  Set_Property(0x0202, 0x1600);
  Set_Property(0x0800, 0x0003);
  Set_Property(0x1710, 0xFC4A);
  Set_Property(0x1711, 0x00F8);

  if (requestedMode == RADIO_MODE_DAB) {
    uint32_t frequencies[38];
    for (uint8_t i = 0; i < 38; ++i) frequencies[i] = DABfrequencyTable_DAB[i].frequency;
    if (chip.dabSetFrequencyList(frequencies, 38) != si468x::Result::Ok) return false;
    Set_Property(0x8100, 0x0001);
    Set_Property(0x8101, 0x0064);
    Set_Property(0xB200, 0x0000);
    Set_Property(0xB201, 0x0080);
    Set_Property(0xB301, 0x0000);
    Set_Property(0xB302, 0x0000);
    Set_Property(0xB303, 0x0000);
    Set_Property(0xB400, 0x0097);
    Set_Property(0xB401, 0x0002);
    Set_Property(0xB500, 0x0000);
    chip.setInterruptEnable(si468x::INTERRUPT_STC | si468x::INTERRUPT_DSRV |
                            si468x::INTERRUPT_RSQ | si468x::INTERRUPT_DEVICE_EVENT);
  } else {
    Set_Property(0x3100, FM_BAND_BOTTOM_10KHZ);
    Set_Property(0x3101, FM_BAND_TOP_10KHZ);
    Set_Property(0x3102, FM_STEP_10KHZ);
    Set_Property(0x3200, 20);    // max tune error
    Set_Property(0x3202, 18);    // seek RSSI threshold (dBuV)
    Set_Property(0x3204, 4);     // seek SNR threshold (dB)
    Set_Property(0x3900, 1);     // 50 us de-emphasis (Europe)
    Set_Property(0x3C00, 0x001B);
    Set_Property(0x3C01, 1);
    Set_Property(0x3C02, 0x0051); // RDS enabled, conservative BLE thresholds
    chip.setInterruptEnable(si468x::INTERRUPT_STC | si468x::INTERRUPT_ACF |
                            si468x::INTERRUPT_RDS | si468x::INTERRUPT_RSQ |
                            si468x::INTERRUPT_DEVICE_EVENT);
    clearFmData();
  }

  clearData();
  return true;
}

// Query DAB ensemble information (RSSI, CNR, FIC quality, sample/lock state,
// EID, ensemble label) and populate the public fields. Also fetches the
// service list when freshly locked. Called periodically from Update().
void DAB::EnsembleInfo(void) {
  static bool lastSignalLock = false;
  static uint8_t lastFic = 0;

  SPIbuffer[0] = 0xB2;  // Get signalstatus
  SPIbuffer[1] = 0x09;
  SPIwrite(SPIbuffer, 2);
  cts();
  SPIread(19);
  fic = SPIbuffer[9];
  cnr = SPIbuffer[10];
  if (fic > 0) signallock = true;
  else signallock = false;

  // Log signal changes
  if (signallock != lastSignalLock) {
    lastSignalLock = signallock;
  }

  lastFic = fic;
  if (signallock) {
    SPIbuffer[0] = 0x80;  // Get servicelist
    SPIbuffer[1] = 0x00;
    SPIwrite(SPIbuffer, 2);
    cts();
    SPIread(8);

    if (SPIbuffer[5] + (SPIbuffer[6] << 8) + 6 < sizeof(SPIbuffer)) {
      SPIread(SPIbuffer[5] + (SPIbuffer[6] << 8) + 6);
      uint8_t numberofcomponents;

      numberofservices = SPIbuffer[9];
      if (numberofservices > sizeof(service) / sizeof(DABService)) {
        clearData();  // Handle overflow when signal is crappy
        numberofservices = 0;
        return;
      }

      uint16_t offset = 13;

      for (uint8_t i = 0; i < numberofservices; i++) {
        if (i >= sizeof(service) / sizeof(DABService)) {
          clearData();  // Handle overflow when signal is crappy
          numberofservices = 0;
          return;
        }

        serviceID = SPIbuffer[offset + 3];
        serviceID <<= 8;
        serviceID += SPIbuffer[offset + 2];
        serviceID <<= 8;
        serviceID += SPIbuffer[offset + 1];
        serviceID <<= 8;
        serviceID += SPIbuffer[offset];
        componentID = 0;

        numberofcomponents = SPIbuffer[offset + 5] & 0x0F;

        for (uint16_t j = 0; j < 16; j++) service[i].Label[j] = SPIbuffer[offset + 8 + j];

        for (int16_t j = 15; j >= 0; j--) {
          if (service[i].Label[j] == ' ' && service[i].Label[j + 1] == '\0') {
            service[i].Label[j] = '\0';
          } else {
            break;
          }
        }
        offset += 24;

        for (uint16_t j = 0; j < numberofcomponents; j++) {
          if (j == 0) {
            componentID = SPIbuffer[offset + 3];
            componentID <<= 8;
            componentID += SPIbuffer[offset + 2];
            componentID <<= 8;
            componentID += SPIbuffer[offset + 1];
            componentID <<= 8;
            componentID += SPIbuffer[offset];
          }
          offset += 4;
        }
        service[i].ServiceID = serviceID;
        service[i].CompID = componentID;
      }


      if (numberofservices > 0) {
        qsort(service, numberofservices, sizeof(DABService), compareCompID);

        if (CurrentServiceID != service[ServiceIndex].ServiceID) {
          for (byte x = 0; x < numberofservices; x++) {
            if (CurrentServiceID == service[x].ServiceID) {
              ServiceIndex = x;
              break;
            }
          }
        }
      }

      for (byte i = 0; i < 5; i++) SPIbuffer[i] = 0;
      SPIwrite(SPIbuffer, 5);

      SPIbuffer[0] = 0xB4;
      SPIbuffer[1] = 0x00;
      SPIwrite(SPIbuffer, 2);
      cts();
      SPIread(26);

      uint16_t eidRaw = ((uint16_t)SPIbuffer[6] << 8) | SPIbuffer[5];
      if (eidRaw != 0x0000 && eidRaw != 0xFFFF) {
        EnsembleInfoSet = true;

        // Only set EID/EnsembleLabel/ECC once after tuning; they don't change on the same frequency
        // This prevents corrupted data during marginal signal from overwriting valid values
        if (EID[0] == '\0' || EnsembleLabel[0] == '\0') {
          EID[2] = (SPIbuffer[5] & 0xF0) >> 4;
          EID[3] = (SPIbuffer[5] & 0x0F);
          EID[0] = (SPIbuffer[6] & 0xF0) >> 4;
          EID[1] = (SPIbuffer[6] & 0x0F);
          EID[4] = '\0';

          for (int i = 0; i < 4; i++) {
            if (EID[i] < 10) {
              EID[i] += '0';
            } else {
              EID[i] += 'A' - 10;
            }
          }
          for (uint8_t i = 0; i < 16 && SPIbuffer[7 + i] != '\0'; i++) {
            EnsembleLabel[i] = static_cast<char>(SPIbuffer[7 + i]);
          }
          EnsembleLabel[16] = '\0';

          for (int8_t i = 15; i >= 0; i--) {
            if (EnsembleLabel[i] == ' ' && EnsembleLabel[i + 1] == '\0') {
              EnsembleLabel[i] = '\0';
            } else {
              break;
            }
          }
          EnsembleLabelCharset = SPIbuffer[24];
        }
        if (ensembleEcc == 0 && SPIbuffer[23] != 0) {
          ensembleEcc = SPIbuffer[23];
        }
      } else {
        EnsembleInfoSet = false;
      }
    }
    SPIbuffer[0] = 0xBC;
    SPIbuffer[1] = 0x00;
    SPIwrite(SPIbuffer, 2);
    cts();
    SPIread(11);

    if (!bitRead(SPIbuffer[1], 6)) {
      Year = SPIbuffer[5] + ((uint16_t)SPIbuffer[6] << 8);
      Months = SPIbuffer[7];
      Days = SPIbuffer[8];
      Hours = SPIbuffer[9];
      Minutes = SPIbuffer[10];
      Seconds = SPIbuffer[11];
    }
  }
}

// Pull one chunk of service-data from the chip. Each chunk is either:
//   - Dynamic Label / Radiotext (group flag 0x02) → copy into ServiceData
//   - MOT slideshow header     (0x80 0x00 0x12 ...) → record total length, TID
//   - MOT slideshow segment    (0x00/0x80 ...   ) → memcpy into slideshowSegBuf
// Called on every Update() while the receiver has signal lock.
void DAB::getServiceData(void) {
  uint32_t byte_count = 0;
  uint32_t byte_number = 0;

  if (lastStatus0 & si468x::INTERRUPT_DSRV) {
    SPIbuffer[0] = 0x84;
    SPIbuffer[1] = 0x01;
    SPIwrite(SPIbuffer, 2);
    cts();
    SPIread(20);

    if ((SPIbuffer[19] + (SPIbuffer[20] << 8)) + 24 < sizeof(SPIbuffer)) {
      if ((SPIbuffer[19] + (SPIbuffer[20] << 8)) > 0) {
        cts();
        SPIread((SPIbuffer[19] + (SPIbuffer[20] << 8)) + 24);
        byte_count = SPIbuffer[19] + (SPIbuffer[20] << 8);

        // Read Radiotext
        if (((SPIbuffer[8] >> 6) & 0x03) == 0x02 && !((SPIbuffer[25] & 0x10) == 0x10)) {
          const uint32_t textLength = byte_count < sizeof(ServiceData) ? byte_count : sizeof(ServiceData) - 1;
          for (byte_number = 0; byte_number < textLength; byte_number++) ServiceData[byte_number] = (char)SPIbuffer[27 + byte_number];
          ServiceData[byte_number] = '\0';

          // Read Slideshow header - extract total length
        } else if (((SPIbuffer[8] >> 6) & 0x03) == 0x01 && SPIbuffer[27] == 0x80 && SPIbuffer[28] == 0x00 && SPIbuffer[29] == 0x12 && byte_count < 200) {
          uint16_t transportID = (SPIbuffer[30] << 8) | SPIbuffer[31];
          uint32_t newLength = (((uint16_t)SPIbuffer[35] << 12) | ((uint16_t)SPIbuffer[36] << 4) | ((uint16_t)SPIbuffer[37] >> 4)) & 0x00FFFF;

          if (newLength > 0) {
            if (SlideShowLength == 0) {
              // First header - set length and lock onto this image
              SlideShowLength = newLength;
              SlideShowAvailable = false;
              slideshowRamSize = 0;

              // If segments were collected with a different TID, discard them
              if (SlideShowTransportID != 0 && transportID != SlideShowTransportID) {
if (SlideShowDebug) Serial.printf("[SLS] Header TID=%u != segments TID=%u, discarding old segments\n", transportID, SlideShowTransportID);
                clearSegmentBuffer();
                SlideShowByteCounter = 0;
                SlideShowHighestSegment = 0;
                SlideShowTotalSegments = 0;
                SlideShowInit = false;
              }

              SlideShowTransportID = transportID;
              SlideShowInit = true;
              if (SlideShowDebug) Serial.printf("[SLS] Header received, length=%u, bytes so far=%u, TID=%u\n", SlideShowLength, SlideShowByteCounter, transportID);

              if (SlideShowByteCounter >= SlideShowLength && allSegmentsReceived()) {
                SlideShowTotalSegments = SlideShowHighestSegment + 1;
                if (SlideShowDebug) Serial.printf("[SLS] All segments ready after header, assembling %u segments\n", SlideShowTotalSegments);
                assembleSlideshow();
              }
            } else if (SlideShowLength == newLength) {
              // Same image, new carousel cycle - update TID to accept segments again
              SlideShowTransportID = transportID;
              if (SlideShowDebug) Serial.printf("[SLS] Header confirmed, length=%u, bytes so far=%u, TID=%u\n", SlideShowLength, SlideShowByteCounter, transportID);

              if (SlideShowByteCounter >= SlideShowLength && allSegmentsReceived()) {
                SlideShowTotalSegments = SlideShowHighestSegment + 1;
                if (SlideShowDebug) Serial.printf("[SLS] All segments ready after header, assembling %u segments\n", SlideShowTotalSegments);
                assembleSlideshow();
              }
            } else {
              // A new carousel object replaced an incomplete one.
              if (SlideShowDebug) Serial.printf("[SLS] New header length=%u replaces %u, TID=%u\n", newLength, SlideShowLength, transportID);
              clearSegmentBuffer();
              SlideShowLength = newLength;
              SlideShowTransportID = transportID;
              SlideShowByteCounter = 0;
              SlideShowHighestSegment = 0;
              SlideShowTotalSegments = 0;
              SlideShowInit = true;
              SlideShowAvailable = false;
              slideshowRamSize = 0;
            }
          }

          // Read Slideshow packets - store each segment (works with or without header)
        } else if (((SPIbuffer[8] >> 6) & 0x03) == 0x01 && (SPIbuffer[27] == 0x00 || SPIbuffer[27] == 0x80) && SPIbuffer[29] == 0x12) {
          uint16_t transportID = (SPIbuffer[30] << 8) | SPIbuffer[31];
          uint8_t segmentNumber = SPIbuffer[28];

          // Check Transport ID
          if (SlideShowTransportID == 0) {
            clearSegmentBuffer();
            SlideShowTransportID = transportID;
            SlideShowAvailable = false;
            slideshowRamSize = 0;
            if (SlideShowDebug) Serial.printf("[SLS] Transport ID set to %u\n", transportID);
          } else if (transportID != SlideShowTransportID) {
            // Different carousel object - skip this segment, don't reset
            if (SlideShowDebug) Serial.printf("[SLS] Skipping segment %u, TID=%u (collecting TID=%u)\n", segmentNumber, transportID, SlideShowTransportID);
          }

          if (transportID == SlideShowTransportID) {
            uint8_t byteIndex = segmentNumber / 8;
            uint8_t bitIndex = segmentNumber % 8;

            // Check if we already have this segment
            if (!(SlideShowSegmentBitmap[byteIndex] & (1 << bitIndex))) {
              uint16_t dataLen = byte_count - 11;

              // Bounds-check: discard segments that don't fit our RAM buffer
              if (segmentNumber < SLS_MAX_SEGMENTS && dataLen <= SLS_MAX_SEG_SIZE) {
                memcpy(&slideshowSegBuf[segmentNumber * SLS_MAX_SEG_SIZE], &SPIbuffer[34], dataLen);
                slideshowSegLen[segmentNumber] = dataLen;

                // Mark segment as received and update highest seen
                SlideShowSegmentBitmap[byteIndex] |= (1 << bitIndex);
                SlideShowByteCounter += dataLen;
                if (segmentNumber > SlideShowHighestSegment) {
                  SlideShowHighestSegment = segmentNumber;
                }
                SlideShowInit = true;
                SlideShowLastActivity = millis();
                if (SlideShowDebug) Serial.printf("[SLS] Segment %u saved, %u bytes (total %u/%u) TID=%u\n", segmentNumber, dataLen, SlideShowByteCounter, SlideShowLength, transportID);

                // Check if complete - using byte count + all segments when we have header length
                if (SlideShowLength > 0 && SlideShowByteCounter >= SlideShowLength && allSegmentsReceived()) {
                  SlideShowTotalSegments = SlideShowHighestSegment + 1;
                  if (SlideShowDebug) Serial.printf("[SLS] Complete by byte count, assembling %u segments\n", SlideShowTotalSegments);
                  assembleSlideshow();
                }
              } else {
                if (SlideShowDebug) Serial.printf("[SLS] Drop seg %u (dataLen=%u out of buffer bounds)\n", segmentNumber, dataLen);
              }
            } else if (segmentNumber == 0 && SlideShowLength == 0 && SlideShowHighestSegment > 0) {
              // Segment 0 received again (duplicate) - a full broadcast cycle has completed
              if (SlideShowDebug) Serial.printf("[SLS] Segment 0 repeated, highest=%u\n", SlideShowHighestSegment);
              if (allSegmentsReceived()) {
                SlideShowTotalSegments = SlideShowHighestSegment + 1;
                if (SlideShowDebug) Serial.printf("[SLS] Complete by cycle detection, assembling %u segments\n", SlideShowTotalSegments);
                assembleSlideshow();
              }
            }
          }
        }
      }
    }
  }
}

// Forget every buffered slideshow segment. Used on TID switch, duplicate-image
// detection, assembly completion, and timeouts. Clearing the lengths array is
// enough — the data bytes in slideshowSegBuf only get read for indices where
// the corresponding length is non-zero.
void DAB::clearSegmentBuffer(void) {
  memset(slideshowSegLen, 0, sizeof(slideshowSegLen));
  memset(SlideShowSegmentBitmap, 0, sizeof(SlideShowSegmentBitmap));
}

// Check if we have every segment from 0..N where N is either:
//   - SlideShowTotalSegments (set from the MOT header when known), or
//   - SlideShowHighestSegment + 1 (best guess until we see segment 0 wrap).
bool DAB::allSegmentsReceived(void) {
  // Determine how many segments to check
  uint8_t segmentsToCheck = SlideShowTotalSegments;
  if (segmentsToCheck == 0) {
    // No header received, use highest segment seen + 1
    segmentsToCheck = SlideShowHighestSegment + 1;
  }

  if (segmentsToCheck == 0) return false;

  for (uint8_t i = 0; i < segmentsToCheck; i++) {
    uint8_t byteIndex = i / 8;
    uint8_t bitIndex = i % 8;
    if (!(SlideShowSegmentBitmap[byteIndex] & (1 << bitIndex))) {
      return false;
    }
  }
  return true;
}

// Compact the fixed-stride segment slots in place, validate the image header,
// and publish the resulting contiguous RAM buffer. memmove is safe here
// because every destination begins at or below its source slot.
void DAB::assembleSlideshow(void) {
  if (SlideShowDebug) Serial.printf("[SLS] Assembling: %u segments, %u bytes received, %u bytes expected\n", SlideShowTotalSegments, SlideShowByteCounter, SlideShowLength);
  uint32_t actualSize = 0;
  for (uint8_t i = 0; i < SlideShowTotalSegments && i < SLS_MAX_SEGMENTS; i++) {
    if (slideshowSegLen[i] > 0) {
      memmove(slideshowSegBuf + actualSize,
              slideshowSegBuf + static_cast<size_t>(i) * SLS_MAX_SEG_SIZE,
              slideshowSegLen[i]);
      actualSize += slideshowSegLen[i];
    } else if (SlideShowDebug) {
      Serial.printf("[SLS] WARNING: segment %u missing!\n", i);
    }
  }
  const bool sizeValid = actualSize > 8 && (SlideShowLength == 0 || actualSize == SlideShowLength);
  const bool validJPEG = sizeValid && slideshowSegBuf[0] == 0xFF && slideshowSegBuf[1] == 0xD8 && slideshowSegBuf[2] == 0xFF;
  const bool validPNG = sizeValid && slideshowSegBuf[0] == 0x89 && slideshowSegBuf[1] == 0x50 &&
                        slideshowSegBuf[2] == 0x4E && slideshowSegBuf[3] == 0x47 &&
                        slideshowSegBuf[4] == 0x0D && slideshowSegBuf[5] == 0x0A &&
                        slideshowSegBuf[6] == 0x1A && slideshowSegBuf[7] == 0x0A;
  if (!validJPEG && !validPNG) {
    if (SlideShowDebug) Serial.println("[SLS] REJECTED: size or image header invalid");
    clearSegmentBuffer();
    SlideShowLength = 0;
    slideshowRamSize = 0;
    SlideShowAvailable = false;
  } else {
    slideshowRamSize = actualSize;
    SlideShowUpdate = true;
    SlideShowUpdate2 = true;
    SlideShowAvailable = true;
    if (SlideShowDebug) Serial.printf("[SLS] RAM image ready: %s, %u bytes\n", validJPEG ? "JPEG" : "PNG", actualSize);
  }
  SlideShowInit = false;
  SlideShowTransportID = 0;
  SlideShowByteCounter = 0;
  SlideShowHighestSegment = 0;
  SlideShowTotalSegments = 0;
  SlideShowLength = 0;
  clearSegmentBuffer();
}

// Populate per-service metadata (PTY, ECC, bitrate, audio mode, sample rate,
// time/date, protection level) for the currently selected service. The
// service list itself is filled in EnsembleInfo().
void DAB::ServiceInfo(void) {
  for (byte x = 0; x < numberofservices; x++) {
    SPIbuffer[0] = 0xBE;
    SPIbuffer[1] = 0x00;
    SPIbuffer[2] = 0x00;
    SPIbuffer[3] = 0x00;
    SPIbuffer[4] = service[x].ServiceID & 0xFF;
    SPIbuffer[5] = (service[x].ServiceID >> 8) & 0xFF;
    SPIbuffer[6] = (service[x].ServiceID >> 16) & 0xFF;
    SPIbuffer[7] = (service[x].ServiceID >> 24) & 0xFF;
    SPIbuffer[8] = service[x].CompID & 0xFF;
    SPIbuffer[9] = (service[x].CompID >> 8) & 0xFF;
    SPIbuffer[10] = (service[x].CompID >> 16) & 0xFF;
    SPIbuffer[11] = (service[x].CompID >> 24) & 0xFF;
    SPIwrite(SPIbuffer, 12);
    cts();
    SPIread(12);
    service[x].ServiceType = SPIbuffer[5];
  }

  if (ServiceStart) {
    SPIbuffer[0] = 0xBD;
    SPIbuffer[1] = 0x00;
    SPIwrite(SPIbuffer, 2);
    cts();
    SPIread(16);

    if (SPIbuffer[1] == 0x80) {
      bitrate = SPIbuffer[5] + (SPIbuffer[6] << 8);
      samplerate = SPIbuffer[7] + (SPIbuffer[8] << 8);
      audiomode = SPIbuffer[9] & 0x03;
    }

    SPIbuffer[0] = 0xBE;
    SPIbuffer[1] = 0x00;
    SPIbuffer[2] = 0x00;
    SPIbuffer[3] = 0x00;
    SPIbuffer[4] = service[ServiceIndex].ServiceID & 0xFF;
    SPIbuffer[5] = (service[ServiceIndex].ServiceID >> 8) & 0xFF;
    SPIbuffer[6] = (service[ServiceIndex].ServiceID >> 16) & 0xFF;
    SPIbuffer[7] = (service[ServiceIndex].ServiceID >> 24) & 0xFF;
    SPIbuffer[8] = service[ServiceIndex].CompID & 0xFF;
    SPIbuffer[9] = (service[ServiceIndex].CompID >> 8) & 0xFF;
    SPIbuffer[10] = (service[ServiceIndex].CompID >> 16) & 0xFF;
    SPIbuffer[11] = (service[ServiceIndex].CompID >> 24) & 0xFF;
    SPIwrite(SPIbuffer, 12);
    cts();
    SPIread(12);

    if (SPIbuffer[1] == 0x80) {
      servicetype = SPIbuffer[5];
      protectionlevel = SPIbuffer[6];
    }

    SPIbuffer[0] = 0xC0;
    SPIbuffer[1] = 0x00;
    SPIbuffer[2] = 0x00;
    SPIbuffer[3] = 0x00;
    SPIbuffer[4] = service[ServiceIndex].ServiceID & 0xFF;
    SPIbuffer[5] = (service[ServiceIndex].ServiceID >> 8) & 0xFF;
    SPIbuffer[6] = (service[ServiceIndex].ServiceID >> 16) & 0xFF;
    SPIbuffer[7] = (service[ServiceIndex].ServiceID >> 24) & 0xFF;
    SPIwrite(SPIbuffer, 8);
    cts();
    SPIread(26);

    if (SPIbuffer[1] == 0x80) {
      for (byte x = 9; x < 25; x++) PStext[x - 9] = SPIbuffer[x];

      for (int8_t i = 15; i >= 0; i--) {
        if (PStext[i] == ' ' && PStext[i + 1] == '\0') {
          PStext[i] = '\0';
        } else {
          break;
        }
      }

      pty = (SPIbuffer[5] >> 1) & 0x1F;
      ServiceLabelCharset = SPIbuffer[7];

      // Use service ECC if available, otherwise fall back to ensemble ECC
      // Only set once after service start to prevent corruption during marginal signal
      if (ecc == 0) {
        uint8_t srvEcc = SPIbuffer[8];
        serviceHasOwnEcc = (srvEcc != 0);
        ecc = serviceHasOwnEcc ? srvEcc : ensembleEcc;
      }
    }
  }
}

// Wipe every cached metadata field. Called when re-tuning so stale labels,
// PTY etc. from the previous channel don't briefly show up on the display.
void DAB::clearData(void) {
  for (byte x = 0; x < 32; x++) {
    service[x].ServiceID = 0;
    service[x].CompID = 0;
    service[x].ServiceType = 0;
    for (byte y = 0; y < 16; y++) service[x].Label[y] = '\0';
  }
  for (byte x = 0; x < 128; x++) ServiceData[x] = '\0';
}

// Start a DAB tune. Completion is handled cooperatively by Update(); there is
// no multi-second polling loop here.
void DAB::setFreq(uint8_t freq) {
  if (isFm()) return;
  memset(SPIbuffer, 0, sizeof(SPIbuffer));
  DataUpdate -= 1000;
  numberofservices = 0;
  clearData();

  for (byte x = 0; x < 16; x++) {
    EnsembleLabel[x] = '\0';
    PStext[x] = '\0';
  }

  EID[0] = '\0';
  SID[0] = '\0';
  pty = 36;
  ecc = 0;
  ensembleEcc = 0;
  serviceHasOwnEcc = false;
  protectionlevel = 0;
  bitrate = 0;
  dataServiceCheck = 0;
  ServiceStart = false;
  SlideShowInit = false;
  SlideShowAvailable = false;
  SlideShowLength = 0;
  slideshowRamSize = 0;
  SlideShowTransportID = 0;
  SlideShowByteCounter = 0;
  SlideShowHighestSegment = 0;
  SlideShowTotalSegments = 0;
  clearSegmentBuffer();

  signallock = false;
  fic = 0;
  cnr = 0;
  lastStatus0 = 0;
  const si468x::Result result = chip.startDabTune(freq);
  tunePending = result == si468x::Result::Pending || result == si468x::Result::Ok;
  tuneDeadline = millis() + 5000UL;
  fmRsqTimer = 0;
}

void DAB::clearFmData(void) {
  fmValid = false;
  fmAfcRail = false;
  fmPilot = false;
  fmStereoBlend = 0;
  fmRssi = -100;
  fmSnr = 0;
  fmMultipath = 0;
  fmPi = 0;
  fmPty = 0;
  fmPsMask = 0;
  fmRtMask = 0;
  fmRtAb = false;
  memset(fmPs, 0, sizeof(fmPs));
  memset(fmRadioText, 0, sizeof(fmRadioText));
  memset(fmPsWork, ' ', 8); fmPsWork[8] = '\0';
  memset(fmPsCandidate, 0, sizeof(fmPsCandidate));
  memset(fmRtWork, ' ', 64); fmRtWork[64] = '\0';
  memset(PStext, 0, sizeof(PStext));
  memset(ServiceData, 0, sizeof(ServiceData));
  ServiceLabelCharset = 0;
  EnsembleLabelCharset = 0;
  SlideShowAvailable = false;
  SlideShowUpdate = false;
  slideshowRamSize = 0;
  signallock = false;
}

void DAB::setFmFrequency(uint16_t frequency10kHz) {
  if (!isFm()) return;
  if (frequency10kHz < FM_BAND_BOTTOM_10KHZ) frequency10kHz = FM_BAND_BOTTOM_10KHZ;
  if (frequency10kHz > FM_BAND_TOP_10KHZ) frequency10kHz = FM_BAND_TOP_10KHZ;
  clearFmData();
  fmFrequency10kHz = frequency10kHz;
  lastStatus0 = 0;
  const si468x::Result result = chip.startFmTune(frequency10kHz);
  tunePending = result == si468x::Result::Pending || result == si468x::Result::Ok;
  seekPending = false;
  tuneDeadline = millis() + 3000UL;
  fmRsqTimer = fmAcfTimer = fmRdsTimer = 0;
}

bool DAB::startFmSeek(bool up) {
  if (!isFm() || tunePending || chip.busy()) return false;
  clearFmData();
  lastStatus0 = 0;
  const si468x::Result result = chip.startFmSeek(up, true);
  tunePending = result == si468x::Result::Pending || result == si468x::Result::Ok;
  seekPending = tunePending;
  tuneDeadline = millis() + 12000UL;
  fmRsqTimer = fmAcfTimer = fmRdsTimer = 0;
  return tunePending;
}

void DAB::processFmRds(void) {
  si468x::FmRdsGroup group;
  if (chip.fmRdsStatus(group, false, false, true, 100000UL) != si468x::Result::Ok) return;
  if (!group.sync || group.fifoLost) {
    fmPsMask = 0;
    fmRtMask = 0;
    memset(fmPsCandidate, 0, sizeof(fmPsCandidate));
    memset(fmPsWork, ' ', 8); fmPsWork[8] = '\0';
    memset(fmRtWork, ' ', 64); fmRtWork[64] = '\0';
    if (group.fifoLost) chip.fmRdsStatus(group, true, true, true, 100000UL);
    return;
  }

  if (group.piValid) fmPi = group.pi;
  if (group.tpPtyValid) {
    fmPty = group.pty;
    pty = fmPty;
  }
  if (!group.blockUsable(1)) return;

  const uint16_t blockB = group.block[1];
  const uint8_t groupType = static_cast<uint8_t>((blockB >> 12) & 0x0F);
  const bool versionB = (blockB & 0x0800U) != 0;

  if (groupType == 0 && group.blockUsable(3)) {
    const uint8_t segment = blockB & 0x03U;
    fmPsWork[segment * 2] = static_cast<char>(group.block[3] >> 8);
    fmPsWork[segment * 2 + 1] = static_cast<char>(group.block[3] & 0xFF);
    fmPsMask |= static_cast<uint8_t>(1U << segment);
    if (fmPsMask == 0x0F) {
      if (memcmp(fmPsCandidate, fmPsWork, 8) == 0) {
        memcpy(fmPs, fmPsWork, 8); fmPs[8] = '\0';
        memset(PStext, 0, sizeof(PStext));
        memcpy(PStext, fmPs, 8);
      } else {
        memcpy(fmPsCandidate, fmPsWork, 8);
        fmPsCandidate[8] = '\0';
      }
      fmPsMask = 0;
    }
  } else if (groupType == 2) {
    const bool ab = (blockB & 0x0010U) != 0;
    if (ab != fmRtAb) {
      fmRtAb = ab;
      fmRtMask = 0;
      memset(fmRtWork, ' ', 64); fmRtWork[64] = '\0';
    }
    const uint8_t segment = blockB & 0x0FU;
    if (!versionB && group.blockUsable(2) && group.blockUsable(3)) {
      const uint8_t pos = segment * 4;
      fmRtWork[pos] = static_cast<char>(group.block[2] >> 8);
      fmRtWork[pos + 1] = static_cast<char>(group.block[2] & 0xFF);
      fmRtWork[pos + 2] = static_cast<char>(group.block[3] >> 8);
      fmRtWork[pos + 3] = static_cast<char>(group.block[3] & 0xFF);
    } else if (versionB && group.blockUsable(3)) {
      const uint8_t pos = segment * 2;
      fmRtWork[pos] = static_cast<char>(group.block[3] >> 8);
      fmRtWork[pos + 1] = static_cast<char>(group.block[3] & 0xFF);
    } else {
      return;
    }
    fmRtMask |= static_cast<uint16_t>(1U << segment);
    memcpy(fmRadioText, fmRtWork, 64); fmRadioText[64] = '\0';
    for (uint8_t i = 0; i < 64; ++i) {
      if (fmRadioText[i] == '\r' || fmRadioText[i] == '\n') {
        fmRadioText[i] = '\0';
        break;
      }
    }
    for (int8_t i = 63; i >= 0 && fmRadioText[i] == ' '; --i) fmRadioText[i] = '\0';
    memset(ServiceData, 0, sizeof(ServiceData));
    strncpy(ServiceData, fmRadioText, sizeof(ServiceData) - 1);
  }
}

void DAB::updateFm(void) {
  const uint32_t now = millis();
  if (chip.busy()) return;

  const bool stc = (lastStatus0 & si468x::INTERRUPT_STC) != 0;
  const bool timedOut = tunePending && static_cast<int32_t>(now - tuneDeadline) >= 0;
  if ((tunePending && (stc || now - fmRsqTimer >= 100U)) ||
      (!tunePending && now - fmRsqTimer >= 250U)) {
    si468x::FmRsqStatus rsq;
    if (chip.fmRsqStatus(rsq, true, false, timedOut, stc || timedOut, 100000UL) == si468x::Result::Ok) {
      if (rsq.frequency10kHz >= FM_BAND_BOTTOM_10KHZ && rsq.frequency10kHz <= FM_BAND_TOP_10KHZ)
        fmFrequency10kHz = rsq.frequency10kHz;
      fmRssi = rsq.rssi;
      fmSnr = rsq.snr;
      fmMultipath = rsq.multipath;
      cnr = rsq.snr < 0 ? 0 : static_cast<uint8_t>(rsq.snr);
      fic = rsq.multipath;
      fmValid = rsq.valid;
      fmAfcRail = rsq.afcRail;
      signallock = rsq.valid;
      if (stc || timedOut) {
        tunePending = false;
        seekPending = false;
      }
      lastStatus0 &= static_cast<uint8_t>(~si468x::INTERRUPT_STC);
    }
    fmRsqTimer = now;
  }

  if (!tunePending && now - fmAcfTimer >= 500U) {
    si468x::FmAcfStatus acf;
    if (chip.fmAcfStatus(acf, true, 100000UL) == si468x::Result::Ok) {
      fmPilot = acf.pilot;
      fmStereoBlend = acf.stereoBlendPercent;
      audiomode = acf.pilot ? 2 : 1;
    }
    fmAcfTimer = now;
  }

  if (!tunePending && (lastStatus0 & si468x::INTERRUPT_RDS || now - fmRdsTimer >= 80U)) {
    processFmRds();
    lastStatus0 &= static_cast<uint8_t>(~si468x::INTERRUPT_RDS);
    fmRdsTimer = now;
  }
}

// Start audio for service[_index] in the current ensemble. Resets the
// slideshow segment buffer because the new service has its own MOT stream.
void DAB::setService(uint8_t _index) {
  union {
    uint32_t combine;
    uint8_t monoctet[4];
  } u;

  pty = 36;
  bitrate = 0;
  protectionlevel = 0;
  for (byte x = 0; x < 128; x++) ServiceData[x] = '\0';
  SlideShowByteCounter = 0;
  SlideShowLength = 0;
  slideshowRamSize = 0;
  SlideShowAvailable = false;
  SlideShowInit = false;
  ServiceStart = true;
  ServiceIndex = _index;
  ecc = 0;  // Reset so ServiceInfo() picks up the new service's ECC
  serviceHasOwnEcc = false;

  // Reset segment tracking (RAM-only)
  clearSegmentBuffer();
  SlideShowTotalSegments = 0;
  SlideShowHighestSegment = 0;
  SlideShowTransportID = 0;
  SlideShowLastActivity = 0;

  SPIbuffer[0] = 0x81;
  SPIbuffer[1] = 0x00;
  SPIbuffer[2] = 0x00;
  SPIbuffer[3] = 0x00;
  SPIbuffer[4] = service[ServiceIndex].ServiceID & 0xff;
  SPIbuffer[5] = (service[ServiceIndex].ServiceID >> 8) & 0xff;
  SPIbuffer[6] = (service[ServiceIndex].ServiceID >> 16) & 0xff;
  SPIbuffer[7] = (service[ServiceIndex].ServiceID >> 24) & 0xff;
  SPIbuffer[8] = service[ServiceIndex].CompID & 0xff;
  SPIbuffer[9] = (service[ServiceIndex].CompID >> 8) & 0xff;
  SPIbuffer[10] = (service[ServiceIndex].CompID >> 16) & 0xff;
  SPIbuffer[11] = (service[ServiceIndex].CompID >> 24) & 0xff;
  SPIwrite(SPIbuffer, 12);
  u.combine = service[ServiceIndex].ServiceID;

  SID[3] = u.monoctet[0] & 0xF;
  SID[2] = (u.monoctet[0] & 0xF0) >> 4;
  SID[1] = u.monoctet[1] & 0xF;
  SID[0] = (u.monoctet[1] & 0xF0) >> 4;

  for (int i = 0; i < 4; i++) {
    if (SID[i] < 10) {
      SID[i] += '0';
    } else {
      SID[i] += 'A' - 10;
    }
  }
  CurrentServiceID = service[ServiceIndex].ServiceID;
  ServiceInfo();
}

// Periodic driver pump. Called every loop iteration:
//   - Pull any pending service-data (RT / MOT) packets
//   - Discard the slideshow buffer if no segment has arrived in 30 s
//   - Every 500 ms, refresh EnsembleInfo + ServiceInfo and start a data
//     service for TPEG-like data components on first activation
void DAB::Update(void) {
  chip.service();
  if (isFm()) {
    updateFm();
    return;
  }
  if (chip.busy()) return;

  if (tunePending && millis() - fmRsqTimer >= 100U) {
    si468x::DabDigradStatus status;
    const bool stc = (lastStatus0 & si468x::INTERRUPT_STC) != 0;
    if (chip.dabDigradStatus(status, true, false, stc, 100000UL) == si468x::Result::Ok) {
      signallock = status.acquired && status.valid;
      fic = status.ficQuality;
      cnr = status.cnr;
      dabRssi10 = static_cast<int16_t>(status.rssi) * 10;
      if (stc || static_cast<int32_t>(millis() - tuneDeadline) >= 0) tunePending = false;
      lastStatus0 &= static_cast<uint8_t>(~si468x::INTERRUPT_STC);
    }
    fmRsqTimer = millis();
    if (tunePending) return;
  }

  if (!tunePending && millis() - dabSignalTimer >= 250U) {
    si468x::DabDigradStatus status;
    if (chip.dabDigradStatus(status, false, false, false, 100000UL) == si468x::Result::Ok) {
      signallock = status.acquired && status.valid;
      fic = status.ficQuality;
      cnr = status.cnr;
      dabRssi10 = static_cast<int16_t>(status.rssi) * 10;
    }
    dabSignalTimer = millis();
  }

  if (signallock) {
    getServiceData();
  }

  // Timeout for incomplete slideshow collection (30 seconds without new segments)
  if (SlideShowInit && SlideShowLastActivity > 0 && millis() - SlideShowLastActivity > 30000) {
    if (SlideShowDebug) Serial.println("[SLS] Collection timeout, resetting");
    clearSegmentBuffer();
    SlideShowTransportID = 0;
    SlideShowByteCounter = 0;
    SlideShowHighestSegment = 0;
    SlideShowTotalSegments = 0;
    SlideShowLength = 0;
    slideshowRamSize = 0;
    SlideShowAvailable = false;
    SlideShowInit = false;
    SlideShowLastActivity = 0;
  }

  if (millis() - DataUpdate > 500 || !signallock) {
    EnsembleInfo();

    if (signallock) {
      ServiceInfo();
    }
    if (ServiceStart) {
      for (int i = 0; i < numberofservices; i++) {
        if (service[i].ServiceType == 3 && strstr(service[i].Label, "tpeg") == NULL && strstr(service[i].Label, "TPEG") == NULL) {
          if (service[i].CompID != dataServiceCheck) {
            SPIbuffer[0] = 0x81;
            SPIbuffer[1] = 0x01;
            SPIbuffer[2] = 0x00;
            SPIbuffer[3] = 0x00;
            SPIbuffer[4] = service[i].ServiceID & 0xff;
            SPIbuffer[5] = (service[i].ServiceID >> 8) & 0xff;
            SPIbuffer[6] = (service[i].ServiceID >> 16) & 0xff;
            SPIbuffer[7] = (service[i].ServiceID >> 24) & 0xff;
            SPIbuffer[8] = service[i].CompID & 0xff;
            SPIbuffer[9] = (service[i].CompID >> 8) & 0xff;
            SPIbuffer[10] = (service[i].CompID >> 16) & 0xff;
            SPIbuffer[11] = (service[i].CompID >> 24) & 0xff;
            SPIwrite(SPIbuffer, 12);
            dataServiceCheck = service[i].CompID;
            break;
          }
        }
      }
    }
    DataUpdate = millis();
  }
}

// Convert a label/text from the DAB-side character set to UTF-8 for the TFT.
// charset 0 = ETSI EBU Latin (the most common), uses a lookup table via
// charConverter()+convertToUTF8(). All other charsets are assumed UTF-8 already.
// Also auto-detects already-UTF-8 input to avoid double conversion.
String DAB::ASCII(const char* input, uint8_t charset) {
  if (!input) return String();
  String result;
  if (charset != 0) return String(input);

  bool looksLikeUTF8 = false;
  for (size_t i = 0; input[i] != '\0'; i++) {
    uint8_t c = (uint8_t)input[i];

    if ((c & 0xE0) == 0xC0) {
      uint8_t c2 = (uint8_t)input[i + 1];
      if ((c2 & 0xC0) == 0x80) {
        looksLikeUTF8 = true;
        break;
      }
    } else if ((c & 0xF0) == 0xE0) {
      uint8_t c2 = (uint8_t)input[i + 1];
      uint8_t c3 = (uint8_t)input[i + 2];
      if ((c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
        looksLikeUTF8 = true;
        break;
      }
    }
  }

  if (looksLikeUTF8) {
    return String(input);
  }

  wchar_t temp[128];
  charConverter(input, temp, sizeof(temp) / sizeof(wchar_t));
  result = convertToUTF8(temp);

  return result;
}


// qsort() comparator: order services by component ID low byte ascending.
static int compareCompID(const void* a, const void* b) {
  uint32_t compID_a = (*((DABService*)a)).CompID & 0xFF;
  uint32_t compID_b = (*((DABService*)b)).CompID & 0xFF;

  if (compID_a < compID_b) return -1;
  if (compID_a > compID_b) return 1;
  return 0;
}

// Translate an EBU-Latin (ETSI EN 300 401) byte string into Unicode code
// points, handling the DAB-specific shift/escape encodings. Output is a
// wchar_t buffer that convertToUTF8() later serialises as UTF-8.
static void charConverter(const char* input, wchar_t* output, size_t outSize) {
    if (!input || !output || outSize == 0) return;

    size_t i = 0;

    for (size_t dbi = 0; input[dbi] != '\0' && i < outSize - 1; dbi++) {
        uint8_t currentChar = (uint8_t)input[dbi];

        // ----- 2-byte UTF-8 decoding -----
        if ((currentChar & 0xE0) == 0xC0) {
            uint8_t nextChar = (uint8_t)input[dbi + 1];

            if ((nextChar & 0xC0) == 0x80) {
                // decode the Unicode code point
                uint16_t codepoint = ((currentChar & 0x1F) << 6) | (nextChar & 0x3F);
                output[i++] = (wchar_t)codepoint;
                dbi++; // skip the second byte
                continue;
            }
        }

        // ----- Single-byte / fallback -----
        switch (currentChar) {
            case 0x20: output[i] = L' '; break;
            case 0x21 ... 0x5D: output[i] = (wchar_t)currentChar; break;
            case 0x5E: output[i] = L'―'; break;
            case 0x5F: output[i] = L'_'; break;
            case 0x60: output[i] = L'`'; break;
            case 0x61 ... 0x7D: output[i] = (wchar_t)currentChar; break;
            case 0x7E: output[i] = L'¯'; break;
            case 0x7F: output[i] = L' '; break;
            case 0x80: output[i] = L'á'; break;
            case 0x81: output[i] = L'à'; break;
            case 0x82: output[i] = L'é'; break;
            case 0x83: output[i] = L'è'; break;
            case 0x84: output[i] = L'í'; break;
            case 0x85: output[i] = L'ì'; break;
            case 0x86: output[i] = L'ó'; break;
            case 0x87: output[i] = L'ò'; break;
            case 0x88: output[i] = L'ú'; break;
            case 0x89: output[i] = L'ù'; break;
            case 0x8A: output[i] = L'Ñ'; break;
            case 0x8B: output[i] = L'Ç'; break;
            case 0x8C: output[i] = L'Ş'; break;
            case 0x8D: output[i] = L'β'; break;
            case 0x8E: output[i] = L'¡'; break;
            case 0x8F: output[i] = L'Ĳ'; break;
            case 0x90: output[i] = L'â'; break;
            case 0x91: output[i] = L'ä'; break;
            case 0x92: output[i] = L'ê'; break;
            case 0x93: output[i] = L'ë'; break;
            case 0x94: output[i] = L'î'; break;
            case 0x95: output[i] = L'ï'; break;
            case 0x96: output[i] = L'ô'; break;
            case 0x97: output[i] = L'ö'; break;
            case 0x98: output[i] = L'û'; break;
            case 0x99: output[i] = L'ü'; break;
            case 0x9A: output[i] = L'ñ'; break;
            case 0x9B: output[i] = L'ç'; break;
            case 0x9C: output[i] = L'ş'; break;
            case 0x9D: output[i] = L'ǧ'; break;
            case 0x9E: output[i] = L'ı'; break;
            case 0x9F: output[i] = L'ĳ'; break;
            case 0xA0: output[i] = L'Ķ'; break;
            case 0xA1: output[i] = L'Ņ'; break;
            case 0xA2: output[i] = L'©'; break;
            case 0xA3: output[i] = L'Ģ'; break;
            case 0xA4: output[i] = L'Ğ'; break;
            case 0xA5: output[i] = L'ě'; break;
            case 0xA6: output[i] = L'ň'; break;
            case 0xA7: output[i] = L'ő'; break;
            case 0xA8: output[i] = L'Ő'; break;
            case 0xA9: output[i] = L'€'; break;
            case 0xAA: output[i] = L'£'; break;
            case 0xAB: output[i] = L'$'; break;
            case 0xAC: output[i] = L'Ā'; break;
            case 0xAD: output[i] = L'Ē'; break;
            case 0xAE: output[i] = L'Ī'; break;
            case 0xAF: output[i] = L'Ū'; break;
            case 0xB0: output[i] = L'ķ'; break;
            case 0xB1: output[i] = L'ņ'; break;
            case 0xB2: output[i] = L'ļ'; break;
            case 0xB3: output[i] = L'ģ'; break;
            case 0xB4: output[i] = L'ľ'; break;
            case 0xB5: output[i] = L'İ'; break;
            case 0xB6: output[i] = L'ń'; break;
            case 0xB7: output[i] = L'ű'; break;
            case 0xB8: output[i] = L'Ű'; break;
            case 0xB9: output[i] = L'¿'; break;
            case 0xBA: output[i] = L'ľ'; break;
            case 0xBB: output[i] = L'°'; break;
            case 0xBC: output[i] = L'ā'; break;
            case 0xBD: output[i] = L'ē'; break;
            case 0xBE: output[i] = L'ī'; break;
            case 0xBF: output[i] = L'ū'; break;
            case 0xC0: output[i] = L'Á'; break;
            case 0xC1: output[i] = L'À'; break;
            case 0xC2: output[i] = L'É'; break;
            case 0xC3: output[i] = L'È'; break;
            case 0xC4: output[i] = L'Í'; break;
            case 0xC5: output[i] = L'Ì'; break;
            case 0xC6: output[i] = L'Ó'; break;
            case 0xC7: output[i] = L'Ò'; break;
            case 0xC8: output[i] = L'Ú'; break;
            case 0xC9: output[i] = L'Ù'; break;
            case 0xCA: output[i] = L'Ř'; break;
            case 0xCB: output[i] = L'Č'; break;
            case 0xCC: output[i] = L'Š'; break;
            case 0xCD: output[i] = L'Ž'; break;
            case 0xCE: output[i] = L'Ð'; break;
            case 0xCF: output[i] = L'Ŀ'; break;
            case 0xD0: output[i] = L'Â'; break;
            case 0xD1: output[i] = L'Ä'; break;
            case 0xD2: output[i] = L'Ê'; break;
            case 0xD3: output[i] = L'Ë'; break;
            case 0xD4: output[i] = L'Î'; break;
            case 0xD5: output[i] = L'Ï'; break;
            case 0xD6: output[i] = L'Ô'; break;
            case 0xD7: output[i] = L'Ö'; break;
            case 0xD8: output[i] = L'Û'; break;
            case 0xD9: output[i] = L'Ü'; break;
            case 0xDA: output[i] = L'ř'; break;
            case 0xDB: output[i] = L'č'; break;
            case 0xDC: output[i] = L'š'; break;
            case 0xDD: output[i] = L'ž'; break;
            case 0xDE: output[i] = L'đ'; break;
            case 0xDF: output[i] = L'ŀ'; break;
            case 0xE0: output[i] = L'Ã'; break;
            case 0xE1: output[i] = L'Å'; break;
            case 0xE2: output[i] = L'Æ'; break;
            case 0xE3: output[i] = L'Œ'; break;
            case 0xE4: output[i] = L'ŷ'; break;
            case 0xE5: output[i] = L'Ý'; break;
            case 0xE6: output[i] = L'Õ'; break;
            case 0xE7: output[i] = L'Ø'; break;
            case 0xE8: output[i] = L'Þ'; break;
            case 0xE9: output[i] = L'Ŋ'; break;
            case 0xEA: output[i] = L'Ŕ'; break;
            case 0xEB: output[i] = L'Ć'; break;
            case 0xEC: output[i] = L'Ś'; break;
            case 0xED: output[i] = L'Ź'; break;
            case 0xEE: output[i] = L'Ť'; break;
            case 0xEF: output[i] = L'ð'; break;
            case 0xF0: output[i] = L'ã'; break;
            case 0xF1: output[i] = L'å'; break;
            case 0xF2: output[i] = L'æ'; break;
            case 0xF3: output[i] = L'œ'; break;
            case 0xF4: output[i] = L'ŵ'; break;
            case 0xF5: output[i] = L'ý'; break;
            case 0xF6: output[i] = L'õ'; break;
            case 0xF7: output[i] = L'ø'; break;
            case 0xF8: output[i] = L'þ'; break;
            case 0xF9: output[i] = L'ŋ'; break;
            case 0xFA: output[i] = L'ŕ'; break;
            case 0xFB: output[i] = L'ć'; break;
            case 0xFC: output[i] = L'ś'; break;
            case 0xFD: output[i] = L'ź'; break;
            case 0xFE: output[i] = L'ť'; break;
            case 0xFF: output[i] = L' '; break;
            default: output[i] = L'?'; break;
        }
        i++;
    }
    output[i] = L'\0';
}

// Substring helper that operates on code-points (not bytes) so cutting a
// UTF-8 string at index N doesn't slice a multi-byte sequence in half.
static String extractUTF8Substring(const String& utf8String, size_t start, size_t length) {
  String substring;
  size_t utf8Length = utf8String.length();
  size_t utf8Index = 0;
  size_t charIndex = 0;

  while (utf8Index < utf8Length && charIndex < start + length) {
    uint8_t currentByte = utf8String.charAt(utf8Index);
    uint8_t numBytes = 0;

    if (currentByte < 0x80) {
      numBytes = 1;
    } else if ((currentByte >> 5) == 0x6) {
      numBytes = 2;
    } else if ((currentByte >> 4) == 0xE) {
      numBytes = 3;
    } else if ((currentByte >> 3) == 0x1E) {
      numBytes = 4;
    }

    if (charIndex >= start) {
      substring += utf8String.substring(utf8Index, utf8Index + numBytes);
    }

    utf8Index += numBytes;
    charIndex++;
  }

  return substring;
}

// Encode the wchar_t code points produced by charConverter() into a UTF-8
// String suitable for the TFT and the serial protocol.
static String convertToUTF8(const wchar_t* input) {
  String output;
  while (*input) {
    uint32_t unicode = *input;
    if (unicode < 0x80) {
      output += (char)unicode;
    } else if (unicode < 0x800) {
      output += (char)(0xC0 | (unicode >> 6));
      output += (char)(0x80 | (unicode & 0x3F));
    } else if (unicode < 0x10000) {
      output += (char)(0xE0 | (unicode >> 12));
      output += (char)(0x80 | ((unicode >> 6) & 0x3F));
      output += (char)(0x80 | (unicode & 0x3F));
    } else {
      output += (char)(0xF0 | (unicode >> 18));
      output += (char)(0x80 | ((unicode >> 12) & 0x3F));
      output += (char)(0x80 | ((unicode >> 6) & 0x3F));
      output += (char)(0x80 | (unicode & 0x3F));
    }
    input++;
  }
  return output;

}
