// Driver for the Skyworks (formerly Silicon Labs) SI4684 DAB+ receiver chip.
//
// Communicates over a dedicated ESP32 HSPI/SPI2 bus prepared once at startup.
// begin() starts/reuses the requested Si4684 image, uploading the ROM patch and
// firmware blob when needed, then exposes high-level operations:
//   - tune to a DAB Band III channel
//   - enumerate the services in the current ensemble
//   - select a service and start audio
//   - decode Dynamic Label / Radiotext, PTY, ECC, time, etc.
//   - reassemble the current MOT slideshow image in RAM
//
// All chip traffic is serialized by the shared Si468x command engine. Runtime
// FM and DAB operations are pumped cooperatively from Update(); the 4 KB
// `SPIbuffer` in si4684.cpp remains the persistent reply workspace used by the
// existing service-list and DLS/MOT parsers.

#ifndef si4684_h
#define si4684_h

#include "Arduino.h"
#include <SPI.h>
#include <cstring>
#include <climits>
#include "FmRegion.h"
#include "fm_features.h"
#include "dab_scheduler_policy.h"
#include "fm_af_policy.h"
#include "dab_service_switch_policy.h"
#include "dab_spi_discovery.h"
#include "dab_spi_probe.h"
#include "dab_diagnostics.h"
#include "i2s_audio.h"

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
// streams in the same service) remain in the separate component inventory.
typedef struct _Services {
  uint32_t  ServiceID;
  uint32_t  CompID;
  char      Label[17];
  byte    ServiceType;
} DABService;

uint8_t DabServiceLabelCharset(uint8_t serviceIndex);

enum RadioMode : uint8_t {
  RADIO_MODE_DAB = 0,
  RADIO_MODE_FM = 1
};

enum RadioControlMode : uint8_t {
  RADIO_CTRL_DETECT = 0,
  RADIO_CTRL_INTB,
  RADIO_CTRL_POLL
};

class DAB {
  public:
    // Reserve persistent radio RAM while the internal heap is still contiguous.
    // Call once immediately after the shared PNG/JPEG decoder workspace.
    bool prepareRuntimeMemory(void);
    // Start the dedicated HSPI/SPI2 host exactly once. External peripheral
    // resets do not require (and must not trigger) another SPI.begin().
    bool prepareSpiBus(void);
    bool begin(uint8_t SSpin, RadioMode requestedMode = RADIO_MODE_DAB);
    // Call only after the shared SI4684/TFT reset pulse has physically
    // completed. Cancels host-side command state without touching the bus.
    void onHardwareResetComplete(void);
    bool panic(void);
    bool ServiceStart;
    bool signallock;
    bool SlideShowAvailable;
    bool SlideShowDebug;
    bool SlideShowVerbose;
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
    void ASCIIToBuffer(const char* input, uint8_t charset, char* output, size_t outputSize);
    uint16_t bitrate;
    uint16_t ecc;
    uint16_t ensembleEcc;
    bool serviceHasOwnEcc;
    int16_t getRSSI(void);
    uint32_t signalSampleGeneration(void) const { return rfSampleGeneration; }
    uint32_t timeSampleGeneration(void) const { return dabTimeGeneration; }
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
    bool fmTp;
    bool fmTa;
    bool fmValid;
    bool fmAfcRail;
    bool fmPilot;
    uint8_t fmStereoBlend;
    char fmPs[9];
    char fmRadioText[65];
    fm_features::AfList fmAf;
    bool fmCtValid;
    fm_features::ClockTime fmCt;
    void clearData(void);
    void EnsembleInfo(void);
    void getServiceData(void);
    void ServiceInfo(void);
    void setFreq(uint8_t freq_index);
    void setFmRegion(uint8_t region, bool applyNow = true);
    void setFmSeekSensitivity(uint8_t profile, bool applyNow = true);
    void setFmFrequency(uint16_t frequency10kHz);
    bool startFmSeek(bool up);
    void setFmAfEnabled(bool enabled);
    void setFmAfAllowed(bool allowed);
    bool isFmAfProbing(void) const;
    bool takeFmAfSwitch(uint16_t& frequency10kHz);
    void setService(uint8_t index);
    // Always pumps an in-flight transport operation. When allowNewWork is
    // false no new metadata, DSRV or status command is scheduled.
    void Update(bool allowNewWork = true);
    void vol(uint8_t vol);
    RadioMode mode(void) const { return activeMode; }
    bool isFm(void) const { return activeMode == RADIO_MODE_FM; }
    uint8_t fmRegion(void) const { return activeFmRegion; }
    const FmRegionProfile& fmProfile(void) const { return fmRegionProfile(activeFmRegion); }
    bool isRbds(void) const { return fmProfile().rbds; }
    RadioControlMode controlMode(void) const;
    const char* controlModeName(void) const;
    const char* intbHardwareName(void) const;
    bool isTunePending(void) const { return tunePending; }
    bool isFmPsStable(void) const {
      return isFm() && fmPsSeenMask == 0x0FU && fmPsConfirmedMask == 0x0FU;
    }
    // Set only after repeated command CTS timeouts. Signal/ensemble lock state
    // never participates in this decision.
    bool transportStalled(void) const { return dabTransportStalled; }
    // True only after a complete, structurally valid DAB service-list reply
    // has been parsed for the currently tuned ensemble. This lets the UI make
    // one-shot restore decisions without guessing from numberofservices.
    bool isDabServiceListReady(void) const { return !isFm() && dabServiceListReady; }
    uint32_t serviceListGeneration(void) const { return dabServiceListGeneration; }
    void requestServiceListRefresh(void) {
      if (!isFm()) dabServiceListRefreshPending = true;
    }
    bool isDabAudioInfoValid(void) const {
      return !isFm() && dabAudioInfoValid;
    }
    // Separate from audio/SLS ownership. No automatic SPI endpoint START.
    const dab_spi::Result& spiDiscovery(void) const { return dabSpiDiscovery; }
    // Advertised SPI presence in this multiplex, not decoded programme data.
    bool spiPresent(void) const { return dabSpiPresent; }
    // Configure the next Si4684 application boot; never reconfigure live audio.
    void setI2sAudioMode(I2sAudioMode mode);
    bool requestSpiProbe(void);
    void stopSpiProbe(void);
    bool isDabServiceMetadataReady(void) const;
    bool isDabServiceStartPending(void) const;
    bool isDabServiceLabelCurrent(void) const {
      return !isFm() && ServiceStart && dabServiceLabelValid;
    }
    const uint8_t* slideshowData(void) const { return slideshowSegBuf; }
    uint32_t slideshowSize(void) const { return SlideShowAvailable ? slideshowRamSize : 0; }
    size_t slideshowCapacity(void) const { return SLS_BUFFER_BYTES; }
    uint32_t slideshowReceivedBytes(void) const { return SlideShowByteCounter; }
    uint32_t slideshowExpectedBytes(void) const { return SlideShowLength; }
    uint16_t slideshowTransportId(void) const {
      return lastCompletedTransportIdValid ? lastCompletedTransportId : 0U;
    }
    // Release the published single-buffer image after the UI has completed
    // its hash/decode step. Until then incoming MOT objects are drained but
    // must not overwrite slideshowSegBuf.
    void acknowledgeSlideshow(void);

