#pragma once
// Bounded, platform-neutral packet MOT/SPI core. ETSI EN 301 234 / TS 102 371.
#include <stdint.h>
#include <stddef.h>

namespace epg {
static const size_t kObjectBytes = 12288, kDirectoryBytes = 4096;
static const size_t kMaxGroup = 4071, kMaxEntries = 80, kMaxProgrammes = 96, kTextBytes = 8192;
// Read-only bytes without exposing a byte pointer to word-only ESP32 IRAM.
// Volatile aligned word accesses prevent the compiler from narrowing loads.
class Bytes {
 protected:
  const uint8_t* bytes_=nullptr;
  const volatile uint32_t* words_=nullptr;
  size_t offset_=0;
  __attribute__((noinline)) static uint32_t loadWord(const volatile uint32_t* p) { return *p; }
 public:
  Bytes() = default;
  Bytes(const uint8_t* p) : bytes_(p) {}
  static Bytes words(const volatile uint32_t* p) { Bytes b; b.words_=p; return b; }
  explicit operator bool() const { return bytes_ || words_; }
  uint8_t operator[](size_t i) const {
    i+=offset_;
    return words_?uint8_t(loadWord(words_+(i>>2))>>((i&3)*8)):bytes_[i];
  }
  Bytes operator+(size_t offset) const { Bytes b=*this; b.offset_+=offset; return b; }
  void copyTo(void* destination,size_t n) const {
    uint8_t* out=static_cast<uint8_t*>(destination);
    for(size_t i=0;i<n;++i) out[i]=(*this)[i];
  }
  bool equals(Bytes other,size_t n) const { for(size_t i=0;i<n;++i) if((*this)[i]!=other[i]) return false; return true; }
};
class Buffer : public Bytes {
  __attribute__((noinline)) static void storeWord(volatile uint32_t* p,uint32_t value) { *p=value; }
 public:
  Buffer() = default;
  Buffer(uint8_t* p) : Bytes(p) {}
  Buffer(void* p,bool wordOnly) {
    if(wordOnly) words_=static_cast<uint32_t*>(p); else bytes_=static_cast<uint8_t*>(p);
  }
  void* allocation() const { return words_?static_cast<void*>(const_cast<uint32_t*>(words_)):const_cast<uint8_t*>(bytes_); }
  void set(size_t i,uint8_t value) {
    i+=offset_;
    if(words_) {
      volatile uint32_t* p=const_cast<volatile uint32_t*>(words_)+(i>>2);
      const unsigned shift=(i&3)*8;
      storeWord(p,(loadWord(p)&~(uint32_t(255)<<shift))|(uint32_t(value)<<shift));
    } else const_cast<uint8_t*>(bytes_)[i]=value;
  }
  void zero(size_t n) {
    if(words_) for(size_t i=0;i<n/4;++i) storeWord(const_cast<volatile uint32_t*>(words_)+i,0);
    else for(size_t i=0;i<n;++i) set(i,0);
  }
  void write(size_t offset,Bytes source,size_t n) { for(size_t i=0;i<n;++i) set(offset+i,source[i]); }
  void move(size_t destination,size_t source,size_t n) {
    if(destination<source) for(size_t i=0;i<n;++i) set(destination+i,(*this)[source+i]);
    else while(n) { --n; set(destination+n,(*this)[source+n]); }
  }
};
uint16_t crc16(Bytes p, size_t n);
uint32_t crc32(Bytes p, size_t n);
uint32_t epoch(int year, unsigned month, unsigned day, unsigned hour=0, unsigned minute=0, unsigned second=0);
void calendar(uint32_t seconds, int& year, unsigned& month, unsigned& day);
bool parseIso(const char* text, uint32_t& utc, int16_t& offsetMinutes);
bool timePoint(Bytes p, size_t n, uint32_t& utc, int16_t& offsetMinutes);
bool bearerValue(Bytes p, size_t n, uint64_t& out);

struct Programme {
  uint64_t bearer = 0;
  uint32_t start=0, duration=0, actual=0, actualDuration=0, id=0;
  uint16_t version=0;
  int16_t lto=0;
  bool event=false, uncertain=false;
  uint8_t rejectedTime[7]={}, rejectedTimeLength=0;
  char name[128]={}, medium[64]={}, shortName[32]={}, description[512]={}, crid[64]={};
};
struct Service {
  uint64_t bearer=0;
  char name[48]={}, logo[32]={};
  bool conflict=false, png=false;
};
struct Entry {
  uint32_t size=0, start=0, stop=0;
  uint64_t bearer=0;
  uint16_t tid=0, subtype=0, epoch=0;
  int16_t startLto=0, stopLto=0;
  uint8_t type=0;
  bool seen=false;
  char name[32]={};
};
struct CachedProgramme {
  uint32_t start=0, duration=0, actual=0, actualDuration=0, id=0;
  uint16_t title=0, description=0, version=0;
  int16_t lto=0;
  bool uncertain=false;
  uint8_t context=0;
};
struct Stats {
  uint32_t frames=0, crcBad=0, invalid=0, duplicate=0, conflict=0, incomplete=0;
  uint32_t unmapped=0, oversize=0, unsupported=0, continuityJumps=0;
  uint8_t lastContinuity[2]={255,255}, lastRepetition[2]={}, lastMotRepetition=0;
  uint32_t timeout=0, busy=0, directories=0, objects=0, pi=0, si=0, png=0, programmes=0, events=0;
};
// The same callbacks receive offline and real receiver objects; no JSON decoder.
struct Observer {
  void* user=nullptr;
  void (*programme)(void*, uint16_t, const Programme&)=nullptr;
  void (*service)(void*, uint16_t, const Service&)=nullptr;
  void (*object)(void*, uint16_t, uint8_t, Bytes, size_t)=nullptr;
};
enum class Feed : uint8_t { Accepted, Busy, Rejected };
struct Assembly {
  uint16_t tid=0, stride=0, last=0xffff, lengths[32]={};
  uint32_t mask=0, touched=0;
  bool active=false;
  void reset();
  // 1=complete, 0=in progress/duplicate, -1=invalid/conflict.
  int add(uint16_t id, uint16_t segment, bool final, Bytes p, uint16_t n,
          Buffer buffer, size_t capacity, uint32_t now, Stats& stats);
  size_t size() const;
};

class Core {
 public:
  Stats stats;
  Observer observer;
  typedef void* (*Allocate)(size_t);
  typedef void (*Release)(void*);
  Core();
  Core(Allocate allocate, Release release, bool wordOnly=true);
  ~Core();
  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;
  bool ready() const { return body_ && directory_ && logo_ && text_; }
  static size_t bufferBytes() { return 2*kObjectBytes+kDirectoryBytes+kTextBytes; }
  static size_t memoryBytes() { return sizeof(Core)+bufferBytes(); }
  static size_t largestBlock() { return sizeof(Core)>kObjectBytes?sizeof(Core):kObjectBytes; }
  void reset();
  void focus(uint16_t tid) { if(focus_!=tid) { focus_=tid; bodyAssembly_.reset(); } } // bounded acquisition target; also used by host inventory test
  void select(uint64_t bearer, uint32_t utc, int16_t displayOffset);
  bool selectSid(uint32_t sid); // rejects multiple identities or SI conflicts
  void setTime(uint32_t utc, int16_t displayOffset);
  void browseDay(int delta);
  int dayOffset() const { return browseOffset_; }
  uint32_t localDay() const { return day_*86400U; }
  uint64_t selected() const { return selected_; }
  uint32_t now() const { return now_; }
  int16_t offset() const { return offset_; }
  const Service* station() const;
  bool ambiguous() const;
  Feed feed(Bytes data, size_t size, uint32_t nowMs);
  void pump(unsigned budget=48);
  void expire(uint32_t nowMs);
  bool busy() const { return phase_ != 0; }
  size_t count() const { return viewCount_; }
  const CachedProgramme& at(size_t i) const { return cache_[view_[i]]; }
  const char* title(size_t i) const; // valid until the next title() call
  const char* description(size_t i) const; // independent DRAM copy
  int current(bool* overlapping=nullptr) const;
  int next() const;
  Bytes logo() const { return logoSize() ? Bytes(logo_) : Bytes(); }
  size_t logoSize() const { return logoBearer_==selected_ ? logoBytes_ : 0; }
  bool fresh() const { return contexts_[context_].haveData && uint32_t(monotonicMs_-contexts_[context_].touched)<=900000U; }
  bool limited=false, identityConflict=false;
  uint32_t revision=0;
  const Entry* entries() const { return entries_; }
  size_t entryCount() const { return entryCount_; }
 private:
  // Allocate only at construction; separate bounded blocks tolerate ESP32 heap regions.
  Release release_;
  Buffer body_, directory_, logo_;
  Entry entries_[kMaxEntries];
  Service services_[16];
  CachedProgramme cache_[kMaxProgrammes];
  struct Context {
    uint64_t bearer=0;
    uint32_t day=0, used=0, touched=0;
    uint16_t epoch=0;
    bool valid=false, haveData=false, limited=false;
  } contexts_[8];
  uint8_t view_[kMaxProgrammes]={}, context_=0;
  size_t viewCount_=0;
  uint32_t useSequence_=0;
  uint64_t logoBearer_=0;
  Buffer text_;
  mutable char titleCopy_[128]={}, descriptionCopy_[512]={};
  size_t entryCount_=0, serviceCount_=0, count_=0, textUsed_=1, logoBytes_=0;
  Assembly dirAssembly_, bodyAssembly_;
  uint64_t selected_=0;
  uint32_t now_=0, day_=0, directoryHash_=0, assemblyTimeoutMs_=300000;
  int16_t offset_=0;
  int browseOffset_=0;
  uint16_t cacheEpoch_=1, nextEpoch_=1, focus_=0;
  uint16_t wanted() const;
  struct Level { size_t end; uint8_t tag; } stack_[24];
  struct Token { uint16_t offset=0, length=0; } tokens_[32];
  size_t position_=0, objectLength_=0;
  uint8_t depth_=0, phase_=0; // 1 validation, 2 incremental semantic decode
  uint16_t objectIndex_=0;
  uint64_t scope_=0, defaultScope_=0;
  uint32_t monotonicMs_=0;
  Programme programme_[2];
  Service service_;
  uint8_t programmeDepth_=0;
  bool rootService_=false;
  bool directory(Bytes p, size_t n);
  bool header(Bytes p, size_t n, Entry& entry, size_t& used) const;
  void clearCache();
  void activateContext();
  uint16_t newEpoch();
  void rebuildView();
  void eraseProgramme(size_t index);
  void eraseText(uint16_t offset);
  void evictContext(unsigned index);
  int oldestContext(bool populatedOnly) const;
  void completeObject();
  void close(uint8_t tag);
  void attribute(uint8_t tag, Bytes p, size_t n);
  bool text(Bytes p, size_t n, char* out, size_t capacity);
  void cache(const Programme& p);
  uint16_t store(const char* p);
  void service(const Service& s);
};
} // namespace epg
