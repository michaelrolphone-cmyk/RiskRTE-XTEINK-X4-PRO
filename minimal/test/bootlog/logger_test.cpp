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
 observing=true;risc_native_diagnostic_observer(s);observing=false;
 risc_native_diagnostic_drain();
}}
namespace fs=std::filesystem;
std::string readFile(const char* name){std::ifstream f(name);return {std::istreambuf_iterator<char>(f),{}};}
void resetRam(bool eraseFlash=false){
 using namespace X4BootLog;
 if(eraseFlash){flash.clear();}
 staged.clear();commits=reads=realMainCalls=0;
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
void mounted(){fs::create_directories(X4_BOOTLOG_INTERNAL_ROOT);emit("RTE_STAGE us=700 boot app-data end result=ok");}
void check(bool condition,const char* description){if(!condition){std::cerr<<"FAIL "<<description<<"\n";std::abort();}std::cout<<"PASS "<<description<<"\n";}
int main(){
 using namespace X4BootLog;
 resetRam(true);noFiles();boot();
 check(commits==3 && current.sequence==1,"three pre-filesystem startup checkpoints without USB");
 check(!fs::exists(Internal),"no attempt to mount or format missing filesystem");
 emit("RTE_STAGE us=1200 boot filesystem-mount begin");
 check(current.checkpoint.milestoneKind==X4Boot::Boot,"mount-begin committed before filesystem exists");
 char snapshot[RISC_DIAGNOSTIC_SOURCE_TEXT_MAX]; uint32_t bytes=0,rev=0; uint64_t seq=0;
 const auto readsBefore=reads,commitsBefore=commits;
 check(risc_native_diagnostic_read(0,snapshot,sizeof(snapshot),&bytes,&seq,&rev)==1 &&
   bytes==std::strlen(snapshot) && seq==current.sequence && rev==current.revision &&
   std::strstr(snapshot,"capture=app-main-after-rail"),"read-only copied current snapshot");
 check(reads==readsBefore && commits==commitsBefore,"source handoff performs no storage I/O");
 check(risc_native_diagnostic_read(9,snapshot,sizeof(snapshot),&bytes,&seq,&rev)==-1 &&
   bytes==0 && seq==0 && rev==0 && !snapshot[0],"source invalid slot clears outputs");
 check(risc_native_diagnostic_read(0,snapshot,2,&bytes,&seq,&rev)==-1 && !snapshot[0],"source rejects truncated export");
 auto failedSeq=current.sequence;
 // Hard reset discards all ordinary RAM and the simulated RTC record.
 risc_x4_boot_record={};resetRam();boot();mounted();
 auto text=readFile(Internal);
 bool recovered=false;
 for(unsigned slot=1;slot<RISC_DIAGNOSTIC_SOURCE_MAX_SLOTS;++slot)
   if(risc_native_diagnostic_read(slot,snapshot,sizeof(snapshot),&bytes,&seq,&rev)==1 && seq==failedSeq)
     recovered=std::strstr(snapshot,"recovered-flash") && std::strstr(snapshot,"filesystem-mount begin");
 check(recovered,"SD source exports pre-mount failure from earlier flash session");
 check(current.sequence==failedSeq+1,"boot identity increases across loss of RTC memory");
 check(text.find("origin=recovered-flash")!=std::string::npos && text.find("filesystem-mount begin")!=std::string::npos,
       "later successful boot exports the failed pre-mount attempt");
 check(text.find("power_source=unmeasured")!=std::string::npos,"does not infer USB/battery from serial or reset flags");
 emit("APP t_ms=1300 stage=display-complete result=complete");
 unsigned saved=commits;
 for(int i=0;i<1000;++i)emit("APP t_ms=1400 stage=touch-picked-up source=software-sample");
 check(commits==saved,"touch/frame chatter produces no flash writes");
 emit("RTE_STAGE us=1500 boot app-session returned result=failed reason=app invocation retained");
 check(commits>saved && current.firstFailure[0],"terminal failure saved after first displayed frame");
 auto first=std::string(current.firstFailure);
 emit("RTE_STAGE us=1600 boot harmless-cleanup result=ok");
 check(current.firstFailure==first && std::string(current.checkpoint.milestone)==first,"cleanup cannot erase first terminal failure");
 check(readFile(Internal).find("app invocation retained")!=std::string::npos,"internal text log includes the terminal failure");
 resetRam();noFiles();boot();
 const auto before=flash;commitResult=ESP_FAIL;
 emit("RTE_STAGE us=2000 boot filesystem-mount begin");
 check(flash==before && !available,"interrupted commit leaves previous committed checkpoint intact");
 resetRam();boot();mounted();
 check(readFile(Internal).find("recovered-flash")!=std::string::npos,"history remains exportable after interrupted write");
 resetRam();noFiles();initResult=ESP_ERR_NVS_NO_FREE_PAGES;auto preserved=flash;boot();mounted();
 check(flash==preserved && current.sequence==0,"NVS exhaustion does not erase settings or old evidence");
 check(readFile(Internal).find("seq=0")!=std::string::npos && readFile(Internal).find("flash_error=4365")!=std::string::npos,
       "file fallback explicitly reports unassigned session and flash error");
 resetRam();noFiles();readResult=ESP_FAIL;preserved=flash;boot();mounted();
 check(flash==preserved,"history read error cannot overwrite an unreadable previous session");
 resetRam(true);noFiles();boot();
 for(int i=0;i<120;++i){char line[100];std::snprintf(line,sizeof(line),"RTE_STAGE us=%d provider load begin id=test-%d",2000+i,i);emit(line);}
 check(current.checkpointWrites==CheckpointLimit,"noncritical flash checkpoint count is bounded");
 emit("APP t_ms=3500 stage=display-complete result=complete");
 check(current.checkpoint.displayCompleted,"first displayed frame survives ordinary checkpoint budget");
 emit("RTE_BOOT error=provider detail=test-terminal-error");
 check(current.firstFailure[0],"first failure bypasses ordinary checkpoint budget");
 saved=commits;
 for(int i=0;i<500;++i)emit("RTE_BOOT error=repeated-terminal-error");
 check(commits==saved,"repeated terminal error cannot wear flash continuously");
 resetRam();noFiles();boot();
 fs::create_directories(Internal);
 emit("RTE_STAGE us=4000 boot app-data end result=ok");
 check(fileError!=0 && available,"file-open failure leaves flash journal usable");
 fs::remove_all(Internal);risc_native_diagnostic_drain();
 check(fs::is_regular_file(Internal),"file export can recover after a temporary open failure");
 failSync=true;emit("RTE_BOOT error=internal-sync-failure");
 check(fileUncertain && fileError && available,"uncertain internal close preserves NVS and disables file retries");
 const auto attempts=fileAttempts;
 for(unsigned i=0;i<1000;++i)emit("APP t_ms=24 stage=touch-picked-up source=software-sample");
 check(fileAttempts==attempts,"touch chatter cannot repeat uncertain internal writes");
 resetRam(true);noFiles();boot();
 emit("RTE_STAGE us=100 provider start end id=x4pro-sd result=failed elapsed_us=20");
 const auto firstFailure=std::string(current.firstFailure);const auto firstCommits=commits;
 emit("RTE_STAGE us=101 provider detail id=x4pro-sd part=1 text=boot-log: close/sync failed; writable log retained");
 check(commits==firstCommits+1 && std::string(current.firstFailure)==firstFailure &&
   std::strstr(current.checkpoint.milestone,"writable log retained"),"one provider detail enriches evidence without replacing first failure");
 for(unsigned i=0;i<100;++i)emit("RTE_STAGE us=102 provider detail id=x4pro-sd part=1 text=cleanup failed");
 check(commits==firstCommits+1,"later provider detail cannot rewrite retained failure continuously");
 resetRam(true);noFiles();
 for(unsigned i=0;i<12;++i){resetRam();boot();}
 check(flash.size()==SlotCount && current.sequence==12,"eight-record ring rotates across repeated boots");
 uint64_t low=UINT64_MAX;for(auto& kv:flash){Session s{};std::memcpy(&s,kv.second.data(),sizeof(s));check(valid(s),"persisted ring record checksum valid");low=std::min(low,s.sequence);}
 check(low==5,"ring retains exactly the eight latest startup sessions");
 // Corruption is rejected, never interpreted as an intact checkpoint.
 auto good=current;current.checkpoint.reset^=0x80;
 check(!valid(current),"corrupt checkpoint fails validation");current=good;
 mounted();auto prior=readFile(Internal);
 {std::ofstream f(Internal,std::ios::app);f<<std::string(MaxLogBytes,'x');}
 emit("RTE_BOOT error=rotation-test");
 check(fs::exists(Previous) && fs::file_size(Internal)<MaxLogBytes,"internal logs rotate with previous evidence retained");
 check(readFile(Previous).find(prior.substr(0,80))==0,"rotation preserves the earlier log prefix");
 {std::ofstream dump("results/simulated-flash-records.bin",std::ios::binary);
  dump<<std::string(93,'x');
  for(auto& item:flash)dump.write(reinterpret_cast<const char*>(item.second.data()),item.second.size());
 }
 std::cout<<"Session bytes="<<sizeof(Session)<<"; production native logger tests complete\n";
 noFiles();return 0;
}
