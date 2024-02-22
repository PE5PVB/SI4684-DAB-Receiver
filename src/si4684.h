#ifndef si4684_h
#define si4684_h
#include "SPIFFS.h"
#include "Arduino.h"
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

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

typedef struct _Services {
  uint32_t  ServiceID;
  uint32_t  CompID;
  char      Label[16];
} DABService;

class DAB {
  public:
    void begin(uint8_t SSpin, uint8_t RSTpin);
    void EnsembleInfo(void);
    void ServiceInfo(void);
    void getServiceData(void);
    void Update(void);
    void setFreq(uint8_t freq_index);
    void setService(uint8_t index);
    void setService(uint32_t _serviceid, uint32_t _componentid);
    uint32_t getFreq(uint8_t freq);
    const char* getChannel(uint8_t freq);
    String ASCII(const char* input);

    DABService service[32];
    char* getChipID(void);
    char* getFirmwareVersion(void);
    uint16_t getRSSI(void);
    uint8_t getFIC(void);
    String getEnsembleLabel(void);
    bool signallock;
    char ServiceData[128];
    uint16_t bitrate;
    uint16_t samplerate;
    uint8_t audiomode;
    uint8_t servicetype;
    uint8_t protectionlevel;
    uint8_t pty;
    uint16_t ecc;
    uint16_t Year;
    uint8_t Months;
    uint8_t Days;
    uint8_t Hours;
    uint8_t Minutes;
    uint8_t Seconds;
    uint8_t numberofservices = 0;
    uint8_t cnr;
    uint8_t fic;
    char EID[5];
    char SID[5];
    uint8_t ServiceIndex;
    bool ServiceStart;
    uint32_t SlideShowLength;
    bool SlideShowAvailable;
    bool SlideShowUpdate;
    bool isJPG;
    bool isPNG;

  private:
    void ParseServiceList(void);
    bool ParseServiceList(bool first, uint8_t* data, uint16_t len);
    uint32_t  serviceID;
    uint32_t  componentID;
    char ChipType[7];
    char FirmwVersion[6];
    char EnsembleLabel[16];
    uint32_t SlideShowByteCounter;
    uint32_t SlideShowLengthOld;
    bool SlideShowNew;
    bool SlideShowInit;
};
#endif