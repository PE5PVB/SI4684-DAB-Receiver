#include "epg_runtime.h"
#include "epg_uart.h"
#include "epg_ui.h"
#include "epg_demo.h"
#include "gui.h"
#include "serial_monitor.h"
#include <esp_heap_caps.h>
#include <new>
#include <stdlib.h>
#include <string.h>
namespace epg {
static Core* core=nullptr;
static UartReceiver receiver;
static bool replay=false, demo=false;
static uint32_t activity=0;
static unsigned demoIndex=0;
static bool snapshotFm=false;
static uint8_t snapshotFreq=0;
static uint32_t snapshotSid=0;
static bool allocationReported=false;
static uint32_t endpointSid=0, endpointCid=0, pendingSid=0;
static void objectReady(void*,uint16_t tid,uint8_t type,Bytes data,size_t n) {
  if(diagSerial.availableForWrite()<192) return;
  const char* kind=type==6?"DIR":n && data[0]==2?"PI":n && data[0]==3?"SI":"PNG";
  diagSerial.printf("EPG MOT tid=%04X kind=%s bytes=%u programmes=%u cached=%u\n",tid,kind,unsigned(n),core->stats.programmes,unsigned(core->count()));
}
Core* runtime() { return core; }
bool replayActive() { return replay; }
// UART replay has its own discovery state; never alter the live radio probe.
bool indicatorPresent() { return replay ? core && core->stats.directories!=0 : radio.spiPresent(); }
IndicatorState indicatorState() {
  if(core && core->count() && core->fresh() && !core->ambiguous()) return IndicatorState::Ready;
  return indicatorPresent()?IndicatorState::Present:IndicatorState::Absent;
}
bool liveEnabled() { return core && !replay; }
static const unsigned byteCaps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
// EXEC selects the classic ESP32 IRAM heap, avoiding consumption of DRAM.
// The memory contains data only; every access goes through Bytes/Buffer.
static const unsigned wordCaps=MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT|MALLOC_CAP_EXEC;
static void* allocateWordBlock(size_t bytes) { return heap_caps_malloc(bytes,wordCaps); }
bool ensure(bool reportFailure) {
  if(core) return true;
  const size_t free8=heap_caps_get_free_size(byteCaps), largest8=heap_caps_get_largest_free_block(byteCaps);
  const size_t free32=heap_caps_get_free_size(wordCaps), largest32=heap_caps_get_largest_free_block(wordCaps);
  // Reserves apply to each actual capability pool, never aggregate INTERNAL.
  if(largest8>=sizeof(Core) && free8>=sizeof(Core)+12000U &&
     largest32>=kObjectBytes && free32>=Core::bufferBytes()+4096U) {
    void* storage=heap_caps_malloc(sizeof(Core),byteCaps);
    if(storage) {
      Core* candidate=new(storage) Core(allocateWordBlock,heap_caps_free,true);
      if(candidate->ready()) core=candidate;
      else { candidate->~Core(); heap_caps_free(storage); }
    }
  }
  if(!core && (reportFailure || !allocationReported)) {
    allocationReported=true;
    diagSerial.printf("EPG ERROR MEMORY need8=%u free8=%u largest8=%u need32=%u free32=%u largest32=%u\n",unsigned(sizeof(Core)),unsigned(free8),unsigned(largest8),unsigned(Core::bufferBytes()),unsigned(free32),unsigned(largest32));
  }
  if(core) core->observer.object=objectReady;
  return core!=nullptr;
}
void liveReset() {
  if(replay) { replay=demo=false; pendingSid=0; receiver.reset(); EpgClose(false); }
  if(core) { core->reset(); core->select(0,0,0); EpgInvalidate(); }
}
void liveContext(uint32_t sid,uint16_t eid,uint8_t ecc,uint32_t utc) {
  if(!core || replay) return;
  uint64_t match=0; bool conflict=false;
  for(size_t i=0;i<core->entryCount();++i) {
    const uint64_t b=core->entries()[i].bearer;
    if(!b || b>0xffffffffffffULL || (uint8_t(b>>40)&0x10) || uint16_t(b)!=sid || ((b>>16)&0xffff)!=eid || ((b>>32)&255)!=ecc) continue;
    if(match && match!=b) { match=0; conflict=true; break; }
    match=b;
  }
  // FIC supplies UTC; SI programme LTO is used for local presentation when known.
  int16_t offset=core->count()?core->at(0).lto:core->offset();
  core->select(match,utc,offset); core->identityConflict=conflict;
}
void liveEndpoint(uint32_t sid,uint32_t cid) {
  if(!core || replay) return;
  if(endpointSid!=sid || endpointCid!=cid) { liveReset(); endpointSid=sid; endpointCid=cid; }
}
void livePayload(const uint8_t* data,size_t n) { if(core && !replay) core->feed(data,n,millis()); }
void stopReplay() {
  replay=demo=false; pendingSid=0; receiver.reset();
  if(core) { core->reset(); core->select(0,0,0); }
  EpgClose();
}
static uint32_t radioSid() { return radio.ServiceStart && radio.ServiceIndex<radio.numberofservices ? radio.service[radio.ServiceIndex].ServiceID:0; }
static void startReplay() {
  core->reset(); replay=true; demo=false; activity=millis(); receiver.reset();
  snapshotFm=radio.isFm(); snapshotFreq=dabfreq; snapshotSid=radioSid();
  core->select(0x40e231533233ULL,epoch(2024,11,12,3,30),60);
}
void poll() {
  static bool autoAttempted=false;
  if(!core && !autoAttempted && !radio.isFm() && radio.spiPresent() && radio.ServiceStart) { autoAttempted=true; ensure(); }
  if(!core) return;
  const uint32_t nowMs=millis();
  if(replay && (snapshotFm!=radio.isFm() || snapshotFreq!=dabfreq || (snapshotSid && radioSid() && snapshotSid!=radioSid()))) {
    diagSerial.println("EPG INFO replay cancelled by radio service/mux change"); stopReplay();
  }
  if(replay && !demo && uint32_t(nowMs-activity)>30000U) {
    diagSerial.println("EPG INFO replay idle timeout; returning to live"); stopReplay();
  }
  if(demo && demoIndex<sizeof(epgDemoGroups)/sizeof(epgDemoGroups[0]) && !core->busy()) {
    const EpgDemoGroup& g=epgDemoGroups[demoIndex++];
    if((g.data[0]&15)==4) core->focus(uint16_t(g.data[5])<<8|g.data[6]);
    core->feed(g.data,g.size,nowMs);
  }
  core->pump(48); core->expire(nowMs); receiver.expire(nowMs);
  if(replay && pendingSid && core->stats.si) {
    if(!core->selectSid(pendingSid)) diagSerial.println("EPG ERROR STATION unknown or ambiguous after SI");
    pendingSid=0; EpgInvalidate();
  }
  if(!replay && !radio.isFm() && radio.ServiceStart) {
    char* end=nullptr; const unsigned eid=strtoul(radio.EID,&end,16);
    static uint32_t timeGeneration=0, timeMillis=0, timeUtc=0;
    if(radio.timeSampleGeneration()!=timeGeneration) {
      timeGeneration=radio.timeSampleGeneration(); timeMillis=nowMs;
      timeUtc=radio.Year>=2020?epoch(radio.Year,radio.Months,radio.Days,radio.Hours,radio.Minutes,radio.Seconds):0;
    }
    const uint32_t utc=timeUtc && uint32_t(nowMs-timeMillis)<300000U?timeUtc+(nowMs-timeMillis)/1000:0;
    liveContext(radioSid(),uint16_t(eid),uint8_t(radio.ensembleEcc),utc);
  }
}
static bool hexValue(const char* text,uint64_t& value) {
  if(!*text || strlen(text)>16) return false;
  value=0;
  for(;*text;++text) { unsigned d=0; if(*text>='0' && *text<='9') d=*text-'0'; else if(*text>='a' && *text<='f') d=*text-'a'+10; else if(*text>='A' && *text<='F') d=*text-'A'+10; else return false; value=(value<<4)|d; }
  return true;
}
static void status() {
  const Stats& s=core->stats;
  diagSerial.printf("EPG OK STATUS mode=%s frames=%u crcBad=%u invalid=%u dup=%u conflict=%u incomplete=%u busy=%u\n",replay?"TEST":"LIVE",s.frames,s.crcBad,s.invalid,s.duplicate,s.conflict,s.incomplete,s.busy);
  diagSerial.printf("EPG DATA dirs=%u objects=%u PI=%u SI=%u PNG=%u programmes=%u cached=%u limited=%u ambiguous=%u now=%d next=%d\n",s.directories,s.objects,s.pi,s.si,s.png,s.programmes,unsigned(core->count()),core->limited,core->ambiguous(),core->current(),core->next());
  diagSerial.printf("EPG RX unmapped=%u oversize=%u timeout=%u fresh=%u continuityJumps=%u unsupported=%u\n",s.unmapped,s.oversize,s.timeout,core->fresh(),s.continuityJumps,s.unsupported);
  diagSerial.printf("EPG RAM arena=%u free=%u min=%u largest=%u audio=%u SLS=%u UARTdrop=%u rxCRC=%u rxOrder=%u rxIdentity=%u\n",unsigned(Core::memoryBytes()),ESP.getFreeHeap(),ESP.getMinFreeHeap(),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),radio.ServiceStart,radio.SlideShowAvailable,diagSerial.droppedBytes(),receiver.crcErrors,receiver.orderErrors,receiver.identityErrors);
  diagSerial.printf("EPG LOGO bytes=%u interlace=%u\n",unsigned(core->logoSize()),core->logoSize()>28?unsigned(core->logo()[28]):0U);
  diagSerial.printf("EPG POOLS free8=%u largest8=%u free32=%u largest32=%u\n",unsigned(heap_caps_get_free_size(byteCaps)),unsigned(heap_caps_get_largest_free_block(byteCaps)),unsigned(heap_caps_get_free_size(wordCaps)),unsigned(heap_caps_get_largest_free_block(wordCaps)));
}
bool command(char* input) {
  if(!strncmp(input,"E1 ",3)) {
    if(!replay || demo || !core) { diagSerial.println("EPG ERROR START required"); return true; }
    if(diagSerial.availableForWrite()<160) return true; // sender retries; no silent commit
    activity=millis();
    const auto result=receiver.receive(input,*core,activity);
    if(result==UartReceiver::Result::Ack) diagSerial.printf("EPG ACK %04X %08X\n",receiver.session(),unsigned(receiver.sequence()));
    else {
      const char* reasons[]={"ACK","BUSY","CRC","FORMAT","ORDER","IDENTITY","DATA"};
      diagSerial.printf("EPG NACK %s\n",reasons[unsigned(result)]);
    }
    return true;
  }
  // EPG command spelling is intentionally case-insensitive, framed CRC input is not modified.
  if(strncasecmp(input,"EPG TEST ",9)) return false;
  if(diagSerial.availableForWrite()<1024) return true;
  char* arg=input+9; activity=millis();
  if(!strcasecmp(arg,"HELP")) {
    diagSerial.println("EPG OK HELP: DEMO | START [SID FULLCID] | TIME ISO8601 | STATION HEX | STATUS | STOP | CLEAR");
    return true;
  }
  if(!strcasecmp(arg,"STOP") || !strcasecmp(arg,"CLEAR")) { const bool clear=!strcasecmp(arg,"CLEAR"); stopReplay(); diagSerial.printf("EPG OK %s\n",clear?"CLEAR":"STOP"); return true; }
  if(!ensure(true)) return true;
  if(!strcasecmp(arg,"DEMO")) {
    startReplay(); demo=true; demoIndex=0;
    core->select(0x40e231533211ULL,epoch(2024,11,12,3,30),60);
    EpgOpen(); diagSerial.println("EPG OK DEMO source=actual-MUX11B MSC directory+F64C+F629; TEST historical data"); return true;
  }
  if(!strncasecmp(arg,"START",5) && (!arg[5] || arg[5]==' ')) {
    unsigned sid=0xE23153FEU,cid=0x01F2C00CU; int used=0;
    if(arg[5] && (sscanf(arg+5," %8x %8x%n",&sid,&cid,&used)!=2 || arg[5+used] || (cid&0xc000)!=0xc000)) { diagSerial.println("EPG ERROR START identity"); return true; }
    startReplay(); const unsigned session=(esp_random()&0xffffU)|1U;
    receiver.reset(sid,cid,session); EpgOpen(); diagSerial.printf("EPG OK START V1 session=%04X fragment=32 group=4071 timeout=5000 idle=30000\n",session); return true;
  }
  if(!strcasecmp(arg,"STATUS")) { status(); return true; }
  if(!replay) { diagSerial.println("EPG ERROR START required"); return true; }
  if(!strncasecmp(arg,"TIME ",5)) {
    uint32_t utc=0; int16_t offset=0;
    if(!parseIso(arg+5,utc,offset)) diagSerial.println("EPG ERROR TIME use ISO8601 with Z or explicit offset");
    else { core->setTime(utc,offset); EpgInvalidate(); diagSerial.println("EPG OK TIME"); }
    return true;
  }
  if(!strncasecmp(arg,"STATION ",8)) {
    uint64_t id=0;
    if(!hexValue(arg+8,id)) diagSerial.println("EPG ERROR STATION hex expected");
    else if(id<=UINT32_MAX) {
      if(core->selectSid(uint32_t(id))) { EpgInvalidate(); diagSerial.println("EPG OK STATION"); }
      else if(!core->stats.si) { pendingSid=uint32_t(id); core->select(0,core->now(),core->offset()); diagSerial.println("EPG OK STATION pending SI"); }
      else diagSerial.println("EPG ERROR STATION unknown or ambiguous; use full bearer");
    } else {
      uint8_t bytes[8]; const unsigned n=id>>48?8:6;
      for(unsigned i=0;i<n;++i) bytes[n-1-i]=uint8_t(id>>(8*i));
      uint64_t validated=0;
      if(!bearerValue(bytes,n,validated)) diagSerial.println("EPG ERROR STATION invalid bearer");
      else { pendingSid=0; core->select(validated,core->now(),core->offset()); EpgInvalidate(); diagSerial.println("EPG OK STATION"); }
    }
    return true;
  }
  diagSerial.println("EPG ERROR unknown command; EPG TEST HELP"); return true;
}
}
