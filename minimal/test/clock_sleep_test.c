#define main fixture_main
#define risc_runtime_get_api fixture_runtime
#include "paper_clock_test.c"
#undef main
#undef risc_runtime_get_api
#include <RiscInputNavigationV1.h>
#include "../drivers/x4pro_power/X4PowerV1.h"
#include "PortableAppSleep.h"
static unsigned case_id,entries,power_grants,power_releases,key_reads,light_calls;
static bool terminal,wake_held;static uint16_t light=40;
static bool key(void*c,bool*down){(void)c;assert(!terminal&&!subs&&!frames);key_reads++;*down=case_id==3;return true;}
static int32_t sleep_key(void*c,uint32_t duration,risc_light_sleep_result_v1*out){
 (void)c;assert(!terminal&&!subs&&!frames&&light==0);entries++;
 assert(duration==(case_id==4?5000u:0));
 if(case_id==2){terminal=true;return RISC_LIGHT_SLEEP_RETAINED;}
 wake_held=true;out->wake_cause=RISC_LIGHT_SLEEP_WAKE_GPIO;
 return case_id==1?RISC_LIGHT_SLEEP_BUSY:RISC_LIGHT_SLEEP_OK;
}
static const x4_power_v1 power={1,sizeof(power),NULL,key,sleep_key};
static bool nav_poll(void*c,risc_input_navigation_frame_v1*out){(void)c;assert(!terminal);*out=(risc_input_navigation_frame_v1){0};if(polls==6
#ifdef TEST_QUICK
 || polls==20
#endif
)out->pressed=out->released=RISC_NAV_HOME;return true;}
static bool nav_foreground(void*c,const risc_input_foreground_v1*f,size_t n){(void)c;(void)f;(void)n;assert(!terminal);return true;}
static bool nav_reset(void*c){(void)c;assert(!terminal);wake_held=false;return true;}
static const risc_input_navigation_api_v1 nav={1,sizeof(nav),NULL,nav_poll,nav_foreground,nav_reset};
static bool brightness(void*c,uint16_t level,uint16_t maximum){(void)c;assert(!terminal&&!subs&&!frames&&maximum==100);light_calls++;light=level;return true;}
static int32_t prepare(void*c,alarm_sleep_v1*out){(void)c;assert(!terminal&&!subs&&!frames&&light==0);out->rtc_seconds=100;out->deadline=case_id==4?105:0;return case_id==5?ALARM_BUSY:ALARM_OK;}
#ifdef TEST_QUICK
static int32_t preferences(void*c,const char*k,void*out,uint32_t cap,uint32_t*len){if(!strcmp(k,"time_format"))return get(c,k,out,cap,len);*len=0;return RISC_KEY_VALUE_NOT_FOUND;}
static bool controls_touch(void*c,risc_touch_snapshot_v1*s){(void)c;memset(s,0,sizeof(*s));s->width=480;s->height=800;if(polls==2||polls==3){s->contact_count=1;s->contacts[0]=(risc_touch_contact_v1){.id=1,.x=200,.y=polls==2?20:100};}return true;}
#endif
static bool obtain(const char*n,uint32_t v,uint64_t id,risc_runtime_capability_v1*g){
 assert(!terminal);
#ifdef TEST_QUICK
 if(!strcmp(n,"storage.key-value")){static risc_key_value_v1 prefs;prefs=kv;prefs.get=preferences;g->api=&prefs;grants++;return true;}
 if(!strcmp(n,"input.touch.raw")){static risc_touch_api_v1 touch;touch=t;touch.snapshot=controls_touch;g->api=&touch;grants++;return true;}
#endif
 if(!strcmp(n,X4_POWER_CAPABILITY)){assert(v==1&&id==17);power_grants++;if(case_id==6)return false;g->api=&power;grants++;return true;}
 if(!strcmp(n,"input.navigation")){g->api=&nav;grants++;return true;}
 if(!strcmp(n,"display.output")){static risc_display_output_api_v1 display;display=d;display.set_brightness=brightness;g->api=&display;grants++;return true;}
 if(!strcmp(n,ALARM_SERVICE_CAPABILITY)){static alarm_service_v1 service;service=alarm_api;service.prepare_sleep=prepare;g->api=&service;grants++;return true;}
 return acquire(n,v,id,g);
}
static bool drop(risc_runtime_capability_v1*g){assert(!terminal);if(g->api==&power)power_releases++;return release(g);}
static bool healthy(risc_runtime_health_v1*h){assert(!terminal);return health(h);}
static void wait_ms(uint32_t ms_wait){assert(!terminal);yield_ms(ms_wait);}
static const risc_runtime_api_v1 runtime={1,sizeof(runtime),healthy,wait_ms,diagnostic,launch_app,obtain,drop};
const risc_runtime_api_v1*risc_runtime_get_api(uint32_t v){return v==1?&runtime:NULL;}
int main(int argc,char**argv){
 assert(argc==2);case_id=(unsigned)atoi(argv[1]);scenario=0;
 assert(app_module_init()==0);app_main();
 assert(power_grants==1&&!launches&&!frames);
 if(case_id==2){assert(terminal&&entries==1&&portable_app_sleep_retained()&&grants&&light==0&&!subs&&!power_releases);unsigned old=grants;app_module_fini();assert(grants==old);return 0;}
 app_module_fini();assert(!grants&&!subs&&!frames&&!wake_held&&light==40);
 assert(entries==((case_id==3||case_id==5||case_id==6)?0u:1u));
 assert(power_releases==(case_id==6?0u:1u));
 assert(light_calls==((case_id==3||case_id==6)?0u:2u));
 assert(presents==
#ifdef TEST_QUICK
 3
#else
 1
#endif
 );printf("Real Clock X4 sleep %u PASS\n",case_id);return 0;
}
