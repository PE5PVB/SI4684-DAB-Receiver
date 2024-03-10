#ifndef si4684_cpp
#define si4684_cpp

#include "si4684.h"
#include "Si468xROM.h"
#include "firmware.h"
#include <SPI.h>
#include <cstring>

unsigned char SPIbuffer[4096];
unsigned long DataUpdate = 0;
bool EnsembleInfoSet;
uint8_t slaveSelectPin;

static void SPIwrite(unsigned char *data, uint32_t length);
static void SPIread(uint16_t length);
static void cts(void);
static void Set_Property(uint16_t property, uint16_t value);
static String convertToUTF8(const wchar_t* input);
static String extractUTF8Substring(const String & utf8String, size_t start, size_t length);
static void RDScharConverter(const char* input, wchar_t* output, size_t size);
static int compareCompID(const void *a, const void *b);

char* DAB::getChipID(void) {
  SPIbuffer[0] = 0x08;
  SPIbuffer[1] = 0x00;
  SPIwrite(SPIbuffer, 2);
  cts();
  SPIread(23);
  itoa((SPIbuffer[10] << 8) + SPIbuffer[9], ChipType + 2, 10);
  ChipType[0] = 'S';
  ChipType[1] = 'I';
  ChipType[6] = '\0';
  return ChipType;
}

char* DAB::getFirmwareVersion(void) {
  SPIbuffer[0] = 0x12;
  SPIbuffer[1] = 0x00;
  SPIwrite(SPIbuffer, 2);
  cts();
  SPIread(12);
  char buffer[5];
  itoa(SPIbuffer[5], buffer, 10);
  FirmwVersion[0] = buffer[0];
  FirmwVersion[1] = '.';
  itoa(SPIbuffer[6], buffer, 10);
  FirmwVersion[2] = buffer[0];
  FirmwVersion[3] = '.';
  itoa(SPIbuffer[7], buffer, 10);
  FirmwVersion[4] = buffer[0];
  FirmwVersion[5] = '\0';
  return FirmwVersion;
}

bool DAB::panic(void) {
  SPIbuffer[0] = 0x09;
  SPIbuffer[1] = 0x00;
  SPIwrite(SPIbuffer, 2);
  cts();
  SPIread(6);
  if (SPIbuffer[5] == 0x09) return true; else return false;
}

uint16_t DAB::getRSSI(void) {
  SPIbuffer[0] = 0xE5;
  SPIbuffer[1] = 0x00;
  SPIwrite(SPIbuffer, 2);
  cts();
  SPIread(6);
  int16_t rssi = static_cast<int16_t>((static_cast<int32_t>(SPIbuffer[5] + (SPIbuffer[6] << 8)) * 10) / 256);
  if (rssi > 1200) rssi = 1200;
  if (rssi < -1000) rssi = -1000;
  return rssi;
}

uint32_t DAB::getFreq(uint8_t freq) {
  return DABfrequencyTable_DAB[freq].frequency;
}

const char* DAB::getChannel(uint8_t freq) {
  return DABfrequencyTable_DAB[freq].label;
}

static void SPIwrite(unsigned char *data, uint32_t length) {
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite (slaveSelectPin, LOW);
  SPI.transfer(data, length);
  digitalWrite (slaveSelectPin, HIGH);
  SPI.endTransaction();
}

static void SPIread(uint16_t length) {
  for (uint16_t i = 0; i < length + 1; i++) SPIbuffer[i] = 0;
  SPIwrite(SPIbuffer, length + 1);
}

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
  bool timeout = false;
  uint16_t countdown = 1000;
  while (!(SPIbuffer[1] & 0x80)) {
    delay(4);
    for (byte i = 0; i < 5; i++) SPIbuffer[i] = 0;
    SPIwrite(SPIbuffer, 5);
    countdown--;
    if (countdown == 0) {
      timeout = true;
      break;
    }
  }

  if (SPIbuffer[1] & 0x40) {
    for (byte i = 0; i < 5; i++) SPIbuffer[i] = 0;
    if (timeout) SPIbuffer[5] |= (1 << 7);
  }
}