  private:
    enum class DabCommand : uint8_t {
      None,
      Tune,
      TuneStatus,
      DsrvHeader,
      EventStatus,
      SignalStatus,
      ServiceListHeader,
      EnsembleInfo,
      Time,
      ServiceType,
      AudioInfo,
      CurrentSubchannelInfo,
      CurrentServiceInfo,
      ComponentInfo,
      SpiProbeStart,
      SpiProbeStop,
      StopDataService,
      StopService,
      StartService,
      StartDataService
    };

    bool SlideShowInit;
    RadioMode activeMode;
    uint8_t activeFmRegion = static_cast<uint8_t>(FmRegion::Europe);
    uint8_t activeFmSeekSensitivity = 1;
    bool tunePending;
    bool seekPending;
    uint32_t tuneDeadline;
    uint32_t fmRsqTimer;
    uint32_t fmAcfTimer;
    uint32_t fmRdsTimer;
    uint32_t dabSignalTimer;
    uint32_t rfSampleGeneration = 0;
    uint32_t dabTimeGeneration = 0;
    int16_t dabRssi10;
    uint8_t fmPsSeenMask;
    uint8_t fmPsConfirmedMask;
    uint16_t fmRtMask;
    uint16_t fmRtSeenMask;
    bool fmRtAb;
    bool fmRtVersionB;
    bool fmRtVersionKnown;
    char fmPsWork[9];
    char fmRtWork[65];
    fm_features::ClockValidator fmCtValidator;
    enum class FmAfState : uint8_t {
      Idle, WeakHold, CandidateTune, CandidatePi, RestoreTune, Cooldown
    };
    FmAfState fmAfState = FmAfState::Idle;
    bool fmAfEnabled = false;
    bool fmAfAllowed = false;
    bool fmAfSwitchPending = false;
    uint8_t fmAfCandidateIndex = 0;
    uint16_t fmAfOriginalFrequency = 0;
    uint16_t fmAfCandidateFrequency = 0;
    uint16_t fmAfExpectedPi = 0;
    uint16_t fmAfCandidatePi = 0;
    int8_t fmAfOriginalRssi = -100;
    int8_t fmAfOriginalSnr = 0;
    int8_t fmAfCandidateRssi = -100;
    int8_t fmAfCandidateSnr = 0;
    bool fmAfCandidateValid = false;
    uint8_t fmAfCandidateSamples = 0;
    uint32_t fmAfStateSince = 0;
    uint32_t fmAfSweepDeadline = 0;
    uint32_t fmAfStationSince = 0;
    uint32_t fmAfCooldownUntil = 0;
    char ChipType[7];
    char FirmwVersion[6];
    uint32_t componentID;
    uint32_t CurrentServiceID;
    uint32_t dataServiceCheck;
    uint32_t serviceID;
    uint32_t SlideShowByteCounter;
    uint32_t slideshowRamSize;

