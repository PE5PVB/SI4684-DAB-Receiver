// Driver for the Skyworks (formerly Silicon Labs) SI4684 DAB+ receiver chip.
//
// Communicates over SPI (pins set up in begin()). Boots the chip by uploading
// the rom-patch + firmware blob, then exposes high-level operations:
//   - tune to a DAB Band III channel
//   - enumerate the services in the current ensemble
//   - select a service and start audio
//   - decode Dynamic Label / Radiotext, PTY, ECC, time, etc.
//   - reassemble the current MOT slideshow image in RAM
//
// All chip status comes back via the SPI status byte + a 4 KB SPI rx buffer
// (`SPIbuffer` in si4684.cpp). Every command goes via cts() to wait for the
// "command ready" bit before the next byte is written.

#ifndef si4684_h
#define si4684_h

#include "Arduino.h"
#include <SPI.h>
#include <cstring>
#include <climits>

struct DABFrequencyLabel_DAB {
  uint32_t frequency;
  const char* label;
};

const DABFrequencyLabel_DAB DABfrequencyTable_DAB[] = {
  { 174928,  "5A"}, { 176640,  "5B"}, { 178352,  "5C"}, { 180064,  "5D"},
  { 181936,  "6A"}, { 183648,  "6B"}, { 185360,  "6C"}, { 187072,  "6D"},
  { 188928,  "7A"}, { 190640,  "7B"}, { 192352,  "7C"}, { 194064,  "7D"},
  { 195936,  "8A"}, { 197648,  "8B"}, { 199360,  "8C"}, { 201072,  "8D"},
  { 202928,  "9A"}, { 204640,  "9B"}, { 206352,  "9C"}, { 208064,  "9D"},
  { 209936, "10A"}, { 211648, "10B"}, { 213360, "10C"}, { 215072, "10D"},
  { 216928, "11A"}, { 218640, "11B"}, { 220352, "11C"}, { 222064, "11D"},
  { 223936, "12A"}, { 225648, "12B"}, { 227360, "12C"}, { 229072, "12D"},
  { 230784, "13A"}, { 232496, "13B"}, { 234208, "13C"}, { 235776, "13D"},
  { 237488, "13E"}, { 239200, "13F"}
};


static const char* const ProtectionText[] {
  "",
  "UEP-1",
  "UEP-2",
  "UEP-3",
  "UEP-4",
  "UEP-5",
  "EEP-A1",
  "EEP-A2",
  "EEP-A3",
  "EEP-A4",
  "EEP-B1",
  "EEP-B2",
  "EEP-B3",
  "EEP-B4"
};

static const char* const AudioModeText[] {
  "Dual",
  "Mono",
  "Stereo",
  "Joint stereo"
};

static const char* const ServiceTypeText[] {
  "TPEG",
  "Data",
  "FIDC",
  "MSC",
  "DAB+",
  "DAB",
  "FIC",
  "XPAD",
  "-",
  ""
};

// One row of the in-memory service table populated by ServiceInfo().
// Only one CompID per service is stored; secondary components (e.g. data
// streams in the same service) are intentionally ignored.
typedef struct _Services {
  uint32_t  ServiceID;
  uint32_t  CompID;
  char      Label[17];
  byte    ServiceType;
} DABService;

enum RadioMode : uint8_t {
  RADIO_MODE_DAB = 0,
  RADIO_MODE_FM = 1
};

static const uint16_t FM_BAND_BOTTOM_10KHZ = 8750;
static const uint16_t FM_BAND_TOP_10KHZ = 10800;
static const uint16_t FM_STEP_10KHZ = 10;  // 100 kHz European raster

