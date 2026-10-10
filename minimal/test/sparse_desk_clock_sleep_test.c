/* Actual X4 client with deterministic shared-loop and provider/Runtime fixtures.
 * These are not real providers or hardware qualification. All I/O is poisoned
 * after ambiguous custody. Storage/yield/release are forbidden under holds. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "PortableDeskClockApp.h"
#include "PortableDeskClockFaces.h"
#include "PortableAppSleep.h"
#include "PortableSleepPolicy.h"
#include "../drivers/x4pro_power/X4PowerDeepV1.h"
#include <RiscDisplayOutputPowerV1.h>
#include <RiscRetainedWakeV1.h>
static const char *scenario;
static bool terminal,pending,panel_off,dark,boot,cancelled,frame,foreground;
static unsigned slots[3];
static unsigned obtains,grants,releases,keys,paints,prepares,steps,stages,clears,resumes,entries,notices,brightnesses;
static unsigned missing,release_fail,alarm_pending,cancel_phase;
static int panel_rc,stage_rc,clear_rc,resume_rc,native_rc,read_rc,alarm_rc,step_rc,status_rc;
static uint32_t uptime,deadline,duration,stage_cost,panel_cost,alarm_cost,entry_cost;
static struct {uint32_t size;uint8_t payload[PORTABLE_DESK_CLOCK_RECORD_BYTES];} staged;
static jmp_buf entry;
static risc_display_output_api_v1_power display,other_display;
static x4_power_deep_v1 power;
#ifdef PORTABLE_DESK_POINTS_SNAPSHOT
static risc_retained_wake_api_v1_extended extended_wake;
#define wake extended_wake.base
#else
static risc_retained_wake_api_v1 wake;
#endif
static alarm_service_v1 alarms;
static risc_key_value_v1 prefs;
static const risc_runtime_api_v1 runtime;
static bool is(const char *s){return !strcmp(scenario,s);}
static void io(void){assert(!terminal);}
static void safe(void){io();assert(!panel_off&&!frame);}
static void point(unsigned n){io();if(cancel_phase==n)cancelled=true;}
static portable_desk_record record(void){
 portable_desk_record r={0};r.config.rtc_reference_epoch=1700000000;strcpy(r.config.time_zone,"UTC0");r.displayed_minute=120;r.has_image=true;r.refresh_modulo=4;
#ifdef PORTABLE_DESK_POINTS_SNAPSHOT
 r.config.face=PORTABLE_DESK_POINTS;r.config.points.status=PORTABLE_DESK_POINTS_READY;
 portable_points_catalog_view *v=&r.config.points.view;
 *v=(portable_points_catalog_view){.struct_size=sizeof(*v),.catalog_revision=12,.snapshot=4,.seconds=100,.valid_until=500,.count=4};
 for(unsigned i=0;i<4;++i){v->next[i]=(portable_points_catalog_event){.event_id=100+i,.type_id=50+i,.revision=12,.deadline=200+i*50,.parent_day=1,.symbol=i+1};strcpy(v->next[i].label,"Full thirty-one byte label test");}
#endif
 return r;
}
static bool health(risc_runtime_health_v1 *h){io();if(is("health-initial") || (is("health-stage")&&stages))return false;h->uptime_ms=uptime;return true;}
static void yield_ms(uint32_t ms){safe();uptime+=ms;if(is("clock-backwards"))uptime-=ms*2;}
static bool key(void*c,bool*down){(void)c;io();keys++;if(is("key-failure") || (is("key-paint")&&frame) || (is("key-prepared")&&panel_off)){terminal=true;return false;}*down=is("held")||cancelled;return true;}
static int32_t light(void*c,uint32_t n,risc_light_sleep_result_v1*out){(void)c;(void)n;(void)out;safe();assert(foreground&&dark);return RISC_LIGHT_SLEEP_BUSY;}
static int32_t deep(void*c,uint32_t ms){(void)c;io();assert(panel_off&&dark&&pending&&!frame&&grants==3);assert(staged.size==PORTABLE_DESK_CLOCK_RECORD_BYTES);entries++;duration=ms;
 if(is("terminal")){terminal=true;longjmp(entry,1);}
 if(native_rc==RISC_DEEP_SLEEP_RETAINED || native_rc>=0 || native_rc<RISC_DEEP_SLEEP_UNSUPPORTED)terminal=true;
 return native_rc;
}
static bool brightness(void*c,uint16_t level,uint16_t max){(void)c;safe();assert(max==100);if(level){assert(foreground);dark=false;brightnesses++;return true;}brightnesses++;dark=true;point(brightnesses==1?2:5);if((is("dark-failed")&&brightnesses==1) || (is("brightness-rollback")&&brightnesses==2)){if(brightnesses==2)terminal=true;return false;}return true;}
#ifdef PORTABLE_QUICK_ACTIONS
unsigned portable_quick_brightness(void){io();assert(foreground);return 73;}
#endif
static bool seed(void*c,risc_display_frame_v1 f){(void)c;(void)f;assert(0);return false;}
static int32_t panel_prepare(void*c,uint32_t ms){(void)c;safe();assert(dark&&pending&&ms==RISC_DISPLAY_POWER_MAX_BUDGET_MS);panel_off=true;uptime+=panel_cost;point(3);if(panel_rc==RISC_DISPLAY_POWER_RETAINED || panel_rc>0 || panel_rc<RISC_DISPLAY_POWER_PLATFORM)terminal=true;return panel_rc;}
static int32_t panel_resume(void*c,uint32_t ms){(void)c;io();assert(panel_off&&ms==RISC_DISPLAY_POWER_MAX_BUDGET_MS);resumes++;if(resume_rc!=RISC_DISPLAY_POWER_OK){terminal=true;return resume_rc;}panel_off=false;point(4);return RISC_DISPLAY_POWER_OK;}
static int32_t read_bytes(void*c,uint32_t type,uint32_t schema,void*payload,uint32_t capacity,uint32_t*size,uint32_t*cause){
 (void)c;safe();assert(boot&&grants==1&&type==PORTABLE_DESK_CLOCK_RECORD_TYPE&&schema==PORTABLE_DESK_CLOCK_RECORD_SCHEMA);
 *cause=is("boot-cold")?RISC_BOOT_POWER_ON:is("boot-reset")?RISC_BOOT_RESET:is("boot-other")?RISC_BOOT_DEEP_OTHER:is("boot-gpio")?RISC_BOOT_DEEP_GPIO:RISC_BOOT_DEEP_TIMER;
 if(read_rc!=RISC_RETAINED_WAKE_OK){if(read_rc!=RISC_RETAINED_WAKE_ABSENT&&read_rc!=RISC_RETAINED_WAKE_MISMATCH&&read_rc!=RISC_RETAINED_WAKE_INVALID)terminal=true;return read_rc;}
 if(capacity<PORTABLE_DESK_CLOCK_RECORD_BYTES)return RISC_RETAINED_WAKE_MISMATCH;
 portable_desk_record r=record();*size=PORTABLE_DESK_CLOCK_RECORD_BYTES;assert(portable_desk_encode(&r,payload,*size));if(is("boot-corrupt"))((uint8_t*)payload)[0]=0;return RISC_RETAINED_WAKE_OK;
}
static int32_t read_wake(void*c,uint32_t type,uint32_t schema,risc_retained_wake_record_v1*out,uint32_t*cause){
 uint32_t size=0;int32_t result=read_bytes(c,type,schema,out->payload,sizeof(out->payload),&size,cause);if(result==0){out->type=type;out->schema_version=schema;out->size=size;}return result;
}
static int32_t stage_bytes(void*c,uint32_t type,uint32_t schema,const void*payload,uint32_t size){
 (void)c;safe();assert(grants==3);portable_desk_record decoded;assert(type==PORTABLE_DESK_CLOCK_RECORD_TYPE&&schema==PORTABLE_DESK_CLOCK_RECORD_SCHEMA&&size==sizeof(staged.payload)&&portable_desk_decode(payload,size,&decoded));assert(decoded.displayed_minute==120);staged.size=size;memcpy(staged.payload,payload,size);pending=true;stages++;uptime+=stage_cost;point(1);if(stage_rc!=RISC_RETAINED_WAKE_OK&&stage_rc!=RISC_RETAINED_WAKE_INVALID)terminal=true;return stage_rc;
}
static int32_t stage(void*c,const risc_retained_wake_record_v1*value){return stage_bytes(c,value->type,value->schema_version,value->payload,value->size);}

static int32_t clear(void*c){(void)c;safe();assert(pending);clears++;point(6);if(clear_rc!=RISC_RETAINED_WAKE_OK){terminal=true;return clear_rc;}pending=false;return RISC_RETAINED_WAKE_OK;}
static int32_t alarm_prepare(void*c,alarm_sleep_v1*out){(void)c;safe();prepares++;point(7);out->rtc_seconds=100;out->deadline=deadline;uptime+=alarm_cost;if(alarm_pending){alarm_pending--;return ALARM_PENDING;}if(alarm_rc==ALARM_OUTPUT)terminal=true;return alarm_rc;}
static int32_t alarm_step(void*c){(void)c;safe();steps++;if(step_rc==ALARM_OUTPUT)terminal=true;return step_rc;}
static int32_t alarm_status(void*c,alarm_status_v1*out){(void)c;safe();out->state=ALARM_STATE_READY;if(is("alarm-uncertain")){out->output_uncertain=1;terminal=true;}if(status_rc==ALARM_OUTPUT)terminal=true;return status_rc;}
static bool acquire(const char *name,uint32_t version,uint64_t instance,risc_runtime_capability_v1 *g){safe();assert(version==1&&g->struct_size==sizeof(*g));obtains++;const void *api=NULL;
 if(foreground&&obtains==1){assert(!strcmp(name,"storage.key-value")&&instance==PORTABLE_SLEEP_STORE_INSTANCE);api=&prefs;}
 else if(foreground&&obtains==2){assert(!strcmp(name,X4_POWER_CAPABILITY)&&instance==17);api=&power;}
 else if(boot){assert(obtains==1&&!strcmp(name,RISC_RETAINED_WAKE_CAPABILITY)&&!instance);api=&wake;}
 else if(obtains==1){assert(!strcmp(name,X4_POWER_CAPABILITY)&&instance==17);api=&power;}
 else if(obtains==2){assert(!strcmp(name,RISC_DISPLAY_OUTPUT_CAPABILITY)&&instance==3);api=is("display-identity")?&other_display:&display;}
 else if(obtains==3){assert(!strcmp(name,RISC_RETAINED_WAKE_CAPABILITY)&&!instance);api=&wake;}
 else assert(0); /* KV, touch, SD, Wi-Fi, HCI, navigation, battery forbidden. */
 if(missing==obtains){terminal=true;if(is("acquire-output")){g->api=api;g->slot=obtains;slots[grants++]=obtains;}else assert(!g->api);return false;}
 g->api=api;g->slot=obtains;slots[grants++]=obtains;return true;
}
static bool release_grant(risc_runtime_capability_v1 *g){safe();assert(grants&&g->slot==slots[grants-1]);releases++;if(g->slot==release_fail){terminal=true;return false;}grants--;g->api=NULL;return true;}
static const risc_runtime_api_v1 runtime={.api_version=1,.struct_size=sizeof(runtime),.health=health,.yield_ms=yield_ms,.acquire=acquire,.release=release_grant};
bool portable_desk_adapter_timer_only(void){io();return !foreground;}
void portable_desk_clock_refused(void){safe();notices++;}
void portable_desk_clock_radios_off(void){assert(0);}
int portable_desk_clock_run(const risc_runtime_api_v1 *rt,const portable_desk_sleep_ops *ops){assert(rt==&runtime&&grants==3);paints++;frame=true;int rc=ops->cancelled(ops->context);if(rc==-2)return -2;frame=false;if(rc)return 0;
 portable_desk_record r=record();unsigned rounds=is("catchup")?2:1;
 for(unsigned n=0;n<rounds;++n){rc=ops->stage(ops->context,&r);if(rc!=1)return rc;rc=ops->prepare(ops->context);if(rc!=1)return rc;if(n+1<rounds){assert(ops->resume(ops->context)==1);paints++;}}
 uint32_t sampled=uptime;uptime+=entry_cost;if(is("record-changed"))r.refresh_modulo++;
 rc=ops->stage_enter(ops->context,&r,30000,sampled);if(rc==-2)return rc;
 return ops->resume(ops->context)==1?0:-2;
}
static int32_t get(void*c,const char*k,void*out,uint32_t capacity,uint32_t*size){(void)c;(void)out;(void)capacity;safe();assert(foreground&&!strcmp(k,PORTABLE_SLEEP_KEY));*size=0;return RISC_KEY_VALUE_NOT_FOUND;}
static int32_t put(void*c,const char*k,const void*data,uint32_t size){(void)c;(void)k;(void)data;(void)size;assert(0);return RISC_KEY_VALUE_IO;}
static void setup(void){
 terminal=pending=panel_off=dark=boot=cancelled=frame=false;foreground=!strncmp(scenario,"foreground-",11);
 obtains=grants=releases=keys=paints=prepares=steps=stages=clears=resumes=entries=notices=brightnesses=0;
 missing=release_fail=alarm_pending=cancel_phase=0;
 panel_rc=stage_rc=clear_rc=resume_rc=read_rc=alarm_rc=step_rc=status_rc=0;native_rc=RISC_DEEP_SLEEP_BUSY;
 uptime=deadline=duration=stage_cost=panel_cost=alarm_cost=entry_cost=0;
 display=(risc_display_output_api_v1_power){0};display.history.base=(risc_display_output_api_v1){.api_version=1,.struct_size=sizeof(display),.set_brightness=brightness};display.history.extension_tag=RISC_DISPLAY_HISTORY_TAG;display.history.extension_version=1;display.history.seed_previous=seed;display.power_tag=RISC_DISPLAY_POWER_TAG;display.power_version=1;display.prepare=panel_prepare;display.resume=panel_resume;other_display=display;
 power=(x4_power_deep_v1){{1,sizeof(power),NULL,key,light},X4_POWER_DEEP_TAG,1,deep};wake=(risc_retained_wake_api_v1){1,sizeof(wake),NULL,read_wake,stage,clear};alarms=(alarm_service_v1){.api_version=1,.struct_size=sizeof(alarms),.status=alarm_status,.step=alarm_step,.prepare_sleep=alarm_prepare};
 #ifdef PORTABLE_DESK_POINTS_SNAPSHOT
 extended_wake.base.struct_size=sizeof(extended_wake);extended_wake.extension_tag=RISC_RETAINED_WAKE_EXTENDED_TAG;extended_wake.extension_version=1;extended_wake.max_payload_bytes=512;extended_wake.read_bytes=read_bytes;extended_wake.stage_bytes=stage_bytes;
#endif
 prefs=(risc_key_value_v1){1,sizeof(prefs),NULL,get,put};
 if(is("foreground-acquire-prefs"))missing=1;
 if(is("foreground-acquire-power"))missing=2;
 if(is("foreground-release-prefs"))release_fail=1;
 if(is("foreground-release-power"))release_fail=2;
 if(!strncmp(scenario,"acquire-",8))missing=is("acquire-output")?2:(unsigned)atoi(scenario+8);
 if(!strncmp(scenario,"release-",8))release_fail=(unsigned)atoi(scenario+8);
 if(!strncmp(scenario,"panel-",6))panel_rc=atoi(scenario+6);
 if(!strncmp(scenario,"resume-",7))resume_rc=atoi(scenario+7);
 if(!strncmp(scenario,"stage-",6))stage_rc=atoi(scenario+6);
 if(!strncmp(scenario,"clear-",6))clear_rc=atoi(scenario+6);
 if(!strncmp(scenario,"native-",7))native_rc=atoi(scenario+7);
 if(!strncmp(scenario,"cancel-",7))cancel_phase=(unsigned)atoi(scenario+7);
 if(!strncmp(scenario,"boot-",5)){boot=true;if(!strncmp(scenario,"boot-read-",10))read_rc=atoi(scenario+10);if(is("boot-acquire"))missing=1;if(is("boot-release"))release_fail=1;}
 if(is("alarm-due"))deadline=100;
 if(is("alarm-past"))deadline=99;
 if(is("alarm-near"))deadline=101;
 if(is("alarm-expired")){deadline=101;panel_cost=5;}
 if(is("alarm-future")){deadline=105;panel_cost=300;entry_cost=20;alarm_cost=7;}
 if(is("alarm-pending"))alarm_pending=2;
 if(is("alarm-stuck"))alarm_pending=100;
 if(is("alarm-busy"))alarm_rc=ALARM_BUSY;
 if(is("alarm-output"))alarm_rc=ALARM_OUTPUT;
 if(is("alarm-step-busy")){alarm_pending=1;step_rc=ALARM_BUSY;}
 if(is("alarm-step-output")){alarm_pending=1;step_rc=ALARM_OUTPUT;}
 if(is("alarm-status-output")){alarm_pending=1;status_rc=ALARM_OUTPUT;}
 if(is("alarm-status-busy")){alarm_pending=1;status_rc=ALARM_BUSY;}
 if(is("alarm-uncertain"))alarm_pending=1;
 if(is("entry-expired"))entry_cost=30000;
 if(is("entry-delay"))entry_cost=17;
 if(is("stage-delay"))stage_cost=30000;
 if(is("bad-panel"))display.power_tag=0;
 if(is("bad-panel-history"))display.history.extension_tag=0;
 if(is("short-panel"))display.history.base.struct_size=sizeof(risc_display_output_api_v1);
 if(is("null-panel"))display.resume=NULL;
 if(is("bad-power"))power.extension_tag=0;
 if(is("short-power"))power.power.struct_size=sizeof(x4_power_v1);
 if(is("null-power"))power.deep_sleep_for=NULL;
 if(is("null-key"))power.power.read_key=NULL;
 if(is("bad-wake"))wake.struct_size=sizeof(risc_retained_wake_api_v1)-1;
 if(is("null-wake"))wake.clear=NULL;
 if(is("null-alarm"))alarms.step=NULL;
}
static void check(void){
 if(is("terminal")){assert(terminal&&entries==1&&grants==3&&pending&&panel_off);return;}
 if(terminal){assert(obtains<=3);if(missing)assert(releases==(foreground&&missing==2?1u:0u)&&!stages&&obtains==missing);if(stages&&stage_rc!=RISC_RETAINED_WAKE_OK)assert(pending&&!clears&&!resumes&&!releases);if(is("key-paint"))assert(frame);return;}
 assert(!grants&&!pending&&!panel_off&&!frame&&releases==obtains);
 if(boot){assert(!stages&&!keys&&!prepares);return;}
 if(foreground){assert(obtains==2&&!paints&&!stages&&!notices&&!dark);return;}
 if(is("null-alarm")){assert(!obtains&&notices==1);return;}
 assert(obtains==3);if(!paints)assert(notices==1);
 if(is("refused")||is("repeat")||is("alarm-near")||is("alarm-future")||is("alarm-pending")||is("catchup")||is("stage-delay"))assert(entries==1);
 if(is("refused")||is("repeat"))assert(resumes==1&&clears==1&&brightnesses==2&&dark);
 if(is("alarm-near"))assert(duration==1);
 if(is("alarm-future"))assert(duration==3673);
 if(is("entry-delay"))assert(duration==29983);
 if(is("stage-delay"))assert(duration==30000);
 if(is("alarm-pending"))assert(steps==2&&prepares==3);
 if(is("alarm-stuck"))assert(steps==64&&prepares==64);
 if(is("catchup"))assert(paints==2&&resumes==2&&clears==2&&stages==2);
 if(is("held"))assert(keys==80&&!paints);
 if(is("record-changed")||is("alarm-due")||is("alarm-past")||is("alarm-expired")||is("entry-expired")||is("alarm-busy")||is("alarm-step-busy")||is("alarm-stuck"))assert(!entries);
 if(panel_rc!=RISC_DISPLAY_POWER_OK)assert(resumes==1&&!entries);
 if(cancel_phase&&cancel_phase!=4&&cancel_phase!=5&&cancel_phase!=6)assert(!entries);
}
int main(int argc,char **argv){assert(argc==2);scenario=argv[1];unsigned rounds=is("repeat")?8:1;
 for(unsigned n=0;n<rounds;++n){setup();int result;
  if(boot){portable_desk_record r={0};result=portable_desk_clock_boot_read(&runtime,&r);assert(result==(terminal?-2:is("boot-timer")?1:0));if(result==1)assert(r.has_image);assert(portable_desk_clock_boot_is_cold()==(is("boot-cold")||is("boot-reset")));assert(portable_desk_clock_boot_is_interactive()==is("boot-gpio"));}
  else if(setjmp(entry)){check();puts("terminal sparse client PASS");return 0;}
  else {result=portable_app_alarm_sleep(&runtime,&display.history.base,NULL,&alarms);assert(result==(terminal?-2:0));}
  check();
 }
 printf("%s sparse client PASS\n",scenario);return 0;
}
