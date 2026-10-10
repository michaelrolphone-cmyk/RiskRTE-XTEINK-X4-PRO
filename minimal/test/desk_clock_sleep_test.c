/* Deployment hook contract test. Real local lifecycle; the shared clock loop
 * is a deterministic driver of paint/catch-up/cancellation callback ordering. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "PortableDeskClockApp.h"
#include "PortableAppSleep.h"
#include "PortableSleepPolicy.h"
#include "WifiApi.h"
#include "PortableBluetoothControl.h"
#include "../drivers/x4pro_power/X4PowerDeepV1.h"
#include <RiscDisplayOutputPowerV1.h>
#include <RiscTouchPowerV1.h>
#include <RiscStorageVolumeV1.h>
#include <RiscRetainedWakeV1.h>

static const char *scenario;
static bool terminal,pending,dark,panel_off,touch_off,storage_off,boot,frame_held;
static unsigned grants,obtains,releases,keys,light_calls,deep_calls,paints,prepares,steps;
static unsigned stage_calls,clear_calls,panel_resumes,touch_resumes,storage_resumes,notices;
static uint32_t uptime,duration,stage_cost,prep_cost,entry_cost;
static int fail_op,fail_rc,cancel_op,phase;
static unsigned mode=PORTABLE_SLEEP_DEEP,alarm_pending;
static uint32_t deadline,boot_cause=RISC_BOOT_DEEP_TIMER;
static jmp_buf entered;
static risc_display_output_api_v1_power display;
static risc_touch_power_api_v1 touch;
static risc_storage_volume_api_v1_sleep storage;
static x4_power_deep_v1 power;
static risc_retained_wake_api_v1 wake;
static risc_key_value_v1 prefs;
static wifi_api_v1 wifi;
static portable_bluetooth_control_v1 bluetooth;
static bool wifi_off,bluetooth_off,cancel_key;
static const risc_runtime_api_v1 runtime;
enum { BRIGHT_OFF=1, TOUCH_PREP, PANEL_PREP, STORAGE_PREP, STORAGE_COMMIT,
       STORAGE_RESUME, PANEL_RESUME, TOUCH_RESUME, BRIGHT_ON, STAGE, CLEAR,
       KEY, RELEASE, ALARM_PREP, ALARM_STEP, ALARM_STATUS, HEALTH, WIFI_OFF, WIFI_STATUS, BT_OFF, BT_STATUS };
static bool is(const char *s){return !strcmp(scenario,s);}
static void io(void){assert(!terminal);}
static int operation(int op,int ok) {
    io();phase=op;if(cancel_op==op)cancel_key=true;
    if(fail_op!=op)return ok;
    if(op==KEY || op==RELEASE || op==CLEAR || op==BRIGHT_ON || op==WIFI_OFF || op==WIFI_STATUS || op==BT_OFF || op==BT_STATUS ||
       (op==STORAGE_RESUME && fail_rc!=RISC_STORAGE_SLEEP_MEDIA_UNAVAILABLE) || op==PANEL_RESUME || op==TOUCH_RESUME ||
       (op==TOUCH_PREP && (fail_rc==RISC_TOUCH_POWER_RETAINED || fail_rc>0 || fail_rc<RISC_TOUCH_POWER_INVALID)) ||
       (op==PANEL_PREP && (fail_rc==RISC_DISPLAY_POWER_RETAINED || fail_rc>0 || fail_rc<RISC_DISPLAY_POWER_PLATFORM)) ||
       ((op==ALARM_PREP || op==ALARM_STEP || op==ALARM_STATUS) && fail_rc==ALARM_OUTPUT))terminal=true;
    return fail_rc;
}
static bool health(risc_runtime_health_v1 *h){io();if(fail_op==HEALTH && (is("health-initial") || (is("health-stage") && stage_calls))){return false;}h->uptime_ms=uptime;return true;}
static void yield_ms(uint32_t ms){io();assert(!panel_off&&!touch_off&&!storage_off);uptime+=ms;if(is("clock-backwards"))uptime-=2*ms;}
static bool key(void *c,bool *down){(void)c;io();keys++;if(fail_op==KEY){(void)operation(KEY,1);return false;}*down=is("held") || (cancel_key || (cancel_op && phase==cancel_op));return true;}
static int32_t light_sleep(void *c,uint32_t ms,risc_light_sleep_result_v1 *out){(void)c;(void)ms;(void)out;io();assert(dark);light_calls++;return RISC_LIGHT_SLEEP_BUSY;}
static int32_t deep_sleep(void *c,uint32_t ms){(void)c;io();assert(dark&&panel_off&&touch_off&&storage_off&&pending&&grants==7&&wifi_off&&bluetooth_off);deep_calls++;duration=ms;
 if(is("terminal")){terminal=true;longjmp(entered,1);}
 if(is("deep-retained") || is("deep-zero") || is("deep-positive") || is("deep-unknown")){terminal=true;return is("deep-retained")?RISC_DEEP_SLEEP_RETAINED:is("deep-zero")?0:is("deep-positive")?1:-99;}
 if(!strncmp(scenario,"deep-code-",10))return -atoi(scenario+10);
 return RISC_DEEP_SLEEP_BUSY;
}
static bool brightness(void *c,uint16_t level,uint16_t max){(void)c;assert(max==100);if(level){
#ifdef PORTABLE_QUICK_ACTIONS
 assert(level==73);
#else
 assert(level==40);
#endif
if(!operation(BRIGHT_ON,1)){return false;}dark=false;return true;}
 dark=true;return operation(BRIGHT_OFF,1)!=0;
}
#ifdef PORTABLE_QUICK_ACTIONS
unsigned portable_quick_brightness(void){io();return 73;}
#endif
static bool seed(void *c,risc_display_frame_v1 f){(void)c;(void)f;assert(0);return false;}
static int32_t panel_prepare(void *c,uint32_t ms){(void)c;assert(ms==RISC_DISPLAY_POWER_MAX_BUDGET_MS&&dark&&touch_off);panel_off=true;return operation(PANEL_PREP,0);}
static int32_t panel_resume(void *c,uint32_t ms){(void)c;(void)ms;panel_resumes++;int rc=operation(PANEL_RESUME,0);if(!rc)panel_off=false;return rc;}
static int32_t touch_prepare(void *c,uint32_t ms){(void)c;assert(ms==RISC_TOUCH_POWER_MAX_BUDGET_MS&&dark);touch_off=true;return operation(TOUCH_PREP,0);}
static int32_t touch_resume(void *c,uint32_t ms){(void)c;(void)ms;touch_resumes++;int rc=operation(TOUCH_RESUME,0);if(!rc)touch_off=false;return rc;}
static uint64_t subscribe(void *c){(void)c;assert(0);return 0;}
static bool unsubscribe(void*c,uint64_t token){(void)c;(void)token;assert(0);return false;}
static bool poll_touch(void*c,size_t n){(void)c;(void)n;assert(0);return false;}
static int32_t next_touch(void*c,uint64_t token,risc_touch_event_v1 *e){(void)c;(void)token;(void)e;assert(0);return 0;}
static bool snapshot(void*c,risc_touch_snapshot_v1*s){(void)c;(void)s;assert(0);return false;}
static bool wifi_disconnect(void*c){(void)c;wifi_off=true;return operation(WIFI_OFF,1)!=0;}
static wifi_link_t wifi_status(void*c){(void)c;return (wifi_link_t)operation(WIFI_STATUS,WIFI_LINK_DOWN);}
static bool bluetooth_enable(void*c,bool enabled){(void)c;assert(!enabled);bluetooth_off=true;return operation(BT_OFF,1)!=0;}
static bool bluetooth_status(void*c,uint8_t*out){(void)c;io();if(is("bt-status-failure")){terminal=true;return false;}*out=(uint8_t)operation(BT_STATUS,PORTABLE_BLUETOOTH_OFF);return true;}
static bool legacy(void*c){(void)c;assert(0);return false;}
static bool storage_prepare(void*c){(void)c;assert(dark&&panel_off&&touch_off);return operation(STORAGE_PREP,1)!=0;}
static bool storage_commit(void*c){(void)c;storage_off=true;uptime+=prep_cost;return operation(STORAGE_COMMIT,1)!=0;}
static int32_t storage_resume(void*c){(void)c;storage_resumes++;int rc=operation(STORAGE_RESUME,RISC_STORAGE_SLEEP_READY);if(rc==RISC_STORAGE_SLEEP_READY || rc==RISC_STORAGE_SLEEP_MEDIA_UNAVAILABLE)storage_off=false;return rc;}
static portable_desk_record record(void){portable_desk_record r={0};r.config.rtc_reference_epoch=1700000000;strcpy(r.config.time_zone,"UTC0");r.displayed_minute=120;r.has_image=true;r.refresh_modulo=4;return r;}
static int32_t read_wake(void*c,uint32_t type,uint32_t schema,risc_retained_wake_record_v1*out,uint32_t*cause){(void)c;io();assert(boot&&grants==1&&type==PORTABLE_DESK_CLOCK_RECORD_TYPE&&schema==1);*cause=boot_cause;
 if(is("boot-absent"))return RISC_RETAINED_WAKE_ABSENT;
 if(is("boot-mismatch"))return RISC_RETAINED_WAKE_MISMATCH;
 *out=(risc_retained_wake_record_v1){.struct_size=sizeof(*out),.type=type,.schema_version=schema,.size=PORTABLE_DESK_CLOCK_RECORD_BYTES};
 portable_desk_record r=record();assert(portable_desk_encode(&r,out->payload,out->size));
 if(is("boot-corrupt"))out->payload[0]=0;
 if(is("boot-type"))out->type++;
 if(is("boot-schema"))out->schema_version++;
 if(is("boot-size"))out->size++;
 return RISC_RETAINED_WAKE_OK;
}
static int32_t stage(void*c,const risc_retained_wake_record_v1*r){(void)c;assert(grants==7&&!dark&&!panel_off&&!touch_off&&!storage_off);portable_desk_record decoded;assert(r->type==PORTABLE_DESK_CLOCK_RECORD_TYPE&&r->schema_version==1&&portable_desk_decode(r->payload,r->size,&decoded));assert(decoded.displayed_minute==120);pending=true;stage_calls++;uptime+=stage_cost;return operation(STAGE,RISC_RETAINED_WAKE_OK);}
static int32_t clear(void*c){(void)c;assert(!panel_off&&!touch_off&&!storage_off&&!dark);clear_calls++;int rc=operation(CLEAR,RISC_RETAINED_WAKE_OK);if(!rc)pending=false;return rc;}
static int32_t get(void*c,const char*k,void*b,uint32_t cap,uint32_t*len){(void)c;io();assert(!panel_off&&!touch_off&&!storage_off);assert(!strcmp(k,PORTABLE_SLEEP_KEY)&&cap==4);if(is("settings-missing")){*len=0;return RISC_KEY_VALUE_NOT_FOUND;}if(is("settings-io"))return RISC_KEY_VALUE_IO;
 uint8_t value[]={0x53,1,(uint8_t)mode,(uint8_t)(mode^0xa5)};if(is("settings-corrupt"))value[0]=0;memcpy(b,value,4);*len=4;return 0;}
static int32_t put(void*c,const char*k,const void*v,uint32_t n){(void)c;(void)k;(void)v;(void)n;assert(0);return -1;}
static int32_t alarm_prepare(void*c,alarm_sleep_v1*out){(void)c;assert(!panel_off&&!touch_off&&!storage_off);prepares++;out->rtc_seconds=100;out->deadline=deadline;if(alarm_pending){alarm_pending--;return operation(ALARM_PREP,ALARM_PENDING);}return operation(ALARM_PREP,ALARM_OK);}
static int32_t alarm_step(void*c){(void)c;assert(!storage_off&&!touch_off&&!panel_off);steps++;return operation(ALARM_STEP,ALARM_OK);}
static int32_t alarm_status(void*c,alarm_status_v1*out){(void)c;assert(!storage_off&&!touch_off&&!panel_off);out->state=ALARM_STATE_READY;if(is("alarm-uncertain")){out->output_uncertain=1;terminal=true;return ALARM_OK;}return operation(ALARM_STATUS,ALARM_OK);}
static alarm_service_v1 alarms={.api_version=1,.struct_size=sizeof(alarms),.status=alarm_status,.step=alarm_step,.prepare_sleep=alarm_prepare};
static bool acquire(const char *name,uint32_t ver,uint64_t instance,risc_runtime_capability_v1 *g){io();assert(!panel_off&&!touch_off&&!storage_off);assert(ver==1);obtains++;const void *api=NULL;
 if(!strcmp(name,"storage.key-value")){assert(instance==1);api=&prefs;}
 else if(!strcmp(name,X4_POWER_CAPABILITY)){assert(instance==17);api=&power;}
 else if(!strcmp(name,RISC_DISPLAY_OUTPUT_CAPABILITY)){assert(!instance);api=&display;}
 else if(!strcmp(name,"input.touch.raw")){assert(!instance);api=&touch;}
 else if(!strcmp(name,"storage.volume")){assert(!instance);api=&storage;}
 else if(!strcmp(name,RISC_RETAINED_WAKE_CAPABILITY)){assert(!instance);api=&wake;}
 else if(!strcmp(name,"net.wifi")){assert(instance==15);api=&wifi;}
 else if(!strcmp(name,"bluetooth.hci")){assert(instance==16);api=&bluetooth;}
 else assert(0);
 if(!strncmp(scenario,"missing-",8) && obtains==(unsigned)atoi(scenario+8))return false;
 if(is("boot-missing"))return false;
 assert(g->struct_size==sizeof(*g));g->api=api;g->slot=obtains;grants++;return true;
}
static bool release_grant(risc_runtime_capability_v1 *g){io();assert(!panel_off&&!touch_off&&!storage_off);assert(grants&&g->api);releases++;if((!strncmp(scenario,"release-slot-",13) && g->slot==(unsigned)atoi(scenario+13)) || (fail_op==RELEASE && (is("release-prefs") || boot || g->api==&wake))){fail_op=RELEASE;(void)operation(RELEASE,0);return false;}grants--;g->api=NULL;return true;}
static const risc_runtime_api_v1 runtime={.api_version=1,.struct_size=sizeof(runtime),.health=health,.yield_ms=yield_ms,.acquire=acquire,.release=release_grant};
void portable_desk_clock_refused(void){io();notices++;}
void portable_desk_clock_radios_off(void){io();assert(wifi_off&&bluetooth_off);}
int portable_desk_clock_run(const risc_runtime_api_v1 *rt,const portable_desk_sleep_ops *ops){assert(rt==&runtime);assert(grants==7&&!panel_off&&!storage_off&&!touch_off);paints++;
 phase=99;frame_held=true;if(is("key-paint"))fail_op=KEY;
 int cancelled=ops->cancelled(ops->context);
 if(cancelled==-2){assert(terminal);return -2;}
 frame_held=false;if(cancelled)return 0;
 portable_desk_record r=record();
 int ready=ops->stage(ops->context,&r);if(ready!=1)return ready;
 ready=ops->prepare(ops->context);if(ready!=1)return ready;
 phase=98;if(is("key-after-prepare"))fail_op=KEY;
 cancelled=ops->cancelled(ops->context);
 if(cancelled==-2){assert(terminal);return -2;}
 if(cancelled)return ops->resume(ops->context)==1?0:-2;
 if(is("catchup")){assert(ops->resume(ops->context)==1);paints++;ready=ops->stage(ops->context,&r);if(ready!=1)return ready;ready=ops->prepare(ops->context);if(ready!=1)return ready;}
 uint32_t sampled=uptime;uptime+=entry_cost;if(is("record-changed"))r.refresh_modulo++;
 int result=ops->stage_enter(ops->context,&r,30000,sampled);
 if(result==-2)return -2;
 return ops->resume(ops->context)==1?0:-2;
}
static void setup(void){
 display.history.base=(risc_display_output_api_v1){.api_version=1,.struct_size=sizeof(display),.set_brightness=brightness};display.history.extension_tag=RISC_DISPLAY_HISTORY_TAG;display.history.extension_version=1;display.history.seed_previous=seed;display.power_tag=RISC_DISPLAY_POWER_TAG;display.power_version=1;display.prepare=panel_prepare;display.resume=panel_resume;
 touch.base=(risc_touch_api_v1){1,sizeof(touch),NULL,subscribe,unsubscribe,poll_touch,next_touch,snapshot};touch.power_tag=RISC_TOUCH_POWER_TAG;touch.power_version=1;touch.prepare=touch_prepare;touch.resume=touch_resume;
 storage.terminal.power.volume.base=(risc_storage_volume_api_v1){.api_version=1,.struct_size=sizeof(storage)};storage.terminal.extension_tag=RISC_STORAGE_POWER_COMMIT_TAG;storage.terminal.extension_version=1;storage.terminal.commit_power_down=legacy;storage.sleep_tag=RISC_STORAGE_SLEEP_TAG;storage.sleep_version=1;storage.prepare_sleep=storage_prepare;storage.commit_sleep=storage_commit;storage.resume_sleep=storage_resume;
 power=(x4_power_deep_v1){{1,sizeof(power),NULL,key,light_sleep},X4_POWER_DEEP_TAG,1,deep_sleep};wake=(risc_retained_wake_api_v1){1,sizeof(wake),NULL,read_wake,stage,clear};prefs=(risc_key_value_v1){1,sizeof(prefs),NULL,get,put};
 wifi=(wifi_api_v1){.api_version=1,.struct_size=sizeof(wifi),.status=wifi_status,.disconnect_checked=wifi_disconnect};
 bluetooth=(portable_bluetooth_control_v1){.api_version=1,.struct_size=sizeof(bluetooth),.set_enabled=bluetooth_enable,.status=bluetooth_status};
}
int main(int argc,char**argv){assert(argc==2);scenario=argv[1];setup();
 if(!strncmp(scenario,"fail-",5))assert(sscanf(scenario+5,"%d-%d",&fail_op,&fail_rc)==2);
 if(!strncmp(scenario,"cancel-",7))cancel_op=atoi(scenario+7);
 if(!strncmp(scenario,"boot-",5))boot=true;
 if(is("held")){}else if(is("key-failure"))fail_op=KEY;
 else if(is("release-prefs") || is("release-deep") || is("boot-release"))fail_op=RELEASE;
 else if(is("health-initial") || is("health-stage"))fail_op=HEALTH;
 else if(is("light"))mode=PORTABLE_SLEEP_LIGHT;
 else if(is("hybrid"))mode=PORTABLE_SLEEP_HYBRID;
 else if(is("future-mode"))mode=9;
 else if(is("alarm-future")){deadline=105;prep_cost=300;entry_cost=20;}
 else if(is("alarm-near"))deadline=101;
 else if(is("alarm-expired")){deadline=101;prep_cost=5;}
 else if(is("alarm-due"))deadline=100;
 else if(is("alarm-past"))deadline=99;
 else if(is("entry-expired"))entry_cost=30000;
 else if(is("stage-delay"))stage_cost=30000;
 else if(is("entry-delay"))entry_cost=17;
 else if(is("alarm-pending"))alarm_pending=2;
 else if(is("alarm-stuck"))alarm_pending=100;
 else if(is("alarm-step-busy")){alarm_pending=1;fail_op=ALARM_STEP;fail_rc=ALARM_BUSY;}
 else if(is("alarm-step-output")){alarm_pending=1;fail_op=ALARM_STEP;fail_rc=ALARM_OUTPUT;}
 else if(is("alarm-status-output")){alarm_pending=1;fail_op=ALARM_STATUS;fail_rc=ALARM_OUTPUT;}
 else if(is("alarm-uncertain"))alarm_pending=1;
 else if(is("boot-gpio"))boot_cause=RISC_BOOT_DEEP_GPIO;
 else if(is("boot-reset"))boot_cause=RISC_BOOT_RESET;
 else if(is("boot-power"))boot_cause=RISC_BOOT_POWER_ON;
 else if(is("bad-panel"))display.power_tag=0;
 else if(is("bad-touch"))touch.power_tag=0;
 else if(is("bad-storage"))storage.sleep_tag=0;
 else if(is("bad-power"))power.extension_tag=0;
 else if(is("bad-wake"))wake.struct_size--;
 else if(is("short-panel"))display.history.base.struct_size=sizeof(risc_display_output_api_v1);
 else if(is("short-touch"))touch.base.struct_size=sizeof(risc_touch_api_v1);
 else if(is("short-storage"))storage.terminal.power.volume.base.struct_size=sizeof(risc_storage_volume_api_v1);
 else if(is("short-power"))power.power.struct_size=sizeof(x4_power_v1);
 else if(is("null-panel"))display.resume=NULL;
 else if(is("null-touch"))touch.prepare=NULL;
 else if(is("null-storage"))storage.resume_sleep=NULL;
 else if(is("null-power"))power.deep_sleep_for=NULL;
 else if(is("null-wake"))wake.clear=NULL;
 else if(is("null-alarm"))alarms.step=NULL;
 else if(is("bad-panel-history"))display.history.extension_tag=0;
 else if(is("bad-storage-terminal"))storage.terminal.extension_tag=0;
 else if(is("short-wifi"))wifi.struct_size=WIFI_PREFIX_V1_SIZE;
 else if(is("short-bt"))bluetooth.struct_size--;
 else if(is("null-wifi"))wifi.disconnect_checked=NULL;
 else if(is("null-bt"))bluetooth.set_enabled=NULL;
 int result;
 if(boot){portable_desk_record r={0};result=portable_desk_clock_boot_read(&runtime,&r);assert(!paints&&!keys&&!prepares&&!stage_calls);if(is("boot-timer")){assert(result==1&&r.has_image);}else assert(result==(terminal?-2:0));}
 else if(setjmp(entered)){assert(terminal&&grants==7&&pending&&deep_calls==1);puts("terminal entry PASS");return 0;}
 else result=portable_app_alarm_sleep(&runtime,&display.history.base,NULL,&alarms);
 if(terminal){assert(result==-2&&grants);if(is("key-paint"))assert(frame_held);printf("%s retained PASS\n",scenario);return 0;}
 assert(result==(is("boot-timer")?1:0)&&!frame_held&&!grants&&!pending&&!dark&&!panel_off&&!touch_off&&!storage_off);
 if(is("refusal") || is("stage-delay") || is("alarm-future") || is("alarm-near") || is("alarm-pending") || is("catchup"))assert(deep_calls==1);
 if(is("entry-delay"))assert(duration==29983);
 if(is("stage-delay"))assert(duration==30000);
 if(is("alarm-future"))assert(duration==3680);
 if(is("alarm-near"))assert(duration==1);
 if(is("alarm-expired") || is("entry-expired") || is("alarm-due") || is("alarm-past") || is("alarm-step-busy") || is("alarm-stuck") || is("record-changed"))assert(!deep_calls);
 if(is("alarm-pending"))assert(steps==2&&prepares==3);
 if(is("alarm-stuck"))assert(steps==64&&prepares==64);
 if(is("catchup"))assert(paints==2&&storage_resumes==2&&touch_resumes==2&&panel_resumes==2);
 if(mode!=PORTABLE_SLEEP_DEEP || !strncmp(scenario,"settings-",9) || is("missing-1"))assert(light_calls==1&&!paints);
 if(is("held"))assert(!paints&&keys==80&&notices==1);
 if(!boot&&!paints&&!light_calls)assert(notices==1);
 if(cancel_op || !strncmp(scenario,"bad-",4) || !strncmp(scenario,"short-",6) ||
    !strncmp(scenario,"null-",5) || !strncmp(scenario,"missing-",8))assert(!deep_calls);
 printf("%s PASS\n",scenario);return 0;
}
