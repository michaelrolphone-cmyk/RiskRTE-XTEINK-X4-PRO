// X4-only native composition. Ordinary providers still require full admission.
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <rom/rtc.h>
#include <soc/gpio_reg.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>
#include <cstdio>
#include <esp_heap_caps.h>
#include "X4BootRecord.h"
#include <RiscDiagnosticSourceV1.h>
#include "X4NativeBuildIdentity.h"

extern "C" const char risc_x4_native_composition_identity[] = X4_NATIVE_COMPOSITION_IDENTITY;
// The selected Runtime environments provide this bounded, sole-owner sink.
// It is used only by the setup gate, after Runtime starts its diagnostics.
namespace RiscDiagnostics { void line(const char* text); }

extern "C" { RTC_NOINIT_ATTR X4Boot::Record risc_x4_boot_record; }


// Storage runs outside the borrowed-line observer and never uses Serial.
// The paired app-data volume is mounted by Runtime, never formatted here.
#include <nvs.h>
#include <nvs_flash.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <climits>
#ifndef X4_BOOTLOG_INTERNAL_ROOT
#define X4_BOOTLOG_INTERNAL_ROOT "/appdata"
#endif
namespace X4BootLog { namespace {
constexpr const char* Namespace="x4_bootlog";
constexpr const char* Internal=X4_BOOTLOG_INTERNAL_ROOT "/x4-boot.log";
constexpr const char* Previous=X4_BOOTLOG_INTERNAL_ROOT "/x4-boot.previous.log";
constexpr size_t MaxLogBytes=128*1024;
Session history[SlotCount]{};
Session current{};
nvs_handle_t handle{};
bool available=false,initialized=false,pending=false,insideDrain=false;
bool fileReady=false,historyExported=false,fileUncertain=false;
uint32_t fileAttemptRevision=0;unsigned fileAttempts=0;
uint32_t writtenRevision=0,fileRevision=0;
esp_err_t storageError=ESP_OK;
int fileError=0;

void key(unsigned slot,char (&out)[8]) { std::snprintf(out,sizeof(out),"boot%u",slot); }
bool store() {
  if(!available || !valid(current))return false;
  if(writtenRevision==current.revision)return true;
  char name[8];key(unsigned(current.sequence%SlotCount),name);
  esp_err_t result=nvs_set_blob(handle,name,&current,sizeof(current));
  if(result==ESP_OK)result=nvs_commit(handle);
  if(result!=ESP_OK) { storageError=result;available=false;return false; }
  writtenRevision=current.revision;
  history[current.sequence%SlotCount]=current;
  return true;
}
void startFlash(const X4Boot::Record& record) {
  if(initialized)return;
  initialized=true;current=begin(0,record);
  // No erase-and-retry: exhausted/corrupt NVS must not erase settings/evidence.
  storageError=nvs_flash_init();
  if(storageError!=ESP_OK)return;
  storageError=nvs_open(Namespace,NVS_READWRITE,&handle);
  if(storageError!=ESP_OK)return;
  uint64_t newest=0;
  for(unsigned i=0;i<SlotCount;++i) {
    char name[8];key(i,name);Session candidate{};size_t bytes=sizeof(candidate);
    esp_err_t result=nvs_get_blob(handle,name,&candidate,&bytes);
    if(result==ESP_OK && bytes==sizeof(candidate) && valid(candidate) &&
       candidate.sequence%SlotCount==i) {
      history[i]=candidate;
      if(candidate.sequence>newest)newest=candidate.sequence;
    } else if(result!=ESP_OK && result!=ESP_ERR_NVS_NOT_FOUND &&
              result!=ESP_ERR_NVS_INVALID_LENGTH) {
      // An unreadable history is not an empty history. Do not reuse its keys.
      storageError=result;nvs_close(handle);return;
    }
  }
  if(newest==UINT64_MAX) { storageError=ESP_FAIL;nvs_close(handle);return; }
  current=begin(newest+1,record);available=true;(void)store();
}
void checkpoint(const X4Boot::Record& record) {
  if(advance(current,record,true))(void)store();
}
void observe() { pending=true; } // O(1), no I/O, called only by diagnostic owner.
bool directory(const char* root) {
  struct stat s{};return stat(root,&s)==0 && S_ISDIR(s.st_mode);
}
int formatSession(char* text,size_t capacity,const Session& s,const char* origin) {
  const auto& r=s.checkpoint;
  int n=std::snprintf(text,capacity,
    "X4_BOOTLOG seq=%llu revision=%lu origin=%s power_source=unmeasured "
    "rtc_boot=%lu phase=%s operation=%lu reset=%lu raw0=%lu raw1=%lu wake=%lu "
    "entry_us=%lu variant_us=%lu gate_us=%lu gpio=0x%08lx gpio1=0x%08lx "
    "strap=0x%08lx hold=0x%08lx brownout_reg=0x%08lx "
    "milestones=%lu kind=%s last_us=%llu first_display=%lu first_display_us=%llu\n"
    "last_line=%s\nfirst_failure=%s\nX4_BOOTLOG record_end=complete\n",
    (unsigned long long)s.sequence,(unsigned long)s.revision,origin,
    (unsigned long)r.boot,X4Boot::phaseName(r.phase),(unsigned long)r.operation,
    (unsigned long)r.reset,(unsigned long)r.raw0,(unsigned long)r.raw1,(unsigned long)r.wake,
    (unsigned long)r.entryUs,(unsigned long)r.variantUs,(unsigned long)r.gateUs,
    (unsigned long)r.gpioBefore,(unsigned long)r.gpioHighBefore,
    (unsigned long)r.strapBefore,(unsigned long)r.holdBefore,(unsigned long)r.brownout,
    (unsigned long)r.milestoneCount,X4Boot::milestoneName(r.milestoneKind),
    (unsigned long long)r.milestoneUs,(unsigned long)r.displayCompleted,
    (unsigned long long)r.firstDisplayUs,r.milestone,
    s.firstFailure[0]?s.firstFailure:"none-recorded");
  return n>=0 && size_t(n)<capacity?n:-1;
}
bool syncClose(FILE* file) {
  bool okay=std::fflush(file)==0;
  if(okay && fsync(fileno(file))!=0)okay=false;
  if(!okay)fileError=errno?errno:EIO;
  if(std::fclose(file)!=0){okay=false;fileError=errno?errno:EIO;}
  return okay;
}
// Actual diagnostic text is retained separately from the small NVS crash
// summary. PSRAM allocation occurs only after Arduino initializes it. Before
// that, native rail events use a fixed RAM prefix. No observer allocates or I/Os.
constexpr size_t TraceBytes=64*1024, EarlyBytes=8192, TailReserve=2048;
char earlyText[EarlyBytes]{};
char* traceText=earlyText;
char* recoveryText=nullptr;
void* traceAllocation=nullptr;
size_t traceCapacity=EarlyBytes,traceBytes=0,recoveryBytes=0,persistedBytes=0;
uint32_t eventSequence=0;
bool traceClosed=false,traceOverflow=false,recoveryReady=false,frameComplete=false;
bool storageProbeReady=false,renderBusy=false,forceFlush=false;
uint64_t lastFileCommitUs=0,lastNvsCommitUs=0;
uint32_t lastFailureFlushedCount=0;
constexpr size_t PersistBatchBytes=4096;
constexpr uint64_t PersistIntervalUs=2000000;
unsigned terminalDetails=0;
struct EarlyEvent { uint64_t us; const char* operation; int result; };
EarlyEvent earlyEvents[10]{};unsigned earlyCount=0;
void early(const char* operation,int result) {
  if(earlyCount<10)earlyEvents[earlyCount++]={uint64_t(esp_timer_get_time()),operation,result};
}
void allocateTrace() {
  if(traceAllocation)return;
  traceAllocation=heap_caps_malloc(TraceBytes*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!traceAllocation)return;
  auto* buffer=static_cast<char*>(traceAllocation);
  std::memcpy(buffer,traceText,traceBytes);
  traceText=buffer;traceCapacity=TraceBytes;recoveryText=buffer+TraceBytes;
}
void textLine(const char* line,uint64_t nowUs,bool essential=false) {
  if(!line || !*line || (traceOverflow && !essential))return;
  char copied[256];size_t length=0;
  while(length+1<sizeof(copied) && line[length]) {
    char c=line[length];copied[length]=(c=='\n'||c=='\r'||uint8_t(c)<32)?' ':c;++length;
  }
  copied[length]=0;
  const bool truncated=line[length]!=0;
  // Never persist credentials accidentally supplied by an application or
  // provider diagnostic. Keep the event position and explicit redaction.
  char lower[sizeof(copied)];
  for(size_t i=0;i<=length;++i)lower[i]=(copied[i]>='A'&&copied[i]<='Z')?char(copied[i]+32):copied[i];
  const char* keys[]={"password", "passphrase", "secret=", "token=", "credential", "authorization", "psk=", "api_key", "private_key"};
  for(const char* key:keys)if(std::strstr(lower,key)){std::strcpy(copied,"[redacted credential-bearing diagnostic]");break;}
  char row[448];
  const int n=std::snprintf(row,sizeof(row),"X4_TRACE session=%llu event=%lu us=%llu%s %s\n",
    (unsigned long long)current.sequence,(unsigned long)(eventSequence+1),
    (unsigned long long)nowUs,truncated?" truncated=1":"",copied);
  if(n<=0 || size_t(n)>=sizeof(row))return;
  const size_t limit=essential?traceCapacity:traceCapacity-TailReserve;
  if(traceBytes+size_t(n)>limit) {
    if(!traceOverflow){traceOverflow=true;forceFlush=true;textLine("capture overflow result=truncated capacity-exhausted",nowUs,true);}
    return;
  }
  std::memcpy(traceText+traceBytes,row,size_t(n));traceBytes+=size_t(n);++eventSequence;
}
void start(const X4Boot::Record& record) {
  startFlash(record);
  char line[256];
  std::snprintf(line,sizeof(line),"session begin reset=%lu raw0=%lu raw1=%lu wake=%lu rtc_boot=%lu power_source=unmeasured capture=app-main no-usb-required firmware=%s",
    (unsigned long)record.reset,(unsigned long)record.raw0,(unsigned long)record.raw1,
    (unsigned long)record.wake,(unsigned long)record.boot,X4_NATIVE_COMPOSITION_IDENTITY);
  textLine(line,record.entryUs,true);
  for(unsigned i=0;i<earlyCount;++i){
    std::snprintf(line,sizeof(line),"native init operation=%s result=%d",earlyEvents[i].operation,earlyEvents[i].result);
    textLine(line,earlyEvents[i].us);
  }
  std::snprintf(line,sizeof(line),"native nvs-init result=%ld session_identity=%s",
    (long)storageError,current.sequence?"persistent":"unassigned");
  textLine(line,uint64_t(esp_timer_get_time()));
}
void trace(const char* line,uint64_t nowUs,const X4Boot::Record& record) {
  if(std::strstr(line,"boot app-data end "))storageProbeReady=true;
  const bool appLine=X4Boot::prefix(line,"APP t_ms=");
  if(appLine && (std::strstr(line,"stage=draw-begin") || std::strstr(line,"stage=display-submit-begin")))renderBusy=true;
  if(appLine && (std::strstr(line,"stage=display-complete") || std::strstr(line,"stage=display-failed") || std::strstr(line,"stage=display-skip")))renderBusy=false;
  const bool failure=X4Boot::classify(record,line)==X4Boot::Failure;
  const bool detail=std::strstr(line," provider detail ")!=nullptr;
  if(failure || X4Boot::classify(record,line)==X4Boot::Display)forceFlush=true;
  if(traceClosed && !(terminalDetails<8 && (failure||detail)))return;
  if(traceClosed)++terminalDetails;
  if(X4Boot::prefix(line,"RTE_STAGE ") && std::strstr(line," app entry begin "))frameComplete=false;
  const bool app=X4Boot::prefix(line,"APP t_ms=");
  if(frameComplete && app && !failure &&
     (std::strstr(line,"stage=touch-") || std::strstr(line,"stage=draw-") ||
      std::strstr(line,"stage=transfer-") || std::strstr(line,"stage=display-")))return;
  textLine(line,nowUs,failure || detail);
  // A logo can be the first frame. Keep all later initialization stages,
  // including Clock/RTC and subsequent app entries, until bounded capacity.
  if(app && std::strstr(line,"stage=display-complete result=complete")) {
    frameComplete=true;
    if(!record.displayCompleted)textLine("first-display result=complete capture=continuing",nowUs,true);
  }
  if(!traceClosed && X4Boot::prefix(line,"RTE_BOOT error=")) {
    traceClosed=true;textLine("capture end result=failed",nowUs,true);
  }
}
void recoverFile() {
  // Freeze the recovered prefix before the source can be consumed. No later
  // prepend/reordering and no file access from the copied-source callbacks.
  if(recoveryReady)return;
  recoveryReady=true;
  if(!fileReady)return;
  struct stat info{};
  if(stat(Internal,&info)!=0){if(errno!=ENOENT)fileError=errno;return;}
  if(!S_ISREG(info.st_mode)){fileError=EISDIR;return;}
  if(recoveryText && info.st_size>0 && size_t(info.st_size)<=TraceBytes) {
    FILE* file=std::fopen(Internal,"rb");
    if(file) {
      recoveryBytes=std::fread(recoveryText,1,size_t(info.st_size),file);
      if(std::ferror(file)){fileError=errno?errno:EIO;recoveryBytes=0;}
      if(std::fclose(file)!=0){fileError=errno?errno:EIO;recoveryBytes=0;}
      // A torn trailing line is not a complete saved event.
      while(recoveryBytes && recoveryText[recoveryBytes-1]!='\n')--recoveryBytes;
    } else fileError=errno;
  } else if(info.st_size>0)fileError=EFBIG;
  // Rotate once per session, before appending current text. Previous contains
  // the last attempt even if the new boot stops before a first display.
  if(std::rename(Internal,Previous)!=0){fileError=errno;fileUncertain=true;}
}
bool append() {
  if(!fileReady || !recoveryReady || fileUncertain || persistedBytes==traceBytes)return false;
  // Pre-write failures may be retried twice per new trace revision. Once any
  // append/close is uncertain it is never repeated in this boot.
  if(fileAttemptRevision!=eventSequence){fileAttemptRevision=eventSequence;fileAttempts=0;}
  if(fileAttempts>=2)return false;
  ++fileAttempts;
  FILE* file=std::fopen(Internal,"ab");
  if(!file){fileError=errno;return false;}
  const size_t bytes=traceBytes-persistedBytes;
  const bool okay=std::fwrite(traceText+persistedBytes,1,bytes,file)==bytes;
  const bool closed=syncClose(file);
  if(!okay || !closed){
    fileUncertain=true;if(!fileError)fileError=EIO;
    char line[128];std::snprintf(line,sizeof(line),"native trace persistence result=uncertain errno=%d retry=disabled",fileError);
    textLine(line,uint64_t(esp_timer_get_time()),true);return false;
  }
  persistedBytes=traceBytes;historyExported=true;fileRevision=current.revision;return true;
}
void drain(const X4Boot::Record& record) {
  if(insideDrain)return;
  // Capture is cheap even in display/input code. Filesystem and ordinary NVS
  // work wait until the frame is complete; a terminal failure remains durable.
  const bool fatal=record.milestoneKind==X4Boot::Failure && record.milestoneCount!=lastFailureFlushedCount;
  if(renderBusy && !fatal)return;
  const uint64_t nowUs=uint64_t(esp_timer_get_time());
  const bool critical=(record.displayCompleted && !current.checkpoint.displayCompleted) ||
                      (fatal && !current.firstFailure[0]);
  const bool batchDue=traceBytes-persistedBytes>=PersistBatchBytes;
  const bool timeDue=nowUs-lastFileCommitUs>=PersistIntervalUs;
  if(!forceFlush && !critical && !fatal && !batchDue && !timeDue)return;
  insideDrain=true;
  if(pending && (critical || nowUs-lastNvsCommitUs>=PersistIntervalUs)) {
    pending=false;
    if(advance(current,record)){
      (void)store();lastNvsCommitUs=uint64_t(esp_timer_get_time());
    }
  }
  if(!fileReady && storageProbeReady)fileReady=directory(X4_BOOTLOG_INTERNAL_ROOT);
  if(!recoveryReady && fileReady)recoverFile();
  if(append())lastFileCommitUs=uint64_t(esp_timer_get_time());
  if(fatal)lastFailureFlushedCount=record.milestoneCount;
  forceFlush=false;
  insideDrain=false;
}

} }

