#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Diagnostics only: no field in this retained image controls boot or GPIOs.
// A checksum-valid image is evidence of writes, never proof that RTC memory
// survived a complete power loss or that the preceding boot was on battery.
namespace X4Boot {
constexpr uint32_t Magic = 0x58344233;
enum Milestone : uint32_t { None=0, Boot, Provider, App, Display, Failure };
enum Phase : uint32_t { AppMain = 1, RailReady, Variant, SetupGate };
struct Record {
  uint32_t magic, checksum, boot, phase, operation, reset, raw0, raw1, wake;
  uint32_t gpioBefore, holdBefore, brownout, entryUs, variantUs, gateUs;
  uint32_t gpioHighBefore, strapBefore, reserved;
  uint64_t milestoneUs, firstDisplayUs;
  uint32_t milestoneKind, milestoneCount, displayCompleted;
  char milestone[160];
  uint32_t messageTruncated;
};
static_assert(sizeof(Record) == 264, "X4 boot breadcrumb size changed");
inline const char* phaseName(uint32_t phase) {
  switch(phase){case AppMain:return "app-main";case RailReady:return "rail-ready";
    case Variant:return "arduino-variant";case SetupGate:return "setup-gate";default:return "unavailable";}
}
inline const char* milestoneName(uint32_t kind) {
  switch(kind){case Boot:return "boot";case Provider:return "provider";case App:return "app";
    case Display:return "first-display";case Failure:return "failure";default:return "none";}
}
inline uint32_t checksum(const Record& record) {
  const auto* bytes = reinterpret_cast<const unsigned char*>(&record);
  uint32_t hash = 2166136261u;
  for (size_t i = offsetof(Record, boot); i < sizeof(record); ++i)
    hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}
inline bool valid(const Record& record) {
  return record.magic == Magic && record.boot && record.phase >= AppMain &&
    record.phase <= SetupGate && record.operation <= 7 && record.reserved==0 &&
    record.milestoneKind<=Failure && record.displayCompleted<=1 && record.messageTruncated<=1 &&
    ((record.milestoneCount==0)==(record.milestoneKind==None)) &&
    std::memchr(record.milestone,0,sizeof(record.milestone)) && record.checksum == checksum(record);
}
inline void changing(Record& record) {
  record.magic = 0;
  asm volatile("" ::: "memory");
}
inline void seal(Record& record) {
  record.checksum = checksum(record);
  asm volatile("" ::: "memory");
  record.magic = Magic;
}
inline void mark(Record& record, Phase phase, uint32_t operation = 0) {
  changing(record); record.phase = phase; record.operation = operation; seal(record);
}
}

namespace X4Boot {
// Bounded observation only. Touch/move/draw chatter never mutates RTC storage.
inline bool prefix(const char* value,const char* wanted) {
  return std::strncmp(value,wanted,std::strlen(wanted))==0;
}
inline Milestone classify(const Record& record,const char* line) {
  if(!line)return None;
  const char* body=line;
  if(prefix(body,"RTE_STAGE us=")) {
    body+=13;unsigned digits=0;
    while(*body>='0'&&*body<='9'&&digits<20){++body;++digits;}
    if(!digits||*body!=' ')return None;
    ++body;
    if(prefix(body,"boot "))return Boot;
    if(prefix(body,"provider ")||prefix(body,"providers "))return Provider;
    if(prefix(body,"app load ")||prefix(body,"app init ")||prefix(body,"app entry "))return App;
  } else if(prefix(body,"RTE_BOOT error="))return Failure;
  else if(prefix(body,"APP t_ms=")) {
    body+=9;unsigned digits=0;
    while(*body>='0'&&*body<='9'&&digits<20){++body;++digits;}
    if(!digits||*body!=' ')return None;
    ++body;
    if(prefix(body,"stage=display-failed "))return Failure;
    if(prefix(body,"stage=display-complete result=complete") && !record.displayCompleted)return Display;
  }
  return None;
}
inline bool observe(Record& record,const char* line,uint64_t nowUs) {
  // Current RAM is already owned/initialized. Checksum validation belongs to
  // reset recovery, not every log/touch/frame callback.
  if(record.magic!=Magic || !record.boot)return false;
  Milestone kind=classify(record,line);
  if(kind==None)return false;
  char text[256];size_t length=0;
  while(length+1<sizeof(text) && line[length]){text[length]=line[length];++length;}
  text[length]=0;
  if(kind!=Display && (std::strstr(text,"failed")||std::strstr(text,"result=-")))kind=Failure;
  changing(record);
  record.milestoneUs=nowUs;record.milestoneKind=kind;
  if(record.milestoneCount!=UINT32_MAX)++record.milestoneCount;
  if(kind==Display){record.displayCompleted=1;record.firstDisplayUs=nowUs;}
  size_t i=0;while(i+1<sizeof(record.milestone)&&text[i]) {
    const char c=text[i];record.milestone[i]=(c=='\r'||c=='\n')?' ':c;++i;
  }
  record.messageTruncated=text[i]!=0;
  if(record.messageTruncated && i>=3){record.milestone[i-3]='.';record.milestone[i-2]='.';record.milestone[i-1]='.';}
  while(i<sizeof(record.milestone))record.milestone[i++]=0;
  seal(record);return true;
}
}

