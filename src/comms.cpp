// Serial-line control protocol implementation.
//
// Inbound (PC -> radio): newline-terminated text. Two forms:
//   "DEBUG"        bare keyword → toggle slideshow debug logging
//   "CMD=VALUE"    key/value pair → see hashCommand() / handleCommunication()
//
// Outbound (radio -> PC): event-driven, only while connectedSerial is true:
//   $L=...   service list changed
//   $I=...   service info (bitrate, PTY, etc.) changed
//   $D=RT=   radiotext changed
//   $S=...   signal/lock/CNR/FIC (rate-limited by `interval` ms)
//   $M=...   slideshow availability
//   *TUNE/*SERVICE/*ENABLE  command acknowledgements
//   #0..#2   numeric status replies (#0 OK, #1 bad arg, #2 unknown cmd)

#include "comms.h"

unsigned long signalMillis;          // last time the periodic signal line was emitted
unsigned int interval = 100;         // signal-line emission interval (ms), configurable via INTERVAL=
byte ServiceIndexOld;
byte dabfreqOld;
String ServiceListOld;               // cached last-emitted service list, used for diff detection
String ServiceInfoOld;
String ServiceDataOld;
bool connectedSerial;                // protocol active only after ENABLE=1 handshake

// Called once per main loop. Parses at most one inbound line (non-blocking)
// and then emits diffs of cached state to the host if connected.
void Communication(void) {
  if (Serial.available() > 0) {
    Serial.setTimeout(100);  // partial packet shouldn't freeze the loop for 1s
    String input = Serial.readStringUntil('\n');
    unsigned int equalsIndex = input.indexOf('=');

    if (equalsIndex == -1) {
      // Bare keyword (no '='): only DEBUG is recognised; anything else is #2 (unknown)
      input.trim();
      input.toUpperCase();
      if (input.equals("DEBUG")) {
        radio.SlideShowDebug = !radio.SlideShowDebug;
        Serial.printf("[SLS] Debug %s\n", radio.SlideShowDebug ? "ON" : "OFF");
      } else {
        DataPrint("#2\n");
      }
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
                if (radio.isFm()) {
                  // The established TUNE command is a DAB channel-table index.
                  DataPrint("#1\n");
                } else if (intValue < sizeof(DABfrequencyTable_DAB) / sizeof(DABfrequencyTable_DAB[0])) {
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
                if (radio.isFm()) {
                  DataPrint("#1\n");
                } else if (intValue < radio.numberofservices) {
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

  // Outbound diff-stream: only emit changes since the last loop iteration
  // so the protocol stays event-driven (signal/CNR is the only periodic line).
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

    String currentList = ServiceList();
    if (currentList != ServiceListOld) {
      DataPrint(currentList);
      ServiceListOld = currentList;
    }

    String currentInfo = ServiceInfo();
    if (currentInfo != ServiceInfoOld) {
      DataPrint(currentInfo);
      ServiceInfoOld = currentInfo;
    }

    String currentData = String(radio.ASCII(radio.ServiceData, radio.ServiceLabelCharset));
    if (currentData != ServiceDataOld) {
      DataPrint("$D=RT=" + currentData + "\n");
      ServiceDataOld = currentData;
    }

    if (millis() - signalMillis > interval) {
      DataPrint("$S=SIGNAL=" + String(SignalLevel / 10) + "." + String(SignalLevel % 10) + ",LOCK=" + String(radio.signallock) + ",CNR=" + String(radio.cnr) + ",FIC=" + String(radio.fic) + "\n");
      signalMillis = millis();
    }

    doMOTShow();
  }
}

// Map a command string to a single-char tag for the switch in handleCommunication().
// Returning 0 = unknown command.
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

// Wraps Serial.print so we have one place to swap the transport if needed.
static void DataPrint(String data) {
  Serial.print(data);
}

// Build the "$L=..." service-list line for the host. Called once per loop
// while connected and diffed against ServiceListOld to avoid spam.
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

// Build the "$I=..." service-info line summarising the currently playing service.
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

// Handshake response after the host sends ENABLE=1. Sends version, chip info,
// the supported mode, the current update interval and the full DAB frequency
// table so the client can label channels.
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

// When a new slideshow is ready, stream its RAM buffer to the host as base64
// in a single "$M=SLIDESHOW=..." line so a client can preview it.
static void doMOTShow(void) {
  if (radio.SlideShowAvailable && radio.SlideShowUpdate2) {
    DataPrint("$M=SLIDESHOW=");

    const uint8_t* image = radio.slideshowData();
    const size_t size = radio.slideshowSize();
    if (!image || size < 8) {
      DataPrint("3\n");
      radio.SlideShowUpdate2 = false;
      return;
    }

    if (image[0] == 0x89 && image[1] == 0x50 && image[2] == 0x4E && image[3] == 0x47 && image[4] == 0x0D && image[5] == 0x0A && image[6] == 0x1A && image[7] == 0x0A) {
      DataPrint("2,");
    } else if (image[0] == 0xFF && image[1] == 0xD8 && image[2] == 0xFF) {
      DataPrint("1,");
    } else {
      DataPrint("3\n");
      radio.SlideShowUpdate2 = false;
      return;
    }

    DataPrint("BASE64=");
    uint8_t encoded[769];
    for (size_t offset = 0; offset < size; offset += 576) {
      const size_t count = (size - offset) < 576 ? (size - offset) : 576;
      size_t encodedSize = 0;
      if (mbedtls_base64_encode(encoded, sizeof(encoded) - 1, &encodedSize,
                                image + offset, count) != 0) {
        DataPrint("\n");
        radio.SlideShowUpdate2 = false;
        return;
      }
      encoded[encodedSize] = '\0';
      DataPrint(reinterpret_cast<char*>(encoded));
    }

    DataPrint("\n");
    radio.SlideShowUpdate2 = false;
  }
}