namespace {
const char* startupError = "x4-app-main-not-entered";
bool attempted = false, variant = false, reported = false, previousValid = false;
constexpr gpio_num_t peripheralRail = GPIO_NUM_1;
X4Boot::Record& retained = risc_x4_boot_record;
X4Boot::Record previous;

void prepareRail() {
  // GPIO1 is the documented peripheral/touch enable. A CPU self-latch has not
  // been established. Keep the previous pad hold until digital HIGH is ready.
  startupError = "x4-gpio1-rtc-deinit";
  X4Boot::mark(retained, X4Boot::AppMain, 1);
  {const int result=rtc_gpio_deinit(peripheralRail);X4BootLog::early("gpio1-rtc-deinit",result);if(result!=ESP_OK)return;}
  startupError = "x4-gpio1-stage-high";
  X4Boot::mark(retained, X4Boot::AppMain, 2);
  {const int result=gpio_set_level(peripheralRail, 1);X4BootLog::early("gpio1-stage-high",result);if(result!=ESP_OK)return;}
  gpio_config_t config{};
  config.pin_bit_mask = uint64_t(1) << peripheralRail;
  config.mode = GPIO_MODE_INPUT_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  startupError = "x4-gpio1-configure";
  X4Boot::mark(retained, X4Boot::AppMain, 3);
  {const int result=gpio_config(&config);X4BootLog::early("gpio1-configure",result);if(result!=ESP_OK)return;}
  startupError = "x4-gpio1-confirm-high";
  X4Boot::mark(retained, X4Boot::AppMain, 4);
  {const int result=gpio_set_level(peripheralRail, 1);X4BootLog::early("gpio1-confirm-high",result);if(result!=ESP_OK)return;}
  startupError = "x4-gpio1-unhold";
  X4Boot::mark(retained, X4Boot::AppMain, 5);
  {const int result=gpio_hold_dis(peripheralRail);X4BootLog::early("gpio1-unhold",result);if(result!=ESP_OK)return;}
  startupError = "x4-gpio1-hold";
  X4Boot::mark(retained, X4Boot::AppMain, 6);
  {const int result=gpio_hold_en(peripheralRail);X4BootLog::early("gpio1-hold",result);if(result!=ESP_OK)return;}
  startupError = "x4-gpio1-readback-low";
  X4Boot::mark(retained, X4Boot::AppMain, 7);
  {const int result=gpio_get_level(peripheralRail)==1?ESP_OK:ESP_FAIL;X4BootLog::early("gpio1-readback",result);if(result!=ESP_OK)return;}
  startupError = nullptr;
  X4Boot::mark(retained, X4Boot::RailReady);
}

void report() {
  char line[256];
  std::snprintf(line, sizeof(line),
    "X4_BOOT boot=%lu reset=%lu raw0=%lu raw1=%lu wake=%lu entry_us=%lu variant_us=%lu gate_us=%lu error=%s",
    (unsigned long)retained.boot, (unsigned long)retained.reset, (unsigned long)retained.raw0,
    (unsigned long)retained.raw1, (unsigned long)retained.wake, (unsigned long)retained.entryUs,
    (unsigned long)retained.variantUs, (unsigned long)retained.gateUs, startupError ? startupError : "none");
  RiscDiagnostics::line(line);
  std::snprintf(line, sizeof(line),
    "X4_BOOT pads gpio_in_before=0x%08lx gpio_in1_before=0x%08lx strap=0x%08lx hold_before=0x%08lx brownout=0x%08lx gpio1_now=%d",
    (unsigned long)retained.gpioBefore, (unsigned long)retained.gpioHighBefore,
    (unsigned long)retained.strapBefore, (unsigned long)retained.holdBefore,
    (unsigned long)retained.brownout, gpio_get_level(peripheralRail));
  RiscDiagnostics::line(line);
  std::snprintf(line, sizeof(line),
    "X4_BOOT previous checksum_valid=%u boot=%lu phase=%s operation=%lu reset=%lu entry_us=%lu variant_us=%lu gate_us=%lu",
    unsigned(previousValid), (unsigned long)previous.boot, X4Boot::phaseName(previous.phase),
    (unsigned long)previous.operation, (unsigned long)previous.reset, (unsigned long)previous.entryUs,
    (unsigned long)previous.variantUs, (unsigned long)previous.gateUs);
  RiscDiagnostics::line(line);
  if(previousValid && previous.milestoneCount) {
    std::snprintf(line,sizeof(line),
      "X4_BOOT previous-stage boot=%lu kind=%s count=%lu us=%llu first_display=%u first_display_us=%llu truncated=%u",
      (unsigned long)previous.boot,X4Boot::milestoneName(previous.milestoneKind),(unsigned long)previous.milestoneCount,
      (unsigned long long)previous.milestoneUs,unsigned(previous.displayCompleted),
      (unsigned long long)previous.firstDisplayUs,unsigned(previous.messageTruncated));
    RiscDiagnostics::line(line);
    std::snprintf(line,sizeof(line),"X4_BOOT previous-line %s",previous.milestone);
    RiscDiagnostics::line(line);
  }
}
}

