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
void start(const X4Boot::Record& record) {
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
bool writeSession(FILE* file,const Session& s,const char* origin) {
  char text[RISC_DIAGNOSTIC_SOURCE_TEXT_MAX];
  const int n=formatSession(text,sizeof(text),s,origin);
  return n>=0 && std::fwrite(text,1,size_t(n),file)==size_t(n);
}
bool syncClose(FILE* file) {
  bool okay=std::fflush(file)==0;
  if(okay && fsync(fileno(file))!=0)okay=false;
  if(!okay)fileError=errno?errno:EIO;
  if(std::fclose(file)!=0){okay=false;fileError=errno?errno:EIO;}
  return okay;
}
bool append() {
  if(!fileReady || fileUncertain || current.magic!=Magic ||
      (historyExported && fileRevision==current.revision))return false;
  if(fileAttemptRevision!=current.revision){fileAttemptRevision=current.revision;fileAttempts=0;}
  if(fileAttempts>=2)return false;
  ++fileAttempts; // At most two pre-write retries per bounded checkpoint.
  struct stat info{};
  if(stat(Internal,&info)==0 && info.st_size>=long(MaxLogBytes)) {
    // If replacement/rotation is unsupported, keep existing evidence intact.
    if(std::rename(Internal,Previous)!=0){fileError=errno;return false;}
  }
  FILE* file=std::fopen(Internal,"ab");
  if(!file){fileError=errno;return false;}
  bool okay=true;
  if(!historyExported) {
    okay=std::fprintf(file,
      "\nX4_BOOTLOG format=1 firmware=%s flash_error=%ld previous_file_error=%d "
      "capture=app-main-after-rail seq0=unassigned no-usb-required\n",
      X4_NATIVE_COMPOSITION_IDENTITY,(long)storageError,fileError)>=0;
    // Oldest to newest. Invalid/torn records never become successful boots.
    uint64_t after=0;
    for(unsigned count=0;count<SlotCount && okay;++count) {
      uint64_t lowest=UINT64_MAX;unsigned selected=SlotCount;
      for(unsigned i=0;i<SlotCount;++i)
        if(valid(history[i]) && history[i].sequence<current.sequence &&
           history[i].sequence>after && history[i].sequence<lowest) {
          lowest=history[i].sequence;selected=i;
        }
      if(selected==SlotCount)break;
      okay=writeSession(file,history[selected],"recovered-flash");after=lowest;
    }
  }
  if(okay)okay=writeSession(file,current,"current");
  bool closed=syncClose(file);
  if(!okay || !closed){fileUncertain=true;if(!fileError)fileError=EIO;return false;}
  historyExported=true;fileRevision=current.revision;return true;
}
void drain(const X4Boot::Record& record) {
  if(insideDrain)return;
  insideDrain=true;
  if(pending) {
    pending=false;
    if(advance(current,record))(void)store();
  }
  if(!fileReady)fileReady=directory(X4_BOOTLOG_INTERNAL_ROOT);
  (void)append();
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
  if (rtc_gpio_deinit(peripheralRail) != ESP_OK) return;
  startupError = "x4-gpio1-stage-high";
  X4Boot::mark(retained, X4Boot::AppMain, 2);
  if (gpio_set_level(peripheralRail, 1) != ESP_OK) return;
  gpio_config_t config{};
  config.pin_bit_mask = uint64_t(1) << peripheralRail;
  config.mode = GPIO_MODE_INPUT_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  startupError = "x4-gpio1-configure";
  X4Boot::mark(retained, X4Boot::AppMain, 3);
  if (gpio_config(&config) != ESP_OK) return;
  startupError = "x4-gpio1-confirm-high";
  X4Boot::mark(retained, X4Boot::AppMain, 4);
  if (gpio_set_level(peripheralRail, 1) != ESP_OK) return;
  startupError = "x4-gpio1-unhold";
  X4Boot::mark(retained, X4Boot::AppMain, 5);
  if (gpio_hold_dis(peripheralRail) != ESP_OK) return;
  startupError = "x4-gpio1-hold";
  X4Boot::mark(retained, X4Boot::AppMain, 6);
  if (gpio_hold_en(peripheralRail) != ESP_OK) return;
  startupError = "x4-gpio1-readback-low";
  X4Boot::mark(retained, X4Boot::AppMain, 7);
  if (gpio_get_level(peripheralRail) != 1) return;
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
  if(!attempted || !variant || X4Boot::classify(retained,line)==X4Boot::None)return;
  if(X4Boot::observe(retained,line,uint64_t(esp_timer_get_time())))
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
