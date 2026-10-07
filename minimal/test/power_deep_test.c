#define _POSIX_C_SOURCE 200809L
#include "../drivers/x4pro_power/X4PowerDeepV1.h"
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include <RiscPlatformClockV1.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
static bool down,owner=true,locked,read_ok=true,release_ok=true,unlock_ok=true,terminal;
static uint32_t expected_ms=60000;
static uint64_t now=100,token;static unsigned reads,entries,releases;static int32_t outcome=RISC_DEEP_SLEEP_BUSY;
static bool own(void*c){(void)c;return owner;}
static bool create(void*c,uint64_t*t){(void)c;*t=9;return true;}
static bool take(void*c,uint64_t t){(void)c;assert(owner&&t==9);if(locked)return false;locked=true;return true;}
static bool unlock(void*c,uint64_t t){(void)c;assert(t==9&&locked);if(!unlock_ok)return false;locked=false;return true;}
static bool destroy(void*c,uint64_t t){(void)c;assert(t==9&&!locked);return true;}
static uint64_t ticks(void*c){(void)c;return now;}
static bool claim(void*c,uint8_t p,bool o,bool i,bool u,uint64_t*t){(void)c;assert(p==3&&!o&&!i&&u&&locked&&!token);*t=token=71;return true;}
static bool read_pin(void*c,uint64_t t,bool*h){(void)c;assert(t==token&&locked);reads++;if(!read_ok)return false;*h=!down;return true;}
static bool release_pin(void*c,uint64_t t){(void)c;assert(t==token&&locked);releases++;if(!release_ok)return false;token=0;return true;}
static int32_t light(void*c,uint64_t t,bool high,risc_light_sleep_result_v1*r){(void)c;assert(t==token&&!high&&locked);r->wake_cause=RISC_LIGHT_SLEEP_WAKE_GPIO;return 0;}
static int32_t timed_light(void*c,uint64_t t,bool high,uint32_t ms,risc_light_sleep_result_v1*r){assert(ms);return light(c,t,high,r);}
static int32_t deep(void*c,uint64_t t,bool high,uint32_t ms){(void)c;assert(t==token&&!high&&locked&&!down&&ms==expected_ms);++entries;if(terminal)_exit(73);return outcome;}
static garden_gpio_v1 gpio={.api_version=1,.struct_size=sizeof(gpio),.claim=claim,.read=read_pin,.release=release_pin,.light_sleep=light,.light_sleep_for=timed_light,.deep_sleep_for=deep};
static const risc_provider_sync_api_v1 sync={1,sizeof(sync),NULL,own,create,take,unlock,destroy};
static const risc_platform_clock_api_v1 clock_api={1,sizeof(clock_api),NULL,ticks,NULL};
static risc_hw_gpio_bank_v1 config={.struct_size=sizeof(config),.count=1,.pull_up=1,.pins={3}};
static const risc_hardware_device_v1 hw={1,sizeof(hw),17,"xteink,x4-pro-power-key","unspecified","gpio.bank",1,sizeof(config),&config};
static const risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hw},{"platform.gpio",1,&gpio},{"platform.sync",1,&sync},{"platform.clock",1,&clock_api}};
int main(int argc,char**argv){
 assert(argc==2);const char*s=argv[1];const risc_driver_v2*d=t5_driver_get(2);const x4_power_v1*base=d->capability;const x4_power_deep_v1*a=x4_power_deep(base);assert(a&&a->power.light_sleep);
 x4_power_deep_v1 invalid=*a;invalid.power.struct_size=sizeof(x4_power_v1);assert(!x4_power_deep(&invalid.power));invalid=*a;invalid.extension_tag=0;assert(!x4_power_deep(&invalid.power));invalid=*a;invalid.extension_version=2;assert(!x4_power_deep(&invalid.power));invalid=*a;invalid.deep_sleep_for=NULL;assert(!x4_power_deep(&invalid.power));
 assert(d->start(deps,4));unsigned before=reads;assert(a->deep_sleep_for(NULL,0)==RISC_DEEP_SLEEP_INVALID);assert(a->deep_sleep_for(NULL,RISC_TIMED_SLEEP_MAX_MS+1)==RISC_DEEP_SLEEP_INVALID);assert(a->deep_sleep_for(NULL,UINT32_MAX)==RISC_DEEP_SLEEP_INVALID);assert(before==reads&&!entries);
 assert(a->deep_sleep_for(NULL,60000)==RISC_DEEP_SLEEP_ACTIVE_WAKE);now+=30;
 if(!strcmp(s,"unsupported-prefix") || !strcmp(s,"unsupported-callback")){
  if(!strcmp(s,"unsupported-prefix"))gpio.struct_size=GARDEN_GPIO_LIGHT_SLEEP_FOR_V1_SIZE;else gpio.deep_sleep_for=NULL;
  assert(a->deep_sleep_for(NULL,60000)==RISC_DEEP_SLEEP_UNSUPPORTED&&!entries);risc_light_sleep_result_v1 result={.struct_size=sizeof(result)};assert(base->light_sleep(NULL,100,&result)==0);
 }else if(!strcmp(s,"bounds")){
  for(unsigned i=0;i<2;++i){expected_ms=i?RISC_TIMED_SLEEP_MAX_MS:1;bool key=false;assert(base->read_key(NULL,&key));now+=30;assert(a->deep_sleep_for(NULL,expected_ms)==RISC_DEEP_SLEEP_BUSY);}
 }else if(!strcmp(s,"held")){down=true;assert(a->deep_sleep_for(NULL,60000)==RISC_DEEP_SLEEP_ACTIVE_WAKE&&!entries);}
 else if(!strcmp(s,"read-failure")){read_ok=false;assert(a->deep_sleep_for(NULL,60000)==RISC_DEEP_SLEEP_PLATFORM&&!entries);read_ok=true;}
 else if(!strcmp(s,"clock-backwards")){now=1;assert(a->deep_sleep_for(NULL,60000)==RISC_DEEP_SLEEP_PLATFORM&&!entries);now=200;}
 else if(!strcmp(s,"owner")){owner=false;assert(a->deep_sleep_for(NULL,60000)==RISC_DEEP_SLEEP_CONTEXT&&!entries);owner=true;}
 else if(!strcmp(s,"terminal")){pid_t pid=fork();assert(pid>=0);if(!pid){terminal=true;(void)a->deep_sleep_for(NULL,60000);_exit(99);}int status=0;assert(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==73);assert(!entries&&!locked);}
 else {
  if(!strcmp(s,"native-invalid"))outcome=RISC_DEEP_SLEEP_INVALID;
  if(!strcmp(s,"native-context"))outcome=RISC_DEEP_SLEEP_CONTEXT;
  if(!strcmp(s,"native-active"))outcome=RISC_DEEP_SLEEP_ACTIVE_WAKE;
  if(!strcmp(s,"native-platform"))outcome=RISC_DEEP_SLEEP_PLATFORM;
  if(!strcmp(s,"native-unsupported"))outcome=RISC_DEEP_SLEEP_UNSUPPORTED;
  if(!strcmp(s,"retained"))outcome=RISC_DEEP_SLEEP_RETAINED;
  if(!strcmp(s,"unexpected-zero"))outcome=0;
  if(!strcmp(s,"unexpected-positive"))outcome=1;
  if(!strcmp(s,"unexpected-negative"))outcome=-99;
  if(!strcmp(s,"unlock-retained"))unlock_ok=false;
  bool retained=outcome==RISC_DEEP_SLEEP_RETAINED||outcome>=0||outcome<-7||!unlock_ok;
  assert(a->deep_sleep_for(NULL,60000)==(retained?RISC_DEEP_SLEEP_RETAINED:outcome)&&entries==1);
  if(retained){before=reads;bool key=false;assert(a->deep_sleep_for(NULL,60000)==RISC_DEEP_SLEEP_RETAINED&&!base->read_key(NULL,&key)&&!d->quiesce());assert(reads==before&&!releases&&token&&locked);return 0;}
  assert(a->deep_sleep_for(NULL,60000)==RISC_DEEP_SLEEP_ACTIVE_WAKE&&entries==1);bool key=false;assert(base->read_key(NULL,&key));now+=30;outcome=RISC_DEEP_SLEEP_BUSY;assert(a->deep_sleep_for(NULL,60000)==RISC_DEEP_SLEEP_BUSY&&entries==2);
 }
 if(!strcmp(s,"release-retry")){release_ok=false;assert(!d->quiesce()&&token);release_ok=true;}
 assert(d->quiesce()&&!token&&!locked);printf("X4 typed deep entry %s PASS\n",s);return 0;
}
