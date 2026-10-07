#include <RiscProviderV2.h>
#include <RiscI2cBusV1.h>
#include <RiscRtcClockV2.h>
#include <TWatchHardwareV1.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t registers[32];static unsigned claims,writes,reads,releases;
static bool claimed,io_ok=true,release_ok=true;
static bool claim_device(void *c,uint8_t address,uint64_t *out){(void)c;*out=0;assert(address==0x51);++claims;if(claimed)return false;claimed=true;*out=7;return true;}
static bool transact(void *c,uint64_t token,const uint8_t *tx,size_t tn,uint8_t *rx,size_t rn,uint32_t ms){
 (void)c;assert(claimed&&token==7&&tx&&tn&&ms==30);if(!io_ok)return false;
 assert(tx[0]+(rn?rn:tn-1)<=sizeof(registers));
 if(rn){assert(tn==1&&rx);memcpy(rx,registers+tx[0],rn);++reads;}
 else{assert(!rx);memcpy(registers+tx[0],tx+1,tn-1);++writes;}return true;
}
static bool release_device(void *c,uint64_t token){(void)c;assert(claimed&&token==7);++releases;if(!release_ok)return false;claimed=false;return true;}
int main(void){
 risc_i2c_bus_contract_v1 bus={{1,sizeof(bus),NULL,claim_device,transact,release_device},RISC_I2C_BUS_CONTRACT_TAG,1,RISC_I2C_BUS_SAFE_CONTRACT_FLAGS};
 tw_hw_i2c_device_v1 config={.struct_size=sizeof(config),.bus={.struct_size=sizeof(config.bus),.kind=2,.instance_id=101},.address=0x51,.irq=-1};
 risc_hardware_device_v1 hardware={1,sizeof(hardware),8,"riscrte,pcf8563-compatible-rtc","unspecified","peripheral.i2c",1,sizeof(config),&config};
 risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hardware},{"i2c.bus",1,&bus}};
 const risc_driver_v2 *driver=t5_driver_get(2);const risc_rtc_clock_api_v2 *api=driver->capability;
 assert(driver&&api->api_version==2);assert(!driver->start(deps,1));
 config.address=0x50;assert(!driver->start(deps,2));config.address=0x51;
 config.irq=4;assert(!driver->start(deps,2));config.irq=-1;
 bus.contract_tag=0;assert(!driver->start(deps,2));bus.contract_tag=RISC_I2C_BUS_CONTRACT_TAG;
 assert(!claims&&!writes&&!reads);
 registers[0]=0x20;assert(driver->start(deps,2)&&claims==1&&!writes);
 risc_rtc_time_v2 value={2026,10,7,3,8,9,10},out={2099,12,31,4,23,59,59},sentinel=out;
 assert(!api->read(NULL,&out)&&!memcmp(&out,&sentinel,sizeof(out))&&!writes);
 assert(api->write(NULL,&value)&&registers[0]==0&&writes==2);
 assert(api->read(NULL,&out)&&out.year==2026&&out.month==10&&out.day==7&&out.minute==9);
 unsigned before=writes;assert(!api->alarm(NULL,1,2,3,4,true)&&writes==before);
 registers[2]|=0x80;out=sentinel;assert(!api->read(NULL,&out)&&!memcmp(&out,&sentinel,sizeof(out)));registers[2]&=0x7f;
 io_ok=false;out=sentinel;assert(!api->read(NULL,&out)&&!memcmp(&out,&sentinel,sizeof(out)));io_ok=true;
 release_ok=false;assert(!driver->quiesce()&&claimed);driver->stop();assert(claimed&&!driver->start(deps,2));
 release_ok=true;assert(driver->quiesce()&&!claimed);driver->stop();before=reads;assert(driver->quiesce()&&reads==before);
 assert(driver->start(deps,2)&&claims==2);assert(driver->quiesce());
 puts("X4 typed RTC: config/safety admission, no startup writes, valid/STOP/VL/I-O behavior and retained claims PASS");
}
