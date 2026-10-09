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
static unsigned fileCommits=0,frameCommits=0;
static bool frameActive=false;
static uint64_t storageCost=0;
static void fileDelay();
static int test_fsync(int fd){++fileCommits;if(frameActive)++frameCommits;fileDelay();if(failSync){errno=EIO;return -1;}return ::fsync(fd);}
#define fsync test_fsync
static std::map<std::string,std::vector<unsigned char>> flash,staged;
static unsigned commits=0,reads=0,realMainCalls=0;
static int initResult=0,readResult=0,commitResult=0;
static int64_t now=1000;
static void fileDelay(){now+=25000;storageCost+=25000;}
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
 assert(!observing);++commits;now+=8000;storageCost+=8000;
 if(commitResult){staged.clear();return commitResult;}
 for(auto& item:staged){flash[item.first]=item.second;}
 staged.clear();return 0;
}
void nvs_close(nvs_handle_t){}
#ifndef X4_NATIVE_SOURCE_UNDER_TEST
#define X4_NATIVE_SOURCE_UNDER_TEST "../../native/X4EarlyBoot.cpp"
#endif
#include X4_NATIVE_SOURCE_UNDER_TEST

extern "C" void __real_app_main(){++realMainCalls;initVariant();}
namespace RiscDiagnostics {void line(const char* s){
 emitted+=s;emitted+='\n';
 observing=true;risc_native_diagnostic_observer(s);observing=false;
 risc_native_diagnostic_drain();
}}

int main(int argc,char**argv){
 assert(argc==2 || argc==3);std::filesystem::remove_all(X4_BOOTLOG_INTERNAL_ROOT);
 __wrap_app_main();assert(!risc_native_startup_error());
 std::filesystem::create_directories(X4_BOOTLOG_INTERNAL_ROOT);
 RiscDiagnostics::line("RTE_STAGE us=1 boot app-data end result=ok");
 std::ifstream input(argv[1]);assert(input);std::string line;unsigned supplied=0;
 while(std::getline(input,line)){
  if(!line.empty() && line.back()=='\r')line.pop_back();
  if(line.empty())continue;
  unsigned long long timestamp=0;
  if(std::sscanf(line.c_str(),"RTE_STAGE us=%llu",&timestamp)==1)now=std::max(now,(int64_t)timestamp);
  else if(std::sscanf(line.c_str(),"APP t_ms=%llu",&timestamp)==1)now=std::max(now,(int64_t)timestamp*1000);
  if(line.find("stage=draw-begin")!=std::string::npos || line.find("stage=display-submit-begin")!=std::string::npos)frameActive=true;
  if(line.find("stage=display-complete")!=std::string::npos || line.find("stage=display-skip")!=std::string::npos || line.find("stage=display-failed")!=std::string::npos)frameActive=false;
  RiscDiagnostics::line(line.c_str());++supplied;
 }
 frameActive=false;RiscDiagnostics::line("RTE_BOOT error=latency-test-complete");
 auto before=fileCommits;for(unsigned i=0;i<1000;++i)risc_native_diagnostic_drain();assert(fileCommits==before);
 std::ifstream saved(X4BootLog::Internal);std::string text{std::istreambuf_iterator<char>(saved),{}};
 assert(text.find("RTE_PROVISION")!=std::string::npos && text.find("x4pro-rtc")!=std::string::npos && text.find("latency-test-complete")!=std::string::npos);
#ifdef X4_EXPECT_BATCHING
 assert(frameCommits==0 && fileCommits<=supplied/8+6 && commits<=16);
#endif
 std::cout<<"input_lines="<<supplied<<" file_commits="<<fileCommits<<" nvs_commits="<<commits<<" commits_during_frame="<<frameCommits<<" modeled_storage_ms="<<storageCost/1000<<" saved_bytes="<<text.size()<<"\n";
 if(argc==3)std::ofstream(argv[2])<<text;
 std::filesystem::remove_all(X4_BOOTLOG_INTERNAL_ROOT);
}
