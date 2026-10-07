#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include "../drivers/x4pro_board_power/X4BoardKeepaliveV1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned claims,reads,releases,writes,holds,unholds,locks,unlocks,destroys;
static bool claim_ok=true,read_ok=true,release_ok=true,write_ok=true,high=true,owner=true,locked,held;
static bool create_ok=true,lock_ok=true,unlock_ok=true,destroy_ok=true;
static int32_t hold_result,unhold_result;
static bool fail_rollback_read,reentrant_restore;
static const x4_board_keepalive_v1 *transaction;
static unsigned rejected_restores;
static void recursive_restore(void){if(reentrant_restore){assert(transaction->restore(NULL)==RISC_DEEP_SLEEP_BUSY);++rejected_restores;}}
static unsigned order;
static bool own(void*c){(void)c;return owner;}
static bool create(void*c,uint64_t*t){(void)c;*t=create_ok?9:0;return create_ok;}
static bool take(void*c,uint64_t t){(void)c;assert(owner&&t==9);++locks;if(locked||!lock_ok)return false;locked=true;return true;}
static bool unlock(void*c,uint64_t t){(void)c;assert(t==9&&locked);++unlocks;if(!unlock_ok)return false;locked=false;return true;}
static bool destroy(void*c,uint64_t t){(void)c;assert(t==9&&!locked);++destroys;return destroy_ok;}
static bool claim(void *c,uint8_t pin,bool output,bool initial,bool pullup,uint64_t *out){
 (void)c;++claims;assert(locked&&pin==1&&output&&initial&&!pullup);*out=claim_ok?41:0;return claim_ok;
}
static bool write_pin(void*c,uint64_t t,bool v){(void)c;assert(locked&&t==41&&v&&!held);++writes;order=1;return write_ok;}
static bool read_pin(void *c,uint64_t t,bool *out){(void)c;++reads;assert(locked&&t==41);recursive_restore();if(order==1)order=2;if(read_ok)*out=high;return read_ok;}
static int32_t hold_pin(void*c,uint64_t t,bool enable){(void)c;assert(locked&&t==41);if(enable){++holds;assert(order==2);order=3;if(!hold_result)held=true;if(fail_rollback_read)read_ok=false;return hold_result;}++unholds;recursive_restore();if(!unhold_result)held=false;return unhold_result;}
static bool release_pin(void *c,uint64_t t){(void)c;++releases;assert(locked&&t==41&&!held);return release_ok;}
int main(int argc,char **argv){
 assert(argc==2);const char*s=argv[1];
 garden_gpio_v1 gpio={.api_version=1,.struct_size=sizeof(gpio),.claim=claim,.write=write_pin,.read=read_pin,.release=release_pin,.deep_sleep_hold=hold_pin};
 risc_provider_sync_api_v1 sync={1,sizeof(sync),NULL,own,create,take,unlock,destroy};
 risc_hw_gpio_bank_v1 config={.struct_size=sizeof(config),.count=1,.active_high=1,.pins={1}};
 risc_hardware_device_v1 hardware={1,sizeof(hardware),1,"xteink,x4-pro-peripheral-enable","unspecified","gpio.bank",1,sizeof(config),&config};
 risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hardware},{"platform.gpio",1,&gpio},{"platform.sync",1,&sync}};
 const risc_driver_v2 *driver=t5_driver_get(2);assert(driver&&!t5_driver_get(1));
 const x4_power_ready_api_v1 *api=driver->capability;assert(api->api_version==1&&api->struct_size==sizeof(x4_board_keepalive_v1));
 const x4_board_keepalive_v1 *a=x4_board_keepalive(api);assert(a);transaction=a;
 x4_board_keepalive_v1 bad=*a;bad.power.struct_size=sizeof(bad.power);assert(!x4_board_keepalive(&bad.power));bad=*a;bad.extension_tag=0;assert(!x4_board_keepalive(&bad.power));bad=*a;bad.extension_version=2;assert(!x4_board_keepalive(&bad.power));bad=*a;bad.prepare=NULL;assert(!x4_board_keepalive(&bad.power));bad=*a;bad.restore=NULL;assert(!x4_board_keepalive(&bad.power));
 assert(!api->ready(api->context));
 if(!strcmp(s,"validation")){
  assert(!driver->start(NULL,3)&&!driver->start(deps,2));
  hardware.config_size--;assert(!driver->start(deps,3));hardware.config_size++;
  hardware.instance_id=2;assert(!driver->start(deps,3));hardware.instance_id=1;
  hardware.compatible="other,device";assert(!driver->start(deps,3));hardware.compatible="xteink,x4-pro-peripheral-enable";
  config.pins[0]=2;assert(!driver->start(deps,3));config.pins[0]=1;
  config.pull_up=1;assert(!driver->start(deps,3));config.pull_up=0;
  gpio.struct_size=8;assert(!driver->start(deps,3));gpio.struct_size=sizeof(gpio);
  sync.unlock=NULL;assert(!driver->start(deps,3));sync.unlock=unlock;
  owner=false;assert(!driver->start(deps,3));owner=true;
  deps[1]=deps[0];assert(!driver->start(deps,3));
  assert(!claims&&!reads&&!releases&&!locks&&driver->quiesce());return 0;
 }
 if(!strcmp(s,"create-failure")||!strcmp(s,"start-lock-failure")){
  create_ok=strcmp(s,"create-failure")!=0;lock_ok=strcmp(s,"start-lock-failure")!=0;
  assert(!driver->start(deps,3)&&!claims);lock_ok=true;assert(driver->quiesce());return 0;
 }
 bool start_retained=!strcmp(s,"retained-claim")||!strcmp(s,"start-read")||!strcmp(s,"start-low")||!strcmp(s,"start-unlock");
 claim_ok=strcmp(s,"retained-claim")!=0;read_ok=strcmp(s,"start-read")!=0;high=strcmp(s,"start-low")!=0;unlock_ok=strcmp(s,"start-unlock")!=0;
 assert(driver->start(deps,3)==!start_retained);
 if(!start_retained){
  assert(api->ready(NULL));assert(!driver->start(deps,3));
  if(!strcmp(s,"legacy-gpio"))gpio.struct_size=offsetof(garden_gpio_v1,waveform);
  if(!strcmp(s,"missing-hold"))gpio.deep_sleep_hold=NULL;
  if(!strcmp(s,"missing-write"))gpio.write=NULL;
  if(!strcmp(s,"owner")){owner=false;assert(a->prepare(NULL)==RISC_DEEP_SLEEP_CONTEXT&&!writes);owner=true;}
  else if(!strcmp(s,"busy-lock")){lock_ok=false;assert(a->prepare(NULL)==RISC_DEEP_SLEEP_CONTEXT&&!writes);lock_ok=true;}
  else if(!strcmp(s,"legacy-gpio")||!strcmp(s,"missing-hold")||!strcmp(s,"missing-write")){assert(a->prepare(NULL)==RISC_DEEP_SLEEP_UNSUPPORTED&&!writes&&api->ready(NULL));}
  else if(!strcmp(s,"release-retry")){release_ok=false;assert(!driver->quiesce()&&!api->ready(NULL));release_ok=true;}
  else if(!strcmp(s,"destroy-retry")){destroy_ok=false;assert(!driver->quiesce()&&!api->ready(NULL));destroy_ok=true;}
  else {
   if(!strcmp(s,"ready-read"))read_ok=false;
   if(!strcmp(s,"ready-low"))high=false;
   if(!strcmp(s,"ready-unlock"))unlock_ok=false;
   if(!strncmp(s,"ready-",6))assert(!api->ready(NULL));
   else {
    if(!strcmp(s,"prepare-write"))write_ok=false;
    if(!strcmp(s,"prepare-read"))read_ok=false;
    if(!strcmp(s,"prepare-low"))high=false;
    if(!strcmp(s,"hold-retained"))hold_result=RISC_DEEP_SLEEP_RETAINED;
    if(!strcmp(s,"hold-unknown"))hold_result=1;
    if(!strcmp(s,"hold-unknown-negative"))hold_result=-99;
    if(!strncmp(s,"hold-refused",12))hold_result=RISC_DEEP_SLEEP_PLATFORM;
    if(!strcmp(s,"hold-refused-read"))fail_rollback_read=true;
    if(!strcmp(s,"hold-refused-invalid"))hold_result=RISC_DEEP_SLEEP_INVALID;
    if(!strcmp(s,"hold-refused-context"))hold_result=RISC_DEEP_SLEEP_CONTEXT;
    if(!strcmp(s,"hold-refused-busy"))hold_result=RISC_DEEP_SLEEP_BUSY;
    if(!strcmp(s,"hold-refused-active"))hold_result=RISC_DEEP_SLEEP_ACTIVE_WAKE;
    if(!strcmp(s,"hold-refused-unsupported"))hold_result=RISC_DEEP_SLEEP_UNSUPPORTED;
    if(!strcmp(s,"hold-refused-unlock"))unlock_ok=false;
    int32_t rc=a->prepare(NULL);
    if(!strncmp(s,"hold-refused",12)&&!strstr(s,"unlock")&&!strstr(s,"-read")){assert(rc==hold_result&&!held&&api->ready(NULL));}
    else if(rc==0){
     assert(held&&locked&&order==3);unsigned old=reads;
     assert(!api->ready(NULL)&&!driver->quiesce()&&reads==old&&!releases);
     if(!strcmp(s,"prepared-stop")){driver->stop();assert(!driver->start(deps,3)&&reads==old);return 0;}
     owner=false;assert(a->restore(NULL)==RISC_DEEP_SLEEP_CONTEXT&&held);owner=true;
     if(!strcmp(s,"restore-unhold"))unhold_result=RISC_DEEP_SLEEP_RETAINED;
     if(!strcmp(s,"restore-ordinary-error"))unhold_result=RISC_DEEP_SLEEP_PLATFORM;
     if(!strcmp(s,"restore-unknown"))unhold_result=1;
     if(!strcmp(s,"restore-read"))read_ok=false;
     if(!strcmp(s,"restore-low"))high=false;
     if(!strcmp(s,"restore-unlock"))unlock_ok=false;
     reentrant_restore=!strcmp(s,"reentrant-restore");
     rc=a->restore(NULL);
     reentrant_restore=false;
     if(!strcmp(s,"lifecycle")||!strcmp(s,"reentrant-restore")){
      if(!strcmp(s,"reentrant-restore"))assert(rejected_restores==2);
      assert(rc==0&&!held&&!locked&&api->ready(NULL));
      for(unsigned i=0;i<5;++i){assert(a->prepare(NULL)==0);assert(a->restore(NULL)==0);assert(api->ready(NULL));}
      assert(holds==6&&unholds==6);assert(a->restore(NULL)==RISC_DEEP_SLEEP_BUSY);
     }else assert(rc==RISC_DEEP_SLEEP_RETAINED);
    }else assert(rc==RISC_DEEP_SLEEP_RETAINED);
   }
  }
 }
 bool uncertain=start_retained||!strncmp(s,"ready-",6)||!strncmp(s,"prepare-",8)||!strncmp(s,"restore-",8)||!strncmp(s,"hold-retained",13)||!strncmp(s,"hold-unknown",12)||!strcmp(s,"hold-refused-unlock")||!strcmp(s,"hold-refused-read");
 if(uncertain){
  unsigned oldio=claims+reads+releases+writes+holds+unholds,oldsync=locks+unlocks+destroys;
  read_ok=high=write_ok=unlock_ok=true;hold_result=unhold_result=0;
  assert(a->prepare(NULL)==RISC_DEEP_SLEEP_RETAINED&&a->restore(NULL)==RISC_DEEP_SLEEP_RETAINED);
  assert(!api->ready(NULL)&&!driver->quiesce());driver->stop();assert(!driver->start(deps,3));
  assert(locked&&!releases&&oldio==claims+reads+releases+writes+holds+unholds&&oldsync==locks+unlocks+destroys);
 }else{assert(driver->quiesce()&&!locked&&!held);driver->stop();assert(driver->quiesce());}
 printf("X4 board keepalive %s PASS\n",s);return 0;
}
