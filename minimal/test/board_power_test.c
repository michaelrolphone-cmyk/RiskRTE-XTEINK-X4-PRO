#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <GardenPlatformV1.h>
#include "../drivers/x4pro_board_power/PowerReadyV1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned claims,reads,releases;
static bool claim_ok=true,read_ok=true,release_ok=true,high=true;
static bool claim(void *c,uint8_t pin,bool output,bool initial,bool pullup,uint64_t *out){
 (void)c;++claims;assert(pin==1 && output && initial && !pullup);*out=claim_ok?41:0;return claim_ok;
}
static bool read_pin(void *c,uint64_t t,bool *out){(void)c;++reads;assert(t==41);if(read_ok)*out=high;return read_ok;}
static bool release_pin(void *c,uint64_t t){(void)c;++releases;assert(t==41);return release_ok;}
int main(int argc,char **argv){
 assert(argc==2);
 garden_gpio_v1 gpio={.api_version=1,.struct_size=sizeof(gpio),.claim=claim,.read=read_pin,.release=release_pin};
 risc_hw_gpio_bank_v1 config={.struct_size=sizeof(config),.count=1,.active_high=1,.pins={1}};
 risc_hardware_device_v1 hardware={1,sizeof(hardware),1,"xteink,x4-pro-peripheral-enable","unspecified","gpio.bank",1,sizeof(config),&config};
 risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hardware},{"platform.gpio",1,&gpio}};
 const risc_driver_v2 *driver=t5_driver_get(2);assert(driver && !t5_driver_get(1));
 const x4_power_ready_api_v1 *api=driver->capability;assert(api->api_version==1 && api->struct_size==sizeof(*api));
 assert(!api->ready(api->context));
 if(!strcmp(argv[1],"validation")){
  assert(!driver->start(NULL,2) && !driver->start(deps,1));
  hardware.config_size--;assert(!driver->start(deps,2));hardware.config_size++;
  hardware.compatible="other,device";assert(!driver->start(deps,2));hardware.compatible="xteink,x4-pro-peripheral-enable";
  config.pins[0]=2;assert(!driver->start(deps,2));config.pins[0]=1;
  config.pull_up=1;assert(!driver->start(deps,2));config.pull_up=0;
  gpio.struct_size=8;assert(!driver->start(deps,2));gpio.struct_size=sizeof(gpio);
  deps[1]=deps[0];assert(!driver->start(deps,2));
  assert(!claims && !reads && !releases && driver->quiesce());
 }else if(!strcmp(argv[1],"retained-claim")){
  claim_ok=false;assert(!driver->start(deps,2));
  assert(!driver->quiesce() && !api->ready(api->context));driver->stop();
  claim_ok=true;assert(!driver->start(deps,2));assert(claims==1 && !releases);
 }else if(!strcmp(argv[1],"readback")){
  high=false;assert(!driver->start(deps,2));assert(claims==1 && reads==1);
  char reason[80];const risc_driver_diagnostics_v2 *diagnostics=(const risc_driver_diagnostics_v2*)driver;
  assert(driver->struct_size>=sizeof(*diagnostics) && diagnostics->last_error(reason,sizeof(reason)) && !strcmp(reason,"gpio1 readback LOW after HIGH claim"));
  assert(driver->quiesce() && releases==1);driver->stop();
  high=true;assert(driver->start(deps,2) && api->ready(api->context));assert(driver->quiesce());
 }else{
  assert(!strcmp(argv[1],"lifecycle"));
  assert(driver->start(deps,2) && api->ready(api->context));
  assert(!driver->start(deps,2) && claims==1);
  release_ok=false;assert(!driver->quiesce() && !api->ready(api->context));
  driver->stop();assert(!driver->start(deps,2) && claims==1);
  release_ok=true;assert(driver->quiesce() && releases==2);driver->stop();
  assert(driver->quiesce() && releases==2);
  assert(driver->start(deps,2));read_ok=false;assert(!api->ready(api->context));
  read_ok=true;assert(driver->quiesce());
 }
 puts("X4 ordinary board-power provider PASS");
}
