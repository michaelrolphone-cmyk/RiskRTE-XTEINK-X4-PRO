/* Production automatic-idle helper + selected fast panel provider. */
#include "PortableAppSleep.h"
#include "PortableBluetoothControl.h"
#include "WifiApi.h"
#include "../drivers/x4pro_power/X4PowerV1.h"
#include <RiscTouchPowerV1.h>
#include <RiscStorageVolumeV1.h>
static bool ip_touch,ip_sd;
static bool ip_require_settled,ip_settle_confirmed;
static unsigned ip_live,ip_lights,ip_resumes;
static const risc_display_output_api_v1 *ip_display;
static bool ip_health(risc_runtime_health_v1 *v){v->uptime_ms=(uint32_t)tick;return true;}
static void ip_yield(uint32_t ms){tick+=ms;owner_poll(8);}
static bool ip_key(void*c,bool*down){(void)c;*down=false;return true;}
static int32_t ip_light(void*c,uint32_t ms,risc_light_sleep_result_v1*out){
 (void)c;assert(ms&&out&&ip_touch&&ip_sd&&reset_held&&!started&&!settle_stage&&!screen_powered);
 if(ip_require_settled)assert(ip_settle_confirmed);
 unsigned e=exchanges,r=refreshes;tick+=60000;owner_poll(8);assert(exchanges==e&&refreshes==r);
 out->wake_cause=RISC_LIGHT_SLEEP_WAKE_GPIO;ip_lights++;return RISC_LIGHT_SLEEP_OK;
}
static const x4_power_v1 ip_power={1,sizeof(ip_power),NULL,ip_key,ip_light};
static int32_t ip_touch_prepare(void*c,uint32_t b){(void)c;assert(b);ip_touch=true;return 0;}
static int32_t ip_touch_resume(void*c,uint32_t b){(void)c;assert(b&&!reset_held&&!ip_sd);ip_touch=false;return 0;}
static uint64_t ip_sub(void*c){(void)c;return 1;}
static bool ip_unsub(void*c,uint64_t t){(void)c;(void)t;return true;}
static bool ip_poll(void*c,size_t n){(void)c;(void)n;return true;}
static int32_t ip_next(void*c,uint64_t t,risc_touch_event_v1*v){(void)c;(void)t;(void)v;return 0;}
static bool ip_snapshot(void*c,risc_touch_snapshot_v1*v){(void)c;(void)v;return true;}
static const risc_touch_power_api_v1 ip_touch_api={{1,sizeof(ip_touch_api),NULL,ip_sub,ip_unsub,ip_poll,ip_next,ip_snapshot},RISC_TOUCH_POWER_TAG,1,ip_touch_prepare,ip_touch_resume};
static bool ip_sd_prepare(void*c){(void)c;assert(reset_held&&ip_touch);return true;}
static bool ip_sd_commit(void*c){(void)c;ip_sd=true;return true;}
static int32_t ip_sd_resume(void*c){(void)c;ip_sd=false;return 0;}
static bool ip_legacy(void*c){(void)c;assert(0);return false;}
static risc_storage_volume_api_v1_sleep ip_storage;
static bool ip_wifi_off(void*c){(void)c;return true;}
static wifi_link_t ip_wifi_status(void*c){(void)c;return WIFI_LINK_DOWN;}
static const wifi_api_v1 ip_wifi={.api_version=1,.struct_size=sizeof(ip_wifi),.status=ip_wifi_status,.disconnect_checked=ip_wifi_off};
static bool ip_bt_off(void*c,bool on){(void)c;assert(!on);return true;}
static bool ip_bt_status(void*c,uint8_t*v){(void)c;*v=PORTABLE_BLUETOOTH_OFF;return true;}
static const portable_bluetooth_control_v1 ip_bt={.api_version=1,.struct_size=sizeof(ip_bt),.set_enabled=ip_bt_off,.status=ip_bt_status};
static bool ip_acquire(const char*n,uint32_t v,uint64_t id,risc_runtime_capability_v1*g){
 assert(v==1);if(!strcmp(n,"x4.power")){assert(id==17);g->api=&ip_power;}
 else if(!strcmp(n,"display.output")){assert(id==3);g->api=ip_display;}
 else if(!strcmp(n,"input.touch.raw")){assert(id==4);g->api=&ip_touch_api;}
 else if(!strcmp(n,"storage.volume")){assert(id==9);g->api=&ip_storage;}
 else if(!strcmp(n,"net.wifi")){assert(id==15);g->api=&ip_wifi;}
 else {assert(!strcmp(n,"bluetooth.hci")&&id==16);g->api=&ip_bt;}
 ip_live++;return true;
}
static bool ip_release(risc_runtime_capability_v1*g){assert(ip_live&&g->api&&!ip_touch&&!ip_sd&&!reset_held);ip_live--;g->api=NULL;return true;}
static const risc_runtime_api_v1 ip_runtime={.api_version=1,.struct_size=sizeof(ip_runtime),.health=ip_health,.yield_ms=ip_yield,.acquire=ip_acquire,.release=ip_release};
static alarm_sleep_v1 ip_ticket;
static int32_t ip_alarm_prepare(void*c,alarm_sleep_v1*t){(void)c;assert(!reset_held&&!ip_touch&&!ip_sd);*t=(alarm_sleep_v1){.struct_size=sizeof(*t),.snapshot=1,.rtc_seconds=1000,.deadline=2000};ip_ticket=*t;return 0;}
static int32_t ip_alarm_status(void*c,alarm_status_v1*s){(void)c;(void)s;return 0;}
static int32_t ip_alarm_step(void*c){(void)c;return 0;}
static int32_t ip_alarm_ack(void*c,const alarm_token_v1*t){(void)c;(void)t;return 0;}
static int32_t ip_alarm_resume(void*c,const alarm_sleep_v1*t){(void)c;assert(!ip_touch&&!ip_sd&&!reset_held&&!memcmp(t,&ip_ticket,sizeof(*t)));ip_resumes++;return 0;}
static const alarm_service_descriptor_v2 ip_alarm={{2,sizeof(ip_alarm),NULL,ip_alarm_status,ip_alarm_step,ip_alarm_step,ip_alarm_ack,ip_alarm_prepare,ip_alarm_step},ALARM_SERVICE_DESCRIPTOR_TAG,ALARM_SERVICE_DESCRIPTOR_VERSION,ALARM_MODE_VISUAL,ALARM_DESCRIPTOR_RESUME_SLEEP,ip_alarm_resume};
unsigned portable_quick_brightness(void){return 0;}
static uint8_t ip_sleep_frame[FRAME_BYTES],ip_foreground_frame[FRAME_BYTES];
static portable_idle_ui_result ip_overlay_begin(void *context) {
 (void)context;memcpy(ip_foreground_frame,previous_frame,FRAME_BYTES);
 risc_display_surface_v1 f=acquire_frame();memcpy(f.pixels,ip_sleep_frame,FRAME_BYTES);
 const uint64_t token=submit_frame(f,NULL,0,false);complete_frame(token);
 const risc_display_output_api_v1_settled *settled=risc_display_output_settled(output);assert(settled);
 assert(settled->request_settle(NULL,token));
 assert(settled->settled_status(NULL,token)==RISC_DISPLAY_SETTLED_PENDING);
 const uint64_t began=tick;unsigned queries=0;
 while(settled->settled_status(NULL,token)==RISC_DISPLAY_SETTLED_PENDING){assert(++queries<10000);ip_yield(1);}
 assert(settled->settled_status(NULL,token)==RISC_DISPLAY_SETTLED_COMPLETE);
 assert(tick-began>=2300u&&dtm1_synced&&!screen_powered);
 settled_planes();ip_settle_confirmed=true;
 printf("Actual Home raster + real provider: settled_ms=%llu queries=%u before power preparation\n",(unsigned long long)(tick-began),queries);
 return PORTABLE_IDLE_UI_READY;
}
static portable_idle_ui_result ip_overlay_restore(void *context) {
 (void)context;assert(ip_settle_confirmed&&!reset_held&&!ip_touch&&!ip_sd);
 risc_display_surface_v1 f=acquire_frame();memcpy(f.pixels,ip_foreground_frame,FRAME_BYTES);
 complete_frame(submit_frame(f,NULL,0,false));settled_planes();return PORTABLE_IDLE_UI_READY;
}
static void idle_policy_panel(const char *scenario) {
 ip_display=output;
 ip_storage.terminal.power.volume.base=(risc_storage_volume_api_v1){.api_version=1,.struct_size=sizeof(ip_storage)};
 ip_storage.terminal.extension_tag=RISC_STORAGE_POWER_COMMIT_TAG;ip_storage.terminal.extension_version=1;ip_storage.terminal.commit_power_down=ip_legacy;
 ip_storage.sleep_tag=RISC_STORAGE_SLEEP_TAG;ip_storage.sleep_version=1;
 ip_storage.prepare_sleep=ip_sd_prepare;ip_storage.commit_sleep=ip_sd_commit;ip_storage.resume_sleep=ip_sd_resume;
 if(!strcmp(scenario,"policy-overlay-settled")) {
  const char *path=getenv("SLEEP_OVERLAY_FRAME");assert(path);FILE *file=fopen(path,"rb");assert(file);
  assert(fread(ip_sleep_frame,1,sizeof(ip_sleep_frame),file)==sizeof(ip_sleep_frame));assert(fgetc(file)==EOF);assert(!fclose(file));
  ip_require_settled=true;
  const portable_idle_sleep_ui ui={NULL,ip_overlay_begin,ip_overlay_restore};
  assert(portable_app_idle_sleep_with_ui(&ip_runtime,output,NULL,&ip_alarm.base,&ui)==1);
  assert(ip_lights==1&&ip_resumes==1&&ip_settle_confirmed&&!ip_live&&started&&!reset_held);
  assert(!memcmp(previous_frame,ip_foreground_frame,FRAME_BYTES));assert(t5_driver_get(2)->quiesce());return;
 }
 fast_band(440,40);assert(settle_until-busy_done_ms==2300u);
 if(strstr(scenario,"finalized")) {
  drain_settle();assert(dtm1_synced&&!screen_powered&&!settle_stage);
  const unsigned before=refreshes;tick+=30000;owner_poll(8);assert(refreshes==before&&!settle_stage);
 }
 uint64_t token=0;
 if(strstr(scenario,"frame")) {
  refresh_pulse_ms=200;
  risc_display_surface_v1 f=acquire_frame();((uint8_t*)f.pixels)[20]=0x55;
  const risc_display_rect_v1 rect={0,0,800,40};token=submit_frame(f,&rect,1,false);
  assert(portable_app_idle_sleep(&ip_runtime,output,NULL,&ip_alarm.base)==0);
  assert(!ip_lights&&!ip_live&&!ip_touch&&!ip_sd);
  complete_frame(token);
 }
 assert(portable_app_idle_sleep(&ip_runtime,output,NULL,&ip_alarm.base)==1);
 assert(ip_lights==1&&ip_resumes==1&&!ip_live&&started&&!reset_held&&!settle_stage&&!screen_powered);
 unsigned e=exchanges,r=refreshes;tick+=90000;owner_poll(8);assert(exchanges==e&&refreshes==r);
 assert(t5_driver_get(2)->quiesce());
 puts("Actual fast0.1.12 panel + automatic Light: settling/finalized-off/pending frame readiness PASS");
}
