#include "epg_core.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>

namespace epg {
static uint16_t be16(Bytes p) { return uint16_t(p[0])<<8 | p[1]; }
static uint32_t be32(Bytes p) { return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3]; }
uint16_t crc16(Bytes p, size_t n) {
  size_t index=0; uint16_t c=0xffff;
  while(n--) { c ^= uint16_t(p[index++])<<8; for(unsigned i=0;i<8;++i) c=uint16_t((c<<1)^((c&0x8000)?0x1021:0)); }
  return c ^ 0xffff;
}
uint32_t crc32(Bytes p, size_t n) {
  size_t index=0; uint32_t c=0xffffffff;
  while(n--) { c ^= p[index++]; for(unsigned i=0;i<8;++i) c=(c>>1)^((c&1)?0xedb88320U:0); }
  return c^0xffffffff;
}
// Gregorian arithmetic independent of host timezone/time_t width.
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m<=2; const int era=(y>=0?y:y-399)/400;
  const unsigned yo=unsigned(y-era*400), mp=m>2?m-3:m+9;
  return int64_t(era)*146097 + yo*365+yo/4-yo/100+(153*mp+2)/5+d-1-719468;
}
uint32_t epoch(int y, unsigned m, unsigned d, unsigned h, unsigned min, unsigned sec) {
  if(y<1970 || y>2105 || m<1 || m>12 || d<1 || d>31 || h>23 || min>59 || sec>59) return 0;
  const int64_t days=daysFromCivil(y,m,d);
  const int64_t next= m==12 ? daysFromCivil(y+1,1,1) : daysFromCivil(y,m+1,1);
  if(days>=next) return 0;
  const int64_t t=days*86400+h*3600+min*60+sec;
  return t>0 && t<=UINT32_MAX ? uint32_t(t) : 0;
}
void calendar(uint32_t seconds, int& y, unsigned& m, unsigned& d) {
  int64_t z=seconds/86400+719468; const int64_t era=z/146097;
  const unsigned doe=unsigned(z-era*146097), yo=(doe-doe/1460+doe/36524-doe/146096)/365;
  y=int(yo)+int(era)*400; const unsigned dy=doe-(365*yo+yo/4-yo/100), mp=(5*dy+2)/153;
  d=dy-(153*mp+2)/5+1; m=mp<10?mp+3:mp-9; y+=m<=2;
}
bool parseIso(const char* text, uint32_t& utc, int16_t& offset) {
  if(!text) return false;
  const size_t length=strlen(text);
  if(length!=20 && length!=25) return false;
  for(size_t i=0;i<19;++i) {
    const char required=i==4 || i==7?'-':i==10?'T':i==13 || i==16?':':0;
    if(required ? text[i]!=required : (text[i]<'0' || text[i]>'9')) return false;
  }
  if(length==20 && text[19]!='Z') return false;
  if(length==25) {
    if((text[19]!='+' && text[19]!='-') || text[22]!=':') return false;
    const unsigned positions[]={20,21,23,24};
    for(unsigned i:positions) if(text[i]<'0' || text[i]>'9') return false;
  }
  int y=0,m=0,d=0,h=0,mi=0,s=0,oh=0,om=0,used=0; char sign=0;
  if(sscanf(text,"%d-%d-%dT%d:%d:%d%n",&y,&m,&d,&h,&mi,&s,&used)!=6) return false;
  const char* suffix=text+used;
  if(strcmp(suffix,"Z")==0) offset=0;
  else {
    int n=0;
    if(sscanf(suffix,"%c%d:%d%n",&sign,&oh,&om,&n)!=3 || suffix[n] || (sign!='+' && sign!='-') || (oh>14 || (oh==14 && om!=0)) || om>59 || oh<0 || om<0) return false;
    offset=int16_t((sign=='-'?-1:1)*(oh*60+om));
  }
  const uint32_t local=epoch(y,unsigned(m),unsigned(d),unsigned(h),unsigned(mi),unsigned(s));
  const int64_t result=int64_t(local)-int32_t(offset)*60;
  if(!local || result<=0 || result>UINT32_MAX) return false;
  utc=uint32_t(result); return true;
}
static uint32_t bits(Bytes p, unsigned offset, unsigned count) {
  uint32_t v=0; for(unsigned i=0;i<count;++i) v=(v<<1)|((p[(offset+i)/8]>>(7-(offset+i)%8))&1); return v;
}
bool timePoint(Bytes p, size_t n, uint32_t& utc, int16_t& off) {
  if(n<4) return false;
  const bool longForm=bits(p,18,1), lto=bits(p,19,1);
  const unsigned len=longForm?6:4;
  if(n!=len+(lto?1:0) || (lto && (p[len]&31)>28)) return false;
  const uint32_t mjd=bits(p,1,17); const unsigned h=bits(p,20,5), m=bits(p,25,6), s=longForm?bits(p,31,6):0;
  if(mjd<40587 || h>23 || m>59 || s>59) return false;
  const uint64_t seconds=uint64_t(mjd-40587)*86400+h*3600+m*60+s;
  if(seconds>UINT32_MAX) return false;
  off=lto?int16_t((p[len]&31)*30*((p[len]&32)?-1:1)):0;
  utc=uint32_t(seconds); return true;
}
bool bearerValue(Bytes p, size_t n, uint64_t& out) {
  // DAB ID: top two flags, SCIdS, ECC, EId, 16/32-bit SId (TS 102 371 fig.4).
  if((n!=6 && n!=8) || !(p[0]&0x40) || bool(p[0]&0x10)!=(n==8) || (p[0]&0x20)) return false;
  out=0; for(size_t i=0;i<n;++i) out=(out<<8)|p[i]; return true;
}
void Assembly::reset() { *this=Assembly{}; }
size_t Assembly::size() const { return last==0xffff?0:size_t(last)*stride+lengths[last]; }
int Assembly::add(uint16_t id, uint16_t seg, bool final, Bytes p, uint16_t n,
                  Buffer buffer, size_t cap, uint32_t now, Stats& stats) {
  if(seg>=32 || !n || n>cap) { ++stats.invalid; return -1; }
  Assembly next=*this;
  const bool replaced=next.active && next.tid!=id;
  if(replaced) next.reset();
  if(!next.active) { next.active=true; next.tid=id; }
  const uint32_t flag=uint32_t(1)<<seg;
  if((next.last!=0xffff && (seg>next.last || (final && seg!=next.last))) ||
     ((next.mask&flag) && final!=(seg==next.last)) ||
     (!final && next.stride && n!=next.stride)) { ++stats.conflict; return -1; }
  if(final && next.last==0xffff) {
    if(next.mask && seg<31 && (next.mask>>(seg+1))) { ++stats.conflict; return -1; }
    next.last=seg;
  }
  const bool relocate=!final && !next.stride && next.last!=0xffff && (next.mask&(uint32_t(1)<<next.last));
  if(!final && !next.stride) next.stride=n;
  if(final && !seg && !next.stride) next.stride=n;
  if(next.last!=0xffff && next.stride && size_t(next.last)*next.stride+(next.last==seg?n:next.lengths[next.last])>cap) { ++stats.invalid; return -1; }
  const size_t offset=next.stride?size_t(seg)*next.stride:cap-n;
  if(offset+n>cap || (final && next.stride && n>next.stride)) { ++stats.invalid; return -1; }
  if(next.mask&flag) {
    if(next.lengths[seg]!=n || !(buffer+offset).equals(p,n)) { ++stats.conflict; return -1; }
    ++stats.duplicate; return 0;
  }
  // No writes or context mutations until every bound and conflict check passed.
  if(relocate) buffer.move(size_t(next.last)*next.stride,cap-next.lengths[next.last],next.lengths[next.last]);
  buffer.write(offset,p,n); next.lengths[seg]=n; next.mask|=flag; next.touched=now;
  *this=next;
  if(replaced) ++stats.incomplete;
  const uint32_t wanted=last==31?UINT32_MAX:last==0xffff?0:((uint32_t(1)<<(last+1))-1);
  return stride && wanted && mask==wanted ? 1:0;
}
Core::Core() : Core(::malloc, ::free) {}
Core::Core(Allocate allocate, Release release, bool wordOnly) : release_(release) {
  if(!allocate || !release_) return;
  body_=Buffer(allocate(kObjectBytes),wordOnly);
  if(body_) directory_=Buffer(allocate(kDirectoryBytes),wordOnly);
  if(directory_) logo_=Buffer(allocate(kObjectBytes),wordOnly);
  if(logo_) text_=Buffer(allocate(kTextBytes),wordOnly);
  if(body_) body_.zero(kObjectBytes);
  if(directory_) directory_.zero(kDirectoryBytes);
  if(logo_) logo_.zero(kObjectBytes);
  if(text_) text_.zero(kTextBytes);
}
Core::~Core() {
  if(!release_) return;
  if(text_) release_(text_.allocation());
  if(logo_) release_(logo_.allocation());
  if(directory_) release_(directory_.allocation());
  if(body_) release_(body_.allocation());
}
void Core::rebuildView() {
  viewCount_=0;
  for(size_t i=0;i<count_;++i) if(cache_[i].context==context_) view_[viewCount_++]=uint8_t(i);
}
void Core::eraseText(uint16_t offset) {
  if(!offset) return;
  size_t bytes=1;
  while(offset+bytes<textUsed_ && text_[offset+bytes-1]) ++bytes;
  text_.move(offset,offset+bytes,textUsed_-offset-bytes);
  textUsed_-=bytes;
  for(size_t i=0;i<count_;++i) {
    uint16_t* fields[]={&cache_[i].title,&cache_[i].description};
    for(auto field:fields) {
      if(*field==offset) *field=0;
      else if(*field>offset) *field=uint16_t(*field-bytes);
    }
  }
}
void Core::eraseProgramme(size_t index) {
  eraseText(cache_[index].title); eraseText(cache_[index].description);
  for(size_t i=index+1;i<count_;++i) cache_[i-1]=cache_[i];
  --count_;
}
void Core::evictContext(unsigned index) {
  size_t kept=0;
  for(size_t i=0;i<count_;++i) if(cache_[i].context!=index) cache_[kept++]=cache_[i];
  count_=kept;
  // Compact once per eviction. Each retained string has a unique offset.
  size_t source=1, destination=1;
  while(true) {
    uint16_t* next=nullptr;
    for(size_t i=0;i<count_;++i) {
      uint16_t* fields[]={&cache_[i].title,&cache_[i].description};
      for(auto field:fields) if(*field>=source && (!next || *field<*next)) next=field;
    }
    if(!next) break;
    const size_t offset=*next;
    size_t bytes=1;
    while(offset+bytes<textUsed_ && text_[offset+bytes-1]) ++bytes;
    text_.move(destination,offset,bytes);
    *next=uint16_t(destination); source=offset+bytes; destination+=bytes;
  }
  textUsed_=destination;
  contexts_[index]=Context{};
  ++revision;
}
int Core::oldestContext(bool populatedOnly) const {
  int oldest=-1; uint32_t age=0;
  for(unsigned i=0;i<8;++i) if(i!=context_ && contexts_[i].valid) {
    bool populated=false;
    for(size_t j=0;j<count_;++j) if(cache_[j].context==i) { populated=true; break; }
    if(populatedOnly && !populated) continue;
    if(!populatedOnly && !populated) return int(i);
    const uint32_t candidate=useSequence_-contexts_[i].used;
    if(oldest<0 || candidate>age) { oldest=int(i); age=candidate; }
  }
  return oldest;
}
uint16_t Core::newEpoch() {
  if(++nextEpoch_==0) {
    nextEpoch_=1;
    for(auto& entry:entries_) entry.epoch=0;
    for(auto& context:contexts_) context.epoch=0;
  }
  return nextEpoch_;
}
void Core::activateContext() {
  contexts_[context_].limited=limited;
  unsigned target=8;
  for(unsigned i=0;i<8;++i) if(contexts_[i].valid && contexts_[i].bearer==selected_ && contexts_[i].day==day_) { target=i; break; }
  if(target==8) {
    for(unsigned i=0;i<8;++i) if(!contexts_[i].valid) { target=i; break; }
    if(target==8) { target=unsigned(oldestContext(false)); evictContext(target); }
    Context& c=contexts_[target]; c=Context{};
    c.valid=true; c.bearer=selected_; c.day=day_;
    c.epoch=newEpoch();
  }
  context_=uint8_t(target); cacheEpoch_=contexts_[target].epoch;
  // An existing context may have survived the epoch counter wrap.
  if(!cacheEpoch_) {
    cacheEpoch_=contexts_[target].epoch=newEpoch();
  }
  contexts_[target].used=++useSequence_; limited=contexts_[target].limited;
  rebuildView(); ++revision;
  // A partially decoded object belongs to the previous selection.
  bodyAssembly_.reset(); phase_=0;
}
void Core::clearCache() {
  count_=viewCount_=0; textUsed_=1; text_.set(0,0); logoBytes_=0; logoBearer_=0; limited=false;
  for(auto& context:contexts_) context=Context{};
  context_=0; activateContext();
}
void Core::reset() {
  identityConflict=false;
  stats=Stats{}; entryCount_=serviceCount_=0; directoryHash_=0; browseOffset_=0; focus_=0;
  dirAssembly_.reset(); bodyAssembly_.reset(); phase_=0; clearCache();
}
void Core::select(uint64_t bearer, uint32_t utc, int16_t off) {
  if(bearer!=selected_) browseOffset_=0;
  const uint32_t day=uint32_t((int64_t(utc)+int32_t(off)*60)/86400+browseOffset_);
  if(bearer!=selected_ || day!=day_) { selected_=bearer; day_=day; activateContext(); }
  now_=utc; offset_=off;
}
void Core::setTime(uint32_t utc, int16_t off) { browseOffset_=0; select(selected_,utc,off); }
void Core::browseDay(int delta) {
  const int next=browseOffset_+delta;
  if(next < -7 || next > 7 || !delta) return;
  browseOffset_=next;
  day_=uint32_t((int64_t(now_)+int32_t(offset_)*60)/86400+browseOffset_);
  activateContext();
}
bool Core::selectSid(uint32_t sid) {
  uint64_t match=0;
  for(size_t i=0;i<serviceCount_;++i) {
    const uint32_t serviceSid=(services_[i].bearer>>48)?uint32_t(services_[i].bearer):uint16_t(services_[i].bearer);
    if(serviceSid!=sid) continue;
    if(services_[i].conflict || (match && match!=services_[i].bearer)) return false;
    match=services_[i].bearer;
  }
  if(!match) return false;
  select(match,now_,offset_); return true;
}
const Service* Core::station() const { for(size_t i=0;i<serviceCount_;++i) if(services_[i].bearer==selected_) return services_+i; return nullptr; }
bool Core::ambiguous() const { const Service* s=station(); return identityConflict || (s && s->conflict); }
void Core::expire(uint32_t now) {
  monotonicMs_=now;
  Assembly* contexts[]={&bodyAssembly_,&dirAssembly_};
  for(auto a:contexts) if(a->active && uint32_t(now-a->touched)>assemblyTimeoutMs_) { a->reset(); ++stats.timeout; }
}
bool Core::header(Bytes p, size_t n, Entry& e, size_t& used) const {
  if(n<7) return false;
  const size_t hs=((size_t(p[3])&15)<<9)|(size_t(p[4])<<1)|(p[5]>>7);
  if(hs<7 || hs>n) return false;
  e.size=(uint32_t(p[0])<<20)|(uint32_t(p[1])<<12)|(uint32_t(p[2])<<4)|(p[3]>>4);
  e.type=(p[5]>>1)&63; e.subtype=((p[5]&1)<<8)|p[6];
  size_t pos=7;
  while(pos<hs) {
    const uint8_t tag=p[pos++], pli=tag>>6; size_t length=pli==0?0:pli==1?1:pli==2?4:0;
    if(pli==3) { if(pos>=hs) return false; length=p[pos]&127; if(p[pos++]&128) { if(pos>=hs) return false; length=(length<<8)|p[pos++]; } }
    if(length>hs-pos) return false;
    if((tag&63)==12 && length>1) { size_t chars=length-1; if(chars>=sizeof(e.name)) return false; (p+pos+1).copyTo(e.name,chars); e.name[chars]=0; }
    if((tag&63)==0x23 || ((tag&63)==0x11 && (length!=1 || p[pos]!=0))) e.type|=128;
    if((tag&63)==0x27) bearerValue(p+pos,length,e.bearer);
    if((tag&63)==0x25 || (tag&63)==0x26) {
      uint32_t value=0; int16_t lto=0;
      if(timePoint(p+pos,length,value,lto)) { if((tag&63)==0x25) { e.start=value; e.startLto=lto; } else { e.stop=value; e.stopLto=lto; } }
    }
    pos+=length;
  }
  used=hs; return true;
}
bool Core::directory(Bytes p, size_t n) {
  if(n<13 || (p[0]&0xc0) || (be32(p)&0x3fffffff)!=n || (p[9]&0x80)) return false;
  const size_t count=be16(p+4), extension=be16(p+11);
  if(count>kMaxEntries || extension>n-13) return false;
  size_t pos=13+extension;
  uint16_t ids[kMaxEntries];
  uint32_t names[kMaxEntries]={};
  // Validate the whole directory before replacing valid metadata.
  for(size_t i=0;i<count;++i) { if(pos+2>n) return false; ids[i]=be16(p+pos); for(size_t j=0;j<i;++j) if(ids[j]==ids[i]) return false; Entry e; size_t used=0; if(!header(p+pos+2,n-pos-2,e,used)) return false; if(e.name[0]) { names[i]=crc32(reinterpret_cast<const uint8_t*>(e.name),strlen(e.name)); for(size_t j=0;j<i;++j) if(names[j]==names[i]) return false; } pos+=2+used; }
  if(pos!=n) return false;
  const uint32_t hash=crc32(p,n);
  if(hash==directoryHash_) return true;
  directoryHash_=hash; entryCount_=count; serviceCount_=0; bodyAssembly_.reset(); clearCache();
  pos=13+extension;
  for(size_t i=0;i<count;++i) { Entry e; size_t used=0; e.tid=be16(p+pos); header(p+pos+2,n-pos-2,e,used); entries_[i]=e; pos+=2+used; }
  for(size_t i=0;i<count;++i) if(entries_[i].start && !entries_[i].stop)
    for(size_t j=0;j<count;++j) if(entries_[j].bearer==entries_[i].bearer && entries_[j].start>entries_[i].start &&
       (!entries_[i].stop || entries_[j].start<entries_[i].stop)) { entries_[i].stop=entries_[j].start; entries_[i].stopLto=entries_[j].startLto; }
  const uint32_t period=(uint32_t(p[6])<<16)|(uint32_t(p[7])<<8)|p[8];
  const uint64_t timeout=uint64_t(period)*300+60000;
  assemblyTimeoutMs_=period?uint32_t(timeout>1800000?1800000:timeout<120000?120000:timeout):300000;
  ++stats.directories;
  if(observer.object) observer.object(observer.user,dirAssembly_.tid,6,p,n);
  return true;
}
static bool tlv(Bytes p, size_t n, size_t pos, uint8_t& tag, size_t& start, size_t& end) {
  if(pos+2>n) return false;
  tag=p[pos++]; size_t length=p[pos++];
  if(length>=254) { const unsigned extra=length==254?2:3; if(extra>n-pos) return false; length=0; for(unsigned i=0;i<extra;++i) length=(length<<8)|p[pos++]; }
  if(length>n-pos) return false;
  start=pos; end=pos+length; return true;
}
static bool container(uint8_t tag) {
  return tag==2 || tag==3 || (tag>=0x10 && tag<=0x1c) || (tag>=0x20 && tag<=0x36);
}
static bool pngValid(Bytes p,size_t n) {
  static const uint8_t magic[]={137,80,78,71,13,10,26,10};
  if(n<33 || !p.equals(magic,8)) return false;
  size_t pos=8; bool ihdr=false, data=false;
  while(n-pos>=12) {
    const size_t len=be32(p+pos); if(len>n-pos-12) return false;
    if(crc32(p+pos+4,len+4)!=be32(p+pos+8+len)) return false;
    if(!ihdr) { if(len!=13 || !(p+pos+4).equals(reinterpret_cast<const uint8_t*>("IHDR"),4) || !be32(p+pos+8) || !be32(p+pos+12) || be32(p+pos+8)>512 || be32(p+pos+12)>512) return false; ihdr=true; }
    if((p+pos+4).equals(reinterpret_cast<const uint8_t*>("IDAT"),4)) data=true;
    if((p+pos+4).equals(reinterpret_cast<const uint8_t*>("IEND"),4)) return !len && data && pos+12==n;
    pos+=len+12;
  }
  return false;
}
uint16_t Core::wanted() const {
  if(focus_) return focus_;
  // Acquire one relevant object across carousel turns. Other bodies are drained,
  // not allowed to evict missing segments of the requested object.
  if(!serviceCount_) for(size_t i=0;i<entryCount_;++i) if(entries_[i].type==7 && entries_[i].subtype==0 && entries_[i].epoch!=cacheEpoch_ &&
      (!entries_[i].start || !now_ || (entries_[i].start<=now_ && entries_[i].stop>now_))) return entries_[i].tid;
  for(size_t i=0;i<entryCount_;++i) {
    const Entry& e=entries_[i];
    if(e.type==7 && e.subtype==1 && e.bearer==selected_ && e.epoch!=cacheEpoch_ &&
       (!e.start || (int64_t(e.start)+int32_t(e.startLto)*60<int64_t(day_+1)*86400 &&
                    (!e.stop || int64_t(e.stop)+int32_t(e.stopLto)*60>int64_t(day_)*86400)))) return e.tid;
  }
  const Service* s=station();
  if(s && !s->conflict && s->png && !logoSize())
    for(size_t i=0;i<entryCount_;++i) if(entries_[i].type==2 && entries_[i].subtype==3 && !strcmp(entries_[i].name,s->logo)) return entries_[i].tid;
  return 0;
}
Feed Core::feed(Bytes p, size_t n, uint32_t now) {
  if(busy()) { ++stats.busy; return Feed::Busy; }
  monotonicMs_=now;
  ++stats.frames;
  if(!p || n<11 || n>kMaxGroup) { ++stats.invalid; return Feed::Rejected; }
  if(crc16(p,n-2)!=be16(p+(n-2))) { ++stats.crcBad; return Feed::Rejected; }
  const uint8_t type=p[0]&15;
  // Require CRC, segmentation and user-access transport ID. Extension supported.
  if((p[0]&0x70)!=0x70 || (type!=4 && type!=6)) { ++stats.invalid; return Feed::Rejected; }
  const unsigned stream=type==6?1:0;
  const uint8_t continuity=p[1]>>4;
  const uint8_t previous=stats.lastContinuity[stream];
  if(previous!=255 && continuity!=previous && continuity!=((previous+1)&15)) ++stats.continuityJumps;
  stats.lastContinuity[stream]=continuity; stats.lastRepetition[stream]=p[1]&15;
  size_t pos=(p[0]&128)?4:2;
  if(pos+3>n-2) { ++stats.invalid; return Feed::Rejected; }
  const uint16_t segment=be16(p+pos); pos+=2;
  const uint8_t ua=p[pos++]; const size_t uaLen=ua&15;
  if(!(ua&16) || uaLen<2 || uaLen>n-2-pos || pos+uaLen+2>n-2) { ++stats.invalid; return Feed::Rejected; }
  const uint16_t tid=be16(p+pos); pos+=uaLen;
  stats.lastMotRepetition=p[pos]>>5;
  const size_t length=be16(p+pos)&8191; pos+=2;
  if(length!=n-2-pos || !length) { ++stats.invalid; return Feed::Rejected; }
  Assembly& a=type==6?dirAssembly_:bodyAssembly_;
  Buffer buffer=type==6?directory_:body_; const size_t cap=type==6?kDirectoryBytes:kObjectBytes;
  size_t index=0;
  if(type==4) {
    while(index<entryCount_ && entries_[index].tid!=tid) ++index;
    if(index==entryCount_) { ++stats.unmapped; return Feed::Accepted; }
    if(entries_[index].type&128) { ++stats.unsupported; return Feed::Accepted; }
    if(entries_[index].size>cap) { ++stats.oversize; return Feed::Accepted; }
    if(entries_[index].type==7 && entries_[index].subtype==1 && entries_[index].bearer==selected_ &&
       entries_[index].epoch==cacheEpoch_ && contexts_[context_].haveData) {
      if(!fresh()) ++revision;
      contexts_[context_].touched=now;
    }
    if(tid!=wanted()) return Feed::Accepted;
    if(entries_[index].seen && entries_[index].epoch==cacheEpoch_) { ++stats.duplicate; return Feed::Accepted; }
  }
  const int result=a.add(tid,segment&32767,(segment&32768)!=0,p+pos,uint16_t(length),buffer,cap,now,stats);
  if(result<0) return Feed::Rejected;
  if(!result) return Feed::Accepted;
  const size_t size=a.size();
  if(type==6) { if(!directory(buffer,size)) { ++stats.invalid; a.reset(); return Feed::Rejected; } a.reset(); return Feed::Accepted; }
  if(size!=entries_[index].size) { ++stats.invalid; a.reset(); return Feed::Rejected; }
  objectIndex_=uint16_t(index); objectLength_=size; a.reset();
  Entry& e=entries_[index];
  if(e.type==2 && e.subtype==3) {
    if(!pngValid(body_,size)) { ++stats.invalid; return Feed::Rejected; }
    if(!e.seen) { ++stats.png; ++stats.objects; if(observer.object) observer.object(observer.user,e.tid,4,body_,size); }
    e.seen=true; e.epoch=cacheEpoch_;
    const Service* s=station();
    if(s && !s->conflict && s->png && !strcmp(s->logo,e.name)) { logo_.write(0,body_,size); logoBytes_=size; logoBearer_=selected_; ++revision; }
    return Feed::Accepted;
  }
  if(e.type!=7 || e.subtype>1 || body_[0]!=(e.subtype==0?3:2)) { ++stats.invalid; return Feed::Rejected; }
  for(auto& token:tokens_) token=Token{};
  phase_=1; position_=0; depth_=0; rootService_=body_[0]==3;
  return Feed::Accepted;
}
bool Core::text(Bytes p,size_t n,char* out,size_t capacity) {
  size_t used=out?strlen(out):0; bool truncated=false;
  for(size_t i=0;i<n;) {
    const uint8_t value=p[i]; Bytes chars=p+i; size_t length=1;
    if(value<32 && tokens_[value].length) { chars=body_+tokens_[value].offset; length=tokens_[value].length; ++i; }
    else {
      length=value<128?1:(value&224)==192?2:(value&240)==224?3:(value&248)==240?4:0;
      if(!length || length>n-i || (value<32 && value!=9 && value!=10 && value!=13)) return false;
      i+=length;
    }
    for(size_t j=0;j<length;) {
      const uint8_t c=chars[j]; const size_t size=c<128?1:(c&224)==192?2:(c&240)==224?3:(c&248)==240?4:0;
      if(!size || size>length-j) return false;
      for(size_t k=1;k<size;++k) if((chars[j+k]&192)!=128) return false;
      if(size==2 && c<194) return false;
      if(size==3 && ((c==224 && chars[j+1]<160) || (c==237 && chars[j+1]>=160))) return false;
      if(size==4 && (c>244 || (c==240 && chars[j+1]<144) || (c==244 && chars[j+1]>=144))) return false;
      if(out) {
        if(used+size<capacity && !truncated) { (chars+j).copyTo(out+used,size); used+=size; }
        else truncated=true;
      }
      j+=size;
    }
  }
  if(out) out[used]=0;
  if(truncated) limited=true;
  return true;
}
uint16_t Core::store(const char* p) {
  size_t n=strlen(p)+1;
  if(n>kTextBytes-textUsed_) { limited=true; return 0; }
  const uint16_t off=uint16_t(textUsed_); text_.write(textUsed_,reinterpret_cast<const uint8_t*>(p),n); textUsed_+=n; return off;
}
static void copyString(Bytes text,char* out,size_t capacity) {
  size_t i=0;
  while(i+1<capacity && text[i]) { out[i]=char(text[i]); ++i; }
  out[i]=0;
}
const char* Core::title(size_t i) const { copyString(text_+at(i).title,titleCopy_,sizeof(titleCopy_)); return titleCopy_; }
const char* Core::description(size_t i) const { copyString(text_+at(i).description,descriptionCopy_,sizeof(descriptionCopy_)); return descriptionCopy_; }
void Core::cache(const Programme& p) {
  if(p.bearer!=selected_ || !p.start || p.duration>604800 || ambiguous()) return;
  const int64_t local=int64_t(p.start)+int32_t(p.lto)*60;
  const int64_t end=local+(p.duration?p.duration:1);
  if(end<=int64_t(day_)*86400 || local>=int64_t(day_+1)*86400) return;
  for(size_t i=0;i<count_;++i) if(cache_[i].context==context_ && cache_[i].id==p.id && cache_[i].start==p.start) {
    if(int16_t(p.version-cache_[i].version)<=0) return;
    eraseProgramme(i); break;
  }
  const size_t titleBytes=strlen(p.name)+1, allBytes=titleBytes+strlen(p.description)+1;
  while(count_==kMaxProgrammes || allBytes>kTextBytes-textUsed_) {
    const int victim=oldestContext(true);
    if(victim<0) break;
    evictContext(unsigned(victim));
  }
  rebuildView();
  if(count_==kMaxProgrammes || titleBytes>kTextBytes-textUsed_) { limited=true; return; }
  CachedProgramme c; c.start=p.start; c.duration=p.duration; c.actual=p.actual;
  c.actualDuration=p.actualDuration; c.id=p.id; c.version=p.version; c.lto=p.lto; c.uncertain=p.uncertain || !p.duration;
  c.context=context_;
  c.title=store(p.name); c.description=store(p.description);
  if(!c.title) return;
  size_t index=count_;
  while(index && (cache_[index-1].start>c.start || (cache_[index-1].start==c.start && cache_[index-1].id>c.id))) { cache_[index]=cache_[index-1]; --index; }
  cache_[index]=c; ++count_; rebuildView();
  contexts_[context_].haveData=true; contexts_[context_].touched=monotonicMs_;
  ++revision;
}
void Core::service(const Service& s) {
  if(!s.bearer || !s.name[0]) return;
  if(observer.service) observer.service(observer.user,entries_[objectIndex_].tid,s);
  for(size_t i=0;i<serviceCount_;++i) if(services_[i].bearer==s.bearer) {
    if(strcmp(services_[i].name,s.name)) { services_[i].conflict=true; if(s.bearer==selected_) clearCache(); }
    return;
  }
  if(serviceCount_==16) { limited=true; return; }
  services_[serviceCount_++]=s; ++revision;
  // A logo may have preceded SI; permit receiving that object on the next turn.
  if(s.bearer==selected_) for(size_t i=0;i<entryCount_;++i) if(!strcmp(entries_[i].name,s.logo)) entries_[i].epoch=0;
}
void Core::close(uint8_t tag) {
  if(tag==0x28 && rootService_) service(service_);
  if(tag==0x1c || tag==0x2e) {
    Programme& p=programme_[tag==0x2e?1:0];
    if(!p.name[0]) snprintf(p.name,sizeof(p.name),"%s",p.medium[0]?p.medium:p.shortName);
    if(!entries_[objectIndex_].seen) { if(tag==0x1c) ++stats.programmes; else ++stats.events; }
    if(entries_[objectIndex_].bearer && p.bearer!=entries_[objectIndex_].bearer) { p.uncertain=true; ++stats.conflict; }
    if(observer.programme) observer.programme(observer.user,entries_[objectIndex_].tid,p);
    if(tag==0x1c) cache(p);
    programmeDepth_=tag==0x2e?1:0;
  }
}
void Core::attribute(uint8_t tag,Bytes p,size_t n) {
  const uint8_t parent=stack_[depth_-1].tag;
  Programme& prog=programme_[programmeDepth_==2?1:0];
  uint32_t value=0; if(n<=4) for(size_t i=0;i<n;++i) value=(value<<8)|p[i];
  if((parent==0x25 || parent==0x29 || parent==0x2d) && tag==0x80) {
    uint64_t bearer=0;
    if(bearerValue(p,n,bearer)) {
      if(rootService_) service_.bearer=bearer;
      else if(programmeDepth_) prog.bearer=bearer;
      else scope_=bearer;
    }
  }
  if(parent==0x1c || parent==0x2e) {
    if(tag==0x81 && n<=4) prog.id=value;
    if(tag==0x82 && n<=2) prog.version=uint16_t(value);
    if(tag==0x80 && n<sizeof(prog.crid)) { p.copyTo(prog.crid,n); prog.crid[n]=0; }
  }
  if(parent==0x2c || parent==0x2f) {
    if(tag==0x80 || tag==0x82) {
      uint32_t seconds=0; int16_t offset=0;
      bool valid=parent==0x2c?timePoint(p,n,seconds,offset):(n<=4 && programmeDepth_==2 && programme_[0].start && value<=604800);
      if(valid) {
        if(parent==0x2f) { seconds=programme_[0].start+value; offset=programme_[0].lto; }
        if(tag==0x82) prog.actual=seconds;
        else { if(prog.start && prog.start!=seconds) prog.uncertain=true; else { prog.start=seconds; prog.lto=offset; } }
      } else { prog.uncertain=true; if(n<=sizeof(prog.rejectedTime)) { p.copyTo(prog.rejectedTime,n); prog.rejectedTimeLength=uint8_t(n); } }
    }
    if(tag==0x81) { if(n==2) prog.duration=value; else prog.uncertain=true; }
    if(tag==0x83) { if(n==2) prog.actualDuration=value; else prog.uncertain=true; }
  }
  if(parent==0x2b && rootService_) {
    if(tag==0x80) service_.png=n==9 && p.equals(reinterpret_cast<const uint8_t*>("image/png"),9);
    if(tag==0x82 && n<sizeof(service_.logo)) { p.copyTo(service_.logo,n); service_.logo[n]=0; }
  }
}
void Core::completeObject() {
  Entry& e=entries_[objectIndex_];
  if(!e.seen) { ++stats.objects; if(rootService_) ++stats.si; else ++stats.pi;
    if(observer.object) observer.object(observer.user,e.tid,4,body_,objectLength_);
  }
  e.seen=true; e.epoch=cacheEpoch_; phase_=0;
  if(!rootService_ && e.bearer==selected_ && viewCount_) {
    if(!fresh()) ++revision;
    contexts_[context_].haveData=true; contexts_[context_].touched=monotonicMs_;
  }
}
void Core::pump(unsigned budget) {
  while(phase_ && budget--) {
    if(depth_ && position_==stack_[depth_-1].end) {
      if(phase_==2) close(stack_[depth_-1].tag);
      --depth_;
      if(!depth_) {
        if(position_!=objectLength_) { ++stats.invalid; phase_=0; return; }
        if(phase_==2) { completeObject(); return; }
        phase_=2; position_=0; scope_=defaultScope_=0; programmeDepth_=0;
        for(auto& token:tokens_) token=Token{};
      }
      continue;
    }
    uint8_t tag=0; size_t start=0,end=0;
    const size_t limit=depth_?stack_[depth_-1].end:objectLength_;
    if(!tlv(body_,limit,position_,tag,start,end) || (!depth_ && end!=objectLength_)) { ++stats.invalid; phase_=0; return; }
    const uint8_t parent=depth_?stack_[depth_-1].tag:0;
    position_=end;
    if(tag==4) {
      size_t pos=start; uint32_t tokenMask=0;
      while(pos<end) {
        if(pos+2>end) { ++stats.invalid; phase_=0; return; }
        const uint8_t key=body_[pos++], length=body_[pos++];
        if(!key || key>=20 || key==9 || key==10 || key==13 || !length || length>end-pos || (tokenMask&(uint32_t(1)<<key))) { ++stats.invalid; phase_=0; return; }
        tokenMask|=uint32_t(1)<<key;
        for(unsigned j=0;j<length;++j) if(body_[pos+j]<32 && body_[pos+j]!=9 && body_[pos+j]!=10 && body_[pos+j]!=13) { ++stats.invalid; phase_=0; return; }
        if(!text(body_+pos,length,nullptr,0)) { ++stats.invalid; phase_=0; return; }
        tokens_[key].offset=uint16_t(pos); tokens_[key].length=length;
        pos+=length;
      }
    } else if(tag>=128) {
      if(phase_==2 && depth_) attribute(tag,body_+start,end-start);
    } else if(tag==1) {
      if(phase_==1 && !text(body_+start,end-start,nullptr,0)) { ++stats.invalid; phase_=0; return; }
      if(phase_==2) {
        Programme& p=programme_[programmeDepth_==2?1:0];
        char* dest=nullptr; size_t cap=0;
        if(rootService_ && (parent==0x10 || parent==0x11 || parent==0x12)) { dest=service_.name; cap=sizeof(service_.name); }
        else if(parent==0x10) { dest=p.shortName; cap=sizeof(p.shortName); }
        else if(parent==0x11) { dest=p.medium; cap=sizeof(p.medium); }
        else if(parent==0x12) { dest=p.name; cap=sizeof(p.name); }
        else if(parent==0x1a || parent==0x1b) { dest=p.description; cap=sizeof(p.description); }
        if(dest && !text(body_+start,end-start,dest,cap)) { ++stats.invalid; phase_=0; return; }
      }
    } else if(tag==5) {
      if(phase_==2) { defaultScope_=0; bearerValue(body_+start,end-start,defaultScope_); scope_=defaultScope_; }
    } else if(container(tag)) {
      if(depth_==24) { ++stats.invalid; phase_=0; return; }
      stack_[depth_++]={end,tag}; position_=start;
      if(phase_==2) {
        if(tag==0x21) scope_=defaultScope_;
        if(tag==0x28 && rootService_) service_=Service{};
        if(tag==0x2b && rootService_) { service_.png=false; service_.logo[0]=0; }
        if(rootService_ && (tag==0x11 || tag==0x12)) service_.name[0]=0;
        if(tag==0x1c || tag==0x2e) { programmeDepth_=tag==0x1c?1:2; Programme& p=programme_[programmeDepth_-1]; p=Programme{}; p.bearer=scope_; p.event=tag==0x2e; }
        if(tag==0x1b) programme_[programmeDepth_==2?1:0].description[0]=0;
      }
    }
  }
}
int Core::current(bool* overlap) const {
  int chosen=-1; unsigned active=0;
  if(now_ && fresh() && !ambiguous()) for(size_t i=0;i<viewCount_;++i) {
    const auto& p=at(i);
    if(p.uncertain) continue;
    // Explicit policy: original planned interval, latest start wins, then lower ID.
    if(p.start<=now_ && uint64_t(p.start)+p.duration>now_) { ++active;
      if(chosen<0 || p.start>at(size_t(chosen)).start || (p.start==at(size_t(chosen)).start && p.id<at(size_t(chosen)).id)) chosen=int(i);
    }
  }
  if(overlap) *overlap=active>1;
  return chosen;
}
int Core::next() const {
  if(!now_ || !fresh() || ambiguous()) return -1;
  for(size_t i=0;i<viewCount_;++i) if(!at(i).uncertain && at(i).start>now_) return int(i);
  return -1;
}
} // namespace epg
