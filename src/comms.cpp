// Serial-line control protocol implementation.
// Runtime path is intentionally Arduino-String-free: RX, diff caches and
// charset conversion use fixed storage so serial traffic cannot fragment heap.

#include "comms.h"
#include <cctype>
#include <cstdlib>
#include <cstring>

unsigned long signalMillis;
unsigned int interval = 100;
byte ServiceIndexOld;
byte dabfreqOld;
bool connectedSerial;

namespace {
constexpr size_t kRxLineBytes = 128;
constexpr size_t kUtf8LabelBytes = 64;
constexpr size_t kServiceDataUtf8Bytes = 385;

char rxLine[kRxLineBytes];
size_t rxLen = 0;
bool rxOverflow = false;
uint32_t rxLastByteMs = 0;
constexpr uint32_t kRxIdleCommitMs = 100U;

struct ServiceListSnapshot {
  bool valid;
  bool signalLock;
  uint8_t count;
  uint8_t ensembleCharset;
  uint8_t serviceCharset;
  char eid[5];
  char ensembleLabel[17];
  struct Item {
    uint8_t type;
    char label[17];
  } items[32];
};

struct ServiceInfoSnapshot {
  bool valid;
  bool started;
  uint8_t serviceIndex;
  uint8_t componentId;
  char sid[5];
  uint8_t pty;
  uint8_t protection;
  uint16_t samplerate;
  uint16_t bitrate;
  uint8_t audioMode;
};

ServiceListSnapshot serviceListOld = {};
ServiceInfoSnapshot serviceInfoOld = {};
char serviceDataOld[kServiceDataUtf8Bytes] = {};
bool serviceDataOldValid = false;

void resetOutputCaches() {
  serviceListOld.valid = false;
  serviceInfoOld.valid = false;
  serviceDataOldValid = false;
  serviceDataOld[0] = '\0';
}

void DataPrint(const char* data) {
  if (data) diagSerial.print(data);
}

char* trimInPlace(char* text) {
  if (!text) return text;
  while (*text && isspace(static_cast<unsigned char>(*text))) ++text;
  char* end = text + strlen(text);
  while (end > text && isspace(static_cast<unsigned char>(end[-1]))) --end;
  *end = '\0';
  return text;
}

void upperInPlace(char* text) {
  if (!text) return;
  for (; *text; ++text)
    *text = static_cast<char>(toupper(static_cast<unsigned char>(*text)));
}

char hashCommand(const char* command) {
  if (!command) return 0;
  if (strcmp(command, "ENABLE") == 0) return 'E';
  if (strcmp(command, "TUNE") == 0) return 'T';
  if (strcmp(command, "SERVICE") == 0) return 'S';
  if (strcmp(command, "INTERVAL") == 0) return 'I';
  return 0;
}

long parseInteger(const char* value) {
  if (!value) return 0;
  char* end = nullptr;
  const long parsed = strtol(value, &end, 10);
  return end == value ? 0 : parsed;
}

void captureServiceList(ServiceListSnapshot& out) {
  memset(&out, 0, sizeof(out));
  out.valid = true;
  out.signalLock = radio.signallock;
  out.count = radio.numberofservices > 32U ? 32U : radio.numberofservices;
  out.ensembleCharset = radio.EnsembleLabelCharset;
  out.serviceCharset = radio.ServiceLabelCharset;
  memcpy(out.eid, radio.EID, sizeof(out.eid));
  memcpy(out.ensembleLabel, radio.EnsembleLabel, sizeof(out.ensembleLabel));
  for (uint8_t i = 0; i < out.count; ++i) {
    out.items[i].type = radio.service[i].ServiceType;
    memcpy(out.items[i].label, radio.service[i].Label, sizeof(out.items[i].label));
  }
}

bool serviceListChanged() {
  ServiceListSnapshot current;
  captureServiceList(current);
  if (!serviceListOld.valid || memcmp(&current, &serviceListOld, sizeof(current)) != 0) {
    serviceListOld = current;
    return true;
  }
  return false;
}

void emitServiceList() {
  char label[kUtf8LabelBytes];
  radio.ASCIIToBuffer(radio.EnsembleLabel, radio.EnsembleLabelCharset,
                      label, sizeof(label));
  diagSerial.printf("$L=COUNT=%u,ENSEMBLE=%s,%s;SERVICES=",
                static_cast<unsigned>(radio.numberofservices), radio.EID, label);

  if (radio.signallock) {
    const uint8_t count = radio.numberofservices > 32U ? 32U : radio.numberofservices;
    for (uint8_t i = 0; i < count; ++i) {
      radio.ASCIIToBuffer(radio.service[i].Label, radio.ServiceLabelCharset,
                          label, sizeof(label));
      diagSerial.printf("%u,%u,%s", static_cast<unsigned>(i),
                    static_cast<unsigned>(radio.service[i].ServiceType), label);
      if (i + 1U < count) diagSerial.print(';');
    }
  } else {
    diagSerial.print('0');
  }
  diagSerial.print('\n');
}

void captureServiceInfo(ServiceInfoSnapshot& out) {
  memset(&out, 0, sizeof(out));
  out.valid = true;
  out.started = radio.ServiceStart;
  out.serviceIndex = radio.ServiceIndex;
  if (!radio.ServiceStart || radio.ServiceIndex >= 32U) return;
  out.componentId = static_cast<uint8_t>(radio.service[radio.ServiceIndex].CompID & 0xFFU);
  memcpy(out.sid, radio.SID, sizeof(out.sid));
  out.pty = radio.pty;
  out.protection = radio.protectionlevel;
  out.samplerate = radio.samplerate;
  out.bitrate = radio.bitrate;
  out.audioMode = radio.audiomode;
}

bool serviceInfoChanged() {
  ServiceInfoSnapshot current;
  captureServiceInfo(current);
  if (!serviceInfoOld.valid || memcmp(&current, &serviceInfoOld, sizeof(current)) != 0) {
    serviceInfoOld = current;
    return true;
  }
  return false;
}

void emitServiceInfo() {
  if (!radio.ServiceStart || radio.ServiceIndex >= 32U) {
    DataPrint("$I=ID=0;SID=0;PTY=0;PROTECTION=0;SAMPLERATE=0;BITRATE=0;AUDIO=0\n");
    return;
  }
  diagSerial.printf("$I=ID=%u;SID=%s;PTY=%u;PROTECTION=%u;SAMPLERATE=%u;BITRATE=%u;AUDIO=%u\n",
                static_cast<unsigned>(radio.service[radio.ServiceIndex].CompID & 0xFFU),
                radio.SID,
                static_cast<unsigned>(radio.pty),
                static_cast<unsigned>(radio.protectionlevel),
                static_cast<unsigned>(radio.samplerate),
                static_cast<unsigned>(radio.bitrate),
                static_cast<unsigned>(radio.audiomode));
}

void emitServiceDataIfChanged() {
  char current[kServiceDataUtf8Bytes];
  radio.ASCIIToBuffer(radio.ServiceData, radio.ServiceLabelCharset,
                      current, sizeof(current));
  if (!serviceDataOldValid || strcmp(current, serviceDataOld) != 0) {
    diagSerial.print("$D=RT=");
    diagSerial.print(current);
    diagSerial.print('\n');
    snprintf(serviceDataOld, sizeof(serviceDataOld), "%s", current);
    serviceDataOldValid = true;
  }
}

void doMOTShow() {
  if (!radio.SlideShowAvailable || !radio.SlideShowUpdate2) return;
  const uint8_t* image = radio.slideshowData();
  const size_t size = radio.slideshowSize();
  uint8_t type = 3;
  if (image && size >= 8) {
    if (image[0] == 0x89 && image[1] == 0x50 && image[2] == 0x4E &&
        image[3] == 0x47 && image[4] == 0x0D && image[5] == 0x0A &&
        image[6] == 0x1A && image[7] == 0x0A) type = 2;
    else if (image[0] == 0xFF && image[1] == 0xD8 && image[2] == 0xFF) type = 1;
  }
  diagSerial.printf("$M=SLIDESHOW=%u\n", static_cast<unsigned>(type));
  radio.SlideShowUpdate2 = false;
}

void doEnableConnection() {
  diagSerial.printf("*ENABLE=1,%s,%s/%s\n", VERSION,
                radio.getChipID(), radio.getFirmwareVersion());
  DataPrint(":MODE=3,3-3\n");
  diagSerial.printf("*INTERVAL=%u\n", interval);

  const size_t count = sizeof(DABfrequencyTable_DAB) / sizeof(DABfrequencyTable_DAB[0]);
  diagSerial.printf(":FREQ=%u,", static_cast<unsigned>(count));
  for (size_t i = 0; i < count; ++i) {
    diagSerial.printf("%u:%lu,%s", static_cast<unsigned>(i),
                  static_cast<unsigned long>(DABfrequencyTable_DAB[i].frequency),
                  DABfrequencyTable_DAB[i].label);
    if (i + 1U < count) diagSerial.print(';');
  }
  diagSerial.print('\n');

  if (radio.ServiceStart)
    diagSerial.printf("*SERVICE=%u\n", static_cast<unsigned>(radio.ServiceIndex));
  diagSerial.printf("*TUNE=%u\n", static_cast<unsigned>(dabfreq));
  DataPrint("$M=SLIDESHOW=0\n");

  resetOutputCaches();
  if (radio.SlideShowAvailable) radio.SlideShowUpdate2 = true;
  else DataPrint("$M=SLIDESHOW=0\n");
}

void processCommandLine(char* line) {
  char* input = trimInPlace(line);
  if (!input || *input == '\0') return;

  char* equals = strchr(input, '=');
  if (!equals) {
    upperInPlace(input);
    if (strcmp(input, "DEBUG") == 0) {
      diagnosticDebug = !diagnosticDebug;
      radio.SlideShowDebug = diagnosticDebug;
      if (!diagnosticDebug) radio.SlideShowVerbose = false;
      diagSerial.printf("[DEBUG] diagnostics %s\n", diagnosticDebug ? "ON" : "OFF");
    } else if (strcmp(input, "DEBUG SPI") == 0) {
      diagnosticDebug = true;
      radio.SlideShowDebug = true;
      if (!radio.requestSpiProbe())
        diagSerial.println("[DAB/SPI/TEST] refused: requires stable DAB, complete SPI discovery, distinct SID/CID and idle probe");
    } else if (strcmp(input, "DEBUG SPI STOP") == 0) {
      radio.stopSpiProbe();
    } else if (strcmp(input, "DEBUG VERBOSE") == 0 ||
               strcmp(input, "DEBUG SLSV") == 0) {
      diagnosticDebug = true;
      radio.SlideShowDebug = true;
      radio.SlideShowVerbose = !radio.SlideShowVerbose;
      diagSerial.printf("[DEBUG] SLS per-segment verbose %s\n",
                    radio.SlideShowVerbose ? "ON" : "OFF");
    } else {
      DataPrint("#2\n");
    }
    return;
  }

  *equals = '\0';
  char* command = trimInPlace(input);
  char* value = trimInPlace(equals + 1);
  upperInPlace(command);
  const long intValue = parseInteger(value);

  if (!connectedSerial) {
    if (strcmp(command, "ENABLE") == 0) {
      if (intValue == 0) {
        DataPrint("*ENABLE=0\n");
        connectedSerial = false;
      } else if (intValue == 1) {
        connectedSerial = true;
        doEnableConnection();
      } else {
        DataPrint("#1\n");
      }
    }
    return;
  }

  switch (hashCommand(command)) {
    case 'E':
      if (intValue == 0) {
        DataPrint("*ENABLE=0\n");
        connectedSerial = false;
      } else if (intValue == 1) {
        doEnableConnection();
      } else DataPrint("#1\n");
      break;

    case 'I':
      if (intValue > 0 && intValue <= 500) {
        interval = static_cast<unsigned int>(intValue);
        diagSerial.printf("*INTERVAL=%u\n#0\n", interval);
      } else DataPrint("#1\n");
      break;

    case 'T':
      if (radio.isFm()) {
        DataPrint("#1\n");
      } else if (intValue >= 0 &&
                 static_cast<size_t>(intValue) <
                     sizeof(DABfrequencyTable_DAB) / sizeof(DABfrequencyTable_DAB[0])) {
        radio.ServiceStart = false;
        radio.ServiceIndex = 0;
        radio.clearData();
        memset(_serviceName, 0, 17);
        dabfreq = static_cast<byte>(intValue);
        radio.setFreq(dabfreq);
        if (SlideShowView || ChannelListView || ShowServiceInformation || menu) {
          SlideShowView = false;
          ChannelListView = false;
          ShowServiceInformation = false;
          menu = false;
          BuildDisplay();
        } else ShowFreq();
        diagSerial.printf("#0\n*TUNE=%u\n", static_cast<unsigned>(dabfreq));
        DataPrint("$M=SLIDESHOW=0\n");
      } else DataPrint("#1\n");
      break;

    case 'S':
      if (radio.isFm()) {
        DataPrint("#1\n");
      } else if (intValue >= 0 && intValue < radio.numberofservices) {
        radio.ServiceIndex = static_cast<uint8_t>(intValue);
        radio.setService(radio.ServiceIndex);
        store = true;
        diagSerial.printf("#0\n*SERVICE=%u\n", static_cast<unsigned>(radio.ServiceIndex));
        DataPrint("$M=SLIDESHOW=0\n");
      } else DataPrint("#1\n");
      break;

    default:
      DataPrint("#2\n");
      break;
  }
}

void finishSerialLine() {
  if (rxOverflow) {
    DataPrint("#1\n");
  } else if (rxLen > 0U) {
    rxLine[rxLen] = '\0';
    processCommandLine(rxLine);
  }
  rxLen = 0;
  rxOverflow = false;
}

void consumeSerialInput() {
  while (Serial.available() > 0) {
    const int raw = Serial.read();
    if (raw < 0) break;
    const char c = static_cast<char>(raw);

    // Accept every common Serial Monitor line-ending mode. CRLF is handled as
    // one command because the following empty terminator is simply ignored.
    if (c == '\r' || c == '\n') {
      if (rxLen > 0U || rxOverflow) finishSerialLine();
      return;  // preserve at-most-one-command-per-loop behaviour
    }

    rxLastByteMs = millis();
    if (rxOverflow) continue;
    if (rxLen + 1U < sizeof(rxLine)) rxLine[rxLen++] = c;
    else rxOverflow = true;
  }

  // Some terminals use "No line ending". Commit a received command after a
  // short idle period so DEBUG/ENABLE remain usable without Arduino String or
  // readStringUntil() timeouts.
  if ((rxLen > 0U || rxOverflow) &&
      static_cast<uint32_t>(millis() - rxLastByteMs) >= kRxIdleCommitMs) {
    finishSerialLine();
  }
}
}  // namespace

