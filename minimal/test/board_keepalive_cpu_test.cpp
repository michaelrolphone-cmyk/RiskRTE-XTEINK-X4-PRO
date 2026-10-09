// Production board and power providers + JSON graph + scoped CPU and native
// GPIO adapters. Hardware is a deterministic IDF shim, never an actual device.
#include "bootstrap/Runtime.h"
#include "diagnostic_source_fixture.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include "ports/esp32s3/NativeSleep.h"
#include "../drivers/x4pro_board_power/X4BoardKeepaliveV1.h"
#include "../drivers/x4pro_power/X4PowerDeepV1.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
using namespace RiscCpu;
extern "C" const risc_driver_v2* board_get(uint32_t);
extern "C" const risc_driver_v2* power_get(uint32_t);
uint64_t native_sleep_test_output_mask=native_sleep_test_gpio_mask;
static gpio_mode_t modes[49]{};
static bool levels[49]{},held[49]{},forcedLow[49]{};
static unsigned io,holdCount,unholdCount,railReads,railWrites,order;
static uint64_t now=100;
static bool armOk=true,timerOk=true,clearOk=true,timerClearOk=true,deepReady=false;
static bool holdOk=true,unholdOk=true,readOk=true,failRestoreRead;
static bool armed,timerArmed,reenter,nativeReturns,lowAfterHold;
static const char* executable;static const char* profile;
static Port* active;
static const risc_driver_v2 *boardDriver,*powerDriver;
static const x4_power_ready_api_v1* boardApi;
static const x4_power_deep_v1* powerApi;
extern "C" esp_err_t gpio_set_level(gpio_num_t pin,uint32_t value){++io;if(pin==1){assert(value==1);++railWrites;order=1;}levels[pin]=value!=0;return ESP_OK;}
extern "C" esp_err_t gpio_config(const gpio_config_t*c){++io;for(unsigned p=0;p<49;++p)if(c->pin_bit_mask&(UINT64_C(1)<<p)){modes[p]=c->mode;if(p==1){assert(order==1&&levels[1]);order=2;}}return ESP_OK;}
extern "C" esp_err_t gpio_hold_en(gpio_num_t pin){++io;assert(pin==1&&levels[1]&&modes[1]==GPIO_MODE_INPUT_OUTPUT&&order==3);++holdCount;held[1]=true;if(lowAfterHold)forcedLow[1]=true;return holdOk?ESP_OK:ESP_FAIL;}
extern "C" esp_err_t gpio_hold_dis(gpio_num_t pin){++io;if(pin==1){assert(levels[1]&&modes[1]==GPIO_MODE_INPUT_OUTPUT);if(order!=3)assert(order==2);++unholdCount;if(!unholdOk)return ESP_FAIL;}held[pin]=false;return ESP_OK;}
extern "C" bool rtc_gpio_is_valid_gpio(gpio_num_t p){return p<=21;}
extern "C" esp_err_t rtc_gpio_deinit(gpio_num_t){++io;return ESP_OK;}
static bool readPin(uint8_t p,bool*out){++io;if(p==1){++railReads;order=3;if(!readOk)return false;}*out=(unsigned(modes[p])&1)&&levels[p]&&!forcedLow[p];return true;}
static bool owner(){return true;}
static bool bind(RiscBoot::Runtime&r){return active->bind(r) && bind_empty_diagnostic_source(r);}
static void lockedCheck(){
 assert(held[1]&&levels[1]&&active->pins_[1].held&&!active->appExitSafe());
 unsigned before=io;assert(!boardApi->ready(nullptr)&&!boardDriver->quiesce());
 const auto* keep=x4_board_keepalive(boardApi);assert(keep&&keep->prepare(nullptr)==RISC_DEEP_SLEEP_CONTEXT);
 bool key=false;assert(!powerApi->power.read_key(nullptr,&key));assert(!powerDriver->quiesce());assert(io==before);
}
static Hardware hardware(){
 Hardware h{};h.owner=owner;h.now=[](){return now;};h.sleep=[](uint32_t){assert(false);};
 h.gpioOpen=NativeSleep::openPin;h.gpioRead=readPin;
 h.gpioWrite=[](uint8_t p,bool v){return gpio_set_level(static_cast<gpio_num_t>(p),v)==ESP_OK;};
 h.gpioClose=[](uint8_t p){++io;assert(!held[p]);modes[p]=GPIO_MODE_DISABLE;return NativeSleep::canClose(p);};
 h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){return false;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){return false;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){return false;};h.i2cClose=[](uint8_t){return false;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){return false;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){return false;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){return false;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){return false;};h.spiClose=[](uint8_t){return false;};
 h.deepHold=NativeSleep::hold;
 h.deepWakeValid=[](uint8_t p){return p==3;};
 h.deepReady=[](){if(reenter)lockedCheck();if(failRestoreRead)readOk=false;return deepReady;};
 h.deepWakeArm=[](uint8_t p,bool high,bool pull){++io;assert(p==3&&!high&&pull&&held[1]);armed=true;return armOk;};
 h.deepWakeClear=[](uint8_t p,bool pull){++io;assert(p==3&&pull);if(clearOk)armed=false;return clearOk;};
 h.timerArm=[](uint32_t ms){++io;assert(ms==60000);timerArmed=true;return timerOk;};
 h.timerClear=[](){++io;if(timerClearOk)timerArmed=false;return timerClearOk;};
 h.deepSleep=[](){assert(held[1]&&levels[1]&&armed&&timerArmed);lockedCheck();if(nativeReturns)return;execl(executable,executable,profile,"fresh-boot",static_cast<char*>(nullptr));_exit(99);};
 return h;
}
int main(int argc,char**argv){
 assert(argc==3);executable=argv[0];profile=argv[1];const char* scenario=argv[2];bool fresh=!strcmp(scenario,"fresh-boot");
 // A fresh process starts with empty provider/CPU BSS and a held physical pad.
 if(fresh){held[1]=true;levels[1]=true;}
 levels[3]=true;
 Port port(hardware());active=&port;
 RiscBoot::Runtime runtime({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind});
 assert(runtime.prepare(profile)&&!io);
 auto gpio=[&](uint64_t id)->garden_gpio_v1*{for(auto&g:port.gpios_)if(g.instance==id)return &g.api;return nullptr;};
 auto sync=[&](uint64_t id)->risc_provider_sync_api_v1*{for(auto&s:port.syncs_)if(s.instance==id)return &s.api;return nullptr;};
 boardDriver=board_get(2);powerDriver=power_get(2);boardApi=static_cast<const x4_power_ready_api_v1*>(boardDriver->capability);powerApi=x4_power_deep(static_cast<const x4_power_v1*>(powerDriver->capability));assert(powerApi);
 const risc_provider_dependency_v1 boardDeps[]={{"hardware.device",1,&runtime.board().device(1)->hardware},{"platform.gpio",1,gpio(1)},{"platform.sync",1,sync(1)}};
 if(!strcmp(scenario,"startup-hold-refusal")||!strcmp(scenario,"startup-hold-retained"))holdOk=false;
 if(!strcmp(scenario,"startup-hold-retained")||!strcmp(scenario,"startup-unhold-retained"))unholdOk=false;
 if(!strcmp(scenario,"startup-low-after-hold"))lowAfterHold=true;
 if(!strncmp(scenario,"startup-",8)){
  assert(!boardDriver->start(boardDeps,3)&&levels[1]);const unsigned before=io;
  assert(!boardDriver->quiesce()&&!boardApi->ready(nullptr)&&!port.appExitSafe()&&!port.providerStorageSafe());
  assert(io==before);printf("Production board/CPU %s retained exact ownership PASS\n",scenario);return 0;
 }
 assert(boardDriver->start(boardDeps,3)&&boardApi->ready(nullptr));
 assert(levels[1]&&held[1]&&railWrites==1&&unholdCount==1&&holdCount==1);
 assert(port.pins_[1].held&&port.pins_[1].retiredHeld&&!port.pins_[1].token);
 assert(port.providerStorageSafe()&&port.appExitSafe());
 if(!strcmp(scenario,"forced-low")){
  forcedLow[1]=true;assert(!boardApi->ready(nullptr));const unsigned before=io;
  assert(!boardDriver->quiesce()&&!boardApi->ready(nullptr)&&!port.providerStorageSafe()&&io==before);
  puts("Production board/CPU physical LOW retained PASS");return 0;
 }
 if(fresh){assert(!port.sleeping_&&!port.poisoned_&&!port.pins_[1].token&&port.pins_[1].retiredHeld&&port.pins_[1].owner&&!port.pins_[3].token);assert(boardDriver->quiesce()&&port.quiescent()&&held[1]&&levels[1]);puts("Fresh process claim stages HIGH/config before unhold and re-holds in CPU custody PASS");return 73;}
 const risc_provider_dependency_v1 powerDeps[]={{"hardware.device",1,&runtime.board().device(17)->hardware},{"platform.gpio",1,gpio(17)},{"platform.sync",1,sync(17)},{"platform.clock",1,&port.clock_},{"board.power.ready",1,boardApi}};
 assert(powerDriver->start(powerDeps,5));now+=30;
 const auto railToken=port.pins_[1].token,keyToken=port.pins_[3].token;
 assert(!railToken&&keyToken&&port.pins_[1].retiredHeld);
 reenter=true;
 if(!strcmp(scenario,"terminal")){
  pid_t child=fork();assert(child>=0);if(!child){deepReady=true;(void)powerApi->deep_sleep_for(nullptr,60000);_exit(98);}
  int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==73);
 }else {
  if(!strcmp(scenario,"arm-refusal")||!strcmp(scenario,"arm-retained")){deepReady=true;armOk=false;}
  if(!strcmp(scenario,"timer-refusal")||!strcmp(scenario,"timer-retained")){deepReady=true;timerOk=false;}
  if(!strcmp(scenario,"arm-retained"))clearOk=false;
  if(!strcmp(scenario,"timer-retained"))timerClearOk=false;
  if(!strcmp(scenario,"restore-read"))failRestoreRead=true;
  if(!strcmp(scenario,"native-return-retained")){deepReady=true;nativeReturns=true;}
  bool retained=strstr(scenario,"retained")||!strcmp(scenario,"restore-read");
  int32_t result=powerApi->deep_sleep_for(nullptr,60000);
  assert(result==(retained?RISC_DEEP_SLEEP_RETAINED:!strcmp(scenario,"refused")?RISC_DEEP_SLEEP_BUSY:RISC_DEEP_SLEEP_PLATFORM));
  assert(port.pins_[1].token==railToken&&port.pins_[3].token==keyToken&&levels[1]);
  if(retained){
   unsigned before=io;assert(!powerDriver->quiesce()&&!boardDriver->quiesce()&&!boardApi->ready(nullptr));
   assert(powerApi->deep_sleep_for(nullptr,60000)==RISC_DEEP_SLEEP_RETAINED);assert(io==before);
   assert(!port.appExitSafe());printf("Production board/power/CPU %s retained exact claims PASS\n",scenario);return 0;
  }
  assert(held[1]&&port.pins_[1].held&&port.pins_[1].retiredHeld&&boardApi->ready(nullptr)&&!armed&&!timerArmed);
  assert(port.providerStorageSafe()&&port.appExitSafe());
  deepReady=false;holdOk=true;
  for(unsigned i=0;i<5;++i){bool down=true;assert(powerApi->power.read_key(nullptr,&down)&&!down);now+=30;assert(powerApi->deep_sleep_for(nullptr,60000)==RISC_DEEP_SLEEP_BUSY);assert(boardApi->ready(nullptr)&&levels[1]&&held[1]);}
 }
 assert(railWrites==1&&unholdCount==1&&holdCount==1);
 assert(powerDriver->quiesce()&&boardDriver->quiesce()&&port.quiescent()&&held[1]&&levels[1]);
 assert(boardDriver->start(boardDeps,3)&&boardApi->ready(nullptr));
 assert(railWrites==2&&unholdCount==2&&holdCount==2&&port.providerStorageSafe());
 assert(boardDriver->quiesce()&&port.quiescent()&&held[1]&&levels[1]);
 printf("Production board/power/CPU %s repeated checked restore PASS\n",scenario);return 0;
}