extern "C" void __real_app_main(void);
extern "C" void __wrap_app_main(void) {
  if (!attempted) {
    attempted = true;
    previousValid = X4Boot::valid(retained);
    if (previousValid) previous = retained;
    X4Boot::changing(retained);
    retained = {};
    retained.boot = previousValid && previous.boot != UINT32_MAX ? previous.boot + 1 : 1;
    retained.phase = X4Boot::AppMain;
    retained.reset = uint32_t(esp_reset_reason());
    retained.raw0 = uint32_t(rtc_get_reset_reason(0));
    retained.raw1 = uint32_t(rtc_get_reset_reason(1));
    retained.wake = uint32_t(esp_sleep_get_wakeup_cause());
    retained.gpioBefore = REG_READ(GPIO_IN_REG);
    retained.gpioHighBefore = REG_READ(GPIO_IN1_REG);
    retained.strapBefore = REG_READ(GPIO_STRAP_REG);
    retained.holdBefore = REG_READ(RTC_CNTL_PAD_HOLD_REG);
    retained.brownout = REG_READ(RTC_CNTL_BROWN_OUT_REG);
    retained.entryUs = uint32_t(esp_timer_get_time());
    X4Boot::seal(retained);
    prepareRail();
    X4BootLog::start(retained);
  }
  // All original Arduino initialization and task creation is still executed.
  // Failure stays latched for Runtime's existing startup gate; no rail cleanup.
  __real_app_main();
}