void Communication(void) {
  consumeSerialInput();
  if (!connectedSerial) return;

  if (radio.ServiceIndex != ServiceIndexOld) {
    if (radio.ServiceStart)
      diagSerial.printf("*SERVICE=%u\n", static_cast<unsigned>(radio.ServiceIndex));
    DataPrint("$M=SLIDESHOW=0\n");
    ServiceIndexOld = radio.ServiceIndex;
  }

  if (dabfreq != dabfreqOld) {
    diagSerial.printf("*TUNE=%u\n", static_cast<unsigned>(dabfreq));
    DataPrint("$M=SLIDESHOW=0\n");
    dabfreqOld = dabfreq;
  }

  if (serviceListChanged()) emitServiceList();
  if (serviceInfoChanged()) emitServiceInfo();
  emitServiceDataIfChanged();

  if (millis() - signalMillis > interval) {
    diagSerial.printf("$S=SIGNAL=%d.%d,LOCK=%u,CNR=%u,FIC=%u\n",
                  static_cast<int>(SignalLevel / 10),
                  static_cast<int>(SignalLevel % 10),
                  radio.signallock ? 1U : 0U,
                  static_cast<unsigned>(radio.cnr),
                  static_cast<unsigned>(radio.fic));
    signalMillis = millis();
  }

  doMOTShow();
}
