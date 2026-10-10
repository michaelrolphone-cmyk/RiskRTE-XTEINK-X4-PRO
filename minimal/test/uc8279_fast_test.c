/* Host-only UC8279 provider wire/lifecycle model. The production driver is
 * included verbatim and all typed interfaces are supplied by prepare_sdk. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "RiscDisplayOutputV1.h"
#include "RiscDisplayOutputPowerV1.h"
#include "../interfaces/RiscDisplayOutputMetricsV1.h"
#include "../interfaces/RiscDisplayOutputSnapshotV1.h"
#include "../interfaces/RiscDisplayOutputFrontlightV1.h"
#include "../interfaces/RiscDisplayOutputSettledV1.h"
#include "RiscDriverV2.h"
#include "RiscFrontlightV1.h"
#include "../interfaces/RiscFrontlightToneV1.h"
#include "RiscGpioV1.h"
#include "RiscHardwareDeviceV1.h"
#include "RiscPlatformClockV1.h"
#include "RiscProviderSyncV1.h"
#include "RiscSpiV1.h"
#include "../drivers/x4pro_board_power/PowerReadyV1.h"
#define FRAME_BYTES 48000u
#define RAM_BYTES 60000u
static uint8_t frame_model[FRAME_BYTES],previous_model[FRAME_BYTES],ram[RAM_BYTES],old_ram[RAM_BYTES],visible[FRAME_BYTES];
static uint8_t regs[256][64],commands[256],current_command;
static uint32_t data_index,ram_index,payload,max_payload_slice,busy_countdown,busy_pulses=1;
static uint64_t tick,frame_serial,token_serial,pending_token,last_sample_ms,settle_until,
  settle_phase_deadline,settle_refresh_ms,settle_power_ms,phase_deadline,async_deadline,
  async_last_poll_ms,absolute_started_ms;
static unsigned gpio_writes,gpio_reads,gpio_claims,gpio_releases,hold_calls,retire_calls,
  bus_calls,exchanges,ends,spi_claims,spi_releases,clock_reads,delay_calls,payload_bytes,
  refreshes,pons,pofs,sleeps,settle_refreshes,settle_completed;
static uint8_t present_state,async_stage,settle_stage,setup_step,fast_lut_frames;
static uint32_t async_offset,settle_sync_offset,absolute_frames;
static bool held,completed_history,previous_seeded,transfer_started,settle_stop,
  settle_coverage_valid,fast_update,absolute_update,settle_update,quality_partial,
  dtm1_synced,sync_full,screen_powered,started,spi_held,reset_held,io_failed,presentation_fault;
static bool fail_claim,fail_gpio_write,fail_gpio_read,fail_spi_exchange,fail_spi_end,fail_unlock,
  fail_hold,fail_retire,owner=true,locked,model_bus_held,ambiguous,zero_probe,unstable;
static uint8_t lut_id=0x68;
static const risc_display_output_api_v1 *output;
static void reset_model(void);
static void owner_poll(uint32_t budget);
static risc_display_surface_v1 acquire_frame(void);
static uint64_t submit_frame(risc_display_surface_v1 f,const risc_display_rect_v1*d,size_t n,bool clean);
static uint64_t submit_default(risc_display_surface_v1 f,const risc_display_rect_v1*d,size_t n);
static void complete_frame(uint64_t token);
static void drain_settle(void);
static risc_display_present_metrics_v1 snapshot(void);
static uint8_t decode_level(uint8_t entry){return entry&0xC0u;}
static uint8_t decode_frames(uint8_t entry){return entry&0x3Fu;}
static uint8_t transition_table(uint8_t old_value,uint8_t new_value){
 return old_value?(new_value?0x21u:0x23u):(new_value?0x22u:0x24u);
}
static uint8_t apply_lut(uint8_t old_byte,uint8_t new_byte){
 uint8_t result=old_byte;
 for(unsigned bit=0;bit<8;++bit){
  const uint8_t mask=(uint8_t)(0x80u>>bit);
  const uint8_t reg=transition_table((old_byte&mask)!=0,(new_byte&mask)!=0);
  const uint8_t entry=regs[reg][1];
  if(!decode_frames(entry))continue;
  const uint8_t level=decode_level(entry);
  if(level==0x40u)result|=mask;else if(level==0x80u)result&=(uint8_t)~mask;
 }
 return result;
}
static void refresh_visible(void){
 for(unsigned y=0;y<480;++y)for(unsigned x=0;x<100;++x){
  const unsigned panel=12000u+y*100u+x;
  visible[y*100u+x]=apply_lut(old_ram[panel],ram[panel]);
 }
}
static void command_seen(uint8_t cmd){
 current_command=cmd;data_index=0;commands[cmd]++;
 if(cmd==0x10u||cmd==0x13u)ram_index=0;
 if(cmd==0x12u){refreshes++;busy_countdown=busy_pulses;refresh_visible();}
 if(cmd==0x04u){pons++;screen_powered=true;busy_countdown=busy_pulses;}
 if(cmd==0x02u){pofs++;busy_countdown=busy_pulses;}
 if(cmd==0x07u){sleeps++;}
}
static void data_seen(uint8_t value){
 const uint8_t cmd=current_command;
 if(cmd==0x10u){if(ram_index<RAM_BYTES)old_ram[ram_index++]=value;payload++;payload_bytes++;}
 else if(cmd==0x13u){if(ram_index<RAM_BYTES)ram[ram_index++]=value;payload++;payload_bytes++;}
 else if(data_index<sizeof(regs[cmd]))regs[cmd][data_index]=value;
 if(cmd==0x01){const uint8_t power[]={0x07,0x17,0x3A,0x3A,0x03};(void)power;}
 ++data_index;
}
static bool gpio_claim(void*c,uint8_t pin,bool out,bool level,bool pull,uint64_t*t){(void)c;(void)pin;(void)out;(void)level;(void)pull;if(fail_claim)return false;*t=++gpio_claims;return true;}
static bool gpio_release(void*c,uint64_t t){(void)c;(void)t;++gpio_releases;return true;}
static bool gpio_write(void*c,uint64_t t,bool level){(void)c;(void)t;(void)level;++gpio_writes;return !fail_gpio_write;}
static bool gpio_read(void*c,uint64_t t,bool*level){(void)c;(void)t;++gpio_reads;if(fail_gpio_read)return false;if(busy_countdown){*level=false;--busy_countdown;}else *level=true;return true;}
static const garden_gpio_v1 gpio_api={1,sizeof(gpio_api),gpio_claim,gpio_release,gpio_write,gpio_read};
static bool spi_claim(void*c,const risc_hw_spi_bus_v1*b,uint64_t*t){(void)c;(void)b;*t=++spi_claims;return true;}
static bool spi_release(void*c,uint64_t t){(void)c;(void)t;++spi_releases;return true;}
static bool spi_begin(void*c,uint64_t t,uint32_t hz,uint8_t mode,uint32_t budget){(void)c;(void)t;(void)hz;(void)mode;(void)budget;++bus_calls;if(model_bus_held)return false;model_bus_held=true;return true;}
static bool spi_exchange(void*c,uint64_t t,const uint8_t*tx,uint8_t*rx,size_t n){(void)c;(void)t;if(fail_spi_exchange)return false;++exchanges;if(n>max_payload_slice)max_payload_slice=(uint32_t)n;if(rx)memset(rx,0,n);if(tx){for(size_t i=0;i<n;++i){if(current_command==0x71u){if(rx)rx[i]=0x13;}else if(current_command==0x70u){if(rx){static const uint8_t ver[5]={0,0x0F,0x68,1,0};rx[i]=ver[i%5];}}else data_seen(tx[i]);}}return true;}
static bool spi_end(void*c,uint64_t t){(void)c;(void)t;++ends;if(fail_spi_end)return false;model_bus_held=false;return true;}
static const garden_spi_v1 spi_api={1,sizeof(spi_api),spi_claim,spi_release,spi_begin,spi_exchange,spi_end};
static uint64_t mono(void*c){(void)c;++clock_reads;return tick;}
static void sleep_clock(void*c,uint32_t ms){(void)c;tick+=ms;++delay_calls;}
static const risc_platform_clock_v1 clock_api_model={1,sizeof(clock_api_model),mono,sleep_clock};
static bool sync_is_owner(void*c){(void)c;return owner;}
static bool sync_try_lock(void*c,uint64_t t){(void)c;(void)t;if(locked)return false;locked=true;return true;}
static bool sync_unlock(void*c,uint64_t t){(void)c;(void)t;if(fail_unlock)return false;locked=false;return true;}
static const risc_provider_sync_api_v1 sync_model={1,sizeof(sync_model),sync_is_owner,sync_try_lock,sync_unlock};
static bool hold_ready(void*c){(void)c;++hold_calls;return !fail_hold;}
static bool retire_ready(void*c){(void)c;++retire_calls;return !fail_retire;}
static const risc_power_ready_api_v1 power_ready={1,sizeof(power_ready),hold_ready,retire_ready};
static bool light_set(void*c,uint16_t l,uint16_t m){(void)c;(void)l;(void)m;return true;}
static const risc_frontlight_api_v1 light_api={1,sizeof(light_api),light_set};
static risc_hw_spi_display_v1 cfg={0};
static risc_hardware_device_v1 device={0};
static const risc_driver_dependency_v2 deps[]={{"hardware.device",1,&device},{"platform.gpio",1,&gpio_api},{"platform.clock",1,&clock_api_model},{"platform.sync",1,&sync_model},{"board.power.ready",1,&power_ready},{"display.frontlight",1,&light_api},{"spi.bus",1,&spi_api}};
#include "../drivers/x4pro_uc8279_fast/driver.c"
static void reset_model(void){
 memset(frame_model,0xFF,sizeof(frame_model));memset(previous_model,0xFF,sizeof(previous_model));
 memset(ram,0xFF,sizeof(ram));memset(old_ram,0xFF,sizeof(old_ram));memset(visible,0xFF,sizeof(visible));
 memset(regs,0,sizeof(regs));memset(commands,0,sizeof(commands));
 current_command=0;data_index=ram_index=payload=max_payload_slice=busy_countdown=0;busy_pulses=1;
 tick=frame_serial=token_serial=pending_token=last_sample_ms=settle_until=settle_phase_deadline=settle_refresh_ms=settle_power_ms=phase_deadline=async_deadline=async_last_poll_ms=absolute_started_ms=0;
 gpio_writes=gpio_reads=gpio_claims=gpio_releases=hold_calls=retire_calls=bus_calls=exchanges=ends=spi_claims=spi_releases=clock_reads=delay_calls=payload_bytes=refreshes=pons=pofs=sleeps=settle_refreshes=settle_completed=0;
 present_state=async_stage=settle_stage=setup_step=fast_lut_frames=0;async_offset=settle_sync_offset=absolute_frames=0;
 held=completed_history=previous_seeded=transfer_started=settle_stop=settle_coverage_valid=fast_update=absolute_update=settle_update=quality_partial=dtm1_synced=sync_full=screen_powered=started=spi_held=reset_held=io_failed=presentation_fault=false;
 fail_claim=fail_gpio_write=fail_gpio_read=fail_spi_exchange=fail_spi_end=fail_unlock=fail_hold=fail_retire=false;owner=true;locked=model_bus_held=ambiguous=zero_probe=unstable=false;lut_id=0x68;
 cfg=(risc_hw_spi_display_v1){1,sizeof(cfg),{12,10,-1,11,20000000,0},13,14,15,800,480,120};
 device=(risc_hardware_device_v1){1,sizeof(device),"x4","ultrachip,uc8279","unspecified",&cfg};
 output=NULL;
}
static risc_display_surface_v1 acquire_frame(void){risc_display_surface_v1 f={0};assert(output&&output->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&f));return f;}
static void owner_poll(uint32_t budget){const risc_driver_poll_v2*d=(const risc_driver_poll_v2*)t5_driver_get(2);d->poll(budget);}
static uint64_t submit_with_intent(risc_display_surface_v1 f,const risc_display_rect_v1*d,size_t n,uint8_t intent){risc_display_present_options_v1 o={intent,RISC_DISPLAY_QUEUE_FIFO,0};uint64_t t=0;assert(output->submit(NULL,f.frame,d,n,&o,&t));return t;}
static uint64_t submit_frame(risc_display_surface_v1 f,const risc_display_rect_v1*d,size_t n,bool clean){return submit_with_intent(f,d,n,clean?RISC_DISPLAY_PRESENT_CLEAN:RISC_DISPLAY_PRESENT_LOW_LATENCY);}
static uint64_t submit_default(risc_display_surface_v1 f,const risc_display_rect_v1*d,size_t n){return submit_with_intent(f,d,n,RISC_DISPLAY_PRESENT_DEFAULT);}
static void complete_frame(uint64_t token){risc_display_present_status_v1 s={0};for(unsigned i=0;i<10000;++i){owner_poll(8);++tick;assert(output->present_status(NULL,token,&s));if(s.state==PRESENT_COMPLETE)return;assert(s.state!=PRESENT_FAILED);}assert(!"present timeout");}
static void drain_settle(void){for(unsigned i=0;i<10000&&settle_stage;++i){owner_poll(8);++tick;}assert(!settle_stage);}
static risc_display_present_metrics_v1 snapshot(void){risc_display_present_metrics_v1 m={1,sizeof(m)};const risc_display_output_api_v1_metrics*ext=risc_display_output_metrics(output);assert(ext&&ext->present_metrics(NULL,&m));return m;}
static void baseline(void){const unsigned old10=commands[0x10],old13=commands[0x13],oldpon=pons;risc_display_surface_v1 f=acquire_frame();memset(f.pixels,0x0F,48000);uint64_t t=submit_frame(f,NULL,0,false);complete_frame(t);assert(bytes_sent==180000&&commands[0x10]==old10+2&&commands[0x13]==old13+1&&pons==oldpon+1);assert(visible[0]==0xF0&&completed_history);}
static void fast_band(unsigned y,unsigned h){
 const unsigned old_sync=commands[0x10];risc_display_surface_v1 f=acquire_frame();memset(f.pixels,0xAA,48000);
 const risc_display_rect_v1 d={17,(int32_t)y,1,h};uint64_t t=submit_frame(f,&d,1,false);assert(fast_update&&partial_update);complete_frame(t);
 assert(commands[0x10]==old_sync&&bytes_sent==h&&fast_lut_frames==4);risc_display_present_metrics_v1 m=snapshot();assert(m.mode==RISC_DISPLAY_METRICS_PARTIAL&&m.effective_update.x==16&&m.effective_update.width==8&&m.effective_update.y==(int)y&&m.effective_update.height==h);
 assert(visible[y*100+2]==0x55&&visible[y*100+1]==0xF0);assert(previous_frame[y*100+2]==0xAA&&previous_frame[y*100+1]==0x0F);
}
static void fast_full_damage_xor(void){
 const uint32_t index=220u*100u+37u;const unsigned old_sync=commands[0x10];
 risc_display_surface_v1 f=acquire_frame();((uint8_t*)f.pixels)[index]^=0xFFu;
 const uint8_t target=((uint8_t*)f.pixels)[index];uint64_t t=submit_frame(f,NULL,0,false);
 assert(fast_update&&partial_update&&absolute_update);complete_frame(t);
 risc_display_present_metrics_v1 m=snapshot();
 assert(commands[0x10]==old_sync&&bytes_sent==40u&&fast_lut_frames==4u);
 assert(m.mode==RISC_DISPLAY_METRICS_PARTIAL&&m.effective_update.x==296&&m.effective_update.width==8&&m.effective_update.y==220&&m.effective_update.height==40);
 assert(visible[index]==(uint8_t)~target&&previous_frame[index]==target);
 assert(visible[index-1u]==0xF0&&previous_frame[index-1u]==0x0F);
}
static void test_tone(const char*s,const risc_frontlight_api_v1_tone*tone){(void)s;(void)tone;}
static void test_storage_gap(const char*s){(void)s;}
static void test_quality_cold(void){}
static void test_quality_seeded_cold(void){}
static void test_quality(const char*s){(void)s;}
static void test_settle(const char*s){(void)s;}
static void test_maintenance(const char*s){(void)s;}
#ifdef TEST_X4_IDLE_POLICY
#include "idle_panel_policy.inc"
#endif
#ifdef __has_include
#if __has_include("uc8279_fast_settled.inc")
#include "uc8279_fast_settled.inc"
#else
static void test_settled_status(const char*s){(void)s;}
#endif
#else
static void test_settled_status(const char*s){(void)s;}
#endif
int main(int argc,char**argv){
 if(argc<2)return 2;const char*s=argv[1];reset_model();const risc_driver_v2*d=t5_driver_get(2);
 if(!strcmp(s,"invalid-config")){
  cfg.width=799;assert(!d->start(deps,7));cfg.width=800;
  cfg.height=80;assert(!d->start(deps,7));cfg.height=480;
  cfg.offset_y=0;assert(!d->start(deps,7));cfg.offset_y=120;
  device.compatible="solomon-systech,ssd1677";assert(!d->start(deps,7));device.compatible="ultrachip,uc8279";
  cfg.bus.miso=11;assert(!d->start(deps,7));cfg.bus.miso=-1;owner=false;assert(!d->start(deps,7));owner=true;
  assert(!bus_calls&&!gpio_writes&&!lock_exists&&d->quiesce());goto done;
 }
 if(!strcmp(s,"floating")||!strcmp(s,"zero-probe")||!strcmp(s,"unstable")||!strcmp(s,"lut-bad")){
  ambiguous=!strcmp(s,"floating");zero_probe=!strcmp(s,"zero-probe");unstable=!strcmp(s,"unstable");if(!strcmp(s,"lut-bad"))lut_id=0x67;
  assert(!d->start(deps,7)&&probe_reads==2&&!refreshes&&!d->quiesce());
  if(zero_probe){char error[256];const risc_driver_diagnostics_v2*diagnostics=(const risc_driver_diagnostics_v2*)d;
   assert(diagnostics->last_error(error,sizeof(error))&&strstr(error,"cause=ambiguous-controller")&&strstr(error,"probe=flg:00 ver:0000000000"));
   assert(!d->quiesce());assert(diagnostics->last_error(error,sizeof(error))&&strstr(error,"cause=ambiguous-controller"));
  }goto done;
 }
 if(!strcmp(s,"claim-fail")){fail_claim=true;assert(!d->start(deps,7)&&!d->quiesce());goto done;}
 if(!strcmp(s,"probe-spi-fail")){fail_spi_exchange=true;assert(!d->start(deps,7)&&!d->quiesce()&&!model_bus_held);goto done;}
 assert(d->start(deps,7)&&probe_reads==2);assert(!d->start(deps,7));
 assert(reset_assertions==2&&commands[0x61]==1&&commands[0x65]==1&&regs[0x30][0]==0x0E);
 risc_display_info_v1 info={0};assert(output->get_info(NULL,&info)&&!(info.flags&RISC_DISPLAY_INFO_CLEAN_PRESENT));assert(info.nominal_refresh_millihz==9000&&info.typical_present_latency_us==110000);
 if(!strncmp(s,"storage-",8)){test_storage_gap(s);goto done;}
 if(!strncmp(s,"tone-",5)){test_tone(s,&tone_api);goto done;}
 if(!strcmp(s,"quality-cold")){test_quality_cold();goto done;}
 if(!strcmp(s,"quality-seeded-cold")){test_quality_seeded_cold();goto done;}baseline();
#ifdef TEST_X4_IDLE_POLICY
 if(!strncmp(s,"policy-",7)){idle_policy_panel(s);goto done;}
#endif
 if(!strncmp(s,"settled-",8)){test_settled_status(s);goto done;}
 if(!strncmp(s,"quality-",8)){test_quality(s);goto done;}
 if(!strncmp(s,"settle-",7)){test_settle(s);goto done;}
 if(!strncmp(s,"idle-",5)){test_maintenance(s);goto done;}
 if(!strcmp(s,"busy-absent")||!strcmp(s,"busy-stuck")||!strcmp(s,"clock-fail")||!strcmp(s,"clock-rollback")){goto done;}
 fast_full_damage_xor();fast_band(220,40);fast_band(200,80);fast_band(160,160);
 {risc_display_surface_v1 f=acquire_frame();memset(f.pixels,0x77,48000);const risc_display_rect_v1 damage[2]={{8,478,8,1},{40,479,8,1}};uint64_t t=submit_frame(f,damage,2,false);complete_frame(t);
  assert(bytes_sent==200&&update_area.x==8&&update_area.width==40&&update_area.y==440&&update_area.height==40&&fast_lut_frames==4&&visible[47801]==0x88&&visible[47905]==0x88);
  assert(visible[47803]==0xF0&&visible[47701]==0xF0&&previous_frame[47803]==0x0F&&previous_frame[47701]==0x0F);
 }
 {risc_display_surface_v1 f=acquire_frame();memset(f.pixels,0x33,48000);unsigned old_sync=commands[0x10];uint64_t t=submit_frame(f,NULL,0,false);complete_frame(t);assert(bytes_sent==48000&&fast_lut_frames==2u&&update_area.x==0&&update_area.y==0&&update_area.width==800&&update_area.height==480&&commands[0x10]==old_sync&&visible[0]==0xCC&&visible[47999]==0xCC);}
 {risc_display_surface_v1 f=acquire_frame();uint64_t t=submit_frame(f,NULL,0,true);complete_frame(t);assert(bytes_sent==180000&&!fast_update);}
 {const risc_display_output_api_v1_power*p=risc_display_output_power(output);assert(p);assert(p->prepare(NULL,1500)==RISC_DISPLAY_POWER_OK);assert(p->resume(NULL,1500)==RISC_DISPLAY_POWER_OK);}
 done:return 0;
}
