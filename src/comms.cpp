#include "comms.h"

unsigned long signalMillis;
unsigned int interval = 100;
byte ServiceIndexOld;
byte dabfreqOld;
String ServiceListOld;
String ServiceInfoOld;
String ServiceDataOld;
bool connectedSerial;

void Communication(void) {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    unsigned int equalsIndex = input.indexOf('=');

    if (equalsIndex == -1) {
      DataPrint("#2\n");
      return;
    }

    if (equalsIndex != -1) {
      String command = input.substring(0, equalsIndex);
      String value = input.substring(equalsIndex + 1);

      unsigned int intValue = value.toInt();
      command.trim();
      command.toUpperCase();

      if (connectedSerial) {
        switch (hashCommand(command)) {
          case 'E':
            if (command.equals("ENABLE")) {
              if (intValue == 0) {
                DataPrint("*ENABLE=0\n");
                connectedSerial = false;
              } else if (intValue == 1) {
                doEnableConnection();
              } else {
                DataPrint("#1\n");
              }
            }
            break;

          case 'I':
            if (command.equals("INTERVAL")) {
              if (intValue > 0 && intValue <= 500) {
                DataPrint("*INTERVAL=" + String(intValue) + "\n#0\n");
                interval = intValue;
              } else {
                DataPrint("#1\n");
              }
              break;

            case 'T':
              if (command.equals("TUNE")) {
                if (intValue < sizeof(DABfrequencyTable_DAB) / sizeof(DABfrequencyTable_DAB[0])) {
                  radio.ServiceStart = false;
                  radio.ServiceIndex = 0;
                  radio.clearData();
                  for (byte x = 0; x < 17; x++) _serviceName[x] = '\0';
                  dabfreq = intValue;
                  radio.setFreq(dabfreq);
                  if (SlideShowView || ChannelListView || ShowServiceInformation || menu) {
                    SlideShowView = false;
                    ChannelListView = false;
                    ShowServiceInformation = false;
                    menu = false;
                    BuildDisplay();
                  } else {
                    ShowFreq();
                  }
                  DataPrint("#0\n*TUNE=" + String(dabfreq) + "\n");
                  DataPrint("$M=SLIDESHOW=0\n");
                } else {
                  DataPrint("#1\n");
                }
              }
              break;
            case 'S':
              if (command.equals("SERVICE")) {
                if (intValue < radio.numberofservices) {
                  radio.ServiceIndex = intValue;
                  radio.setService(radio.ServiceIndex);
                  radio.ServiceStart = true;
                  store = true;
                  DataPrint("#0\n*SERVICE=" + String(radio.ServiceIndex) + "\n");
                  DataPrint("$M=SLIDESHOW=0\n");
                } else {
                  DataPrint("#1\n");
                }
              }
              break;
            default:
              DataPrint("#2\n");
              break;
            }
        }
      } else {
        if (command == "ENABLE") {
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
      }
    }
  }

  if (connectedSerial) {
    if (radio.ServiceIndex != ServiceIndexOld) {
      if (radio.ServiceStart) DataPrint("*SERVICE=" + String(radio.ServiceIndex) + "\n");
      DataPrint("$M=SLIDESHOW=0\n");
      ServiceIndexOld = radio.ServiceIndex;
    }

    if (dabfreq != dabfreqOld) {
      DataPrint("*TUNE=" + String(dabfreq) + "\n");
      DataPrint("$M=SLIDESHOW=0\n");
      dabfreqOld = dabfreq;
    }

    if (ServiceList() != ServiceListOld) {
      DataPrint(ServiceList());
      ServiceListOld = ServiceList();
    }

    if (ServiceInfo() != ServiceInfoOld) {
      DataPrint(ServiceInfo());
      ServiceInfoOld = ServiceInfo();
    }

    if (String(radio.ASCII(radio.ServiceData, radio.ServiceLabelCharset)) != ServiceDataOld) {
      DataPrint("$D=RT=" + String(radio.ASCII(radio.ServiceData, radio.ServiceLabelCharset)) + "\n");
      ServiceDataOld = String(radio.ASCII(radio.ServiceData, radio.ServiceLabelCharset));
    }

    doMOTShow();

    if (millis() - signalMillis > interval) {
      DataPrint("$S=SIGNAL=" + String(SignalLevel / 10) + "." + String(SignalLevel % 10) + ",LOCK=" + String(radio.signallock) + ",CNR=" + String(radio.cnr) + ",FIC=" + String(radio.fic) + "\n");
      signalMillis = millis();
    }
  }
}

static char hashCommand(String command) {
  if (command.equals("ENABLE")) {
    return 'E';
  } else if (command.equals("TUNE")) {
    return 'T';
  } else if (command.equals("SERVICE")) {
    return 'S';
  } else if (command.equals("INTERVAL")) {
    return 'I';
  } else {
    return 0;
  }
}

static void DataPrint(String data) {
  Serial.print(data);
}

