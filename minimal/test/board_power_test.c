#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include "../drivers/x4pro_board_power/X4BoardKeepaliveV1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned claims,reads,retired_reads,holds,retires,locks,unlocks,destroys;
static bool claim_ok=true,read_ok=true,high=true,owner=true,locked,held,retired;
static bool create_ok=true,lock_ok=true,unlock_ok=true,destroy_ok=true,retire_ok=true;
static bool retired_read_ok=true,reentrant_restore;
static int32_t hold_result;
static const x4_board_keepalive_v1 *transaction;
static unsigned rejected_restores;
static bool own(void*c){(void)c;return owner;}
static bool create(void*c,uint64_t*t){(void)c;*t=create_ok?9:0;return create_ok;}
static bool take(void*c,uint64_t t){(void)c;assert(owner&&t==9);++locks;if(locked||!lock_ok)return false;locked=true;return true;}
static bool unlock(void*c,uint64_t t){(void)c;assert(t==9&&locked);++unlocks;if(!unlock_ok)return false;locked=false;return true;}
static bool destroy(void*c,uint64_t t){(void)c;assert(t==9&&!locked);++destroys;return destroy_ok;}
static bool claim(void*c,uint8_t p,bool output,bool initial,bool pullup,uint64_t*out){
 (void)c;++claims;assert(locked&&p==1&&output&&initial&&!pullup);*out=claim_ok?41:0;
 if(claim_ok){held=false;retired=false;}return claim_ok;
}
static bool read_pin(void*c,uint64_t t,bool*out){(void)c;++reads;assert(locked&&t==41&&!retired);*out=high;return read_ok;}
static int32_t hold_pin(void*c,uint64_t t,bool enable){(void)c;assert(locked&&t==41&&enable&&!retired);++holds;if(!hold_result)held=true;return hold_result;}
static bool retire_pin(void*c,uint64_t t){(void)c;assert(locked&&t==41&&held&&!retired);++retires;if(retire_ok)retired=true;return retire_ok;}
static bool read_retired(void*c,uint8_t p,bool*out){
 (void)c;assert(locked&&p==1&&held&&retired);++retired_reads;
 if(reentrant_restore){assert(transaction->restore(NULL)==RISC_DEEP_SLEEP_BUSY);++rejected_restores;}
 *out=high;return retired_read_ok;
}
static bool forbidden_write(void*c,uint64_t t,bool v){(void)c;(void)t;(void)v;assert(!"permanent board hold must not be rewritten");return false;}
static bool forbidden_release(void*c,uint64_t t){(void)c;(void)t;assert(!"permanent board hold must not be released");return false;}
int main(int argc,char**argv){
 assert(argc==2);const char*s=argv[1];
 garden_gpio_v1 gpio={.api_version=1,.struct_size=sizeof(gpio),.claim=claim,.write=forbidden_write,.read=read_pin,.release=forbidden_release,.deep_sleep_hold=hold_pin,.retire_held_output=retire_pin,.read_retired_output=read_retired};
 risc_provider_sync_api_v1 sync={1,sizeof(sync),NULL,own,create,take,unlock,destroy};
 risc_hw_gpio_bank_v1 config={.struct_size=sizeof(config),.count=1,.active_high=1,.pins={1}};
 risc_hardware_device_v1 hardware={1,sizeof(hardware),1,"xteink,x4-pro-peripheral-enable","unspecified","gpio.bank",1,sizeof(config),&config};
 risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hardware},{"platform.gpio",1,&gpio},{"platform.sync",1,&sync}};
 const risc_driver_v2*d=t5_driver_get(2);assert(d&&!t5_driver_get(1));const x4_power_ready_api_v1*api=d->capability;
 const x4_board_keepalive_v1*a=x4_board_keepalive(api);assert(a);transaction=a;
 x4_board_keepalive_v1 bad=*a;bad.power.struct_size=sizeof(bad.power);assert(!x4_board_keepalive(&bad.power));bad=*a;bad.extension_tag=0;assert(!x4_board_keepalive(&bad.power));bad=*a;bad.extension_version=2;assert(!x4_board_keepalive(&bad.power));bad=*a;bad.prepare=NULL;assert(!x4_board_keepalive(&bad.power));bad=*a;bad.restore=NULL;assert(!x4_board_keepalive(&bad.power));
 assert(!api->ready(NULL));
 if(!strcmp(s,"validation")){
  assert(!d->start(NULL,3)&&!d->start(deps,2));hardware.config_size--;assert(!d->start(deps,3));hardware.config_size++;
  hardware.instance_id=2;assert(!d->start(deps,3));hardware.instance_id=1;
  hardware.compatible="other,device";assert(!d->start(deps,3));hardware.compatible="xteink,x4-pro-peripheral-enable";
  config.pins[0]=2;assert(!d->start(deps,3));config.pins[0]=1;config.pull_up=1;assert(!d->start(deps,3));config.pull_up=0;
  gpio.struct_size=GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE;assert(!d->start(deps,3));gpio.struct_size=sizeof(gpio);
  gpio.read_retired_output=NULL;assert(!d->start(deps,3));gpio.read_retired_output=read_retired;
  gpio.retire_held_output=NULL;assert(!d->start(deps,3));gpio.retire_held_output=retire_pin;
  gpio.deep_sleep_hold=NULL;assert(!d->start(deps,3));gpio.deep_sleep_hold=hold_pin;
  sync.unlock=NULL;assert(!d->start(deps,3));sync.unlock=unlock;owner=false;assert(!d->start(deps,3));owner=true;
  deps[1]=deps[0];assert(!d->start(deps,3));assert(!claims&&!reads&&!holds&&!locks&&d->quiesce());return 0;
 }
 if(!strcmp(s,"create-failure")||!strcmp(s,"start-lock-failure")){
  create_ok=strcmp(s,"create-failure")!=0;lock_ok=strcmp(s,"start-lock-failure")!=0;
  assert(!d->start(deps,3)&&!claims);lock_ok=true;assert(d->quiesce());return 0;
 }
 bool uncertain=!strncmp(s,"start-",6);
 claim_ok=strcmp(s,"start-claim")!=0;read_ok=strcmp(s,"start-read")!=0;high=strcmp(s,"start-low")!=0;
 retire_ok=strcmp(s,"start-retire")!=0;retired_read_ok=strcmp(s,"start-retired-read")!=0;unlock_ok=strcmp(s,"start-unlock")!=0;
 if(!strncmp(s,"start-hold-",11)){
  const char*name=s+11;
  if(!strcmp(name,"invalid"))hold_result=RISC_DEEP_SLEEP_INVALID;
  else if(!strcmp(name,"context"))hold_result=RISC_DEEP_SLEEP_CONTEXT;
  else if(!strcmp(name,"busy"))hold_result=RISC_DEEP_SLEEP_BUSY;
  else if(!strcmp(name,"active"))hold_result=RISC_DEEP_SLEEP_ACTIVE_WAKE;
  else if(!strcmp(name,"unsupported"))hold_result=RISC_DEEP_SLEEP_UNSUPPORTED;
  else if(!strcmp(name,"retained"))hold_result=RISC_DEEP_SLEEP_RETAINED;
  else if(!strcmp(name,"unknown"))hold_result=1;
  else if(!strcmp(name,"negative"))hold_result=-99;
  else hold_result=RISC_DEEP_SLEEP_PLATFORM;
 }
 assert(d->start(deps,3)==!uncertain);
 if(!uncertain){
  assert(api->ready(NULL)&&held&&retired&&!locked&&holds==1&&retires==1);assert(!d->start(deps,3));
  unsigned before=retired_reads;
  if(!strcmp(s,"owner")){owner=false;assert(!api->ready(NULL)&&a->prepare(NULL)==RISC_DEEP_SLEEP_CONTEXT&&retired_reads==before);owner=true;}
  else if(!strcmp(s,"busy-lock")){lock_ok=false;assert(!api->ready(NULL)&&a->prepare(NULL)==RISC_DEEP_SLEEP_CONTEXT&&retired_reads==before);lock_ok=true;}
  else if(!strcmp(s,"destroy-retry")){destroy_ok=false;assert(!d->quiesce()&&!api->ready(NULL));destroy_ok=true;}
  else if(!strncmp(s,"ready-",6)||!strncmp(s,"quiesce-",8)){
   retired_read_ok=strstr(s,"read")==NULL;high=strstr(s,"low")==NULL;unlock_ok=strstr(s,"unlock")==NULL;
   if(!strncmp(s,"ready-",6))assert(!api->ready(NULL));else assert(!d->quiesce());uncertain=true;
  }else{
   retired_read_ok=strcmp(s,"prepare-read")!=0;high=strcmp(s,"prepare-low")!=0;
   int32_t rc=a->prepare(NULL);
   if(!strncmp(s,"prepare-",8)){assert(rc==RISC_DEEP_SLEEP_RETAINED);uncertain=true;}
   else{
    assert(rc==0&&held&&retired&&locked);before=retired_reads;assert(!api->ready(NULL)&&!d->quiesce()&&retired_reads==before);
    owner=false;assert(a->restore(NULL)==RISC_DEEP_SLEEP_CONTEXT);owner=true;
    if(!strcmp(s,"prepared-stop")){d->stop();assert(!d->start(deps,3)&&held&&retired&&locked);return 0;}
    retired_read_ok=strcmp(s,"restore-read")!=0;high=strcmp(s,"restore-low")!=0;unlock_ok=strcmp(s,"restore-unlock")!=0;
    reentrant_restore=!strcmp(s,"reentrant-restore");rc=a->restore(NULL);reentrant_restore=false;
    if(!strncmp(s,"restore-",8)){assert(rc==RISC_DEEP_SLEEP_RETAINED);uncertain=true;}
    else{assert(rc==0&&!locked&&held&&retired&&api->ready(NULL));if(!strcmp(s,"reentrant-restore"))assert(rejected_restores==1);
     for(unsigned i=0;i<5;++i){assert(a->prepare(NULL)==0&&a->restore(NULL)==0&&api->ready(NULL));}
     assert(a->restore(NULL)==RISC_DEEP_SLEEP_BUSY&&holds==1&&retires==1);
    }
   }
  }
 }
 if(uncertain){
  unsigned io=claims+reads+retired_reads+holds+retires,ops=locks+unlocks+destroys;
  read_ok=retired_read_ok=high=unlock_ok=true;
  assert(a->prepare(NULL)==RISC_DEEP_SLEEP_RETAINED&&a->restore(NULL)==RISC_DEEP_SLEEP_RETAINED);
  assert(!api->ready(NULL)&&!d->quiesce());d->stop();assert(!d->start(deps,3));
  assert(locked&&io==claims+reads+retired_reads+holds+retires&&ops==locks+unlocks+destroys);
 }else{
  assert(d->quiesce()&&!locked&&held&&retired);d->stop();assert(d->quiesce());
  assert(d->start(deps,3)&&api->ready(NULL)&&claims==2&&holds==2&&retires==2);assert(d->quiesce()&&held&&retired);
 }
 printf("X4 persistent board keepalive %s PASS\n",s);return 0;
}