    // Segment buffering for faster slideshow assembly
    uint8_t SlideShowSegmentBitmap[32];   // Bitmap for segment numbers 0..255
    uint16_t SlideShowTotalSegments;      // 0 = unknown, otherwise 1..256
    uint8_t SlideShowHighestSegment;
    uint16_t SlideShowTransportID;
    bool SlideShowTransportIDValid;
    uint16_t lastCompletedTransportId;
    bool lastCompletedTransportIdValid;
    bool slideshowPublishedPending;
    bool SlideShowLastSegmentValid;
    uint8_t SlideShowLastSegment;
    uint32_t SlideShowLastActivity;       // millis() of last new segment received

    // Cooperative DAB command scheduler. startCommand() retains the reply
    // pointer until CTS, so every async reply is stored in the global,
    // persistent SPIbuffer rather than in a temporary stack array.
    DabCommand dabCommand = DabCommand::None;
    uint32_t dabCommandRequestId = 0;
    uint32_t dabGeneration = 1;
    uint32_t dabCommandGeneration = 1;
    uint32_t dabTuneRequestId = 0;
    uint32_t dabWaitingTuneRequestId = 0;
    uint8_t dabRequestedFrequency = 0;
    bool dabTuneRequestPending = false;
    bool dabWaitingForStc = false;
    uint32_t dabTuneDeadlineMs = 0;
    uint8_t dabTuneBusyRetries = 0;
    uint32_t dabTuneRetryNotBeforeMs = 0;

    uint32_t dabRequestedServiceId = 0;
    uint32_t dabRequestedComponentId = 0;
    uint32_t dabActiveServiceId = 0;
    uint32_t dabActiveComponentId = 0;
    uint32_t dabCommandServiceId = 0;
    uint32_t dabCommandComponentId = 0;
    dab_switch::Controller dabSwitch;
    bool dabServiceRequestPending = false;
    bool dabActiveServiceValid = false;
    bool dabServiceListReady = false;
    uint32_t dabServiceListGeneration = 0;

    bool dabSignalRefreshPending = false;
    bool dabServiceListRefreshPending = false;
    bool dabEnsembleRefreshPending = false;
    bool dabTimeRefreshPending = false;
    bool dabAudioRefreshPending = false;
    bool dabAudioInfoValid = false;
    uint8_t dabAudioInfoRetryCount = 0;
    uint32_t dabAudioInfoNotBeforeMs = 0;
    bool dabCurrentSubchannelRefreshPending = false;
    bool dabCurrentServiceRefreshPending = false;
    uint8_t dabServiceTypeScanIndex = 0;
    // Separate diagnostic inventory: the public station table keeps only the
    // first component. 32 services x at most 14 components (AN649 NUM_COMP).
    struct UaService {
      uint32_t sid;
      uint16_t first;
      uint8_t count;
      uint8_t info1; // P/D + PTY/linking, occupies the former structure padding
    };
    static_assert(sizeof(UaService) == 8U, "compact retained service inventory");
    UaService dabUaServices[32] = {};
    uint32_t dabUaComponents[32U * 14U] = {};
    dab_spi::Identity dabSpiIdentities[32U * 14U] = {};
    dab_spi::Result dabSpiDiscovery;
    bool dabSpiDiscoveryDirty = true;
    dab_probe::Probe dabSpiProbe;
    uint32_t dabSpiProbeLastRouteMs[4] = {};
    uint32_t dabSpiProbeLastSummaryMs = 0;
    uint32_t dabSpiProbeSuppressedRoutes = 0;
    uint32_t dabSpiProbeCtsStart = 0;
    uint32_t dabSpiProbeOverflowStart = 0;
    uint32_t dabSpiProbeUartDropStart = 0;
    void logSpiProbeSummary(const char* phase);
    uint16_t dabUaCommandIndex = 0;
    dab_spi::Scan dabUaScan;
    dab_spi::RefreshCycle dabUaRefresh;
    bool dabSpiPresent = false;
    bool dabServiceLabelValid = false;
    uint32_t dabSpiDiscoveryGeneration = 0;
    uint8_t dabUaSummaryStage = 3;
    dab_diag::XpadReadback dabXpad;
    dab_diag::RawLog dabRawUaLog;
    void configureDabXpad(void);
    bool dabXpadLogPending = false;
    uint8_t dabUaListLogService = 0;
    uint8_t dabUaListLogComponent = 0;
    bool dabUaListHeaderPending = true;
    bool dabUaListLogPending = false;
    void resetDabSpiDiscovery(bool preservePresence = false, bool logInventory = true);
    void refreshDabSpiDiscovery(void);
    uint16_t dabUaComponentCount = 0;
    uint8_t dabUaServiceCount = 0;
    uint16_t dabUaListVersion = 0;
    bool dabUaInventoryValid = false;
    bool dabUaPacketLogPending = false;
    void invalidateDabInventory(void);
    uint16_t dabUaScanIndex = 0;
    bool dabUaDebugWasEnabled = false;
    uint32_t dabUaNextQueryMs = 0;
    uint32_t dabUaNextLogMs = 0;
    uint32_t dabUaCommandListGeneration = 0;
    // Copy only the small UA reply while SPIbuffer remains free for DSRV.
    uint8_t dabUaRaw[255] = {};
    char dabUaLabel[17] = {};
    uint8_t dabUaLanguage = 0;
    uint8_t dabUaCount = 0;
    uint8_t dabUaBytes = 0;
    uint16_t dabUaOffset = 0;
    uint16_t dabUaDataOffset = 0;
    uint16_t dabUaRemaining = 0;
    uint32_t dabUaReplySid = 0;
    uint32_t dabUaReplyCid = 0;
    bool dabUaLogPending = false;
    bool dabUaHeaderPending = false;
    bool dabUaEntryPending = false;
    void logDabUserApplications(uint32_t now);
    uint32_t dabSignalNextDueMs = 0;
    uint32_t dabEnsembleNextDueMs = 0;
    uint32_t dabTimeNextDueMs = 0;
    uint32_t dabAudioNextDueMs = 0;
    uint32_t dabCurrentServiceNextDueMs = 0;
    uint32_t dabCurrentSubchannelNextDueMs = 0;
    uint32_t dabLastLowPriorityCommandMs = 0;

