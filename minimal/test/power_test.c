#include "../drivers/x4pro_power/X4PowerV1.h"
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include <RiscPlatformClockV1.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool down,owner=true,locked,read_ok=true,release_ok=true,destroy_ok=true,unlock_ok=true;
static uint64_t now=100,live;static unsigned entries,reads,releases;static int32_t outcome;
static bool own(void*c){(void)c;return owner;}
static bool create(void*c,uint64_t*t){(void)c;*t=9;return true;}
static bool take(void*c,uint64_t t){(void)c;assert(t==9);if(locked)return false;locked=true;return true;}
static bool unlock(void*c,uint64_t t){(void)c;assert(t==9&&locked);if(!unlock_ok)return false;locked=false;return true;}
static bool destroy(void*c,uint64_t t){(void)c;assert(t==9&&!locked);return destroy_ok;}
static uint64_t ticks(void*c){(void)c;return now;}
static bool claim(void*c,uint8_t p,bool o,bool i,bool u,uint64_t*t){(void)c;assert(p==3&&!o&&!i&&u&&locked&&!live);*t=live=71;return true;}
static bool read_pin(void*c,uint64_t t,bool*h){(void)c;assert(t==live&&locked);reads++;if(!read_ok)return false;*h=!down;return true;}
static bool release_pin(void*c,uint64_t t){(void)c;assert(t==live&&locked);releases++;if(!release_ok)return false;live=0;return true;}
static int32_t sleep_pin(void*c,uint64_t t,bool high,risc_light_sleep_result_v1*r){(void)c;assert(t==live&&!high&&locked&&!down);entries++;r->wake_cause=RISC_LIGHT_SLEEP_WAKE_GPIO;return outcome;}
static int32_t timed(void*c,uint64_t t,bool h,uint32_t ms,risc_light_sleep_result_v1*r){assert(ms==250);return sleep_pin(c,t,h,r);}
static garden_gpio_v1 gpio={.api_version=1,.struct_size=sizeof(gpio),.claim=claim,.read=read_pin,.release=release_pin,.light_sleep=sleep_pin,.light_sleep_for=timed};
static const risc_provider_sync_api_v1 sync={1,sizeof(sync),NULL,own,create,take,unlock,destroy};
static const risc_platform_clock_api_v1 clock_api={1,sizeof(clock_api),NULL,ticks,NULL};
static risc_hw_gpio_bank_v1 cfg={.struct_size=sizeof(cfg),.count=1,.pull_up=1,.pins={3}};
static risc_hardware_device_v1 hw={1,sizeof(hw),17,"xteink,x4-pro-power-key","unspecified","gpio.bank",1,sizeof(cfg),&cfg};
static risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hw},{"platform.gpio",1,&gpio},{"platform.sync",1,&sync},{"platform.clock",1,&clock_api}};
int main(int argc,char**argv){
 assert(argc==2);const risc_driver_v2*d=t5_driver_get(2);const x4_power_v1*a=d->capability;
 assert(!strcmp(d->capability_id,X4_POWER_CAPABILITY));
 if(!strcmp(argv[1],"validation")) {
  cfg.pins[0]=7;assert(!d->start(deps,4)&&!live);cfg.pins[0]=3;
  gpio.struct_size=GARDEN_GPIO_LIGHT_SLEEP_V1_SIZE;assert(!d->start(deps,4)&&!live);return 0;
 }
 down=!strcmp(argv[1],"held");assert(d->start(deps,4));
 risc_light_sleep_result_v1 r={.struct_size=sizeof(r)};bool key=false;
 assert(a->light_sleep(NULL,0,&r)==RISC_LIGHT_SLEEP_ACTIVE_WAKE&&!entries);
 if(down){now+=100;assert(a->light_sleep(NULL,0,&r)==RISC_LIGHT_SLEEP_ACTIVE_WAKE);down=false;assert(a->read_key(NULL,&key)&&!key);}
 now+=30;
 if(!strcmp(argv[1],"read-failure")){read_ok=false;assert(a->light_sleep(NULL,0,&r)==RISC_LIGHT_SLEEP_PLATFORM&&!entries);read_ok=true;}
 else if(!strcmp(argv[1],"clock-backwards")){now=1;assert(a->light_sleep(NULL,0,&r)==RISC_LIGHT_SLEEP_PLATFORM&&!entries);now=200;}
 else if(!strcmp(argv[1],"context")){owner=false;assert(a->light_sleep(NULL,0,&r)==RISC_LIGHT_SLEEP_CONTEXT&&!entries);owner=true;}
 else {
  if(!strcmp(argv[1],"retained"))outcome=RISC_LIGHT_SLEEP_RETAINED;
  if(!strcmp(argv[1],"refused"))outcome=RISC_LIGHT_SLEEP_BUSY;
  assert(a->light_sleep(NULL,250,&r)==outcome&&entries==1);
  if(outcome==RISC_LIGHT_SLEEP_RETAINED){unsigned old=reads;assert(!a->read_key(NULL,&key)&&!d->quiesce()&&live&&reads==old&&!releases);assert(a->light_sleep(NULL,0,&r)==RISC_LIGHT_SLEEP_RETAINED);return 0;}
  assert(a->light_sleep(NULL,0,&r)==RISC_LIGHT_SLEEP_ACTIVE_WAKE&&entries==1);
  down=true;now+=30;assert(a->read_key(NULL,&key)&&key);assert(a->light_sleep(NULL,0,&r)==RISC_LIGHT_SLEEP_ACTIVE_WAKE);
  down=false;assert(a->read_key(NULL,&key));now+=30;outcome=0;assert(a->light_sleep(NULL,0,&r)==0&&entries==2);
 }
 if(!strcmp(argv[1],"release-retry")){release_ok=false;assert(!d->quiesce()&&live);release_ok=true;}
 if(!strcmp(argv[1],"destroy-retry")){destroy_ok=false;assert(!d->quiesce()&&!live);destroy_ok=true;}
 if(!strcmp(argv[1],"unlock-retained")){unlock_ok=false;assert(!a->read_key(NULL,&key));assert(!d->quiesce()&&live);return 0;}
 assert(d->quiesce()&&!live&&!locked);printf("X4 power %s PASS\n",argv[1]);return 0;
}