static String ServiceList(void) {
  String ServiceList;
  ServiceList = "$L=COUNT=" + String(radio.numberofservices) + ",ENSEMBLE=" + String(radio.EID) + "," + String(radio.ASCII(radio.EnsembleLabel, radio.EnsembleLabelCharset)) + ";SERVICES=";

  if (radio.signallock) {
    for (int x = 0; x < radio.numberofservices; x++) {
      ServiceList += String(x) + ",";
      ServiceList += String(radio.service[x].ServiceType);
      ServiceList += ",";
      ServiceList += String(radio.ASCII(radio.service[x].Label, radio.ServiceLabelCharset));

      if (x < radio.numberofservices - 1) ServiceList += ";";
    }
  } else {
    ServiceList += "0";
  }
  ServiceList += "\n";
  return ServiceList;
}

static String ServiceInfo(void) {
  String ServiceInfo;
  ServiceInfo = "$I=";
  ServiceInfo += "ID=" + (radio.ServiceStart ? String(radio.service[radio.ServiceIndex].CompID & 0xFF, DEC) : "0") + ";";
  ServiceInfo += "SID=" + (radio.ServiceStart ? String(radio.SID) : "0") + ";";
  ServiceInfo += "PTY=" + (radio.ServiceStart ? String(radio.pty, DEC) : "0") + ";";
  ServiceInfo += "PROTECTION=" + (radio.ServiceStart ? String(radio.protectionlevel) : "0") + ";";
  ServiceInfo += "SAMPLERATE=" + (radio.ServiceStart ? String(radio.samplerate) : "0") + ";";
  ServiceInfo += "BITRATE=" + (radio.ServiceStart ? String(radio.bitrate, DEC) : "0") + ";";
  ServiceInfo += "AUDIO=" + (radio.ServiceStart ? String(radio.audiomode, DEC) : "0");
  ServiceInfo += "\n";
  return ServiceInfo;
}

static void doEnableConnection(void) {
  DataPrint("*ENABLE=1," + String(VERSION) + "," + String(radio.getChipID()) + "/" + String(radio.getFirmwareVersion()) + "\n");
  DataPrint(":MODE=3,3-3\n");
  DataPrint("*INTERVAL=" + String(interval) + "\n");
  DataPrint(":FREQ=" + String(sizeof(DABfrequencyTable_DAB) / sizeof(DABfrequencyTable_DAB[0])) + ",");

  for (int i = 0; i < sizeof(DABfrequencyTable_DAB) / sizeof(DABfrequencyTable_DAB[0]); ++i) {
    DataPrint(String(i));
    DataPrint(":");
    DataPrint(String(DABfrequencyTable_DAB[i].frequency));
    DataPrint(",");
    DataPrint(String(DABfrequencyTable_DAB[i].label));

    if (i < sizeof(DABfrequencyTable_DAB) / sizeof(DABfrequencyTable_DAB[0]) - 1) {
      DataPrint(";");
    }
  }
  DataPrint("\n");

  if (radio.ServiceStart) DataPrint("*SERVICE=" + String(radio.ServiceIndex) + "\n");
  DataPrint("*TUNE=" + String(dabfreq) + "\n");
  DataPrint("$M=SLIDESHOW=0\n");

  ServiceListOld = "";
  ServiceInfoOld = "";
  ServiceDataOld = "";
  if (radio.SlideShowAvailable) radio.SlideShowUpdate2 = true; else DataPrint("$M=SLIDESHOW=0\n");
}

static void doMOTShow(void) {
  if (radio.SlideShowAvailable && radio.SlideShowUpdate2) {
    DataPrint("$M=SLIDESHOW=");

    fs::File file = LittleFS.open("/slideshow.img", "r");

    if (!file) {
      DataPrint("3\n");
      return;
    }

    size_t size = file.size();
    uint8_t* image = (uint8_t*)malloc(size);

    if (image == nullptr) {
      DataPrint("3\n");
      file.close();
      return;
    }

    file.read(image, size);
    file.close();

    size_t buffer_size_needed = (size + 2 - ((size + 2) % 3)) / 3 * 4 + 1;
    uint8_t *buffer = (uint8_t*)calloc(buffer_size_needed, sizeof(char));

    if (buffer == nullptr) {
      DataPrint("3\n");
      free(image);
      return;
    }

    size_t buff_size = 0;
    int ret = mbedtls_base64_encode(buffer, buffer_size_needed, &buff_size, image, size);

    if (ret != 0) {
      DataPrint("3\n");
      free(image);
      free(buffer);
      return;
    }

    buffer[buff_size] = '\0';

    if (radio.isJPG) DataPrint("1,");
    if (radio.isPNG) DataPrint("2,");
    DataPrint("BASE64=");
    DataPrint((char*)buffer);

    free(image);
    free(buffer);

    DataPrint("\n");
    radio.SlideShowUpdate2 = false;
  }
}