extern "C" void initVariant(void) {
  if (variant || !attempted) return;
  variant = true;
  X4Boot::changing(retained);
  retained.variantUs = uint32_t(esp_timer_get_time());
  retained.phase = X4Boot::Variant;
  X4Boot::seal(retained);
  X4BootLog::checkpoint(retained);
  X4BootLog::allocateTrace();
  X4BootLog::textLine("native arduino-variant result=entered",retained.variantUs);
}

extern "C" const char* risc_native_startup_error(void) {
  if (!attempted || !variant) return startupError ? startupError : "x4-init-variant-not-entered";
  if (!reported) {
    reported = true;
    X4Boot::changing(retained);
    retained.gateUs = uint32_t(esp_timer_get_time());
    retained.phase = X4Boot::SetupGate;
    X4Boot::seal(retained);
    X4BootLog::checkpoint(retained);
    report();
  }
  return startupError;
}

// Called only by the selected Runtime diagnostic owner, before USB availability
// can discard a line. No retained field authorizes hardware or boot behavior.
extern "C" void risc_native_diagnostic_observer(const char* line) {
  if(!attempted || !variant || !line)return;
  const uint64_t nowUs=uint64_t(esp_timer_get_time());
  X4BootLog::trace(line,nowUs,retained);
  if(!X4BootLog::recoveryReady && std::strstr(line,"boot app-data end result=unavailable"))
    X4BootLog::recoveryReady=true;
  if(X4Boot::observe(retained,line,nowUs))
    X4BootLog::observe();
}

