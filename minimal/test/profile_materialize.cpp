#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
using namespace RiscCpu;
static Port* active;
static unsigned io;
static bool owner(){return true;}
static bool bind(RiscBoot::Runtime&r){return active->bind(r);}
static Hardware hardware(){Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){++io;};
h.gpioOpen=[](uint8_t,bool,bool,bool){++io;return false;};h.gpioWrite=[](uint8_t,bool){++io;return false;};h.gpioRead=[](uint8_t,bool*){++io;return false;};h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){++io;return false;};h.gpioClose=[](uint8_t){++io;return false;};
h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){++io;return false;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){++io;return false;};h.i2cClose=[](uint8_t){++io;return false;};
h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){++io;return false;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){++io;return false;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){++io;return false;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){++io;return false;};h.spiClose=[](uint8_t){++io;return false;};return h;}
int main(int argc,char**argv){assert(argc==2);Port p(hardware());active=&p;RiscBoot::Runtime r({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind});
if(!r.prepare(argv[1])){fprintf(stderr,"%s\n",r.error());return 1;}assert(!io);assert(r.board().deviceCount()==9);
for(unsigned id=1;id<=9;++id){const auto*d=r.board().device(id);assert(d);std::ofstream f(std::string(argv[1])+"/config-"+std::to_string(id)+".bin",std::ios::binary);f.write(static_cast<const char*>(d->hardware.config),d->hardware.config_size);}
assert(r.board().device(7)->config.peripheral.address==0x63 && r.board().device(8)->config.peripheral.address==0x51);
for(unsigned i=4;i<8;++i)assert(r.board().device(9)->config.gpio.pins[i]==0);
for(unsigned i=0;i<4;++i)assert(r.board().device(3)->config.display.power_pins[i]==0);
puts("Nine-provider real JSON/graph: selected battery/RTC, exact bindings, canonical zero-fill, scoped touch and no preflight hardware I/O PASS");}
