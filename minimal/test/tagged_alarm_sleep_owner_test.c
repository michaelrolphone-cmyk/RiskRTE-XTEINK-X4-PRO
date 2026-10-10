/* Actual Utilities service + actual X4 sleep owner. Only X4 hardware/Runtime and
 * shared Clock rendering are fixtures. Service ticket/state/storage logic runs
 * unchanged from the pinned Utilities source included by its production test. */
#define main unused_alarm_provider_suite_main
#include "native_utc_alarm_service_test.c"
#undef main
#include <setjmp.h>
#include "PortableAppSleep.h"
#include "PortableDeskClockApp.h"
#include "PortableSleepPolicy.h"
#include "PortableBluetoothControl.h"
#include "WifiApi.h"
#include "../drivers/x4pro_power/X4PowerDeepV1.h"
#include <RiscDisplayOutputPowerV1.h>
#include <RiscTouchPowerV1.h>
#include <RiscStorageVolumeV1.h>
#include <RiscRetainedWakeV1.h>
static const char *h_route,*h_case;
static bool h_stop,h_dark,h_panel,h_touch,h_sd,h_pending;
static unsigned h_grants,h_acquires,h_releases,h_brightness,h_light_calls,h_deep_calls;
static unsigned h_prepares,h_steps,h_statuses,h_resumes,h_restore,h_clears,h_notices,h_tokens[8];
static alarm_sleep_v1 h_ticket;
static alarm_service_descriptor_v2 h_service;
static risc_display_output_api_v1_power h_display;
static risc_touch_power_api_v1 h_touch_api;
static risc_storage_volume_api_v1_sleep h_sd_api;
static jmp_buf h_entered;
static bool h_is(const char *v){return !strcmp(h_case,v);}
static bool h_timer(void){return !strcmp(h_route,"timer");}
static bool h_light_mode(void){return !strcmp(h_route,"light");}
static void h_io(void){assert(!h_stop);}
static void h_safe(void){h_io();assert(!h_panel&&!h_touch&&!h_sd);}
static int32_t h_outcome(int32_t rc){if(rc==ALARM_RETAINED){h_stop=true;forbid=true;}return rc;}
static int32_t h_prepare(void*c,alarm_sleep_v1*out){h_safe();h_prepares++;int32_t rc=client->prepare_sleep(c,out);if(rc==ALARM_OK){h_ticket=*out;assert(sleep_ticket_valid&&!memcmp(out,&sleep_ticket,sizeof(*out)));}return h_outcome(rc);}
static int32_t h_step(void*c){h_safe();h_steps++;return h_outcome(client->step(c));}
static int32_t h_status(void*c,alarm_status_v1*out){h_safe();h_statuses++;if(h_is("status-retained"))return h_outcome(ALARM_RETAINED);int32_t rc=client->status(c,out);if(h_is("copied-retained")){out->error=ALARM_RETAINED;(void)h_outcome(ALARM_RETAINED);}return rc;}
static int32_t h_resume(void*c,const alarm_sleep_v1*in){(void)c;h_safe();assert(h_light_mode()&&h_light_calls==h_resumes+1);assert(!memcmp(in,&h_ticket,sizeof(*in)));h_resumes++;return h_outcome(alarm_service_resume(client,in));}
static bool h_health(risc_runtime_health_v1*out){h_io();out->uptime_ms=(uint32_t)ms;return true;}
static void h_yield(uint32_t n){h_safe();ms+=n;}
static bool h_key(void*c,bool*down){(void)c;h_io();*down=false;return true;}
static bool h_bright(void*c,uint16_t n,uint16_t scale){(void)c;h_safe();assert(scale==100);h_brightness++;h_dark=!n;return !(n&&h_is("restore-failure"));}
unsigned portable_quick_brightness(void){h_io();assert(!h_timer());return 40;}
static int32_t h_light(void*c,uint32_t n,risc_light_sleep_result_v1*out){(void)c;(void)out;h_safe();assert(h_dark&&sleep_ticket_valid);if(h_is("near")||h_is("future"))assert(n==(h_ticket.deadline-h_ticket.rtc_seconds)*1000u);h_light_calls++;
 if(h_is("refused"))return RISC_LIGHT_SLEEP_BUSY;
 if(h_is("native-retained")){h_stop=true;return RISC_LIGHT_SLEEP_RETAINED;}
 ms+=10000;epoch+=3;
 if(h_is("resume-retained"))read_result=RISC_REALTIME_CONTEXT;
 if(h_is("resume-rtc"))valid=false;
 if(h_is("resume-backward"))epoch-=100;
 if(h_is("ticket-stale"))assert(client->refresh(NULL)==ALARM_PENDING);
 if(h_is("descriptor-changed"))h_service.tag++;
 return RISC_LIGHT_SLEEP_OK;
}
static int32_t h_deep(void*c,uint32_t n){(void)c;h_io();assert(!h_light_mode()&&n&&h_pending&&h_dark&&h_panel&&sleep_ticket_valid);if(!h_timer())assert(h_touch&&h_sd);if(h_is("near"))assert(n==1);h_deep_calls++;
 if(h_is("native-retained")){h_stop=true;return RISC_DEEP_SLEEP_RETAINED;}
 if(h_is("terminal")){h_stop=true;longjmp(h_entered,1);}
 return RISC_DEEP_SLEEP_BUSY;
}
static x4_power_deep_v1 h_power={{1,sizeof(h_power),NULL,h_key,h_light},X4_POWER_DEEP_TAG,1,h_deep};
static bool h_seed(void*c,risc_display_frame_v1 frame){(void)c;(void)frame;assert(0);return false;}
static int32_t h_panel_prepare(void*c,uint32_t budget){(void)c;h_io();assert(budget&&h_dark);h_panel=true;return 0;}
static int32_t h_panel_resume(void*c,uint32_t budget){(void)c;h_io();assert(budget&&h_panel&&!h_sd);if(h_is("panel-resume-retained")){h_stop=true;return RISC_DISPLAY_POWER_RETAINED;}h_panel=false;h_restore++;return 0;}
static uint64_t h_subscribe(void*c){(void)c;assert(0);return 0;}
static bool h_unsubscribe(void*c,uint64_t token){(void)c;(void)token;assert(0);return false;}
static bool h_poll(void*c,size_t budget){(void)c;(void)budget;assert(0);return false;}
static int32_t h_next(void*c,uint64_t token,risc_touch_event_v1*out){(void)c;(void)token;(void)out;assert(0);return -1;}
static bool h_snapshot(void*c,risc_touch_snapshot_v1*out){(void)c;(void)out;assert(0);return false;}
static int32_t h_touch_prepare(void*c,uint32_t budget){(void)c;h_safe();assert(budget);h_touch=true;return 0;}
static int32_t h_touch_resume(void*c,uint32_t budget){(void)c;h_io();assert(budget&&!h_panel&&!h_sd);h_touch=false;return 0;}
static bool h_sd_prepare(void*c){(void)c;h_io();assert(h_panel&&h_touch);return true;}
static bool h_sd_commit(void*c){(void)c;h_io();h_sd=true;return true;}
static int32_t h_sd_resume(void*c){(void)c;h_io();h_sd=false;return RISC_STORAGE_SLEEP_READY;}
static bool h_legacy(void*c){(void)c;assert(0);return false;}
static bool h_wifi_off(void*c){(void)c;h_safe();return true;}
static wifi_link_t h_wifi_status(void*c){(void)c;h_safe();return WIFI_LINK_DOWN;}
static bool h_bt_off(void*c,bool on){(void)c;h_safe();assert(!on);return true;}
static bool h_bt_status(void*c,uint8_t*out){(void)c;h_safe();*out=PORTABLE_BLUETOOTH_OFF;return true;}
static wifi_api_v1 h_wifi={.api_version=1,.struct_size=sizeof(h_wifi),.status=h_wifi_status,.disconnect_checked=h_wifi_off};
static portable_bluetooth_control_v1 h_bt={.api_version=1,.struct_size=sizeof(h_bt),.set_enabled=h_bt_off,.status=h_bt_status};
static int32_t h_get_pref(void*c,const char*k,void*out,uint32_t capacity,uint32_t*size){(void)c;h_safe();assert(!h_timer()&&!strcmp(k,PORTABLE_SLEEP_KEY)&&capacity>=4);uint8_t mode=h_light_mode()?PORTABLE_SLEEP_LIGHT:PORTABLE_SLEEP_DEEP;uint8_t data[]={0x53,1,mode,(uint8_t)(mode^0xa5)};memcpy(out,data,4);*size=4;return 0;}
static int32_t h_put_pref(void*c,const char*k,const void*in,uint32_t n){(void)c;(void)k;(void)in;(void)n;assert(0);return -1;}
static risc_key_value_v1 h_prefs={1,sizeof(h_prefs),NULL,h_get_pref,h_put_pref};
static int32_t h_read_record(void*c,uint32_t t,uint32_t s,risc_retained_wake_record_v1*out,uint32_t*cause){(void)c;(void)t;(void)s;(void)out;(void)cause;assert(0);return -1;}
static int32_t h_stage(void*c,const risc_retained_wake_record_v1*in){(void)c;h_safe();assert(in->size==PORTABLE_DESK_CLOCK_RECORD_BYTES);h_pending=true;return 0;}
static int32_t h_clear(void*c){(void)c;h_safe();h_pending=false;h_clears++;return 0;}
static risc_retained_wake_api_v1 h_wake={1,sizeof(h_wake),NULL,h_read_record,h_stage,h_clear};
static bool h_acquire(const char*name,uint32_t v,uint64_t instance,risc_runtime_capability_v1*out){h_safe();assert(v==1);const void *value=NULL;
 if(!strcmp(name,"storage.key-value")){assert(!h_timer()&&instance==1);value=&h_prefs;}
 else if(!strcmp(name,X4_POWER_CAPABILITY)){assert(instance==17);value=&h_power;}
 else if(!strcmp(name,RISC_DISPLAY_OUTPUT_CAPABILITY)){assert(instance==(h_timer()?3u:0u));value=&h_display;}
 else if(!strcmp(name,RISC_RETAINED_WAKE_CAPABILITY)){assert(!instance);value=&h_wake;}
 else {assert(!h_timer());if(!strcmp(name,"input.touch.raw"))value=&h_touch_api;else if(!strcmp(name,"storage.volume"))value=&h_sd_api;else if(!strcmp(name,"net.wifi"))value=&h_wifi;else if(!strcmp(name,"bluetooth.hci"))value=&h_bt;else assert(0);}
 out->api=value;out->slot=++h_acquires;h_tokens[h_grants++]=out->slot;return true;
}
static bool h_release(risc_runtime_capability_v1*g){h_safe();assert(h_grants&&g->slot==h_tokens[h_grants-1]);if(h_is("release-retained")&&(h_light_mode()?g->api==&h_power:g->api==&h_wake)){h_stop=true;return false;}h_grants--;h_releases++;g->api=NULL;return true;}
static risc_runtime_api_v1 h_runtime={.api_version=1,.struct_size=sizeof(h_runtime),.health=h_health,.yield_ms=h_yield,.acquire=h_acquire,.release=h_release};
bool portable_desk_adapter_timer_only(void){return h_timer();}
void portable_desk_clock_refused(void){h_safe();h_notices++;}
void portable_desk_clock_radios_off(void){h_safe();assert(!h_timer());}
int portable_desk_clock_run(const risc_runtime_api_v1*rt,const portable_desk_sleep_ops*ops){assert(rt==&h_runtime);portable_desk_record r={0};r.config.rtc_reference_epoch=1700000000;strcpy(r.config.time_zone,"UTC0");r.displayed_minute=120;r.has_image=true;
 int rc=ops->stage(ops->context,&r);if(rc!=1)return rc;rc=ops->prepare(ops->context);if(rc!=1)return rc;rc=ops->stage_enter(ops->context,&r,30000,(uint32_t)ms);if(rc==-2)return rc;return ops->resume(ops->context)==1?0:-2;
}
static void h_setup(void){epoch=INT64_C(946684800)+date(2026,10,5,12,0,0);boot(true);clean_schedule();
 if(h_is("due")||h_is("near")||h_is("future")){
  uint32_t now=(uint32_t)(epoch-946684800);alarm_config c={.revision=1,.deadline=now+(h_is("due")?0u:h_is("near")?1u:60u),.created=now-1,.kind=ALARM_KIND_ALARM,.enabled=1};
  alarm_config_encode(&c,blobs[0]);sizes[0]=32;
 }
 const alarm_service_descriptor_v2 *real=alarm_service_descriptor(client);assert(real&&real->resume_sleep);h_service=*real;
 h_service.base.prepare_sleep=h_prepare;h_service.base.step=h_step;h_service.base.status=h_status;h_service.resume_sleep=h_resume;
 h_display.history.base=(risc_display_output_api_v1){.api_version=1,.struct_size=sizeof(h_display),.set_brightness=h_bright};h_display.history.extension_tag=RISC_DISPLAY_HISTORY_TAG;h_display.history.extension_version=1;h_display.history.seed_previous=h_seed;h_display.power_tag=RISC_DISPLAY_POWER_TAG;h_display.power_version=1;h_display.prepare=h_panel_prepare;h_display.resume=h_panel_resume;
 h_touch_api.base=(risc_touch_api_v1){.api_version=1,.struct_size=sizeof(h_touch_api),.subscribe=h_subscribe,.unsubscribe=h_unsubscribe,.poll=h_poll,.next=h_next,.snapshot=h_snapshot};h_touch_api.power_tag=RISC_TOUCH_POWER_TAG;h_touch_api.power_version=1;h_touch_api.prepare=h_touch_prepare;h_touch_api.resume=h_touch_resume;
 h_sd_api.terminal.power.volume.base=(risc_storage_volume_api_v1){.api_version=1,.struct_size=sizeof(h_sd_api)};h_sd_api.terminal.extension_tag=RISC_STORAGE_POWER_COMMIT_TAG;h_sd_api.terminal.extension_version=1;h_sd_api.terminal.commit_power_down=h_legacy;h_sd_api.sleep_tag=RISC_STORAGE_SLEEP_TAG;h_sd_api.sleep_version=1;h_sd_api.prepare_sleep=h_sd_prepare;h_sd_api.commit_sleep=h_sd_commit;h_sd_api.resume_sleep=h_sd_resume;
 if(h_is("api1"))h_service.base.api_version=1;
 if(h_is("short"))h_service.base.struct_size=sizeof(alarm_service_v1);
 if(h_is("tag"))h_service.tag++;
 if(h_is("descriptor-version"))h_service.descriptor_version++;
 if(h_is("output-modes"))h_service.output_modes=4;
 if(h_is("features"))h_service.features=2;
 if(h_is("no-resume"))h_service.resume_sleep=NULL;
 if(h_is("unsupported-resume")){h_service.features=0;h_service.resume_sleep=NULL;}
 if(h_is("feature-mismatch"))h_service.features=0;
 if(h_is("no-status"))h_service.base.status=NULL;
 if(h_is("no-step"))h_service.base.step=NULL;
 if(h_is("no-refresh"))h_service.base.refresh=NULL;
 if(h_is("no-acknowledge"))h_service.base.acknowledge=NULL;
 if(h_is("no-prepare"))h_service.base.prepare_sleep=NULL;
 if(h_is("no-stop"))h_service.base.stop_only=NULL;
 if(h_is("prepare-retained")){read_result=RISC_REALTIME_CONTEXT;for(unsigned n=0;n<100&&!custody_retained;n++)client->step(NULL);assert(custody_retained);}
 if(h_is("step-retained"))read_result=RISC_REALTIME_CONTEXT;
 if(h_is("storage-retained"))get_result=RISC_BOUND_KEY_VALUE_CONTEXT;
}
int main(int argc,char**argv){assert(argc==3);h_route=argv[1];h_case=argv[2];h_setup();bool malformed=alarm_service_descriptor(&h_service.base)==NULL;
 if(setjmp(h_entered)){assert(!h_light_mode()&&h_deep_calls==1&&!h_resumes&&h_grants&&h_pending);puts("Terminal Deep path does not consume Light ticket PASS");return 0;}
 int result=portable_app_alarm_sleep(&h_runtime,&h_display.history.base,NULL,&h_service.base);
 if(h_stop){assert(result==-2&&h_grants);if(h_is("resume-retained"))assert(h_resumes==1&&h_brightness==1);else if(h_is("release-retained")&&h_light_mode())assert(h_resumes==1);else assert(!h_resumes);puts("Production alarm/client retains custody PASS");return 0;}
 assert(!h_grants&&!h_pending&&!h_panel&&!h_touch&&!h_sd);
 if(malformed||(h_light_mode()&&h_is("unsupported-resume"))){assert(!h_prepares&&!h_steps&&!h_statuses&&!h_resumes&&!h_light_calls&&!h_deep_calls&&result==0);puts("Malformed tagged descriptor rejected before dispatch PASS");return 0;}
 if(h_is("due")){assert(!h_resumes&&!h_light_calls&&!h_deep_calls&&result==0&&snapshot().state==ALARM_STATE_ALERT);puts("Actual due alarm refuses sleep without consuming a ticket PASS");return 0;}
 if(h_light_mode()) {
  if(h_is("descriptor-changed")){assert(!h_resumes&&h_light_calls==1&&sleep_ticket_valid&&result==-1);assert(provider->quiesce());puts("Resume descriptor revalidation PASS");return 0;}
  if(h_is("refused")){assert(!h_resumes&&h_light_calls==1&&sleep_ticket_valid&&result==0);}
  else {assert(h_resumes==1&&h_light_calls==1&&!sleep_ticket_valid);if(h_is("resume-rtc")||h_is("resume-backward")||h_is("ticket-stale")||h_is("restore-failure"))assert(result==-1);else assert(result==1);}
  if(h_is("repeat")){alarm_sleep_v1 prior=h_ticket;assert(portable_app_alarm_sleep(&h_runtime,&h_display.history.base,NULL,&h_service.base)==1);assert(h_resumes==2&&h_light_calls==2&&h_ticket.snapshot>prior.snapshot&&!h_grants);unsigned before=reads;assert(alarm_service_resume(client,&prior)==ALARM_STALE&&reads==before);}
 } else {assert(!h_resumes&&h_deep_calls==1&&sleep_ticket_valid&&result==0&&h_restore==1&&h_clears==1);}
 assert(provider->quiesce());printf("%s %s production alarm/client PASS\n",h_route,h_case);return 0;
}