// Called outside Runtime's nonblocking diagnostic observer/USB guard.
extern "C" void risc_native_diagnostic_drain(void) {
  X4BootLog::drain(retained);
}

// Read-only provider handoff. Runtime verifies owner/context before entry.
// No NVS/file I/O, allocation, callback registration or borrowed storage escapes.
extern "C" int32_t risc_native_diagnostic_read(uint32_t slot,char* text,uint32_t capacity,
    uint32_t* written,uint64_t* sequence,uint32_t* revision) {
  if(written)*written=0;
  if(sequence)*sequence=0;
  if(revision)*revision=0;
  if(text && capacity)text[0]=0;
  if(!text || !written || !sequence || !revision || !capacity ||
      capacity>RISC_DIAGNOSTIC_SOURCE_TEXT_MAX || slot>=RISC_DIAGNOSTIC_SOURCE_MAX_SLOTS)return -1;
  using namespace X4BootLog;
  if(!initialized || current.magic!=Magic)return 0;
  const Session& session=slot?history[slot-1]:current;
  if(slot && (!valid(session) || session.sequence==current.sequence))return 0;
  const int header=std::snprintf(text,capacity,
    "X4_BOOTLOG format=1 firmware=%s flash_error=%ld internal_error=%d "
    "capture=app-main-after-rail seq0=unassigned no-usb-required\n",
    X4_NATIVE_COMPOSITION_IDENTITY,(long)storageError,fileError);
  if(header<0 || uint32_t(header)>=capacity){text[0]=0;return -1;}
  const int length=formatSession(text+header,capacity-uint32_t(header),session,
                                slot?"recovered-flash":"current");
  if(length<0){text[0]=0;return -1;}
  *written=uint32_t(header+length);*sequence=session.sequence;*revision=session.revision;
  return 1;
}

// Immutable-prefix byte stream: repeated reads are idempotent; the provider
// acknowledges only in its own cursor after checked durable close.
extern "C" int32_t risc_native_diagnostic_read_after(uint64_t after,char* text,uint32_t capacity,
    uint32_t* written,uint64_t* next) {
  if(written)*written=0;
  if(next)*next=0;
  if(text && capacity)text[0]=0;
  if(!text || !written || !next || !capacity || capacity>RISC_DIAGNOSTIC_SOURCE_TEXT_MAX)return -1;
  using namespace X4BootLog;
  if(!recoveryReady)return 0;
  const uint64_t total=recoveryBytes+traceBytes;
  if(after>total)return -1;
  if(after==total)return 0;
  auto byte=[&](uint64_t at){return at<recoveryBytes?recoveryText[at]:traceText[at-recoveryBytes];};
  if(after && byte(after-1)!='\n')return -1;
  uint32_t count=0,lastLine=0;
  while(after+count<total && count+1<capacity) {
    const char c=byte(after+count);text[count++]=c;if(c=='\n')lastLine=count;
  }
  if(!lastLine){text[0]=0;return -1;}
  text[lastLine]=0;*written=lastLine;*next=after+lastLine;return 1;
}
