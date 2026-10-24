#pragma once

#include <Arduino.h>

// Fixed-size UART facade. During setup it writes through so boot diagnostics
// are visible immediately. Runtime producers only enqueue; service() moves a
// bounded amount to the hardware FIFO and therefore cannot stall the radio
// scheduler for the duration of a complete diagnostic line.
class NonBlockingSerialMonitor : public Stream {
 public:
  explicit NonBlockingSerialMonitor(HardwareSerial& serial)
      : serial_(serial) {}

  void begin(unsigned long baud) { serial_.begin(baud); }
  void enableBuffering() { buffered_ = true; }

  size_t write(uint8_t value) override {
    if (!buffered_) return serial_.write(value);
    if (count_ >= kCapacity) {
      if (dropped_ != UINT32_MAX) ++dropped_;
      return 0;
    }
    buffer_[head_] = value;
    head_ = (head_ + 1U) % kCapacity;
    ++count_;
    return 1;
  }

  size_t write(const uint8_t* data, size_t length) override {
    if (data == nullptr) return 0;
    if (!buffered_) return serial_.write(data, length);
    size_t accepted = 0;
    while (accepted < length && write(data[accepted]) == 1U) ++accepted;
    return accepted;
  }

  int available() override { return serial_.available(); }
  int read() override { return serial_.read(); }
  int peek() override { return serial_.peek(); }
  int availableForWrite() override {
    return buffered_ ? static_cast<int>(kCapacity - count_)
                     : serial_.availableForWrite();
  }

  // Print::flush() callers must remain non-blocking at runtime.
  void flush() override { service(kDefaultServiceBytes); }

  void service(size_t budget = kDefaultServiceBytes) {
    if (!buffered_ || count_ == 0U || budget == 0U) return;
    int writable = serial_.availableForWrite();
    if (writable <= 0) return;
    size_t room = static_cast<size_t>(writable);
    if (room > budget) room = budget;
    while (count_ != 0U && room != 0U) {
      serial_.write(buffer_[tail_]);
      tail_ = (tail_ + 1U) % kCapacity;
      --count_;
      --room;
    }
  }

  uint32_t droppedBytes() const { return dropped_; }
  size_t queuedBytes() const { return count_; }

  static constexpr size_t kCapacity = 4096U;
  static constexpr size_t kDefaultServiceBytes = 64U;

 private:
  HardwareSerial& serial_;
  uint8_t buffer_[kCapacity] = {};
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t count_ = 0;
  uint32_t dropped_ = 0;
  bool buffered_ = false;
};

extern NonBlockingSerialMonitor diagSerial;