bool DAB::begin(uint8_t SSpin) {
  memset(SPIbuffer, 0, sizeof(SPIbuffer));
  if (LittleFS.exists("/temp.img")) LittleFS.remove("/temp.img");
  slaveSelectPin = SSpin;
  pinMode(slaveSelectPin, OUTPUT);                                        // Configure SPI
  digitalWrite(slaveSelectPin, HIGH);
  SPI.begin(14, 16, 13, 15);
  delay(3);
  SPIbuffer[0] = 0x09;
  SPIbuffer[1] = 0x00;
  SPIwrite(SPIbuffer, 2);
  cts();
  SPIread(6);

  bool result;
  if (SPIbuffer[1] != 0x80) {
    result = false;
  } else {
    result = true;
  }

  if (SPIbuffer[1] != 2) {
    SPIbuffer[0] = 0x01;                                                    // POWER_UP
    SPIbuffer[1] = 0x00;
    SPIbuffer[2] = 0x17;
    SPIbuffer[3] = 0x48;
    SPIbuffer[4] = 0x00;
    SPIbuffer[5] = 0xf8;
    SPIbuffer[6] = 0x24;
    SPIbuffer[7] = 0x01;
    SPIbuffer[8] = 0x1F;
    SPIbuffer[9] = 0x10;
    SPIbuffer[10] = 0x00;
    SPIbuffer[11] = 0x00;
    SPIbuffer[12] = 0x00;
    SPIbuffer[13] = 0x18;
    SPIbuffer[14] = 0x00;
    SPIbuffer[15] = 0x00;
    SPIwrite(SPIbuffer, 16);
    cts();

    delayMicroseconds(20);

    SPIbuffer[0] = 0x06;                                                    // LOAD_INIT
    SPIbuffer[1] = 0x00;
    SPIwrite(SPIbuffer, 2);
    cts();

    uint32_t index = 0;                                                     // Write bootloader
    for (uint16_t i = 0; index < sizeof(rom_patch_016); i++) {
      SPIbuffer[0] = 0x04;
      SPIbuffer[1] = 0x00;
      SPIbuffer[2] = 0x00;
      SPIbuffer[3] = 0x00;
      for (uint16_t j = 4; j < 128 && index < sizeof(rom_patch_016); j++, index++) SPIbuffer[j] = pgm_read_byte_near(rom_patch_016 + index);
      SPIwrite(SPIbuffer, 128);
      cts();
    }

    delay(4);
    SPIbuffer[0] = 0x06;                                                    // LOAD_INIT
    SPIbuffer[1] = 0x00;
    SPIwrite(SPIbuffer, 2);
    cts();

    index = 0;
    uint16_t i = 0;
    while (index < sizeof(firmware)) {
      SPIbuffer[0] = 0x04;
      SPIbuffer[1] = 0x00;
      SPIbuffer[2] = 0x00;
      SPIbuffer[3] = 0x00;
      for (i = 4; (i < 2048) && (index < sizeof(firmware)); i++) {
        SPIbuffer[i] = pgm_read_byte_near(firmware + index);
        index++;
      }
      SPIwrite(SPIbuffer, i);
      cts();
    }

    SPIbuffer[0] = 0x07;                                                    // BOOT
    SPIbuffer[1] = 0x00;
    SPIwrite(SPIbuffer, 2);
    cts();

    SPIbuffer[0] = 0xB8;                                                    // Write DAB frequencyplan
    SPIbuffer[1] = 0x26;
    SPIbuffer[2] = 0x00;
    SPIbuffer[3] = 0x00;
    for (i = 0; i < sizeof(DABfrequencyTable_DAB) / sizeof(DABFrequencyLabel_DAB); i++) {
      SPIbuffer[4 + (i * 4)] = DABfrequencyTable_DAB[i].frequency & 0xFF;
      SPIbuffer[5 + (i * 4)] = (DABfrequencyTable_DAB[i].frequency >> 8) & 0xFF;
      SPIbuffer[6 + (i * 4)] = (DABfrequencyTable_DAB[i].frequency >> 16) & 0xFF;
      SPIbuffer[7 + (i * 4)] = (DABfrequencyTable_DAB[i].frequency >> 24) & 0xFF;
    }
    SPIwrite(SPIbuffer, 4 + (38 * 4));
    cts();

    Set_Property(0x0200, 0x8000);                                            // Set properties
    Set_Property(0x0202, 0x1600);
    Set_Property(0x0800, 0x0003);
    Set_Property(0x1710, 0xF8A0);
    Set_Property(0x1711, 0x01E0);
    Set_Property(0x8100, 0x0001);
    Set_Property(0x8101, 0x0064);
    Set_Property(0xB200, 0x0000);
    Set_Property(0xB201, 0x0080);
    Set_Property(0xB301, 0x0000);
    Set_Property(0xB302, 0x0000);
    Set_Property(0xB303, 0x0000);
    Set_Property(0xB400, 0x0003);
    Set_Property(0xB401, 0x0002);
    Set_Property(0xB500, 0x0000);
  }

  return result;
}

