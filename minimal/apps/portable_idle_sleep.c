/* X4 automatic idle is reversible Light only. Keep the foreground stack,
 * drafts and grants. No retained record, terminal entry or GPIO authority. */
#include "PortableAppSleep.h"
#include "PortableBluetoothControl.h"
#include "WifiApi.h"
#include "../drivers/x4pro_power/X4PowerV1.h"
#include <RiscTimedSleepV1.h>
#include <RiscDisplayOutputPowerV1.h>
#include <RiscTouchPowerV1.h>
#include <RiscStorageVolumeV1.h>
#ifndef ALARM_SERVICE_TAGGED_V2
#error X4 automatic idle requires the tagged alarm resume contract
#endif
extern unsigned portable_quick_brightness(void);

typedef struct {
 const risc_runtime_api_v1 *rt;
 const risc_display_output_api_v1 *display;
 const x4_power_v1 *power;
 const risc_display_output_api_v1_power *panel;
 const risc_touch_power_api_v1 *touch;
 const risc_storage_volume_api_v1_sleep *storage;
 bool dark,touch_touched,panel_touched,storage_touched;
} x4_idle;
static int idle_key(x4_idle *s) {
 bool down=true;
 if(!s->power->read_key(s->power->context,&down))return -2;
 return down?0:1;
}
static int idle_restore(x4_idle *s) {
 if(s->storage_touched) {
  int32_t rc=s->storage->resume_sleep(s->storage->terminal.power.volume.base.context);
  if(rc!=RISC_STORAGE_SLEEP_READY && rc!=RISC_STORAGE_SLEEP_MEDIA_UNAVAILABLE)return -2;
  s->storage_touched=false;
 }
 if(s->panel_touched) {
  if(s->panel->resume(s->display->context,RISC_DISPLAY_POWER_MAX_BUDGET_MS)!=RISC_DISPLAY_POWER_OK)return -2;
  s->panel_touched=false;
 }
 if(s->touch_touched) {
  if(s->touch->resume(s->touch->base.context,RISC_TOUCH_POWER_MAX_BUDGET_MS)!=RISC_TOUCH_POWER_OK)return -2;
  s->touch_touched=false;
 }
 return 1;
}
static int idle_alarm(const alarm_service_v1 *a,int32_t rc) {
 if(rc==ALARM_RETAINED || rc==ALARM_OUTPUT || rc<ALARM_FOREGROUND || rc>ALARM_PENDING)return -2;
 alarm_status_v1 status={.struct_size=sizeof(status)};
 int32_t got=a->status(a->context,&status);
 if(got==ALARM_RETAINED || got==ALARM_OUTPUT || got<ALARM_FOREGROUND || got>ALARM_PENDING ||
    status.error<ALARM_FOREGROUND || status.error>ALARM_PENDING || status.error==ALARM_OUTPUT || status.output_uncertain)return -2;
 return got==ALARM_OK && (rc==ALARM_OK || rc==ALARM_PENDING)?1:0;
}
int portable_app_idle_sleep(const risc_runtime_api_v1 *rt,const risc_display_output_api_v1 *display,
                           const risc_battery_gauge_api_v1 *gauge,const alarm_service_v1 *alarms) {
 (void)gauge;
 const alarm_service_descriptor_v2 *alarm=alarm_service_descriptor(alarms);
 if(!rt || rt->api_version!=1 || rt->struct_size<RISC_RUNTIME_CAPABILITIES_V1_SIZE || !rt->acquire || !rt->release || !rt->health || !rt->yield_ms ||
    !display || display->api_version!=1 || display->struct_size<sizeof(*display) || !display->set_brightness || !alarm || !(alarm->features&ALARM_DESCRIPTOR_RESUME_SLEEP))return 0;
 const char *names[]={X4_POWER_CAPABILITY,"display.output","input.touch.raw","storage.volume","net.wifi","bluetooth.hci"};
 const uint64_t instances[]={17,3,4,9,15,16};
 risc_runtime_capability_v1 grants[6]={0};unsigned acquired=0;
 x4_idle s={.rt=rt,.display=display};int result=0,key;
 alarm_sleep_v1 ticket={.struct_size=sizeof(ticket)};
 for(;acquired<6;++acquired) {
  grants[acquired].struct_size=sizeof(grants[acquired]);
  /* Boolean failed-start cleanup gives no proof of native custody. */
  if(!rt->acquire(names[acquired],1,instances[acquired],&grants[acquired]))return -2;
 }
 s.power=grants[0].api;s.panel=risc_display_output_power(grants[1].api);
 s.touch=risc_touch_power(grants[2].api);s.storage=risc_storage_volume_sleep(grants[3].api);
 const wifi_api_v1 *wifi=grants[4].api;
 const portable_bluetooth_control_v1 *bt=grants[5].api;
 if(!s.power || s.power->api_version!=1 || s.power->struct_size<sizeof(*s.power) ||
    !s.power->read_key || !s.power->light_sleep || !s.panel || grants[1].api!=display || !s.touch || !s.storage ||
    !wifi || wifi->api_version!=1 || wifi->struct_size<WIFI_MANAGEMENT_V1_SIZE || !wifi->disconnect_checked || !wifi->status ||
    !bt || bt->api_version!=1 || bt->struct_size<sizeof(*bt) || !bt->set_enabled || !bt->status)goto done;
 risc_runtime_health_v1 now={.struct_size=sizeof(now)};
 if(!rt->health(&now))return -2;
 uint32_t start=now.uptime_ms,previous=start;
 bool neutral=false;
 for(unsigned n=0;n<80;++n) {
  key=idle_key(&s);if(key<0)return -2;
  if(!rt->health(&now) || now.uptime_ms<previous)return -2;
  previous=now.uptime_ms;
  if(!key)start=now.uptime_ms;
  else if(now.uptime_ms-start>=30u){neutral=true;break;}
  rt->yield_ms(5);
 }
 if(!neutral)goto done;
 if(!wifi->disconnect_checked(wifi->context) || wifi->status(wifi->context)!=WIFI_LINK_DOWN)return -2;
 uint8_t bluetooth=PORTABLE_BLUETOOTH_RETAINED;
 if(!bt->set_enabled(bt->context,false) || !bt->status(bt->context,&bluetooth) || bluetooth!=PORTABLE_BLUETOOTH_OFF)return -2;
 int32_t ready=ALARM_PENDING;
 uint32_t sampled_at=0,duration=RISC_TIMED_SLEEP_MAX_MS;
 for(unsigned n=0;n<64 && ready==ALARM_PENDING;++n) {
  key=idle_key(&s);if(key<0)return -2;if(!key)goto done;
  if(!rt->health(&now))return -2;
  sampled_at=now.uptime_ms;
  ticket=(alarm_sleep_v1){.struct_size=sizeof(ticket)};
  ready=alarms->prepare_sleep(alarms->context,&ticket);
  if(ready==ALARM_OK)break;
  int checked=idle_alarm(alarms,ready);if(checked<0)return -2;
   if(!checked)goto done;
  if(ready==ALARM_PENDING) {
   checked=idle_alarm(alarms,alarms->step(alarms->context));
   if(checked<0)return -2;
   if(!checked)goto done;
  }
 }
 if(ready!=ALARM_OK || (ticket.deadline && ticket.deadline<=ticket.rtc_seconds))goto done;
 if(ticket.deadline) {
  uint32_t seconds=ticket.deadline-ticket.rtc_seconds;
  duration=seconds>RISC_TIMED_SLEEP_MAX_MS/1000u?RISC_TIMED_SLEEP_MAX_MS:seconds>1u?(seconds-1u)*1000u:1u;
 }
 /* Keep the unchanged alarm ticket until this exact native Light return.
  * No service calls during prepared peripheral custody. */
 s.dark=true;
 if(!display->set_brightness(display->context,0,100))goto restore;
 key=idle_key(&s);if(key<0)return -2;if(!key)goto restore;
 s.touch_touched=true;
 int32_t rc=s.touch->prepare(s.touch->base.context,RISC_TOUCH_POWER_MAX_BUDGET_MS);
 if(rc==RISC_TOUCH_POWER_RETAINED || rc>0 || rc<RISC_TOUCH_POWER_INVALID)return -2;
 if(rc!=RISC_TOUCH_POWER_OK)goto restore;
 key=idle_key(&s);if(key<0)return -2;if(!key)goto restore;
 s.panel_touched=true;
 rc=s.panel->prepare(display->context,RISC_DISPLAY_POWER_MAX_BUDGET_MS);
 if(rc==RISC_DISPLAY_POWER_RETAINED || rc>0 || rc<RISC_DISPLAY_POWER_PLATFORM)return -2;
 if(rc!=RISC_DISPLAY_POWER_OK)goto restore;
 key=idle_key(&s);if(key<0)return -2;if(!key)goto restore;
 s.storage_touched=true;
 if(!s.storage->prepare_sleep(s.storage->terminal.power.volume.base.context))goto restore;
 if(!s.storage->commit_sleep(s.storage->terminal.power.volume.base.context))goto restore;
 key=idle_key(&s);if(key<0)return -2;if(!key)goto restore;
 if(!rt->health(&now))return -2;
 uint32_t elapsed=now.uptime_ms-sampled_at;
 if(elapsed>=duration)goto restore;
 risc_light_sleep_result_v1 wake={.struct_size=sizeof(wake)};
 rc=s.power->light_sleep(s.power->context,duration-elapsed,&wake);
 if(rc==RISC_LIGHT_SLEEP_RETAINED || rc>0 || rc<RISC_LIGHT_SLEEP_UNSUPPORTED)return -2;
 if(idle_restore(&s)!=1)return -2;
 if(rc==RISC_LIGHT_SLEEP_OK) {
  int32_t resumed=alarm_service_resume(alarms,&ticket);
  if(resumed==ALARM_RETAINED || resumed==ALARM_OUTPUT || resumed<ALARM_FOREGROUND || resumed>ALARM_PENDING)return -2;
  result=resumed==ALARM_OK?1:-1;
 }
restore:
 if(idle_restore(&s)!=1)return -2;
 if(s.dark && !display->set_brightness(display->context,(uint16_t)portable_quick_brightness(),100))return -2;
done:
 while(acquired)if(!rt->release(&grants[--acquired]))return -2;
 return result;
}
