#ifndef IR_DECODE_H
#define IR_DECODE_H

#include <stdint.h>

// Stable protocol IDs intentionally match Arduino-IRremote 4.7.1 decode_type_t.
// The persisted IR profile can therefore be reused
// without migration even though Arduino-IRremote is no longer a dependency.
enum IrProtocolId : uint8_t {
  IR_PROTO_UNKNOWN = 0,
  IR_PROTO_PULSE_WIDTH = 1,
  IR_PROTO_PULSE_DISTANCE = 2,
  IR_PROTO_APPLE = 3,
  IR_PROTO_DENON = 4,
  IR_PROTO_JVC = 5,
  IR_PROTO_LG = 6,
  IR_PROTO_NEC = 7,
  IR_PROTO_NEC2 = 8,
  IR_PROTO_ONKYO = 9,
  IR_PROTO_PANASONIC = 10,
  IR_PROTO_KASEIKYO = 11,
  IR_PROTO_KASEIKYO_DENON = 12,
  IR_PROTO_KASEIKYO_SHARP = 13,
  IR_PROTO_KASEIKYO_JVC = 14,
  IR_PROTO_KASEIKYO_MITSUBISHI = 15,
  IR_PROTO_RC5 = 16,
  IR_PROTO_RC6 = 17,
  IR_PROTO_RC6A = 18,
  IR_PROTO_SAMSUNG = 19,
  IR_PROTO_SAMSUNGLG = 20,
  IR_PROTO_SAMSUNG48 = 21,
  IR_PROTO_SHARP = 22,
  IR_PROTO_SONY = 23,
  IR_PROTO_BANG_OLUFSEN = 24,
  IR_PROTO_BOSEWAVE = 25,
  IR_PROTO_LEGO_PF = 26,
  IR_PROTO_MAGIQUEST = 27,
  IR_PROTO_WHYNTER = 28,
  IR_PROTO_MARANTZ = 29,
  IR_PROTO_FAST = 30,
  IR_PROTO_OPENLASIR = 31,
  IR_PROTO_OTHER = 32
};

// Flag values are kept identical to Arduino-IRremote 4.7.1 for continuity in
// the application logic. They are now local project definitions.
static constexpr uint8_t IR_FLAG_REPEAT = 0x01;
static constexpr uint8_t IR_FLAG_AUTO_REPEAT = 0x02;
static constexpr uint8_t IR_FLAG_PARITY_FAILED = 0x04;
static constexpr uint8_t IR_FLAG_TOGGLE = 0x08;
static constexpr uint8_t IR_FLAG_EXTRA_INFO = 0x10;
static constexpr uint8_t IR_FLAG_DIFFERENT_REPEAT = 0x20;
static constexpr uint8_t IR_FLAG_OVERFLOW = 0x40;
static constexpr uint8_t IR_FLAG_MSB_FIRST = 0x80;

struct IrFrame {
  uint8_t protocol = IR_PROTO_UNKNOWN;
  uint16_t address = 0;
  uint16_t command = 0;
  uint16_t extra = 0;
  uint16_t bits = 0;
  uint64_t raw = 0;
  uint8_t flags = 0;
};

struct IrDecoderState {
  bool hasLast = false;
  uint8_t lastProtocol = IR_PROTO_UNKNOWN;
  uint16_t lastAddress = 0;
  uint16_t lastCommand = 0;
  uint64_t lastRaw = 0;
  uint8_t denonRepeatCount = 0;
};

void IrDecoderReset(IrDecoderState& state);

// durationsUs starts with the first active-low MARK and then alternates
// MARK, SPACE, MARK, SPACE...  initialGapUs is the idle-HIGH interval before
// the first MARK. No sampling timer is used by this decoder.
bool IrDecodeFrame(const uint16_t* durationsUs,
                   uint16_t durationCount,
                   uint32_t initialGapUs,
                   IrDecoderState& state,
                   IrFrame& frame);

const char* IrProtocolName(uint8_t protocol);

#endif
