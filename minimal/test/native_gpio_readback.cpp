// Production JSON materializer, scoped CPU GPIO table, native open adapter and
// compiled board-power provider. The shim models IDF's input-enable rule rather
// than returning the output latch unconditionally (gpio.h's gpio_get_level warning).
#include "bootstrap/Runtime.h"
#include "diagnostic_source_fixture.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <driver/gpio.h>
#ifndef GPIO_MODE_INPUT_OUTPUT
#define GPIO_MODE_INPUT_OUTPUT static_cast<gpio_mode_t>(3)
#endif
#include "ports/esp32s3/NativeSleep.h"
#include "../drivers/x4pro_board_power/PowerReadyV1.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
using namespace RiscCpu;
[[maybe_unused]] static garden_gpio_v1& gpioBase(garden_gpio_v1& api){return api;}
template<class Api> static auto& gpioBase(Api& api){return api.base;}
uint64_t native_sleep_test_output_mask=native_sleep_test_gpio_mask;
static gpio_mode_t modes[49]{};
static bool levels[49]{}, forcedLow[49]{}, held[49]{};
static unsigned io;
extern "C" esp_err_t gpio_set_level(gpio_num_t pin,uint32_t level){++io;levels[pin]=level!=0;return ESP_OK;}
extern "C" esp_err_t gpio_config(const gpio_config_t* c){++io;for(unsigned p=0;p<49;++p)if(c->pin_bit_mask&(UINT64_C(1)<<p))modes[p]=c->mode;return ESP_OK;}
extern "C" esp_err_t gpio_hold_dis(gpio_num_t p){++io;held[p]=false;return ESP_OK;}
extern "C" esp_err_t gpio_hold_en(gpio_num_t p){++io;held[p]=true;return ESP_OK;}
extern "C" bool rtc_gpio_is_valid_gpio(gpio_num_t p){return p<=21;}
extern "C" esp_err_t rtc_gpio_deinit(gpio_num_t){++io;return ESP_OK;}
static bool padRead(uint8_t p,bool* out){++io;*out=(unsigned(modes[p])&1) && levels[p] && !forcedLow[p];return true;}
static Port* active;
static bool owner(){return true;}
static bool bind(RiscBoot::Runtime&r){return active->bind(r) && bind_empty_diagnostic_source(r);}
static Hardware hardware(){
 Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){++io;};
 h.gpioOpen=NativeSleep::openPin;h.gpioRead=padRead;h.deepHold=NativeSleep::hold;
 h.gpioWrite=[](uint8_t p,bool v){return gpio_set_level(static_cast<gpio_num_t>(p),v)==ESP_OK;};
 h.gpioClose=[](uint8_t p){++io;modes[p]=GPIO_MODE_DISABLE;return NativeSleep::canClose(p);};
 h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){return false;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){return false;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){return false;};h.i2cClose=[](uint8_t){return false;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){return false;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){return false;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){return false;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){return false;};h.spiClose=[](uint8_t){return false;};return h;
}
int main(int argc,char**argv){
 assert(argc==3);bool expectBroken=!strcmp(argv[2],"expect-broken");
 Port port(hardware());active=&port;
 RiscBoot::Runtime runtime({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind});
 assert(runtime.prepare(argv[1]));assert(io==0);
 auto scoped=[&](uint64_t id)->garden_gpio_v1&{for(auto&g:port.gpios_)if(g.instance==id)return gpioBase(g.api);assert(false);return gpioBase(port.gpios_[0].api);};
 auto& rail=scoped(1);auto& sd=scoped(9);
 risc_provider_sync_api_v1* sync=nullptr;for(auto&s:port.syncs_)if(s.instance==1)sync=&s.api;assert(sync);
 auto* driver=t5_driver_get(2);assert(driver);
 const risc_provider_dependency_v1 deps[]={{"hardware.device",1,&runtime.board().device(1)->hardware},{"platform.gpio",1,&rail},{"platform.sync",1,sync}};
 bool started=driver->start(deps,3);
 assert(started==!expectBroken);
 const auto* power=static_cast<const x4_power_ready_api_v1*>(driver->capability);
 assert(power->ready(power->context)==!expectBroken);
 // An output latch HIGH must not hide an externally low pad.
 if(expectBroken){assert(!driver->quiesce());puts("Original readback failure safely retained PASS");return 0;}
 pid_t child=fork();assert(child>=0);if(!child){forcedLow[1]=true;assert(!power->ready(power->context));assert(!driver->quiesce());_exit(0);}
 int status=0;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
 assert(driver->quiesce());driver->stop();assert(port.quiescent()&&held[1]&&levels[1]);
 // Native SD CLK is the second materialized bank pin (GPIO41). Its tick()
 // waits for BOTH output levels through the same scoped readback contract.
 auto& bank=runtime.board().device(9)->config.gpio;assert(bank.pins[1]==41);
 uint64_t token=0;assert(sd.claim(sd.context,bank.pins[1],true,false,false,&token));
 bool level=true;assert(sd.read(sd.context,token,&level) && !level);
 assert(sd.write(sd.context,token,true));assert(sd.read(sd.context,token,&level));assert(level==!expectBroken);
 forcedLow[41]=true;assert(sd.read(sd.context,token,&level) && !level);forcedLow[41]=false;
 assert(sd.write(sd.context,token,false));assert(sd.read(sd.context,token,&level) && !level);
 assert(sd.release(sd.context,token));assert(!sd.read(sd.context,token,&level));assert(port.quiescent());
 printf("Production board-power start + JSON + CPU scope + native GPIO; SD CLK low/high/low: %s PASS\n",expectBroken?"original failure reproduced":"readback regression");
}