void DAB::EnsembleInfo(void) {
  SPIbuffer[0] = 0xB2;                                                      // Get signalstatus
  SPIbuffer[1] = 0x09;
  SPIwrite(SPIbuffer, 2);
  cts();
  SPIread(19);
  fic = SPIbuffer[9];
  cnr = SPIbuffer[10];
  if (fic > 0) signallock = true; else signallock = false;
  if (signallock) {
    SPIbuffer[0] = 0x80;                                                    // Get servicelist
    SPIbuffer[1] = 0x00;
    SPIwrite(SPIbuffer, 2);
    cts();
    SPIread(8);

    if (SPIbuffer[6] != 0 && SPIbuffer[5] + (SPIbuffer[6] << 8) + 6 < sizeof(SPIbuffer)) {
      SPIread(SPIbuffer[5] + (SPIbuffer[6] << 8) + 6);
      uint8_t numberofcomponents;

      numberofservices = SPIbuffer[9];
      uint16_t offset = 13;

      for (uint8_t i = 0; i < numberofservices; i++) {
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


      qsort(service, numberofservices, sizeof(DABService), compareCompID);

      if (CurrentServiceID != service[ServiceIndex].ServiceID) {
        for (byte x = 0; x < numberofservices; x++) {
          if (CurrentServiceID == service[x].ServiceID) {
            ServiceIndex = x;
            break;
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

      if (SPIbuffer[5] != 0 && SPIbuffer[6] != 0 && SPIbuffer[5] != 0xFF && SPIbuffer[6] != 0xFF) {
        EnsembleInfoSet = true;
        EID[0] = (SPIbuffer[5] + (SPIbuffer[6] << 8) >> 12) & 0xF;
        EID[1] = (SPIbuffer[5] + (SPIbuffer[6] << 8) >> 8) & 0xF;
        EID[2] = (SPIbuffer[5] + (SPIbuffer[6] << 8) >> 4) & 0xF;
        EID[3] = SPIbuffer[5] + (SPIbuffer[6] << 8) & 0xF;
        EID[4] = '\0';
        for (int i = 0; i < 4; i++) {
          if (EID[i] < 10) {
            EID[i] += '0';
          } else {
            EID[i] += 'A' - 10;
          }
        }

        for (uint8_t i = 0; i < 16; i++) EnsembleLabel[i] = static_cast<char>(SPIbuffer[7 + i]);

        for (int8_t i = 15; i >= 0; i--) {
          if (EnsembleLabel[i] == ' ' && EnsembleLabel[i + 1] == '\0') {
            EnsembleLabel[i] = '\0';
          } else {
            break;
          }
        }
        ecc = SPIbuffer[23];
      } else {
        EnsembleInfoSet = false;
      }
    }
    SPIbuffer[0] = 0xBC;
    SPIbuffer[1] = 0x00;
    SPIwrite(SPIbuffer, 2);
    cts();
    SPIread(11);

    if (SPIbuffer[0] == 0xe8) {
      Year = SPIbuffer[5] + ((uint16_t)SPIbuffer[6] << 8);
      Months = SPIbuffer[7];
      Days = SPIbuffer[8];
      Hours = SPIbuffer[9];
      Minutes = SPIbuffer[10];
      Seconds = SPIbuffer[11];
    }
  }
}

void DAB::getServiceData(void) {
  uint32_t byte_count = 0;
  uint32_t byte_number = 0;

  if (bitRead(SPIbuffer[1], 4)) {
    SPIbuffer[0] = 0x84;
    SPIbuffer[1] = 0x01;
    SPIwrite(SPIbuffer, 2);
    cts();
    SPIread(20);

    if (SPIbuffer[1] == 0x80 && (SPIbuffer[19] + (SPIbuffer[20] << 8)) + 24 < sizeof(SPIbuffer)) {
      if ((SPIbuffer[19] + (SPIbuffer[20] << 8)) > 0) {
        SPIread((SPIbuffer[19] + (SPIbuffer[20] << 8)) + 24);
        byte_count = SPIbuffer[19] + (SPIbuffer[20] << 8);


        // Read Radiotext
        if (((SPIbuffer[8] >> 6) & 0x03) == 0x02 && !((SPIbuffer[25] & 0x10) == 0x10)) {
          for (byte_number = 0; byte_number < byte_count; byte_number++) ServiceData[byte_number] = (char)SPIbuffer[27 + byte_number];
          ServiceData[byte_number] = '\0';

          // Read Slideshow initialisation and finish Slideshow
        } else if (((SPIbuffer[8] >> 6) & 0x03) == 0x01 && SPIbuffer[27] == 0x80 && SPIbuffer[28] == 0x00 && SPIbuffer[29] == 0x12 && byte_count < 65) {
          if (SlideShowLength == SlideShowByteCounter && SlideShowInit) {
            // Open source file
            File sourceFile = LittleFS.open("/temp.img", "rb");
            if (!sourceFile) return;

            // Remove existing slideshow file
            LittleFS.remove("/slideshow.img");

            // Create and copy to destination file
            File destinationFile = LittleFS.open("/slideshow.img", "wb");
            if (destinationFile) {
              while (sourceFile.available()) {
                destinationFile.write(sourceFile.read());
              }
              destinationFile.close();
            }

            // Close source file and remove it
            sourceFile.close();
            LittleFS.remove("/temp.img");

            // Update variables
            SlideShowLengthOld = SlideShowLength;
            SlideShowUpdate = true;
            SlideShowAvailable = true;
            SlideShowInit = false;
          }

          SlideShowLength = (((uint16_t)SPIbuffer[35] << 12) | ((uint16_t)SPIbuffer[36] << 4) | ((uint16_t)SPIbuffer[37]  >> 4)) & 0x00FFFF;

          if (SlideShowLength > 0 && SlideShowLength != SlideShowLengthOld) {
            SlideShowNew = true;
            SlideShowInit = true;
            SlideShowByteCounter = 0;
            if (LittleFS.exists("/temp.img")) LittleFS.remove("/temp.img");
          } else {
            SlideShowNew = false;
          }

          // Read Slideshow packets
        } else if (((SPIbuffer[8] >> 6) & 0x03) == 0x01 && (SPIbuffer[27] == 0x00 || SPIbuffer[27] == 0x80) && SPIbuffer[29] == 0x12 && SlideShowNew) {
          File slideshowFile;
          if (SlideShowInit) {
            if (SPIbuffer[34] == 0xff && SPIbuffer[35] == 0xd8 && SPIbuffer[36] == 0xff && (SPIbuffer[37] == 0xe0 || SPIbuffer[37] == 0xe1)) {
              isJPG = true;
              isPNG = false;
            }
            if (SPIbuffer[34] == 0x89 && SPIbuffer[35] == 0x50 && SPIbuffer[36] == 0x4e && SPIbuffer[37] == 0x47 && SPIbuffer[38] == 0x0d && SPIbuffer[39] == 0x0a && SPIbuffer[40] == 0x1a && SPIbuffer[41] == 0x0a) {
              isPNG = true;
              isJPG = false;
            }

            if (!LittleFS.exists("/temp.img")) {
              slideshowFile = LittleFS.open("/temp.img", "wb");
              if (!slideshowFile) return;
              slideshowFile.close();
            }

            slideshowFile = LittleFS.open("/temp.img", "ab");
            if (!slideshowFile) return;

            for (byte_number = 0; byte_number < byte_count - 11; byte_number++) {
              slideshowFile.write(SPIbuffer[34 + byte_number]);
              SlideShowByteCounter++;
            }
            slideshowFile.close();
          }
        }
      }
    }
  }
}



void DAB::ServiceInfo(void) {
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

  for (byte x; x < numberofservices; x++) {
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

    if (SPIbuffer[5] != 0x00 && SPIbuffer[5] != 0x04 && SPIbuffer[5] != 0x05) {
      service[x].Audioservice = false;
    } else {
      service[x].Audioservice = true;
    }
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
  }
}

void DAB::clearData(void) {
  for (byte x = 0; x < 32; x++) {
    service[x].ServiceID = 0;
    service[x].CompID = 0;
    service[x].Audioservice = true;
    for (byte y = 0; y < 16; y++) service[x].Label[y] = '\0';
  }
  for (byte x = 0; x < 128; x++) ServiceData[x] = '\0';
}

void DAB::setFreq(uint8_t freq) {
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
  protectionlevel = 0;
  bitrate = 0;
  ServiceStart = false;
  SlideShowInit = false;
  SlideShowAvailable = false;
  isJPG = false;
  isPNG = false;
  SPIbuffer[0] = 0xB0;
  SPIbuffer[1] = 0x00;
  SPIbuffer[2] = freq;
  SPIbuffer[3] = 0x00;
  SPIbuffer[4] = 0x00;
  SPIbuffer[5] = 0x00;
  SPIwrite(SPIbuffer, 6);

  uint16_t timeout = 1000;
  while (bitRead(SPIbuffer[1], 0) == false) {
    delay(20);
    for (byte i = 0; i < 5; i++) SPIbuffer[i] = 0;
    SPIwrite(SPIbuffer, 5);
    timeout--;
    if (timeout <= 0) break;
  }
}

void DAB::setService(uint8_t _index) {
  pty = 36;
  bitrate = 0;
  protectionlevel = 0;
  for (byte x = 0; x < 128; x++) ServiceData[x] = '\0';
  SlideShowByteCounter = 0;
  SlideShowLength = 0;
  SlideShowLengthOld = 0;
  SlideShowAvailable = false;
  SlideShowNew = true;
  SlideShowInit = false;
  isJPG = false;
  isPNG = false;
  ServiceStart = true;
  ServiceIndex = _index;
  if (LittleFS.exists("/temp.img")) LittleFS.remove("/temp.img");

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
  SID[0] = (service[ServiceIndex].ServiceID >> 12) & 0xF;
  SID[1] = (service[ServiceIndex].ServiceID >> 8) & 0xF;
  SID[2] = (service[ServiceIndex].ServiceID >> 4) & 0xF;
  SID[3] = service[ServiceIndex].ServiceID & 0xF;
  SID[4] = '\0';
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

void DAB::Update(void) {
  if (signallock) getServiceData();
  if (millis() - DataUpdate > 1000 || !signallock) {
    EnsembleInfo();
    if (ServiceStart && signallock) ServiceInfo();
    DataUpdate = millis();
  }
}

String DAB::ASCII(const char* input) {
  wchar_t temp[256] = L"";
  RDScharConverter(input, temp, sizeof(temp) / sizeof(wchar_t));
  String temp2 = convertToUTF8(temp);
  temp2 = extractUTF8Substring(temp2, 0, temp2.length());
  return temp2;
}


static int compareCompID(const void *a, const void *b) {
  uint32_t compID_a = (*((DABService *)a)).CompID & 0xFF;
  uint32_t compID_b = (*((DABService *)b)).CompID & 0xFF;

  if (compID_a < compID_b) return -1;
  if (compID_a > compID_b) return 1;
  return 0;
}

static void RDScharConverter(const char* input, wchar_t* output, size_t size) {
  for (size_t i = 0; i < size - 1; i++) {
    char currentChar = input[i];
    switch (currentChar) {
      case 0x20: output[i] = L' '; break;
      case 0x21 ... 0x5D: output[i] = static_cast<wchar_t>(currentChar); break;
      case 0x5E: output[i] = L'―'; break;
      case 0x5F: output[i] = L'_'; break;
      case 0x60: output[i] = L'`'; break;
      case 0x61 ... 0x7d: output[i] = static_cast<wchar_t>(currentChar); break;
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
      case 0xA0: output[i] = L'ª'; break;
      case 0xA1: output[i] = L'α'; break;
      case 0xA2: output[i] = L'©'; break;
      case 0xA3: output[i] = L'‰'; break;
      case 0xA4: output[i] = L'Ǧ'; break;
      case 0xA5: output[i] = L'ě'; break;
      case 0xA6: output[i] = L'ň'; break;
      case 0xA7: output[i] = L'ő'; break;
      case 0xA8: output[i] = L'π'; break;
      case 0xA9: output[i] = L'€'; break;
      case 0xAA: output[i] = L'£'; break;
      case 0xAB: output[i] = L'$'; break;
      case 0xAC: output[i] = L'←'; break;
      case 0xAD: output[i] = L'↑'; break;
      case 0xAE: output[i] = L'→'; break;
      case 0xAF: output[i] = L'↓'; break;
      case 0xB0: output[i] = L'º'; break;
      case 0xB1: output[i] = L'¹'; break;
      case 0xB2: output[i] = L'²'; break;
      case 0xB3: output[i] = L'³'; break;
      case 0xB4: output[i] = L'±'; break;
      case 0xB5: output[i] = L'İ'; break;
      case 0xB6: output[i] = L'ń'; break;
      case 0xB7: output[i] = L'ű'; break;
      case 0xB8: output[i] = L'µ'; break;
      case 0xB9: output[i] = L'¿'; break;
      case 0xBA: output[i] = L'÷'; break;
      case 0xBB: output[i] = L'°'; break;
      case 0xBC: output[i] = L'¼'; break;
      case 0xBD: output[i] = L'½'; break;
      case 0xBE: output[i] = L'¾'; break;
      case 0xBF: output[i] = L'§'; break;
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
      case 0xEE: output[i] = L'Ŧ'; break;
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
      case 0xFE: output[i] = L'ŧ'; break;
      case 0xFF: output[i] = L' '; break;
    }
  }
  output[size - 1] = L'\0';
}

static String extractUTF8Substring(const String & utf8String, size_t start, size_t length) {
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
#endif