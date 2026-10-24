#pragma once
#include "epg_core.h"
#include "dab_spi_probe.h"
namespace epg {
// 32 payload bytes/fragment, maximum ASCII line 121 bytes incl LF. Fixed RX.
class UartReceiver {
 public:
  enum class Result { Ack, Busy, Crc, Format, Order, Identity, Data };
  void reset(uint32_t sid=0xE23153FEU, uint32_t cid=0x01F2C00CU, unsigned session=0);
  Result receive(const char* line, Core& core, uint32_t now);
  void expire(uint32_t now);
  unsigned session() const { return session_; }
  uint32_t sequence() const { return lastSequence_; }
  uint32_t crcErrors=0, orderErrors=0, identityErrors=0;
  bool partial() const { return filled_!=0; }
 private:
  uint8_t frame_[kMaxGroup];
  dab_probe::Endpoint endpoint_;
  uint32_t lastSequence_=0, lastCrc_=0, touched_=0;
  unsigned session_=0;
  uint16_t filled_=0, total_=0;
};
}
