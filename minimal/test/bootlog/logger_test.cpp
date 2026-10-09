#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <vector>
#include "mock.h"
#include <unistd.h>
#include <cerrno>
static bool failSync=false;
static int test_fsync(int fd){if(failSync){errno=EIO;return -1;}return ::fsync(fd);}
#define fsync test_fsync
static std::map<std::string,std::vector<unsigned char>> flash,staged;
static unsigned commits=0,reads=0,realMainCalls=0;
static int initResult=0,readResult=0,commitResult=0;
static int64_t now=1000;
static bool observing=false;
static std::string emitted;
static int level=0;
int rtc_gpio_deinit(gpio_num_t){return 0;}
int gpio_set_level(gpio_num_t,int v){level=v;return 0;}
int gpio_config(const gpio_config_t*){return 0;}
int gpio_hold_dis(gpio_num_t){return 0;}
int gpio_hold_en(gpio_num_t){return 0;}
int gpio_get_level(gpio_num_t){return level;}
int esp_reset_reason(){return 1;}
int rtc_get_reset_reason(int c){return 1+c;}
int esp_sleep_get_wakeup_cause(){return 0;}
int64_t esp_timer_get_time(){return ++now;}
int nvs_flash_init(){assert(!observing);return initResult;}
int nvs_open(const char* name,int,nvs_handle_t* h){assert(!std::strcmp(name,"x4_bootlog"));*h=1;return 0;}
int nvs_set_blob(nvs_handle_t,const char* key,const void* p,size_t size){
 assert(!observing);assert(!std::strncmp(key,"boot",4));
 auto b=static_cast<const unsigned char*>(p);staged[key]={b,b+size};return 0;
}
int nvs_get_blob(nvs_handle_t,const char* key,void* out,size_t* bytes){
 assert(!observing);++reads;if(readResult)return readResult;
 auto it=flash.find(key);if(it==flash.end())return ESP_ERR_NVS_NOT_FOUND;
 if(*bytes<it->second.size())return ESP_ERR_NVS_INVALID_LENGTH;
 *bytes=it->second.size();std::memcpy(out,it->second.data(),*bytes);return 0;
}
int nvs_commit(nvs_handle_t){
 assert(!observing);++commits;
 if(commitResult){staged.clear();return commitResult;}
 for(auto& item:staged){flash[item.first]=item.second;}
 staged.clear();return 0;
}
void nvs_close(nvs_handle_t){}
#include "../../native/X4EarlyBoot.cpp"
extern "C" void __real_app_main(){++realMainCalls;initVariant();}
namespace RiscDiagnostics {void line(const char* s){
 emitted+=s;emitted+='\n';
 observing=true;risc_native_diagnostic_observer(s);observing=false;
 risc_native_diagnostic_drain();
}}
namespace fs=std::filesystem;
std::string readFile(const char* name){std::ifstream f(name);return {std::istreambuf_iterator<char>(f),{}};}
void resetRam(bool eraseFlash=false){
 using namespace X4BootLog;
 if(traceAllocation)std::free(traceAllocation);
 traceAllocation=nullptr;traceText=earlyText;recoveryText=nullptr;heap_caps_test_fail=false;
 traceCapacity=EarlyBytes;traceBytes=recoveryBytes=persistedBytes=0;
 eventSequence=earlyCount=terminalDetails=0;
 traceClosed=traceOverflow=recoveryReady=frameComplete=false;
 storageProbeReady=renderBusy=forceFlush=false;lastFileCommitUs=lastNvsCommitUs=0;lastFailureFlushedCount=0;
 if(eraseFlash){flash.clear();}
 staged.clear();commits=reads=realMainCalls=0;emitted.clear();
 initResult=readResult=commitResult=0;now=1000;level=0;
 available=initialized=pending=insideDrain=fileReady=historyExported=fileUncertain=false;
 fileAttemptRevision=fileAttempts=0;failSync=false;
 writtenRevision=fileRevision=0;
 storageError=0;fileError=0;handle=0;current={};
 for(auto& entry:history)entry={};
 attempted=variant=reported=previousValid=false;previous={};
 startupError="x4-app-main-not-entered";
}
void noFiles(){fs::remove_all(X4_BOOTLOG_INTERNAL_ROOT);}
void boot(){__wrap_app_main();assert(realMainCalls==1);assert(!risc_native_startup_error());}
void emit(const char* line){RiscDiagnostics::line(line);}
void mounted(){fs::create_directories(X4_BOOTLOG_INTERNAL_ROOT);now+=2000001;emit("RTE_STAGE us=700 boot app-data end result=ok");}
void check(bool condition,const char* description){if(!condition){std::cerr<<"FAIL "<<description<<"\n";std::abort();}std::cout<<"PASS "<<description<<"\n";}
std::string sourceText(){
 std::string text;uint64_t cursor=0,next=0;uint32_t written=0;char out[1536];
 const auto before=commits;
 for(unsigned count=0;count<1024;++count){
  int result=risc_native_diagnostic_read_after(cursor,out,sizeof(out),&written,&next);
  if(!result)break;
  assert(result==1 && next==cursor+written && out[written-1]=='\n');
  text.append(out,written);cursor=next;
 }
 assert(commits==before);return text;
}
void productionBoot(unsigned providers){
 emit("RTE_PROVISION action=continue-installed reason=installed");
 for(unsigned i=0;i<providers;++i){
  const char* names[]={"x4pro-board-power","x4pro-sd","x4pro-i2c","x4pro-gt911","x4pro-panel","x4pro-battery","x4pro-rtc","x4pro-buttons","x4pro-frontlight","ble-hid"};
  const char* steps[]={"provider load begin id=", "provider reference id=", "provider start begin id=", "provider start end id=", "provider load end id="};
  for(unsigned j=0;j<5;++j){char line[256];std::snprintf(line,sizeof(line),"RTE_STAGE us=%lld %s%s result=%s elapsed_us=20",(long long)now,steps[j],names[i%10],j>=3?"ok":"begin");emit(line);}
 }
 emit("RTE_STAGE us=6000 app load begin file=system.elf");
 emit("RTE_STAGE us=6010 app load end file=system.elf result=ok elapsed_us=10");
 emit("RTE_STAGE us=6020 app init begin file=system.elf");
 emit("RTE_STAGE us=6030 app init end file=system.elf result=0 elapsed_us=10");
 emit("RTE_STAGE us=6040 app entry begin file=system.elf");
 emit("APP t_ms=7 stage=draw-begin result=begin");
 emit("APP t_ms=8 stage=transfer-begin result=begin");
}
int main(){
 using namespace X4BootLog;
 resetRam(true);noFiles();boot();
 check(commits==3 && current.sequence==1,"native checkpoints remain separate from full ordered trace");
 check(!fs::exists(Internal),"early text buffers without mounting or formatting storage");
 emit("RTE_STAGE us=1200 boot filesystem-mount begin");
 char out[1536];uint32_t written=99;uint64_t next=99;
 check(!risc_native_diagnostic_read_after(0,out,sizeof(out),&written,&next) && !written && !next,"source waits for recovered prefix resolution");
 mounted();productionBoot(20);
 auto first=readFile(Internal);
 check(eventSequence>110 && first.find("x4pro-gt911")!=std::string::npos && first.find("result=ok")!=std::string::npos && first.find("RTE_PROVISION")!=std::string::npos,"actual named initialization and result lines persist beyond event 64");
 check(first.find("gpio1-rtc-deinit result=0")!=std::string::npos && first.find("event=1 us=1001")!=std::string::npos,"native rail operations and reset/session start have actual timing");
 // Sudden reset after setup/SD but before first display; RAM and RTC disappear.
 risc_x4_boot_record={};resetRam();boot();mounted();
 productionBoot(18);
 emit("RTE_BOOT error=panel-start-timeout");
 auto failed=readFile(Internal);auto failedInputs=emitted;
 check(failed.find("capture end result=failed")!=std::string::npos,"failure outcome closes current boot trace");
 check(readFile(Previous)==first,"previous failed attempt survives reset in internal file");
 auto combined=sourceText();
 check(combined==first+failed,"copied source exports previous and current actual text in exact order");
 // Second reset and USB recovery also has no USB prerequisite in production hooks.
 risc_x4_boot_record={};resetRam();boot();mounted();productionBoot(20);
 emit("APP t_ms=9000 stage=display-complete result=complete");
 auto success=readFile(Internal);combined=sourceText();
 check(combined==failed+success && current.sequence==3,"USB recovery exports preceding failed text and current session with reset boundaries");
 check(current.checkpoint.displayCompleted && frameComplete && !traceClosed,"first-display special record does not end startup capture");
 emit("APP t_ms=9001 stage=rtc-recovery result=ok");
 emit("RTE_STAGE us=9100 app entry begin file=clock.elf");
 emit("APP t_ms=9101 stage=draw-begin result=begin");
 emit("APP t_ms=9200 stage=display-complete result=complete");
 combined=sourceText();
 check(combined.find("stage=rtc-recovery result=ok")!=std::string::npos && combined.find("file=clock.elf")!=std::string::npos,"RTC and Clock initialization after logo remain captured");
 auto meaningfulInputs=emitted;
 unsigned before=commits;size_t size=traceBytes;unsigned attempts=fileAttempts;
 for(unsigned i=0;i<1000;++i)emit("APP t_ms=10000 stage=touch-picked-up source=software-sample");
 check(commits==before && traceBytes==size && fileAttempts==attempts,"idle frame/touch chatter causes no flash writes");
 std::ofstream("results/full-boot-trace.txt")<<combined;
 const char* fixture=std::getenv("X4_TRACE_FIXTURE");if(fixture)std::ofstream(fixture)<<combined;
 const char* inputs=std::getenv("X4_TRACE_INPUT_ARTIFACT");if(inputs)std::ofstream(inputs)<<"SESSION 2 diagnostic inputs (native rail rows are generated separately)\n"<<failedInputs<<"SESSION 3 diagnostic inputs\n"<<meaningfulInputs;
 // Events retain monotonically increasing sequence and timestamp within each boot.
 uint64_t lastSession=0,lastUs=0;unsigned long lastEvent=0;size_t offset=0;
 while(offset<combined.size()){
  unsigned long long session,us;unsigned long event;
  assert(std::sscanf(combined.c_str()+offset,"X4_TRACE session=%llu event=%lu us=%llu",&session,&event,&us)==3);
  if(session==lastSession){assert(event==lastEvent+1 && us>=lastUs);}else assert(event==1);
  lastSession=session;lastEvent=event;lastUs=us;offset=combined.find('\n',offset)+1;
 }
 check(true,"saved text verifies monotonic event order and timestamp/reset boundaries");
 check(risc_native_diagnostic_read_after(1,out,sizeof(out),&written,&next)==-1 && !written && !next && !out[0],"unaligned cursor rejected without source mutation");
 check(risc_native_diagnostic_read_after(0,out,2,&written,&next)==-1 && !out[0],"insufficient capacity cannot split a line");
 resetRam();noFiles();boot();mounted();
 emit("RTE_STAGE us=4 provider wifi password=do-not-log-this");
 now+=2000001;risc_native_diagnostic_drain();
 check(readFile(Internal).find("do-not-log-this")==std::string::npos && readFile(Internal).find("redacted credential-bearing")!=std::string::npos,"credential-bearing diagnostics are explicitly redacted");
 failSync=true;now+=2000001;emit("RTE_STAGE us=5 provider start begin id=panel");
 check(fileUncertain && fileError,"uncertain internal write is never retried");
 attempts=fileAttempts;for(unsigned i=0;i<20;++i)emit("RTE_STAGE us=6 provider start end result=ok");
 check(fileAttempts==attempts,"subsequent lines stay buffered after uncertain internal close");
 resetRam();noFiles();initResult=ESP_ERR_NVS_NO_FREE_PAGES;auto preserved=flash;boot();mounted();
 check(flash==preserved && current.sequence==0 && readFile(Internal).find("session_identity=unassigned")!=std::string::npos,"NVS failure does not erase settings and labels fallback session identity");
 resetRam();noFiles();boot();mounted();
 for(unsigned i=0;i<2000;++i)emit("RTE_STAGE us=100 provider reference id=x4pro-panel stage=hardware-start-begin result=begin");
 now+=2000001;risc_native_diagnostic_drain();
 check(traceOverflow && traceBytes<=TraceBytes && readFile(Internal).find("capture overflow result=truncated")!=std::string::npos,"finite capture capacity emits explicit overflow instead of silent last-checkpoint loss");
 emit("RTE_BOOT error=late-terminal-failure");
 check(current.firstFailure[0] && readFile(Internal).find("late-terminal-failure")!=std::string::npos,"failure special record and text use reserved tail after trace overflow");
 resetRam();noFiles();heap_caps_test_fail=true;boot();mounted();
 for(unsigned i=0;i<100;++i)emit("RTE_STAGE us=1 provider start begin id=panel");
 check(!traceAllocation && traceCapacity==EarlyBytes && traceOverflow,"PSRAM failure uses only fixed early RAM and reports overflow");
 resetRam();noFiles();boot();emit("RTE_STAGE us=12 boot app-data end result=unavailable");
 check(!sourceText().empty() && !fs::exists(Internal),"unavailable internal storage still exposes buffered trace to SD");
 if(const char* hardware=std::getenv("X4_REAL_SERIAL")) {
  resetRam(true);noFiles();boot();mounted();
  std::ifstream input(hardware);assert(input);std::string line;unsigned supplied=0,filtered=0;
  while(std::getline(input,line)){
   if(!line.empty() && line.back()=='\r')line.pop_back();
   if(line.empty())continue;
   const bool skip=frameComplete && line.rfind("APP t_ms=",0)==0 &&
    (line.find("stage=touch-")!=std::string::npos || line.find("stage=draw-")!=std::string::npos ||
     line.find("stage=transfer-")!=std::string::npos || line.find("stage=display-")!=std::string::npos) &&
    line.find("stage=display-failed")==std::string::npos;
   unsigned long long stamp=0;
   if(std::sscanf(line.c_str(),"RTE_STAGE us=%llu",&stamp)==1)now=std::max(now,(int64_t)stamp);
   else if(std::sscanf(line.c_str(),"APP t_ms=%llu",&stamp)==1)now=std::max(now,(int64_t)stamp*1000);
   emit(line.c_str());++supplied;if(skip)++filtered;
   if(!skip && line.size()<256)assert(std::string(traceText,traceBytes).find(line)!=std::string::npos);
  }
  assert(!traceOverflow && supplied>400 && sourceText()==readFile(Internal));
  check(std::string(traceText,traceBytes).find("SPARSE_CLOCK stage=native-recovery")!=std::string::npos,"actual hardware transcript retains SPARSE_CLOCK, provisioning, radio and terminal detail prefixes");
  if(const char* artifact=std::getenv("X4_REAL_TRACE_ARTIFACT"))std::ofstream(artifact)<<sourceText();
  std::cout<<"Hardware replay input_lines="<<supplied<<" filtered_frame_chatter="<<filtered<<" output_events="<<eventSequence<<" bytes="<<traceBytes<<" capacity="<<traceCapacity<<"\n";
 }
 resetRam(true);noFiles();
 for(unsigned i=0;i<12;++i){resetRam();boot();}
 check(flash.size()==SlotCount && current.sequence==12,"bounded eight-session NVS summaries remain recoverable");
 {std::ofstream dump("results/simulated-flash-records.bin",std::ios::binary);dump<<std::string(93,'x');for(auto& item:flash)dump.write(reinterpret_cast<const char*>(item.second.data()),item.second.size());}
 noFiles();return 0;
}
