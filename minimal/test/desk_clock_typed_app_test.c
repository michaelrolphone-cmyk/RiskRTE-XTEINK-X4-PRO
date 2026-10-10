/* Real paper Clock + adapter + deployment hook; typed provider/Runtime fakes.
 * Each timer invocation is a fresh process with only serialized bytes/pixels.
 * Strict staging/clear gates mirror Runtime RetainedWakeRuntime.inc context
 * validation and CpuPort.cpp providerStorageSafe: live pad holds forbid both.
 * No runtime yield/KV access/release may bypass that gate while prepared. */
#define main unused_clock_fixture_main
#define risc_runtime_get_api unused_clock_fixture_runtime
#include "paper_clock_test.c"
#undef main
#undef risc_runtime_get_api
#include "PortableDeskClockApp.h"
#include "PortableAppSleep.h"
#include "PortableDeskClockSettings.h"
#include "PortableBluetoothControl.h"
#include "WifiApi.h"
#include "RiscInputNavigationV1.h"
#include "../drivers/x4pro_power/X4PowerDeepV1.h"
#include <RiscDisplayOutputPowerV1.h>
#include <RiscTouchPowerV1.h>
#include <RiscStorageVolumeV1.h>
#include <RiscRetainedWakeV1.h>
#include <setjmp.h>
#include <time.h>
static const char *test,*state_path;
static bool terminal,in_main,loaded,pending,dark,touch_off,panel_off,sd_off,wifi_off,bt_off,lease_seeded;
static unsigned entries,seeds,preparations,restores,clears;
static uint32_t epoch=1791331197u,start_ms;
static uint8_t physical[sizeof(pixels)];
static risc_retained_wake_record_v1 value;
static jmp_buf entry;
static risc_display_output_api_v1_power panel;
static risc_touch_power_api_v1 touch_power;
static risc_storage_volume_api_v1_sleep sd;
static x4_power_deep_v1 power;
static bool which(const char *name){return !strcmp(test,name);}
static void io(void){assert(!terminal);}
static bool healthy(risc_runtime_health_v1 *h){io();h->uptime_ms=ms;return polls<45;}
static void wait_ms(uint32_t n){io();assert(!panel_off&&!touch_off&&!sd_off);ms+=n;}
static bool read_rtc(void*c,twatch_rtc_time_v1*out){(void)c;io();ms+=2;time_t stamp=(time_t)epoch+(ms-start_ms)/1000u;struct tm *tm=gmtime(&stamp);assert(tm);*out=(twatch_rtc_time_v1){(uint16_t)(tm->tm_year+1900),(uint8_t)(tm->tm_mon+1),(uint8_t)tm->tm_mday,(uint8_t)tm->tm_wday,(uint8_t)tm->tm_hour,(uint8_t)tm->tm_min,(uint8_t)tm->tm_sec};return true;}
static bool read_key(void*c,bool*down){(void)c;io();if(which("key-retained")&&!subs&&frames){terminal=true;return false;}*down=which("held");return true;}
static bool bright(void*c,uint16_t level,uint16_t max){(void)c;io();assert(max==100&&(level==0||level==40));dark=level==0;return true;}
static bool seed_previous(void*c,risc_display_frame_v1 f){(void)c;io();assert(loaded&&frames&&f==1&&!subs&&!memcmp(pixels,physical,sizeof(pixels)));seeds++;if(which("seed-retained")){terminal=true;return false;}lease_seeded=true;return true;}
static bool frame_acquire(void*c,uint32_t format,risc_display_surface_v1*out){io();assert(!panel_off);lease_seeded=false;return acquire_frame(c,format,out);}
static void frame_release(void*c,risc_display_frame_v1 f){io();assert(!panel_off);lease_seeded=false;release_frame(c,f);}
static bool frame_submit(void*c,risc_display_frame_v1 f,const risc_display_rect_v1*r,size_t n,const risc_display_present_options_v1*o,risc_display_present_token_v1*out){(void)c;(void)r;(void)n;io();assert(!panel_off&&frames&&f==1);if(!subs&&loaded&&value.payload[24])assert(seeds&&lease_seeded&&o->intent==RISC_DISPLAY_PRESENT_QUALITY);lease_seeded=false;frames=0;*out=++presents;return true;}
static bool frame_wait(void*c,risc_display_present_token_v1 tkn,uint32_t timeout,risc_display_present_status_v1*out){(void)c;io();assert(tkn==presents&&timeout&&!panel_off);ms+=350;out->state=RISC_DISPLAY_PRESENT_COMPLETE;memcpy(physical,pixels,sizeof(pixels));return true;}
static int32_t panel_prepare(void*c,uint32_t timeout){(void)c;io();assert(timeout&&!subs&&!frames&&dark&&touch_off);panel_off=true;return RISC_DISPLAY_POWER_OK;}
static int32_t panel_resume(void*c,uint32_t timeout){(void)c;(void)timeout;io();assert(panel_off&&!sd_off);panel_off=false;restores++;return RISC_DISPLAY_POWER_OK;}
static int32_t touch_prepare(void*c,uint32_t timeout){(void)c;io();assert(timeout&&!subs&&!frames);preparations++;touch_off=true;if(which("touch-retained")){terminal=true;return RISC_TOUCH_POWER_RETAINED;}return which("touch-refused")?RISC_TOUCH_POWER_BUSY:RISC_TOUCH_POWER_OK;}
static int32_t touch_resume(void*c,uint32_t timeout){(void)c;(void)timeout;io();assert(!panel_off&&!sd_off);touch_off=false;return RISC_TOUCH_POWER_OK;}
static bool prepare_sd(void*c){(void)c;io();assert(!subs&&!frames&&panel_off&&touch_off);return !which("sd-refused");}
static bool commit_sd(void*c){(void)c;io();sd_off=true;return true;}
static int32_t resume_sd(void*c){(void)c;io();sd_off=false;return RISC_STORAGE_SLEEP_READY;}
static bool legacy_sd(void*c){(void)c;assert(0);return false;}
static bool disconnect_wifi(void*c){(void)c;io();if(which("wifi-retained")){terminal=true;return false;}wifi_off=true;return true;}
static wifi_link_t wifi_status(void*c){(void)c;io();assert(wifi_off);return WIFI_LINK_DOWN;}
static bool disable_bt(void*c,bool enabled){(void)c;io();assert(!enabled||!loaded||which("gpio"));bt_off=!enabled;return true;}
static bool bt_status(void*c,uint8_t*out){(void)c;io();*out=bt_off?PORTABLE_BLUETOOTH_OFF:PORTABLE_BLUETOOTH_ON;return true;}
static const wifi_api_v1 wifi={.api_version=1,.struct_size=sizeof(wifi),.status=wifi_status,.disconnect_checked=disconnect_wifi};
static const portable_bluetooth_control_v1 bt={.api_version=1,.struct_size=sizeof(bt),.set_enabled=disable_bt,.status=bt_status};
static int32_t prepare_alarm(void*c,alarm_sleep_v1*out){(void)c;io();assert(!sd_off&&!touch_off&&!panel_off&&wifi_off&&bt_off);out->rtc_seconds=epoch+(ms-start_ms)/1000u;out->deadline=which("alarm-due")?out->rtc_seconds:0;return ALARM_OK;}
static int32_t read_record(void*c,uint32_t type,uint32_t schema,risc_retained_wake_record_v1*out,uint32_t*cause){(void)c;io();assert(in_main&&!presents&&type==PORTABLE_DESK_CLOCK_RECORD_TYPE&&schema==1);*cause=which("gpio")?RISC_BOOT_DEEP_GPIO:RISC_BOOT_DEEP_TIMER;if(!loaded)return RISC_RETAINED_WAKE_ABSENT;*out=value;return RISC_RETAINED_WAKE_OK;}
static int32_t stage_record(void*c,const risc_retained_wake_record_v1*in){(void)c;io();assert(!panel_off&&!touch_off&&!sd_off&&!subs&&!frames);value=*in;pending=true;ms+=17;return which("stage-refused")?RISC_RETAINED_WAKE_CONTEXT:RISC_RETAINED_WAKE_OK;}
static int32_t clear_record(void*c){(void)c;io();assert(!panel_off&&!touch_off&&!sd_off&&!dark);clears++;pending=false;return RISC_RETAINED_WAKE_OK;}
static const risc_retained_wake_api_v1 wake={1,sizeof(wake),NULL,read_record,stage_record,clear_record};
static int32_t deep(void*c,uint32_t duration){(void)c;io();assert(pending&&duration&&duration<=60000&&!subs&&!frames&&dark&&panel_off&&touch_off&&sd_off&&wifi_off&&bt_off);entries++;
 if(which("refused"))return RISC_DEEP_SLEEP_BUSY;
 if(which("retained")){terminal=true;return RISC_DEEP_SLEEP_RETAINED;}
 FILE *out=fopen(state_path,"wb");assert(out);assert(fwrite(&value,1,sizeof(value),out)==sizeof(value));assert(fwrite(physical,1,sizeof(physical),out)==sizeof(physical));assert(!fclose(out));terminal=true;longjmp(entry,1);
}
static bool nav_poll(void*c,risc_input_navigation_frame_v1*out){(void)c;io();*out=(risc_input_navigation_frame_v1){0};if(!which("gpio")&&polls==6)out->pressed=out->released=RISC_NAV_HOME;return true;}
static bool nav_foreground(void*c,const risc_input_foreground_v1*f,size_t n){(void)c;(void)f;(void)n;io();return true;}
static bool nav_reset(void*c){(void)c;io();return true;}
static const risc_input_navigation_api_v1 navigation={1,sizeof(navigation),NULL,nav_poll,nav_foreground,nav_reset};
static int32_t preferences(void*c,const char*k,void*out,uint32_t cap,uint32_t*size){(void)c;io();assert(!panel_off&&!touch_off&&!sd_off);uint8_t bytes[]={0x53,1,1,0xa4};if(!strcmp(k,"time_format"))bytes[0]=0x54;else if(!strcmp(k,PORTABLE_DESK_FACE_KEY)){bytes[0]=0x46;bytes[2]=0;bytes[3]=0xa5;}else if(!strcmp(k,"quick_radio")){bytes[0]=0x51;bytes[2]=3;bytes[3]=0xa6;}else if(strcmp(k,PORTABLE_SLEEP_KEY)){*size=0;return RISC_KEY_VALUE_NOT_FOUND;}assert(cap>=4);memcpy(out,bytes,4);*size=4;return RISC_KEY_VALUE_OK;}
static int32_t no_put(void*c,const char*k,const void*v,uint32_t n){(void)c;(void)k;(void)v;(void)n;assert(0);return -1;}
static bool obtain(const char*name,uint32_t version,uint64_t id,risc_runtime_capability_v1*g){io();assert(!panel_off&&!touch_off&&!sd_off);const void *api=NULL;assert(grants<16);
 if(!strcmp(name,"display.output"))api=&panel;
 else if(!strcmp(name,"input.touch.raw"))api=&touch_power;
 else if(!strcmp(name,"rtc.clock")){static twatch_rtc_api_v1 clock;clock=rtc_api;clock.read=read_rtc;api=&clock;}
 else if(!strcmp(name,"storage.key-value")){static risc_key_value_v1 keyvalue;keyvalue=kv;keyvalue.get=preferences;keyvalue.put=no_put;api=&keyvalue;}
 else if(!strcmp(name,ALARM_SERVICE_CAPABILITY)){static alarm_service_v1 service;service=alarm_api;service.prepare_sleep=prepare_alarm;api=&service;}
 else if(!strcmp(name,"input.navigation"))api=&navigation;
 else if(!strcmp(name,X4_POWER_CAPABILITY)){assert(version==1&&id==17);api=&power;}
 else if(!strcmp(name,RISC_RETAINED_WAKE_CAPABILITY)){assert(version==1&&!id);api=&wake;}
 else if(!strcmp(name,"storage.volume"))api=&sd;
 else if(!strcmp(name,"net.wifi")){assert(id==15);api=&wifi;}
 else if(!strcmp(name,"bluetooth.hci")){assert(id==16);api=&bt;}
 else return acquire(name,version,id,g);
 g->api=api;grants++;return true;
}
static bool drop(risc_runtime_capability_v1*g){io();assert(!panel_off&&!touch_off&&!sd_off);return release(g);}
static const risc_runtime_api_v1 runtime={1,sizeof(runtime),healthy,wait_ms,diagnostic,launch_app,obtain,drop};
const risc_runtime_api_v1*risc_runtime_get_api(uint32_t version){return version==1?&runtime:NULL;}
int main(int argc,char**argv){assert(argc==3);test=argv[1];state_path=argv[2];scenario=0;
 panel.history.base=d;panel.history.base.struct_size=sizeof(panel);panel.history.base.acquire=frame_acquire;panel.history.base.release=frame_release;panel.history.base.submit=frame_submit;panel.history.base.wait_present=frame_wait;panel.history.base.set_brightness=bright;panel.history.extension_tag=RISC_DISPLAY_HISTORY_TAG;panel.history.extension_version=1;panel.history.seed_previous=seed_previous;panel.power_tag=RISC_DISPLAY_POWER_TAG;panel.power_version=1;panel.prepare=panel_prepare;panel.resume=panel_resume;
 touch_power.base=t;touch_power.base.struct_size=sizeof(touch_power);touch_power.power_tag=RISC_TOUCH_POWER_TAG;touch_power.power_version=1;touch_power.prepare=touch_prepare;touch_power.resume=touch_resume;
 sd.terminal.power.volume.base=(risc_storage_volume_api_v1){.api_version=1,.struct_size=sizeof(sd)};sd.terminal.extension_tag=RISC_STORAGE_POWER_COMMIT_TAG;sd.terminal.extension_version=1;sd.terminal.commit_power_down=legacy_sd;sd.sleep_tag=RISC_STORAGE_SLEEP_TAG;sd.sleep_version=1;sd.prepare_sleep=prepare_sd;sd.commit_sleep=commit_sd;sd.resume_sleep=resume_sd;
 power=(x4_power_deep_v1){{1,sizeof(power),NULL,read_key,NULL},X4_POWER_DEEP_TAG,1,deep};
 FILE *in=fopen(state_path,"rb");if(in){assert(fread(&value,1,sizeof(value),in)==sizeof(value));assert(fread(physical,1,sizeof(physical),in)==sizeof(physical));assert(!fclose(in));portable_desk_record decoded;assert(portable_desk_decode(value.payload,value.size,&decoded));epoch=(uint32_t)decoded.displayed_minute+60u;loaded=true;}
 assert(app_module_init()==0);in_main=true;start_ms=ms;
 if(setjmp(entry)){assert(terminal&&entries==1&&grants&&!subs&&!frames);if(loaded&&value.payload[24]!=1)assert(seeds==1);puts("Real typed desk clock terminal PASS");return 0;}
 app_main();if(portable_app_sleep_retained()){assert(terminal&&grants);unsigned held=grants;app_module_fini();assert(grants==held);if(which("key-retained")||which("seed-retained"))assert(frames==1);puts("Real typed desk clock retained PASS");return 0;}
 assert(!terminal);app_module_fini();assert(!grants&&!subs&&!frames&&!launches&&!pending&&!dark&&!panel_off&&!touch_off&&!sd_off);if(which("refused"))assert(entries==1&&clears==1&&restores==1);if(which("stage-refused"))assert(!entries&&clears==1&&restores==0);if(which("gpio")||which("held"))assert(!entries&&!seeds);printf("Real typed desk clock %s PASS\n",test);return 0;
}
