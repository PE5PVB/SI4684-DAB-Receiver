#include "epg_uart.h"
#include <stdio.h>
#include <string.h>
namespace epg {
void UartReceiver::reset(uint32_t sid,uint32_t cid,unsigned session) {
  endpoint_=dab_probe::Endpoint(sid,cid); lastSequence_=lastCrc_=touched_=0;
  session_=session; filled_=total_=0; crcErrors=orderErrors=identityErrors=0;
  dsrv_=false; source_=0;
}
void UartReceiver::resetDsrv(uint32_t sid,uint32_t cid,unsigned session,uint8_t source) {
  reset(sid,cid,session); dsrv_=true; source_=source;
}
static int digit(char c) { return c>='0' && c<='9'?c-'0':c>='A' && c<='F'?c-'A'+10:c>='a' && c<='f'?c-'a'+10:-1; }
void UartReceiver::expire(uint32_t now) {
  if(filled_ && uint32_t(now-touched_)>5000U) { filled_=total_=0; ++orderErrors; }
}
UartReceiver::Result UartReceiver::receive(const char* line,Core& core,uint32_t now,DsrvHandler handler) {
  const size_t length=strlen(line);
  if(length>126 || length<50) return Result::Format;
  if(strncmp(line,dsrv_?"E2 ":"E1 ",3)) return Result::Format;
  const char* last=strrchr(line,' ');
  if(!last || strlen(last+1)!=8) return Result::Format;
  unsigned expected=0; for(unsigned i=1;i<=8;++i) { int d=digit(last[i]); if(d<0) return Result::Format; expected=(expected<<4)|unsigned(d); }
  if(crc32(reinterpret_cast<const uint8_t*>(line),size_t(last-line))!=expected) { ++crcErrors; return Result::Crc; }
  unsigned session=0, seq=0, offset=0, total=0, sid=0, cid=0, source=0; char hex[65]={}; int end=0;
  if(sscanf(line+3,"%4x %8x %4x %4x %8x %8x %1x %64s %n",&session,&seq,&offset,&total,&sid,&cid,&source,hex,&end)!=8 || line+3+end!=last+1) return Result::Format;
  const size_t encoded=strlen(hex), count=encoded/2;
  if(!encoded || (encoded&1) || count>32 || !total || total>kMaxGroup || offset>total || count>total-offset) return Result::Format;
  const bool identity=endpoint_.matches(sid,cid) ||
      (source_==0?dab_probe::matchesPacketData(endpoint_,sid,cid):dab_probe::matchesAudioPad(endpoint_,sid,cid));
  if(source!=source_ || !identity) { ++identityErrors; return Result::Identity; }
  if(session_ && session_!=session) { ++orderErrors; return Result::Order; }
  if(session_==session && seq==lastSequence_) return expected==lastCrc_?Result::Ack:Result::Order;
  if(seq!=lastSequence_+1 || offset!=filled_ || (filled_ && total!=total_)) { ++orderErrors; return Result::Order; }
  if(core.busy()) return Result::Busy;
  uint8_t fragment[32];
  for(size_t i=0;i<count;++i) { const int hi=digit(hex[i*2]), lo=digit(hex[i*2+1]); if(hi<0 || lo<0) return Result::Format; fragment[i]=uint8_t((hi<<4)|lo); }
  memcpy(frame_+filled_,fragment,count); total_=uint16_t(total);
  const uint16_t received=uint16_t(filled_+count);
  if(received==total) {
    const Feed result=dsrv_?(handler?handler(frame_,total,now):Feed::Rejected):core.feed(frame_,total,now);
    // A corrupt complete group is rejected atomically; retrying the last fragment
    // cannot commit it. STOP/START begins a new session after such a NACK.
    if(result!=Feed::Accepted) { filled_=total_=0; return result==Feed::Busy?Result::Busy:Result::Data; }
    filled_=total_=0;
  } else filled_=received;
  session_=session; lastSequence_=seq; lastCrc_=expected; touched_=now;
  return Result::Ack;
}
}