// Versioned on-flash diagnostic data. Contains no application settings or
// Bluetooth identifiers. Session numbers come from flash, not RTC retention.
namespace X4BootLog {
constexpr uint32_t Magic = 0x58424c31;
constexpr unsigned SlotCount = 8;
constexpr unsigned CheckpointLimit = 64;
struct Session {
  uint32_t magic, bytes;
  uint64_t sequence;
  uint32_t revision, checkpointWrites;
  X4Boot::Record checkpoint;
  char firstFailure[160];
  uint32_t checksum;
};
inline uint32_t checksum(const Session& s) {
  const auto* p=reinterpret_cast<const unsigned char*>(&s);
  uint32_t h=2166136261u;
  for(size_t i=0;i<offsetof(Session,checksum);++i)h=(h^p[i])*16777619u;
  return h;
}
inline bool valid(const Session& s) {
  return s.magic==Magic && s.bytes==sizeof(Session) && s.sequence &&
    s.revision && X4Boot::valid(s.checkpoint) &&
    std::memchr(s.firstFailure,0,sizeof(s.firstFailure)) && s.checksum==checksum(s);
}
inline void seal(Session& s) { s.checksum=checksum(s); }
inline Session begin(uint64_t sequence,const X4Boot::Record& r) {
  Session s{};s.magic=Magic;s.bytes=sizeof(s);s.sequence=sequence;
  s.revision=1;s.checkpointWrites=1;s.checkpoint=r;seal(s);return s;
}
inline bool failure(const X4Boot::Record& r) {
  return r.milestoneKind==X4Boot::Failure;
}
inline bool advance(Session& s,const X4Boot::Record& r,bool early=false) {
  if(!X4Boot::valid(r) || s.magic!=Magic || s.bytes!=sizeof(Session))return false;
  // Preserve terminal evidence even if shutdown emits additional ordinary
  // milestones. A successfully displayed frame does not mean shutdown was clean.
  const bool failed=failure(r);
  if(!early && s.firstFailure[0])return false;
  const bool firstFrame=r.displayCompleted && !s.checkpoint.displayCompleted;
  if(!early && !failed &&
     (s.checkpoint.displayCompleted ||
      (s.checkpointWrites>=CheckpointLimit && !firstFrame)))return false;
  if(s.revision==UINT32_MAX)return false;
  s.checkpoint=r;++s.revision;
  if(s.checkpointWrites!=UINT32_MAX)++s.checkpointWrites;
  if(failed && !s.firstFailure[0]) {
    std::memcpy(s.firstFailure,r.milestone,sizeof(s.firstFailure));
    s.firstFailure[sizeof(s.firstFailure)-1]=0;
  }
  seal(s);return true;
}
}