    uint32_t dabDataServiceId = 0;
    uint32_t dabDataComponentId = 0;
    uint32_t dabActiveDataServiceId = 0;
    uint32_t dabActiveDataComponentId = 0;
    bool dabActiveDataServiceValid = false;
    uint8_t dabDsrvBurstCount = 0;
    uint8_t dabConsecutiveCtsTimeouts = 0;
    bool dabTransportStalled = false;

    // One RAM-only MOT buffer. Received segments are kept packed in ascending
    // SegmentNumber order, so all 51200 bytes are usable payload capacity even
    // when segment sizes vary or segments arrive out of order.
    static const uint16_t SLS_MAX_SEGMENTS   = 256;
    static const uint16_t SLS_MAX_SEG_SIZE   = 2048;
    static const size_t   SLS_BUFFER_BYTES   = 50U * 1024U;
    // Allocated once from internal 8-bit heap by prepareRuntimeMemory(), before
    // TFT sprites/fonts. A static 50 KiB member overflows the classic ESP32
    // dram0 linker segment in this build.
    uint8_t* slideshowSegBuf = nullptr;
    uint16_t slideshowSegLen[SLS_MAX_SEGMENTS];
    void beginSlideshowReception(void);
    bool storeSlideshowSegment(uint8_t segmentNumber,
                               const uint8_t* data,
                               uint16_t dataLength);
    void clearSegmentBuffer(void);
    void resetSlideshowCollector(void);
    void lockSlideshowTransport(uint16_t transportId);

    void assembleSlideshow(void);
    bool allSegmentsReceived(void);
    void applyFmRegionProperties(void);
    void clearFmData(void);
    void processFmRds(void);
    void updateFm(void);
    bool processFmAf(uint32_t now);
    bool startFmAfTune(uint16_t frequency10kHz, FmAfState nextState,
                       uint32_t now);
    void cancelFmAf(bool userRetune);
    bool startDabCommand(DabCommand operation, uint8_t command,
                         const uint8_t* args, uint16_t argLength,
                         uint16_t replyLength = 0,
                         uint32_t timeoutUs = 1000000UL);
    void finishDabCommand(void);
    void scheduleNextDabCommand(void);
    void resetDabPeriodicDeadlines(uint32_t now);
    void resetDabServiceDeadlines(uint32_t now);
    void updateDabPeriodicRequests(uint32_t now);
    void invalidateDabAudioInfo(uint32_t notBeforeMs = 0U);
    dab_scheduler::CtsTimeoutClass recordDabCtsResult(bool timedOut);
    void parseDabServiceListReply(uint16_t replyLength);
    void requestDabDataServiceEvaluation(void);
    bool resolveDabDataService(void);
    void processDabSwitchState(uint32_t now);
    void setDabSwitchState(dab_switch::State state);
};

#endif
