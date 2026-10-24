/*
 * Standalone IR protocol decoder for SI4684-FMDAB-Receiver.
 *
 * The protocol IDs, timing constants and decoding behaviour are derived from
 * Arduino-IRremote 4.7.1 decoder sources, but all GPIO capture, timers and
 * receiver state machines are intentionally independent from that library.
 *
 * Arduino-IRremote portions are used under the MIT License:
 *
 * Copyright (c) 2017-2026 Arduino-IRremote contributors, including
 * Kristian Lauszus, Darryl Smith and Armin Joachimsmeyer.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "ir_decode.h"

#include <stddef.h>

namespace {

static constexpr uint8_t IR_TOLERANCE_PERCENT = 25U;
static constexpr int16_t IR_MARK_EXCESS_US = 20;

static bool matchUs(uint32_t measured, uint32_t expected) {
  const uint32_t low = expected * (100U - IR_TOLERANCE_PERCENT) / 100U;
  const uint32_t high = expected * (100U + IR_TOLERANCE_PERCENT) / 100U;
  return measured >= low && measured <= high;
}

static bool matchMark(uint32_t measured, uint32_t expected) {
  return matchUs(measured, expected + IR_MARK_EXCESS_US);
}

static bool matchSpace(uint32_t measured, uint32_t expected) {
  const uint32_t compensated =
      expected > static_cast<uint32_t>(IR_MARK_EXCESS_US)
          ? expected - static_cast<uint32_t>(IR_MARK_EXCESS_US)
          : expected;
  return matchUs(measured, compensated);
}

static bool decodePulseDistance(const uint16_t* d, uint16_t count,
                                uint16_t start, uint8_t bits,
                                uint16_t bitMark, uint16_t oneSpace,
                                uint16_t zeroSpace, bool lsbFirst,
                                uint64_t& value) {
  const uint32_t needed = static_cast<uint32_t>(start) +
                          static_cast<uint32_t>(bits) * 2U + 1U;
  if (!d || needed > count) return false;

  uint64_t decoded = 0;
  for (uint8_t i = 0; i < bits; ++i) {
    const uint16_t markIndex = start + static_cast<uint16_t>(i) * 2U;
    const uint16_t spaceIndex = markIndex + 1U;
    if (!matchMark(d[markIndex], bitMark)) return false;

    uint8_t bit;
    if (matchSpace(d[spaceIndex], oneSpace))
      bit = 1U;
    else if (matchSpace(d[spaceIndex], zeroSpace))
      bit = 0U;
    else
      return false;

    if (lsbFirst)
      decoded |= static_cast<uint64_t>(bit) << i;
    else
      decoded = (decoded << 1U) | bit;
  }

  if (!matchMark(d[start + static_cast<uint16_t>(bits) * 2U], bitMark))
    return false;

  value = decoded;
  return true;
}

static bool decodeSonyPulseWidth(const uint16_t* d, uint16_t count,
                                 uint8_t bits, uint64_t& value) {
  static constexpr uint16_t SONY_ONE_MARK = 1200U;
  static constexpr uint16_t SONY_ZERO_MARK = 600U;
  static constexpr uint16_t SONY_SPACE = 600U;

  // Header occupies d[0], d[1]. The final bit has no trailing SPACE.
  if (count != static_cast<uint16_t>(2U * bits + 1U)) return false;

  uint64_t decoded = 0;
  for (uint8_t i = 0; i < bits; ++i) {
    const uint16_t markIndex = 2U + static_cast<uint16_t>(i) * 2U;
    uint8_t bit;
    if (matchMark(d[markIndex], SONY_ONE_MARK))
      bit = 1U;
    else if (matchMark(d[markIndex], SONY_ZERO_MARK))
      bit = 0U;
    else
      return false;

    decoded |= static_cast<uint64_t>(bit) << i;

    if (i + 1U < bits && !matchSpace(d[markIndex + 1U], SONY_SPACE))
      return false;
  }

  value = decoded;
  return true;
}

static bool gapIsRepeat(uint32_t initialGapUs, uint32_t maxGapUs) {
  return initialGapUs != 0U && initialGapUs < maxGapUs;
}

static void copyLastAsRepeat(const IrDecoderState& state, IrFrame& frame,
                             uint8_t extraFlags = 0U) {
  frame = IrFrame{};
  frame.protocol = state.lastProtocol;
  frame.address = state.lastAddress;
  frame.command = state.lastCommand;
  frame.raw = state.lastRaw;
  frame.flags = static_cast<uint8_t>(IR_FLAG_REPEAT | extraFlags);
}

static bool decodeNEC(const uint16_t* d, uint16_t count, uint32_t gap,
                      const IrDecoderState& state, IrFrame& frame) {
  static constexpr uint16_t HEADER_MARK = 8960U;
  static constexpr uint16_t HEADER_SPACE = 4480U;
  static constexpr uint16_t REPEAT_SPACE = 2240U;
  static constexpr uint16_t BIT_MARK = 560U;
  static constexpr uint16_t ONE_SPACE = 1680U;
  static constexpr uint16_t ZERO_SPACE = 560U;
  static constexpr uint32_t MAX_FULL_REPEAT_GAP = 70000UL;
  static constexpr uint16_t APPLE_ADDRESS = 0x87EEU;

  if (count != 3U && count != 67U) return false;
  if (!matchMark(d[0], HEADER_MARK)) return false;

  if (count == 3U) {
    if (!state.hasLast ||
        !matchSpace(d[1], REPEAT_SPACE) ||
        !matchMark(d[2], BIT_MARK))
      return false;
    copyLastAsRepeat(state, frame);
    return true;
  }

  if (!matchSpace(d[1], HEADER_SPACE)) return false;
  uint64_t raw64 = 0;
  if (!decodePulseDistance(d, count, 2U, 32U, BIT_MARK,
                           ONE_SPACE, ZERO_SPACE, true, raw64))
    return false;

  const uint32_t raw = static_cast<uint32_t>(raw64);
  const uint8_t b0 = static_cast<uint8_t>(raw);
  const uint8_t b1 = static_cast<uint8_t>(raw >> 8U);
  const uint8_t b2 = static_cast<uint8_t>(raw >> 16U);
  const uint8_t b3 = static_cast<uint8_t>(raw >> 24U);
  const uint16_t lowWord = static_cast<uint16_t>(raw);
  const uint16_t highWord = static_cast<uint16_t>(raw >> 16U);

  frame = IrFrame{};
  frame.raw = raw;
  frame.bits = 32U;
  frame.command = b2;

  if (lowWord == APPLE_ADDRESS) {
    frame.protocol = IR_PROTO_APPLE;
    frame.address = b3;
  } else {
    frame.address = (b0 == static_cast<uint8_t>(~b1)) ? b0 : lowWord;
    if (b2 == static_cast<uint8_t>(~b3)) {
      frame.protocol = IR_PROTO_NEC;
    } else {
      frame.protocol = IR_PROTO_ONKYO;
      frame.command = highWord;
    }
  }

  if (gapIsRepeat(gap, MAX_FULL_REPEAT_GAP)) {
    frame.protocol = IR_PROTO_NEC2;
    frame.flags = static_cast<uint8_t>(IR_FLAG_REPEAT |
                                       IR_FLAG_DIFFERENT_REPEAT);
  }
  return true;
}

static bool decodeKaseikyo(const uint16_t* d, uint16_t count, uint32_t gap,
                           IrFrame& frame) {
  static constexpr uint16_t HEADER_MARK = 3456U;
  static constexpr uint16_t HEADER_SPACE = 1728U;
  static constexpr uint16_t BIT_MARK = 432U;
  static constexpr uint16_t ONE_SPACE = 1296U;
  static constexpr uint16_t ZERO_SPACE = 432U;
  static constexpr uint32_t MAX_REPEAT_GAP = 92500UL;
  static constexpr uint16_t PANASONIC_VENDOR = 0x2002U;
  static constexpr uint16_t DENON_VENDOR = 0x3254U;
  static constexpr uint16_t MITSUBISHI_VENDOR = 0xCB23U;
  static constexpr uint16_t SHARP_VENDOR = 0x5AAAU;
  static constexpr uint16_t JVC_VENDOR = 0x0103U;

  if (count != 99U ||
      !matchMark(d[0], HEADER_MARK) ||
      !matchSpace(d[1], HEADER_SPACE))
    return false;

  uint64_t raw = 0;
  if (!decodePulseDistance(d, count, 2U, 48U, BIT_MARK,
                           ONE_SPACE, ZERO_SPACE, true, raw))
    return false;

  const uint16_t vendor = static_cast<uint16_t>(raw);
  const uint32_t rest = static_cast<uint32_t>(raw >> 16U);
  const uint8_t b0 = static_cast<uint8_t>(rest);
  const uint8_t b1 = static_cast<uint8_t>(rest >> 8U);
  const uint8_t b2 = static_cast<uint8_t>(rest >> 16U);
  const uint8_t b3 = static_cast<uint8_t>(rest >> 24U);

  frame = IrFrame{};
  frame.raw = raw;
  frame.bits = 48U;
  frame.address = static_cast<uint16_t>(rest & 0xFFFFU) >> 4U;
  frame.command = b2;

  if (vendor == PANASONIC_VENDOR)
    frame.protocol = IR_PROTO_PANASONIC;
  else if (vendor == SHARP_VENDOR)
    frame.protocol = IR_PROTO_KASEIKYO_SHARP;
  else if (vendor == DENON_VENDOR)
    frame.protocol = IR_PROTO_KASEIKYO_DENON;
  else if (vendor == JVC_VENDOR)
    frame.protocol = IR_PROTO_KASEIKYO_JVC;
  else if (vendor == MITSUBISHI_VENDOR)
    frame.protocol = IR_PROTO_KASEIKYO_MITSUBISHI;
  else {
    frame.protocol = IR_PROTO_KASEIKYO;
    frame.flags |= IR_FLAG_EXTRA_INFO;
    frame.extra = vendor;
  }

  uint8_t vendorParity = static_cast<uint8_t>(vendor ^ (vendor >> 8U));
  vendorParity = static_cast<uint8_t>((vendorParity ^ (vendorParity >> 4U)) & 0x0FU);
  if (vendorParity != (b0 & 0x0FU)) frame.flags |= IR_FLAG_PARITY_FAILED;

  const uint8_t parity = static_cast<uint8_t>(b0 ^ b1 ^ b2);
  if (parity != b3) frame.flags |= IR_FLAG_PARITY_FAILED;

  if (gapIsRepeat(gap, MAX_REPEAT_GAP)) frame.flags |= IR_FLAG_REPEAT;
  return true;
}

static bool decodeDenon(const uint16_t* d, uint16_t count, uint32_t gap,
                        IrDecoderState& state, IrFrame& frame) {
  static constexpr uint16_t BIT_MARK = 260U;
  static constexpr uint16_t ONE_SPACE = 1820U;
  static constexpr uint16_t ZERO_SPACE = 780U;
  static constexpr uint32_t AUTO_REPEAT_GAP = 56250UL;

  if (count != 31U || d[0] >= 520U) return false;

  uint64_t raw = 0;
  if (!decodePulseDistance(d, count, 0U, 15U, BIT_MARK,
                           ONE_SPACE, ZERO_SPACE, true, raw))
    return false;

  frame = IrFrame{};
  frame.raw = raw;
  frame.bits = 15U;
  frame.address = static_cast<uint16_t>(raw & 0x1FU);
  uint16_t commandWithFrame = static_cast<uint16_t>(raw >> 5U);
  uint8_t frameBits = static_cast<uint8_t>((commandWithFrame >> 8U) & 0x03U);
  frame.command = static_cast<uint8_t>(commandWithFrame);

  if (gapIsRepeat(gap, AUTO_REPEAT_GAP) && state.hasLast) {
    ++state.denonRepeatCount;
    frame.flags |= IR_FLAG_AUTO_REPEAT;
    if (state.denonRepeatCount & 0x01U) {
      if (state.denonRepeatCount > 1U)
        frame.flags = IR_FLAG_REPEAT;
      if (static_cast<uint8_t>(state.lastCommand) !=
          static_cast<uint8_t>(~frame.command))
        frame.flags |= IR_FLAG_PARITY_FAILED;
      frame.command = state.lastCommand;
      frameBits ^= 0x03U;
    }
  } else {
    state.denonRepeatCount = 0U;
  }

  frame.protocol = frameBits ? IR_PROTO_SHARP : IR_PROTO_DENON;
  return true;
}

static bool decodeSony(const uint16_t* d, uint16_t count, uint32_t gap,
                       IrFrame& frame) {
  static constexpr uint16_t HEADER_MARK = 2400U;
  static constexpr uint16_t HEADER_SPACE = 600U;
  static constexpr uint32_t MAX_REPEAT_GAP = 27600UL;

  uint8_t bits;
  if (count == 25U)
    bits = 12U;
  else if (count == 31U)
    bits = 15U;
  else if (count == 41U)
    bits = 20U;
  else
    return false;

  if (!matchMark(d[0], HEADER_MARK) ||
      !matchSpace(d[1], HEADER_SPACE))
    return false;

  uint64_t raw = 0;
  if (!decodeSonyPulseWidth(d, count, bits, raw)) return false;

  frame = IrFrame{};
  frame.protocol = IR_PROTO_SONY;
  frame.raw = raw;
  frame.bits = bits;
  frame.command = static_cast<uint16_t>(raw & 0x7FU);
  frame.address = static_cast<uint16_t>(raw >> 7U);
  if (gapIsRepeat(gap, MAX_REPEAT_GAP)) frame.flags |= IR_FLAG_REPEAT;
  return true;
}

class BiphaseReader {
 public:
  BiphaseReader(const uint16_t* durations, uint16_t count,
                uint16_t start, uint16_t unit)
      : d_(durations), count_(count), index_(start), unit_(unit) {}

  int next() {
    if (remaining_ == 0U) {
      if (!d_ || index_ >= count_) return -1;
      const int level = ((index_ & 1U) == 0U) ? 1 : 0; // MARK=1, SPACE=0
      uint32_t duration = d_[index_];
      // Arduino-IRremote compensates demodulator mark excess before rounding
      // biphase intervals to half-bit units.
      if (level != 0)
        duration += static_cast<uint32_t>(IR_MARK_EXCESS_US);
      else if (duration > static_cast<uint32_t>(IR_MARK_EXCESS_US))
        duration -= static_cast<uint32_t>(IR_MARK_EXCESS_US);
      uint32_t units = (duration + unit_ / 2U) / unit_;
      if (units == 0U || units > 8U) return -1;
      const uint32_t nominal = units * unit_;
      // Biphase intervals naturally combine when adjacent half-bit levels are
      // equal. Half-a-unit timing margin matches the source decoder's rounding.
      const uint32_t delta = duration > nominal ? duration - nominal : nominal - duration;
      if (delta > (unit_ / 2U + 80U)) return -1;
      remaining_ = static_cast<uint8_t>(units);
      level_ = level;
      ++index_;
    }
    --remaining_;
    return level_;
  }

  int nextOrTrailingSpace() {
    if (!hasLevel()) return 0; // Same trailing-idle assumption as IRremote.
    return next();
  }

  bool hasLevel() const { return remaining_ != 0U || index_ < count_; }

 private:
  const uint16_t* d_ = nullptr;
  uint16_t count_ = 0;
  uint16_t index_ = 0;
  uint16_t unit_ = 0;
  uint8_t remaining_ = 0;
  int level_ = 0;
};

static bool decodeRC5(const uint16_t* d, uint16_t count, uint32_t gap,
                      IrFrame& frame) {
  static constexpr uint16_t UNIT = 889U;
  static constexpr uint32_t MAX_REPEAT_GAP = 112500UL;

  if (count < 13U || count > 27U || d[0] > (2U * UNIT + UNIT / 2U))
    return false;

  BiphaseReader reader(d, count, 0U, UNIT);
  if (reader.next() != 1) return false; // MARK half of start bit

  uint32_t raw = 0;
  uint8_t bitCount = 0;
  while (reader.hasLevel()) {
    const int start = reader.next();
    if (start < 0) return false;
    const int end = reader.nextOrTrailingSpace();
    if (end < 0) return false;

    if (start == 0 && end == 1)
      raw = (raw << 1U) | 1U;
    else if (start == 1 && end == 0)
      raw <<= 1U;
    else
      return false;
    ++bitCount;
    if (bitCount > 13U) return false;
  }

  if (bitCount != 13U) return false;

  frame = IrFrame{};
  frame.protocol = IR_PROTO_RC5;
  frame.raw = raw;
  frame.bits = bitCount;
  frame.command = static_cast<uint16_t>(raw & 0x3FU);
  frame.address = static_cast<uint16_t>((raw >> 6U) & 0x1FU);
  if ((raw & (1UL << 12U)) == 0U) frame.command += 0x40U;
  frame.flags = IR_FLAG_MSB_FIRST;
  if (raw & (1UL << 11U)) frame.flags |= IR_FLAG_TOGGLE;
  if (gapIsRepeat(gap, MAX_REPEAT_GAP)) frame.flags |= IR_FLAG_REPEAT;
  return true;
}

static bool decodeRC6(const uint16_t* d, uint16_t count, uint32_t gap,
                      IrFrame& frame) {
  static constexpr uint16_t UNIT = 444U;
  static constexpr uint16_t HEADER_MARK = 2664U;
  static constexpr uint16_t HEADER_SPACE = 888U;
  static constexpr uint8_t TOGGLE_INDEX = 3U;
  static constexpr uint32_t MAX_REPEAT_GAP = 133750UL;

  if (count < 25U || count > 71U ||
      !matchMark(d[0], HEADER_MARK) ||
      !matchSpace(d[1], HEADER_SPACE))
    return false;

  BiphaseReader reader(d, count, 2U, UNIT);
  if (reader.next() != 1 || reader.next() != 0) return false;

  uint64_t raw = 0;
  uint8_t bitCount = 0;
  while (reader.hasLevel()) {
    const int start = reader.next();
    if (start < 0) return false;
    int end = reader.nextOrTrailingSpace();
    if (end < 0) return false;

    if (bitCount == TOGGLE_INDEX) {
      if (start != end || !reader.hasLevel()) return false;
      end = reader.next();
      if (end < 0 || !reader.hasLevel()) return false;
      const int fourth = reader.next();
      if (fourth < 0 || end != fourth) return false;
    }

    if (start == 1 && end == 0)
      raw = (raw << 1U) | 1U;
    else if (start == 0 && end == 1)
      raw <<= 1U;
    else
      return false;

    ++bitCount;
    if (bitCount > 35U) return false;
  }

  if (bitCount < 20U) return false;

  const uint32_t low32 = static_cast<uint32_t>(raw);
  frame = IrFrame{};
  frame.raw = low32; // Matches Arduino-IRremote 4.7.1 RC6 storage behaviour.
  frame.bits = bitCount;
  frame.command = static_cast<uint8_t>(low32);
  frame.address = static_cast<uint8_t>(low32 >> 8U);
  frame.flags = IR_FLAG_MSB_FIRST;

  if (bitCount < 35U) {
    frame.protocol = IR_PROTO_RC6;
    if ((low32 >> 16U) & 0x01U) frame.flags |= IR_FLAG_TOGGLE;
    if (bitCount > 20U) frame.flags |= IR_FLAG_EXTRA_INFO;
  } else {
    frame.protocol = IR_PROTO_RC6A;
    frame.flags |= IR_FLAG_EXTRA_INFO;
    frame.extra = static_cast<uint16_t>((low32 >> 16U) & 0x3FFFU);
    if (low32 & 0x80000000UL) frame.flags |= IR_FLAG_TOGGLE;
  }

  if (gapIsRepeat(gap, MAX_REPEAT_GAP)) frame.flags |= IR_FLAG_REPEAT;
  return true;
}

static bool decodeLG(const uint16_t* d, uint16_t count,
                     const IrDecoderState& state, IrFrame& frame) {
  static constexpr uint16_t HEADER_MARK = 8416U;
  static constexpr uint16_t HEADER_SPACE = 4208U;
  static constexpr uint16_t REPEAT_SPACE = 2104U;
  static constexpr uint16_t BIT_MARK = 526U;
  static constexpr uint16_t ONE_SPACE = 1578U;
  static constexpr uint16_t ZERO_SPACE = 550U;

  if (count != 3U && count != 59U) return false;
  if (!matchMark(d[0], HEADER_MARK)) return false;

  if (count == 3U) {
    if (!state.hasLast || state.lastProtocol != IR_PROTO_LG ||
        !matchSpace(d[1], REPEAT_SPACE) ||
        !matchMark(d[2], BIT_MARK))
      return false;
    copyLastAsRepeat(state, frame, IR_FLAG_MSB_FIRST);
    return true;
  }

  if (!matchSpace(d[1], HEADER_SPACE)) return false;
  uint64_t raw = 0;
  if (!decodePulseDistance(d, count, 2U, 28U, BIT_MARK,
                           ONE_SPACE, ZERO_SPACE, false, raw))
    return false;

  frame = IrFrame{};
  frame.protocol = IR_PROTO_LG;
  frame.raw = raw;
  frame.bits = 28U;
  frame.flags = IR_FLAG_MSB_FIRST;
  frame.command = static_cast<uint16_t>((raw >> 4U) & 0xFFFFU);
  frame.address = static_cast<uint8_t>(raw >> 20U);

  uint8_t checksum = 0;
  uint16_t temp = frame.command;
  for (uint8_t i = 0; i < 4U; ++i) {
    checksum = static_cast<uint8_t>(checksum + (temp & 0x0FU));
    temp >>= 4U;
  }
  if ((checksum & 0x0FU) != (raw & 0x0FU))
    frame.flags |= IR_FLAG_PARITY_FAILED;
  return true;
}

static bool decodeJVC(const uint16_t* d, uint16_t count, uint32_t gap,
                      const IrDecoderState& state, IrFrame& frame) {
  static constexpr uint16_t HEADER_MARK = 8416U;
  static constexpr uint16_t HEADER_SPACE = 4208U;
  static constexpr uint16_t BIT_MARK = 526U;
  static constexpr uint16_t ONE_SPACE = 1578U;
  static constexpr uint16_t ZERO_SPACE = 526U;
  static constexpr uint32_t MAX_REPEAT_GAP = 29531UL;

  if (count == 33U) {
    if (!state.hasLast || state.lastProtocol != IR_PROTO_JVC ||
        !gapIsRepeat(gap, MAX_REPEAT_GAP) ||
        !matchMark(d[0], BIT_MARK) ||
        !matchMark(d[count - 1U], BIT_MARK))
      return false;
    copyLastAsRepeat(state, frame);
    frame.protocol = IR_PROTO_JVC;
    return true;
  }

  if (count != 35U ||
      !matchMark(d[0], HEADER_MARK) ||
      !matchSpace(d[1], HEADER_SPACE))
    return false;

  uint64_t raw = 0;
  if (!decodePulseDistance(d, count, 2U, 16U, BIT_MARK,
                           ONE_SPACE, ZERO_SPACE, true, raw))
    return false;

  frame = IrFrame{};
  frame.protocol = IR_PROTO_JVC;
  frame.raw = raw;
  frame.bits = 16U;
  frame.address = static_cast<uint8_t>(raw);
  frame.command = static_cast<uint8_t>(raw >> 8U);
  return true;
}

static bool decodeSamsung(const uint16_t* d, uint16_t count, uint32_t gap,
                          const IrDecoderState& state, IrFrame& frame) {
  static constexpr uint16_t HEADER_MARK = 4480U;
  static constexpr uint16_t HEADER_SPACE = 4480U;
  static constexpr uint16_t BIT_MARK = 560U;
  static constexpr uint16_t ONE_SPACE = 1680U;
  static constexpr uint16_t ZERO_SPACE = 560U;
  static constexpr uint32_t MAX_REPEAT_GAP = 137000UL;

  if (count != 5U && count != 67U && count != 99U) return false;
  if (!matchMark(d[0], HEADER_MARK) || !matchSpace(d[1], HEADER_SPACE))
    return false;

  if (count == 5U) {
    if (!state.hasLast ||
        (state.lastProtocol != IR_PROTO_SAMSUNG &&
         state.lastProtocol != IR_PROTO_SAMSUNGLG) ||
        !matchMark(d[2], BIT_MARK) ||
        !matchSpace(d[3], ZERO_SPACE) ||
        !matchMark(d[4], BIT_MARK))
      return false;
    copyLastAsRepeat(state, frame, IR_FLAG_DIFFERENT_REPEAT);
    frame.protocol = IR_PROTO_SAMSUNGLG;
    return true;
  }

  uint64_t first32 = 0;
  if (!decodePulseDistance(d, count, 2U, 32U, BIT_MARK,
                           ONE_SPACE, ZERO_SPACE, true, first32))
    return false;

  const uint32_t raw32 = static_cast<uint32_t>(first32);
  const uint8_t b0 = static_cast<uint8_t>(raw32);
  const uint8_t b1 = static_cast<uint8_t>(raw32 >> 8U);
  const uint8_t b2 = static_cast<uint8_t>(raw32 >> 16U);
  const uint8_t b3 = static_cast<uint8_t>(raw32 >> 24U);

  frame = IrFrame{};
  frame.address = static_cast<uint16_t>(raw32);

  if (count == 99U) {
    // Decode the full 48 bits in one pass. Byte order is transmission order:
    // address L/H, commandLo, ~commandLo, commandHi, ~commandHi.
    uint64_t raw48 = 0;
    if (!decodePulseDistance(d, count, 2U, 48U, BIT_MARK,
                             ONE_SPACE, ZERO_SPACE, true, raw48))
      return false;
    const uint8_t b4 = static_cast<uint8_t>(raw48 >> 32U);
    const uint8_t b5 = static_cast<uint8_t>(raw48 >> 40U);
    if (b2 != static_cast<uint8_t>(~b3) &&
        b4 != static_cast<uint8_t>(~b5))
      frame.flags |= IR_FLAG_PARITY_FAILED;
    frame.protocol = IR_PROTO_SAMSUNG48;
    frame.command = static_cast<uint16_t>((static_cast<uint16_t>(b4) << 8U) | b2);
    frame.bits = 48U;
    frame.raw = raw48;
  } else {
    frame.protocol = IR_PROTO_SAMSUNG;
    frame.command = (b2 == static_cast<uint8_t>(~b3))
                        ? b2
                        : static_cast<uint16_t>(raw32 >> 16U);
    if (b1 == b0) frame.address = b0;
    frame.bits = 32U;
    frame.raw = raw32;
  }

  if (gapIsRepeat(gap, MAX_REPEAT_GAP)) frame.flags |= IR_FLAG_REPEAT;
  return true;
}

static void decodeHash(const uint16_t* d, uint16_t count, IrFrame& frame) {
  static constexpr uint32_t FNV_PRIME = 16777619UL;
  static constexpr uint32_t FNV_BASIS = 2166136261UL;
  uint32_t hash = FNV_BASIS;

  auto compare = [](uint16_t oldValue, uint16_t newValue) -> uint8_t {
    if (static_cast<uint32_t>(newValue) * 10U <
        static_cast<uint32_t>(oldValue) * 8U)
      return 0U;
    if (static_cast<uint32_t>(oldValue) * 10U <
        static_cast<uint32_t>(newValue) * 8U)
      return 2U;
    return 1U;
  };

  if (d && count >= 5U) {
    for (uint16_t i = 0; i + 2U < count; ++i)
      hash = (hash * FNV_PRIME) ^ compare(d[i], d[i + 2U]);
  }

  frame = IrFrame{};
  frame.protocol = IR_PROTO_UNKNOWN;
  frame.raw = hash;
  frame.bits = 32U;
}

static void rememberFrame(IrDecoderState& state, const IrFrame& frame) {
  if (frame.protocol == IR_PROTO_UNKNOWN ||
      (frame.flags & (IR_FLAG_REPEAT | IR_FLAG_AUTO_REPEAT)))
    return;
  state.hasLast = true;
  state.lastProtocol = frame.protocol;
  state.lastAddress = frame.address;
  state.lastCommand = frame.command;
  state.lastRaw = frame.raw;
}

} // namespace

void IrDecoderReset(IrDecoderState& state) {
  state = IrDecoderState{};
}

bool IrDecodeFrame(const uint16_t* durationsUs,
                   uint16_t durationCount,
                   uint32_t initialGapUs,
                   IrDecoderState& state,
                   IrFrame& frame) {
  frame = IrFrame{};
  if (!durationsUs || durationCount == 0U) return false;

  bool decoded =
      decodeNEC(durationsUs, durationCount, initialGapUs, state, frame) ||
      decodeKaseikyo(durationsUs, durationCount, initialGapUs, frame) ||
      decodeDenon(durationsUs, durationCount, initialGapUs, state, frame) ||
      decodeSony(durationsUs, durationCount, initialGapUs, frame) ||
      decodeRC5(durationsUs, durationCount, initialGapUs, frame) ||
      decodeRC6(durationsUs, durationCount, initialGapUs, frame) ||
      decodeLG(durationsUs, durationCount, state, frame) ||
      decodeJVC(durationsUs, durationCount, initialGapUs, state, frame) ||
      decodeSamsung(durationsUs, durationCount, initialGapUs, state, frame);

  if (!decoded) {
    // Preserve DECODE_HASH fallback support for arbitrary learned remotes.
    if (durationCount < 5U) return false;
    decodeHash(durationsUs, durationCount, frame);
    decoded = true;
  }

  rememberFrame(state, frame);
  return decoded;
}

const char* IrProtocolName(uint8_t protocol) {
  static const char* const names[] = {
      "UNKNOWN", "PulseWidth", "PulseDistance", "Apple", "Denon", "JVC",
      "LG", "NEC", "NEC2", "Onkyo", "Panasonic", "Kaseikyo",
      "Kaseikyo_Denon", "Kaseikyo_Sharp", "Kaseikyo_JVC",
      "Kaseikyo_Mitsubishi", "RC5", "RC6", "RC6A", "Samsung",
      "SamsungLG", "Samsung48", "Sharp", "Sony", "Bang&Olufsen",
      "BoseWave", "Lego", "MagiQuest", "Whynter", "Marantz", "FAST",
      "OpenLASIR", "OTHER"};
  const uint8_t count = static_cast<uint8_t>(sizeof(names) / sizeof(names[0]));
  return protocol < count ? names[protocol] : names[0];
}
