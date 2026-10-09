#include <Arduino.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../native/X4EarlyBoot.cpp"

extern "C" void app_main();
static int step, failure, initialHeld, resetReason=1, crash;
static bool held, configured, high, sensing, physicalHigh, arduinoStarted;
static unsigned timeUs, reports;
static std::vector<std::string> calls, lines;
static int operation(int expected) {
  assert(!arduinoStarted && ++step == expected);
  return step == failure ? -1 : ESP_OK;
}
extern "C" esp_err_t rtc_gpio_deinit(gpio_num_t pin) {
  assert(pin == 1);return operation(1);
}
extern "C" esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level) {
  assert(pin == 1 && level == 1);
  const int result = operation(step == 1 ? 2 : 4);
  if (result == ESP_OK) {high = true;if (!held && configured) physicalHigh = high;}
  return result;
}
extern "C" esp_err_t gpio_config(const gpio_config_t* config) {
  assert(config->pin_bit_mask == 2 && config->mode == GPIO_MODE_INPUT_OUTPUT);
  assert(config->pull_up_en == 0 && config->pull_down_en == 0 && config->intr_type == 0 && high);
  const int result = operation(3);
  if (result == ESP_OK) {configured = sensing = true;if (!held) physicalHigh = high;}
  return result;
}
extern "C" esp_err_t gpio_hold_dis(gpio_num_t pin) {
  assert(pin == 1 && high && configured && sensing);
  const int result = operation(5);
  if (result == ESP_OK) {held = false;physicalHigh = high;}
  return result;
}
extern "C" esp_err_t gpio_hold_en(gpio_num_t pin) {
  assert(pin == 1 && high && configured && sensing && !held && physicalHigh);
  const int result = operation(6);
  if (result == ESP_OK) held = true;
  return result;
}
extern "C" int gpio_get_level(gpio_num_t pin) {
  assert(pin == 1);
  if (!arduinoStarted) {
    assert(high && configured && sensing && held);
    return operation(7) == ESP_OK ? 1 : 0;
  }
  return physicalHigh && sensing;
}
int esp_reset_reason(){return resetReason;}
int rtc_get_reset_reason(int core){return 10 + core;}
int esp_sleep_get_wakeup_cause(){return resetReason == 8 ? 4 : 0;}
int64_t esp_timer_get_time(){return timeUs += 100;}
uint32_t x4_test_reg_read(int reg){return reg == 1 ? (physicalHigh ? 2 : 0) : reg == 2 ? (held ? 2 : 0) : reg == 4 ? 0xabcdef01 : reg == 5 ? 0x654321 : 0x12345678;}
namespace RiscDiagnostics {void line(const char* line){assert(arduinoStarted);++reports;lines.emplace_back(line);}}
SerialFake Serial;UsbFake USB;void (*serialEventRun)()=nullptr;
static void arduino(const char* name) {
  assert(step == (failure ? failure : 7));arduinoStarted = true;calls.emplace_back(name);
  if(crash && calls.back()=="cpu")throw 1;
}
void SerialFake::begin(){arduino("serial");}
void UsbFake::begin(){arduino("usb");}
extern "C" void setCpuFrequencyMhz(int mhz){assert(mhz==240);arduino("cpu");}
extern "C" void psramInit(){arduino("psram");}
extern "C" void esp_log_level_set(const char*,int){arduino("log");}
extern "C" int nvs_flash_init(){
  if(!arduinoStarted){calls.emplace_back("early-nvs");return ESP_FAIL;}
  arduino("nvs");return ESP_OK;
}
extern "C" int nvs_open(const char*,int,nvs_handle_t*){assert(false);return ESP_FAIL;}
extern "C" int nvs_get_blob(nvs_handle_t,const char*,void*,size_t*){assert(false);return ESP_FAIL;}
extern "C" int nvs_set_blob(nvs_handle_t,const char*,const void*,size_t){assert(false);return ESP_FAIL;}
extern "C" int nvs_commit(nvs_handle_t){assert(false);return ESP_FAIL;}
extern "C" void nvs_close(nvs_handle_t){assert(false);}
extern "C" const esp_partition_t* esp_partition_find_first(int,int,const char*){assert(false);return nullptr;}
extern "C" int esp_partition_erase_range(const esp_partition_t*,size_t,size_t){assert(false);return -1;}
extern "C" void init(){arduino("init");}
void setup() {calls.emplace_back("setup");(void)risc_native_startup_error();}
void loop() {assert(false);}
void esp_task_wdt_reset(){assert(false);}
void xTaskCreateUniversal(void(*)(void*),const char*,size_t,void*,int,void**,int){
  assert(variant && X4Boot::valid(retained) && retained.phase==X4Boot::Variant);
  calls.emplace_back("task");setup();
}
static void freshGlobals() {
  attempted=variant=reported=previousValid=false;previous={};startupError="x4-app-main-not-entered";
  X4BootLog::initialized=false;X4BootLog::available=false;X4BootLog::pending=false;
  X4BootLog::current={};
  step=reports=timeUs=0;configured=high=sensing=arduinoStarted=false;calls.clear();lines.clear();
}
int main(int argc, char** argv) {
  assert(argc>=2);
  if(!std::strcmp(argv[1],"milestone")) {
    freshGlobals();retained={};failure=0;held=physicalHigh=true;crash=0;
    risc_native_diagnostic_observer("RTE_STAGE us=1 boot begin");assert(timeUs==0);
    app_main();assert(reports==3 && retained.milestoneCount==0);
    const unsigned oldTime=timeUs;
    for(unsigned i=0;i<1000;++i){
      risc_native_diagnostic_observer("APP t_ms=23 stage=touch-picked-up source=software-sample");
      risc_native_diagnostic_observer("APP t_ms=24 stage=draw-begin ");
      risc_native_diagnostic_observer("RTE_STAGE us=12 gpio operation");
    }
    assert(timeUs==oldTime && retained.milestoneCount==0);
    risc_native_diagnostic_observer("RTE_STAGE us=900 provider start begin id=x4pro-panel");
    assert(retained.milestoneKind==X4Boot::Provider && retained.milestoneCount==1);
    assert(std::strstr(retained.milestone,"id=x4pro-panel"));
    risc_native_diagnostic_observer("RTE_STAGE us=1000 app init end file=default.elf result=0");
    assert(retained.milestoneKind==X4Boot::App && retained.milestoneCount==2);
    risc_native_diagnostic_observer("APP t_ms=101 stage=display-complete result=complete");
    assert(retained.displayCompleted && retained.firstDisplayUs && retained.milestoneCount==3);
    const auto firstTime=retained.firstDisplayUs;const auto afterFirst=timeUs;
    risc_native_diagnostic_observer("APP t_ms=102 stage=display-complete result=complete");
    assert(retained.firstDisplayUs==firstTime && retained.milestoneCount==3 && timeUs==afterFirst);
    std::string longFailure="RTE_BOOT error=runtime detail="+std::string(900,'x');
    risc_native_diagnostic_observer(longFailure.c_str());
    assert(retained.milestoneKind==X4Boot::Failure && retained.messageTruncated==1 && std::strlen(retained.milestone)==159);
    assert(X4Boot::valid(retained));
    freshGlobals();resetReason=3;app_main();
    assert(previousValid && previous.displayCompleted && previous.milestoneCount==4 && reports==5);
    assert(lines[3].find("first_display=1")!=std::string::npos);
    assert(lines[4].find("previous-line RTE_BOOT error=runtime")!=std::string::npos);
    assert(retained.milestoneCount==0 && !retained.displayCompleted);
    std::puts("X4 named milestone/cold host/first frame/reset report/bounds/no-touch-work PASS");return 0;
  }
  if(!std::strcmp(argv[1],"reset")) {
    for(const int cause : {1,3,4,5,6,7,8,9}) {
      freshGlobals();retained={};failure=0;held=physicalHigh=true;crash=1;
      try {app_main();assert(false);} catch(int){}
      assert(X4Boot::valid(retained) && retained.phase==X4Boot::RailReady);
      freshGlobals();crash=0;resetReason=cause;app_main();
      assert(previousValid && previous.phase==X4Boot::RailReady && retained.boot==2);
      assert(retained.reset==uint32_t(cause) && reports==3 && retained.phase==X4Boot::SetupGate);
      assert(lines.back().find("checksum_valid=1")!=std::string::npos);
      auto good=retained;
      for(size_t byte=0;byte<sizeof(retained);++byte) {
        retained=good;reinterpret_cast<unsigned char*>(&retained)[byte]^=1;
        assert(!X4Boot::valid(retained));
      }
      retained=good;X4Boot::changing(retained);assert(!X4Boot::valid(retained));
      freshGlobals();app_main();assert(!previousValid && retained.boot==1);
    }
    std::puts("X4 retained-reset/crash/corruption PASS");return 0;
  }
  assert(argc==3);
  failure=std::atoi(argv[1]);initialHeld=std::atoi(argv[2]);
  held=initialHeld!=0;physicalHigh=initialHeld==1;
  assert(!std::strcmp(risc_native_startup_error(),"x4-app-main-not-entered"));
  initVariant();assert(!attempted && !variant && step==0);
  app_main();
  const char* expected[]={nullptr,"x4-gpio1-rtc-deinit","x4-gpio1-stage-high","x4-gpio1-configure",
    "x4-gpio1-confirm-high","x4-gpio1-unhold","x4-gpio1-hold","x4-gpio1-readback-low"};
  const auto error=risc_native_startup_error();
  if(failure)assert(error && !std::strcmp(error,expected[failure]));
  else assert(!error && held && physicalHigh && sensing);
  if(failure>=1 && failure<=5)assert(held==(initialHeld!=0));
  if(failure==6)assert(!held && physicalHigh);
  if(failure==7)assert(held && physicalHigh);
  assert(X4Boot::valid(retained) && retained.operation==uint32_t(failure) && retained.phase==X4Boot::SetupGate);
  assert(retained.entryUs<retained.variantUs && retained.variantUs<retained.gateUs);
  assert(retained.brownout==0x12345678 && retained.holdBefore==uint32_t(initialHeld?2:0));
  assert(retained.gpioHighBefore==0xabcdef01 && retained.strapBefore==0x654321);
  assert(reports==3 && !previousValid);
  const std::vector<std::string> expectedCalls={
    "early-nvs",
#if !ARDUINO_USB_MODE
    "serial","usb",
#endif
    "cpu","psram","log","nvs","init","task","setup"};
  assert(calls==expectedCalls);
  const int saved=step;initVariant();(void)risc_native_startup_error();assert(step==saved && reports==3);
  std::puts("X4 pinned startup ordering/failure PASS");
}