class DAB {
  public:
    bool begin(uint8_t SSpin, RadioMode requestedMode = RADIO_MODE_DAB);
    bool panic(void);
    bool ServiceStart;
    bool signallock;
    bool SlideShowAvailable;
    bool SlideShowDebug;
    bool SlideShowUpdate;
    bool SlideShowUpdate2;
    char EID[5];
    char EnsembleLabel[17];
    char PStext[17];
    char ServiceData[128];
    char SID[5];
    char* getChipID(void);
    char* getFirmwareVersion(void);
    const char* getChannel(uint8_t freq);
    DABService service[32];
    String ASCII(const char* input, uint8_t charset);
    uint16_t bitrate;
    uint16_t ecc;
    uint16_t ensembleEcc;
    bool serviceHasOwnEcc;
    int16_t getRSSI(void);
    uint16_t samplerate;
    uint16_t Year;
    uint32_t getFreq(uint8_t freq);
    uint32_t SlideShowLength;
    uint8_t audiomode;
    uint8_t cnr;
    uint8_t Days;
    uint8_t EnsembleLabelCharset;
    uint8_t fic;
    uint8_t getFIC(void);
    uint8_t Hours;
    uint8_t Minutes;
    uint8_t Months;
    uint8_t numberofservices = 0;
    uint8_t protectionlevel;
    uint8_t pty;
    uint8_t Seconds;
    uint8_t ServiceIndex;
    uint8_t ServiceLabelCharset;
    uint8_t servicetype;
    uint16_t fmFrequency10kHz;
    int8_t fmRssi;
    int8_t fmSnr;
    uint8_t fmMultipath;
    uint16_t fmPi;
    uint8_t fmPty;
    bool fmValid;
    bool fmAfcRail;
    bool fmPilot;
    uint8_t fmStereoBlend;
    char fmPs[9];
    char fmRadioText[65];
    void clearData(void);
    void EnsembleInfo(void);
    void getServiceData(void);
    void ServiceInfo(void);
    void setFreq(uint8_t freq_index);
    void setFmFrequency(uint16_t frequency10kHz);
    bool startFmSeek(bool up);
    void setService(uint8_t index);
    void Update(void);
    void vol(uint8_t vol);
    RadioMode mode(void) const { return activeMode; }
    bool isFm(void) const { return activeMode == RADIO_MODE_FM; }
    bool isTunePending(void) const { return tunePending; }
    const uint8_t* slideshowData(void) const { return slideshowSegBuf; }
    uint32_t slideshowSize(void) const { return SlideShowAvailable ? slideshowRamSize : 0; }

  private:
    bool SlideShowInit;
    RadioMode activeMode;
    bool tunePending;
    bool seekPending;
    uint32_t tuneDeadline;
    uint32_t fmRsqTimer;
    uint32_t fmAcfTimer;
    uint32_t fmRdsTimer;
    uint32_t dabSignalTimer;
    int16_t dabRssi10;
    uint8_t fmPsMask;
    uint16_t fmRtMask;
    uint16_t fmRtSeenMask;
    bool fmRtAb;
    bool fmRtVersionB;
    bool fmRtVersionKnown;
    char fmPsWork[9];
    char fmPsCandidate[9];
    char fmRtWork[65];
    char ChipType[7];
    char FirmwVersion[6];
    uint32_t componentID;
    uint32_t CurrentServiceID;
    uint32_t dataServiceCheck;
    uint32_t serviceID;
    uint32_t SlideShowByteCounter;
    uint32_t slideshowRamSize;

    // Segment buffering for faster slideshow assembly
    uint8_t SlideShowSegmentBitmap[32];  // Bitmap for up to 256 segments
    uint8_t SlideShowTotalSegments;       // Total segments expected (0 = unknown)
    uint8_t SlideShowHighestSegment;      // Highest segment number seen
    uint16_t SlideShowTransportID;        // Current transport ID
    uint32_t SlideShowLastActivity;       // millis() of last new segment received

    // One 40 KB MOT buffer with adaptive fixed slots. Services seen in the
    // field use 512-, 1024- or 2000-byte DSRV blocks. Large slots use the
    // actual block size (up to 2048 B), preserving all 40960 reserved bytes.
    static const uint8_t  SLS_MAX_SEGMENTS   = 80;
    static const uint16_t SLS_BASE_SEG_SIZE  = 512;
    static const uint16_t SLS_MAX_SEG_SIZE   = 2048;
    static const size_t   SLS_BUFFER_BYTES   = 80U * 512U;
    uint8_t  slideshowSegBuf[SLS_BUFFER_BYTES];
    uint16_t slideshowSegLen[SLS_MAX_SEGMENTS];
    uint16_t slideshowSlotSize;
    void beginSlideshowReception(void);
    bool ensureSlideshowSlotSize(uint16_t dataLength);
    void clearSegmentBuffer(void);

    void assembleSlideshow(void);
    bool allSegmentsReceived(void);
    void clearFmData(void);
    void processFmRds(void);
    void updateFm(void);
};

#endif
