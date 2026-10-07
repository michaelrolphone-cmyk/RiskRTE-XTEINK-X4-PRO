#include <RiscInputNavigationV1.h>
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include <RiscPlatformClockV1.h>
#include "../drivers/x4pro_power/X4PowerV1.h"
#include "../drivers/x4pro_board_power/PowerReadyV1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern const risc_driver_v2 *power_get(uint32_t);
static bool locked[2],pressed[3];static uint64_t live[3],now=100;static unsigned claimed[2],entries;
static unsigned pin_index(uint8_t p){assert(p==0||p==7||p==3);return p==0?0:p==7?1:2;}
static bool owner(void*c){(void)c;return true;}
static bool create(void*c,uint64_t*t){*t=(uintptr_t)c+1;return true;}
static bool take(void*c,uint64_t t){unsigned i=(unsigned)(uintptr_t)c;assert(t==i+1);if(locked[i])return false;locked[i]=true;return true;}
static bool unlock(void*c,uint64_t t){unsigned i=(unsigned)(uintptr_t)c;assert(t==i+1&&locked[i]);locked[i]=false;return true;}
static bool destroy(void*c,uint64_t t){assert(t==(uintptr_t)c+1&&!locked[(uintptr_t)c]);return true;}
static uint64_t ticks(void*c){(void)c;return now;}
static bool claim(void*c,uint8_t p,bool output,bool initial,bool up,uint64_t*t){unsigned i=pin_index(p),scope=(unsigned)(uintptr_t)c;assert(locked[scope]&&!output&&!initial&&up&&!live[i]);assert((scope==1)==(p==3));*t=live[i]=p+10;claimed[scope]++;return true;}
static bool read_pin(void*c,uint64_t t,bool*h){unsigned i=pin_index((uint8_t)(t-10));assert(locked[(uintptr_t)c]&&live[i]==t);*h=!pressed[i];return true;}
static bool release_pin(void*c,uint64_t t){unsigned i=pin_index((uint8_t)(t-10));assert(locked[(uintptr_t)c]&&live[i]==t);live[i]=0;return true;}
static int32_t sleep_pin(void*c,uint64_t t,bool high,risc_light_sleep_result_v1*r){assert((uintptr_t)c==1&&t==live[2]&&!high&&!pressed[2]);entries++;pressed[2]=true;r->wake_cause=1;return 0;}
static int32_t timed(void*c,uint64_t t,bool h,uint32_t ms,risc_light_sleep_result_v1*r){(void)ms;return sleep_pin(c,t,h,r);}
static risc_input_navigation_frame_v1 poll(const risc_input_navigation_api_v1*n){risc_input_navigation_frame_v1 f={0};now+=10;assert(n->poll(NULL,&f));return f;}
static bool ready(void*c){(void)c;return true;}
int main(void){
 const x4_power_ready_api_v1 board={1,sizeof(board),NULL,ready};
 garden_gpio_v1 gpio[2];risc_provider_sync_api_v1 sync[2];
 for(unsigned i=0;i<2;i++){gpio[i]=(garden_gpio_v1){.api_version=1,.struct_size=sizeof(gpio[i]),.context=(void*)(uintptr_t)i,.claim=claim,.read=read_pin,.release=release_pin,.light_sleep=sleep_pin,.light_sleep_for=timed};sync[i]=(risc_provider_sync_api_v1){1,sizeof(sync[i]),(void*)(uintptr_t)i,owner,create,take,unlock,destroy};}
 risc_platform_clock_api_v1 clock_api={1,sizeof(clock_api),NULL,ticks,NULL};
 risc_hw_gpio_bank_v1 cfg={.struct_size=sizeof(cfg),.count=1,.pull_up=1,.pins={3}};
 risc_hardware_device_v1 hw={1,sizeof(hw),17,"xteink,x4-pro-power-key","unspecified","gpio.bank",1,sizeof(cfg),&cfg};
 risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hw},{"platform.gpio",1,&gpio[1]},{"platform.sync",1,&sync[1]},{"platform.clock",1,&clock_api},{"board.power.ready",1,&board}};
 const risc_driver_v2*p=power_get(2);assert(p->start(deps,5));const x4_power_v1*power=p->capability;
 cfg.count=2;cfg.pins[0]=0;cfg.pins[1]=7;cfg.long_press_us=1000000;hw.instance_id=6;hw.compatible="xteink,x4-pro-buttons";deps[1].api=&gpio[0];deps[2].api=&sync[0];deps[4]=(risc_provider_dependency_v1){X4_POWER_CAPABILITY,1,power};
 const risc_driver_v2*d=t5_driver_get(2);assert(!d->start(deps,4));assert(d->start(deps,5));
 const risc_input_navigation_api_v1*n=d->capability;assert(n->struct_size==sizeof(risc_input_navigation_traits_v1));assert(claimed[0]==2&&claimed[1]==1);
 for(unsigned i=0;i<4;i++)assert(!poll(n).pressed);
 pressed[2]=true;for(unsigned i=0;i<5;i++)assert(!poll(n).pressed);
 pressed[2]=false;unsigned homes=0;for(unsigned i=0;i<5;i++)homes+=!!(poll(n).pressed&RISC_NAV_HOME);assert(homes==1);
 now+=30;risc_light_sleep_result_v1 r={.struct_size=sizeof(r)};assert(power->light_sleep(NULL,0,&r)==0&&entries==1);
 assert(n->reset(NULL));for(unsigned i=0;i<10;i++)assert(!poll(n).pressed);
 pressed[2]=false;for(unsigned i=0;i<6;i++)assert(!poll(n).pressed);
 pressed[0]=true;unsigned left=0;for(unsigned i=0;i<5;i++)left+=!!(poll(n).pressed&RISC_NAV_LEFT);assert(left==1);
 assert(d->quiesce()&&live[2]&&!live[0]&&!live[1]);assert(p->quiesce()&&!live[2]);puts("Split power/navigation ownership, short crown, wake suppression and page keys PASS");
}
