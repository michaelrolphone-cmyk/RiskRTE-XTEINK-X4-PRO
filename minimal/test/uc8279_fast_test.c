/* Real provider over scoped GPIO + phased native-SPI hardware models. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../drivers/x4pro_uc8279_fast/driver.c"

static bool owner=true, locked, lock_exists, fail_unlock, fail_destroy;
static bool fail_claim, fail_gpio, fail_spi_begin, fail_spi_exchange, fail_spi_end, fail_spi_release;
static bool fail_hold, fail_unhold, fail_retire, fail_release, no_busy, stuck_busy, stuck_power, fail_clock, reverse_clock;
static bool ambiguous, zero_probe, unstable;static uint8_t lut_id=0x68;
static uint64_t tick=100, busy_until, bus_deadline, gpio_serial=10, model_bus_token;
static unsigned phase, gpio_writes, bus_calls, exchanges, ends, probe_reads, refreshes, pons, pofs, sleeps, polls;
static uint32_t payload, max_exchange, max_poll_bytes;
static unsigned command_cost, scheduler_gap=1, refresh_pulse_ms=20, reset_assertions;
static unsigned commands[256], data_index;static uint8_t cmd, regs[256][42];
static uint8_t ram[60000], old_ram[60000], visible[48000];
static bool model_bus_held, ptin;
static struct { uint64_t token; bool output, level, held; } pins[49];
static const risc_display_output_api_v1 *output;

static bool own(void*c){(void)c;return owner;}
static bool create(void*c,uint64_t*t){(void)c;assert(owner);if(lock_exists)return false;lock_exists=true;*t=7;return true;}
static bool lock(void*c,uint64_t t){(void)c;assert(t==7);if(!owner||locked)return false;locked=true;return true;}
static bool unlock(void*c,uint64_t t){(void)c;assert(t==7&&locked);if(fail_unlock)return false;locked=false;return true;}
static bool destroy(void*c,uint64_t t){(void)c;assert(t==7&&!locked);if(fail_destroy)return false;lock_exists=false;return true;}
static uint64_t time_now(void*c){(void)c;if(fail_clock)return UINT64_MAX;if(reverse_clock)return --tick;return tick;}
static void delay(void*c,uint32_t ms){(void)c;tick+=ms;}
static bool ready(void*c){(void)c;return true;}
static bool light(void*c,uint16_t n,uint16_t m){(void)c;return m&&n<=m;}
static unsigned pin_for(uint64_t t){for(unsigned p=0;p<49;++p)if(t&&pins[p].token==t)return p;assert(!"foreign GPIO token");return 0;}
static bool gpio_claim(void*c,uint8_t p,bool out,bool level,bool pull,uint64_t*t){
 (void)c;assert(owner&&locked);*t=0;assert(p==6||p==14||p==18);assert(!pull);
 if(fail_claim)return false;assert(!pins[p].token);pins[p].token=++gpio_serial;pins[p].output=out;pins[p].level=level;pins[p].held=false;*t=pins[p].token;return true;
}
static bool gpio_write(void*c,uint64_t t,bool level){(void)c;unsigned p=pin_for(t);assert(owner&&locked&&pins[p].output&&!pins[p].held);++gpio_writes;if(fail_gpio)return false;pins[p].level=level;if(p==14&&!level){++reset_assertions;phase=0;busy_until=0;}return true;}
static bool gpio_read(void*c,uint64_t t,bool*v){(void)c;unsigned p=pin_for(t);assert(owner&&locked&&p==6);if(tick>=busy_until&&!stuck_busy&&!stuck_power)phase=0;*v=!((phase==1&&stuck_busy)||(phase==2&&stuck_power)||tick<busy_until);return true;}
static bool gpio_release(void*c,uint64_t t){(void)c;unsigned p=pin_for(t);if(fail_release||pins[p].held)return false;pins[p].token=0;return true;}
static int32_t gpio_hold(void*c,uint64_t t,bool on){(void)c;unsigned p=pin_for(t);assert(p==14&&pins[p].level);if(fail_unhold&&!on)return RISC_DEEP_SLEEP_RETAINED;if(fail_hold)return RISC_DEEP_SLEEP_PLATFORM;pins[p].held=on;return 0;}
static bool gpio_retire(void*c,uint64_t t){(void)c;unsigned p=pin_for(t);assert(p==14&&pins[p].held);if(fail_retire)return false;pins[p].token=0;return true;}
/* Lock the selected 05d811ae lab0.1.5 register vocabulary. In particular,
 * TCON0x60 is never written: no0x00/below0x22 timing experiment is imported. */
static void model_command(uint8_t value){
 if(commands[cmd]){
  if(cmd==0x61||cmd==0x65)assert(data_index==4);
  if(cmd==0x30)assert(data_index==1);
 }
 switch(value){
 case 0x00:case 0x02:case 0x03:case 0x04:case 0x07:
 case 0x10:case 0x12:case 0x13:
 case 0x20:case 0x21:case 0x22:case 0x23:case 0x24:
 case 0x30:case 0x50:case 0x61:case 0x65:case 0x70:case 0x71:
 case 0x90:case 0x91:case 0x92:case 0xE0:case 0xE1:case 0xE5:break;
 default:assert(!"unselected timing/voltage/controller command");
 }
 cmd=value;++commands[cmd];data_index=0;
 if(cmd==0x71)++probe_reads;
 if(cmd==0x91)ptin=true;if(cmd==0x92)ptin=false;
 if(cmd==0x04){++pons;phase=2;busy_until=tick+2;}
 if(cmd==0x02){++pofs;phase=2;busy_until=tick+2;}
 if(cmd==0x07)++sleeps;
 if(cmd==0x12){
  ++refreshes;phase=1;busy_until=no_busy?tick:tick+refresh_pulse_ms;
  unsigned top=0,height=480;
  if(regs[0x00][0]==0x37){
   assert(ptin&&regs[0x30][0]==0x0F&&regs[0x50][0]==0xD7);
   for(unsigned r=0;r<5;++r)for(unsigned i=0;i<42;++i){uint8_t want=0;if(i==0||i==5||i==6)want=1;if(i==1)want=r==0?1:(r<=2?0x81:0x41);assert(regs[0x20+r][i]==want);}
   top=(((unsigned)regs[0x90][4]<<8)|regs[0x90][5])-120;
   height=(((unsigned)regs[0x90][6]<<8)|regs[0x90][7])-120-top+1;
  }else assert(regs[0x00][0]==0x17&&regs[0x30][0]==0x0E);
  assert(top+height<=480);
  if(regs[0x00][0]==0x37){
   /* Independent X4 plane-code oracle, derived from FreeInk's absolute
    * LSB/MSB fold + inverted transfers + empirical quality-bank register map:
    * {DTM1,DTM2}=00->24,01->22,10->23,11->21. 0x4x darkens,0x8x whitens.
    * Decode actual emitted LUT/RAM, rather than assuming DTM2 is visible. */
   const uint8_t selector[4]={0x24,0x22,0x23,0x21};
   for(unsigned y=top;y<top+height;++y)for(unsigned x=0;x<100;++x){
    unsigned i=(y+120)*100+x;uint8_t result=0;
    for(unsigned bit=0;bit<8;++bit){unsigned n=(ram[i]>>bit)&1u,o=(old_ram[i]>>bit)&1u;
     const uint8_t rail=regs[selector[(o<<1)|n]][1]&0xC0u;assert(rail==0x40||rail==0x80);
     if(rail==0x80)result|=(uint8_t)(1u<<bit);
    }
    visible[y*100+x]=result;
   }
  }else memcpy(visible+top*100,ram+(top+120)*100,height*100);
 }
}
static void model_data(uint8_t value){
 /* Every initialization/resume write must preserve normal600-gate geometry,
  * zero gate/source start and selected PLLs, not merely the last refresh. */
 if(cmd==0x61){const uint8_t tres[]={0x03,0x20,0x02,0x58};assert(data_index<sizeof(tres)&&value==tres[data_index]);}
 if(cmd==0x65)assert(data_index<4&&value==0);
 if(cmd==0x30)assert(data_index==0&&(value==0x0E||value==0x0F));
 if(data_index<42)regs[cmd][data_index]=value;
 if(cmd==0x10||cmd==0x13){
  ++payload;
  unsigned pos=data_index;
  if(ptin){assert(regs[0x90][0]==0&&regs[0x90][1]==0&&regs[0x90][2]==3&&regs[0x90][3]==0x1F);pos+=(((unsigned)regs[0x90][4]<<8)|regs[0x90][5])*100;}
  assert(pos<sizeof(ram));if(cmd==0x13)ram[pos]=value;else old_ram[pos]=value;
 }
 ++data_index;
}
static bool spi_claim(void*c,uint8_t sclk,uint8_t mosi,uint8_t cs,uint64_t*t){(void)c;assert(owner&&locked&&sclk==12&&mosi==11&&cs==13);*t=0;if(fail_claim)return false;assert(!model_bus_token);model_bus_token=900+gpio_serial;*t=model_bus_token;return true;}
static bool spi_begin(void*c,uint64_t t,uint32_t hz,uint8_t mode,uint32_t ms){
 (void)c;assert(owner&&locked&&t==model_bus_token&&!model_bus_held&&!mode&&ms&&ms<=1000);assert(hz==100000||hz==20000000);++bus_calls;model_bus_held=true;bus_deadline=tick+ms;return !fail_spi_begin;
}
static bool spi_exchange(void*c,uint64_t t,const uint8_t*tx,uint8_t*rx,size_t n){
 (void)c;assert(owner&&locked&&model_bus_held&&t==model_bus_token&&!!tx!=!!rx&&n&&n<=512&&tick<bus_deadline);++exchanges;if(n>max_exchange)max_exchange=(uint32_t)n;if(fail_spi_exchange)return false;
 if(tx){for(size_t i=0;i<n;++i){if(pins[18].level)model_data(tx[i]);else { model_command(tx[i]); tick+=command_cost; }}}
 else {assert(pins[18].level);for(size_t i=0;i<n;++i){const uint8_t ver[]={1,2,lut_id,4,5};rx[i]=cmd==0x71?0x13:ver[i];if(ambiguous)rx[i]=0xFF;if(zero_probe)rx[i]=0;if(unstable&&probe_reads==2)rx[i]^=0x10;}}
 return true;
}
static bool spi_end(void*c,uint64_t t){(void)c;assert(owner&&locked&&model_bus_held&&t==model_bus_token);++ends;if(fail_spi_end)return false;model_bus_held=false;return true;}
static bool spi_release(void*c,uint64_t t){(void)c;assert(owner&&locked&&t==model_bus_token&&!model_bus_held);if(fail_spi_release)return false;model_bus_token=0;return true;}
static risc_display_present_metrics_v1 snapshot(void){risc_display_present_metrics_v1 m={.api_version=1,.struct_size=sizeof(m)};const unsigned g=gpio_writes,e=exchanges;assert(risc_display_output_metrics(output)->snapshot(NULL,&m));assert(g==gpio_writes&&e==exchanges);return m;}
static risc_display_surface_v1 acquire_frame(void){risc_display_surface_v1 f={0};assert(output->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&f));assert(f.width==800&&f.height==480&&f.size_bytes==48000);return f;}
static uint64_t submit_frame(risc_display_surface_v1 f,const risc_display_rect_v1*d,size_t n,bool clean){uint64_t token=0;const risc_display_present_options_v1 opt={clean?RISC_DISPLAY_PRESENT_CLEAN:0,RISC_DISPLAY_QUEUE_FIFO,0};assert(output->submit(NULL,f.frame,d,n,&opt,&token)&&token);return token;}
static void complete_frame(uint64_t t){
 risc_display_present_status_v1 status={0};
 for(unsigned n=0;n<11000;++n){
  unsigned before=payload;((const risc_driver_poll_v2*)t5_driver_get(2))->poll(8);++polls;
  unsigned delta=payload-before;if(delta>max_poll_bytes)max_poll_bytes=delta;assert(delta<=16384&&!locked);
  assert(output->present_status(NULL,t,&status));if(status.state==PRESENT_COMPLETE)break;assert(status.state!=PRESENT_FAILED);tick+=scheduler_gap;
 }
 assert(status.state==PRESENT_COMPLETE&&!model_bus_held);assert(snapshot().valid_times==63);
}
static void baseline(void){const unsigned old10=commands[0x10],old13=commands[0x13],oldpon=pons;risc_display_surface_v1 f=acquire_frame();memset(f.pixels,0x0F,48000);uint64_t t=submit_frame(f,NULL,0,false);complete_frame(t);assert(bytes_sent==180000&&commands[0x10]==old10+2&&commands[0x13]==old13+1&&pons==oldpon+1);assert(visible[0]==0xF0&&completed_history);}
static void fast_band(unsigned y,unsigned h){
 const unsigned old_sync=commands[0x10];risc_display_surface_v1 f=acquire_frame();memset(f.pixels,0xAA,48000);
 const risc_display_rect_v1 d={17,(int32_t)y,1,h};uint64_t t=submit_frame(f,&d,1,false);assert(fast_update&&partial_update);complete_frame(t);
 assert(commands[0x10]==old_sync&&bytes_sent==100*h);risc_display_present_metrics_v1 m=snapshot();assert(m.mode==RISC_DISPLAY_METRICS_PARTIAL&&m.effective_update.x==0&&m.effective_update.width==800&&m.effective_update.y==(int)y&&m.effective_update.height==h);
 assert(visible[y*100+2]==0x55&&visible[y*100+1]==0xF0);assert(previous_frame[y*100+2]==0xAA&&previous_frame[y*100+1]==0x0F);
}
static void test_polarity(void) {
 /* Initial OLD=0xF0 contains both old states. NEW=0xCC exercises all four
  * {old,new} combinations in one byte. Further targets deliberately leave
  * DTM1 untouched, so a mistaken old-dependent LUT cannot pass by syncing. */
 const unsigned syncs=commands[0x10];const uint8_t old=old_ram[12000];assert(old==0xF0);
 const uint8_t targets[]={0x33,0x66,0x00,0x00,0xFF,0xFF,0x33};
 for(unsigned n=0;n<sizeof(targets);++n){
  risc_display_surface_v1 f=acquire_frame();memset(f.pixels,targets[n],48000);
  uint64_t token=submit_frame(f,NULL,0,false);complete_frame(token);
  const uint8_t expected=(uint8_t)~targets[n];
  assert(bytes_sent==48000&&commands[0x10]==syncs&&old_ram[12000]==old);
  for(unsigned i=0;i<48000;++i)assert(visible[i]==expected&&previous_frame[i]==targets[n]);
 }
}
static void test_snapshot(void) {
 const risc_display_output_api_v1_snapshot *ext=risc_display_output_snapshot(output);assert(ext);
 static uint8_t buffer[104u*480u], saved[sizeof(buffer)];memset(buffer,0xA5,sizeof(buffer));
 risc_display_output_api_v1_snapshot legacy=api;
 legacy.metrics.power.history.base.struct_size=sizeof(risc_display_output_api_v1_metrics);
 assert(!risc_display_output_snapshot(&legacy.metrics.power.history.base));legacy=api;legacy.snapshot_tag^=1;assert(!risc_display_output_snapshot(&legacy.metrics.power.history.base));
 legacy=api;legacy.snapshot_version=2;assert(!risc_display_output_snapshot(&legacy.metrics.power.history.base));legacy=api;legacy.copy_completed=NULL;assert(!risc_display_output_snapshot(&legacy.metrics.power.history.base));
 const unsigned g=gpio_writes,e=exchanges,b=bus_calls;const uint64_t now=tick,serial=frame_serial;
 assert(ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,buffer,sizeof(buffer),104));
 assert(g==gpio_writes&&e==exchanges&&b==bus_calls&&now==tick&&serial==frame_serial&&!locked&&!held);
 for(unsigned y=0;y<480;++y){assert(!memcmp(buffer+y*104,previous_frame+y*100,100));for(unsigned x=100;x<104;++x)assert(buffer[y*104+x]==0xA5);}
 memcpy(saved,buffer,sizeof(buffer));
 assert(!ext->copy_completed(NULL,0,buffer,sizeof(buffer),104));assert(!ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,buffer,sizeof(buffer)-1,104));
 assert(!ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,buffer,sizeof(buffer),99));assert(!ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,buffer,SIZE_MAX,UINT32_MAX));
 assert(!ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,(void*)(UINTPTR_MAX-20u),48000,100));
 assert(!ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,frame,sizeof(frame),100));assert(!ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,previous_frame,sizeof(previous_frame),100));
 assert(!ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,(void*)&api,48000,100));
 owner=false;assert(!ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,buffer,sizeof(buffer),104));owner=true;
 retained=true;assert(!ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,buffer,sizeof(buffer),104));retained=false;
 risc_display_surface_v1 f=acquire_frame();((uint8_t*)f.pixels)[0]^=0xFF;
 assert(!ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,buffer,sizeof(buffer),104));output->release(NULL,f.frame);
 assert(!memcmp(buffer,saved,sizeof(buffer)));assert(ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,buffer,sizeof(buffer),104));assert(!memcmp(buffer,saved,sizeof(buffer)));
 f=acquire_frame();assert(risc_display_output_history(output)->seed_previous(NULL,f.frame));output->release(NULL,f.frame);
 assert(!ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,buffer,sizeof(buffer),104));
 f=acquire_frame();uint64_t t=submit_frame(f,NULL,0,false);assert(!ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,buffer,sizeof(buffer),104));
 ((const risc_driver_poll_v2*)t5_driver_get(2))->poll(8);assert(!ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,buffer,sizeof(buffer),104));complete_frame(t);
 assert(ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,buffer,sizeof(buffer),104));
 const risc_display_output_api_v1_power *power=risc_display_output_power(output);assert(power->prepare(NULL,1500)==RISC_DISPLAY_POWER_OK);
 memcpy(saved,buffer,sizeof(buffer));assert(!ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,buffer,sizeof(buffer),104));assert(!memcmp(buffer,saved,sizeof(buffer)));
 assert(power->resume(NULL,1500)==RISC_DISPLAY_POWER_OK);assert(!ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,buffer,sizeof(buffer),104));
 baseline();assert(ext->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,buffer,sizeof(buffer),104));
}
int main(int argc,char**argv){
 assert(argc==2);const char*s=argv[1];if(!strcmp(s,"lut69"))lut_id=0x69;
 if(!strcmp(s,"busy-boundary")){command_cost=1;scheduler_gap=50;}
 if(!strcmp(s,"busy-short"))refresh_pulse_ms=1; /* Observed pulse; no invented minimum. */
 garden_gpio_v1 g={.api_version=1,.struct_size=sizeof(g),.claim=gpio_claim,.write=gpio_write,.read=gpio_read,.release=gpio_release,.deep_sleep_hold=gpio_hold,.retire_held_output=gpio_retire};
 garden_spi_v1 b={.api_version=1,.struct_size=sizeof(b),.begin=spi_begin,.exchange=spi_exchange,.end=spi_end,.release=spi_release,.claim_three_wire=spi_claim};
 risc_platform_clock_api_v1 clock={1,sizeof(clock),NULL,time_now,delay};risc_provider_sync_api_v1 sync={1,sizeof(sync),NULL,own,create,lock,unlock,destroy};x4_power_ready_api_v1 power={1,sizeof(power),NULL,ready};risc_frontlight_api_v1 light_api={1,sizeof(light_api),NULL,light,NULL};
 risc_hw_spi_display_v1 cfg={.struct_size=sizeof(cfg),.bus={.struct_size=sizeof(cfg.bus),.kind=1,.instance_id=101,.controller=0,.frequency_hz=20000000,.sclk=12,.mosi=11,.miso=-1,.sda=-1,.scl=-1},.width=800,.height=480,.offset_y=120,.cs=13,.dc=18,.reset=14,.backlight=-1,.busy=6,.reset_assert_ms=50,.reset_recovery_ms=50};
 risc_hardware_device_v1 device={1,sizeof(device),3,"ultrachip,uc8279","unspecified","display.spi",1,sizeof(cfg),&cfg};
 risc_provider_dependency_v1 deps[]={{"hardware.device",1,&device},{"platform.gpio",1,&g},{"platform.clock",1,&clock},{"platform.sync",1,&sync},{"board.power.ready",1,&power},{"display.frontlight",1,&light_api},{"spi.bus",1,&b}};
 const risc_driver_v2*d=t5_driver_get(2);assert(!t5_driver_get(1)&&!strcmp(d->driver_id,"x4pro-uc8279-fast"));output=d->capability;
 if(!strcmp(s,"validation")){
  assert(!d->start(deps,6));b.struct_size=GARDEN_SPI_THREE_WIRE_V1_SIZE-1;assert(!d->start(deps,7));b.struct_size=sizeof(b);
  cfg.bus.frequency_hz=40000000;assert(!d->start(deps,7));
  cfg.bus.frequency_hz=80000000;assert(!d->start(deps,7));cfg.bus.frequency_hz=20000000;
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
 risc_display_info_v1 info={0};assert(output->get_info(NULL,&info)&&!(info.flags&RISC_DISPLAY_INFO_CLEAN_PRESENT));assert(info.nominal_refresh_millihz==10000&&info.typical_present_latency_us==100000);baseline();
 if(!strcmp(s,"busy-absent")||!strcmp(s,"busy-stuck")||!strcmp(s,"clock-fail")||!strcmp(s,"clock-rollback")){
  risc_display_surface_v1 f=acquire_frame();uint64_t t=submit_frame(f,NULL,0,false);no_busy=!strcmp(s,"busy-absent");stuck_busy=!strcmp(s,"busy-stuck");fail_clock=!strcmp(s,"clock-fail");reverse_clock=!strcmp(s,"clock-rollback");
  risc_display_present_status_v1 status={0};if(fail_clock){assert(!output->wait_present(NULL,t,5000,&status));((const risc_driver_poll_v2*)d)->poll(8);assert(output->present_status(NULL,t,&status));}else assert(output->wait_present(NULL,t,5000,&status));assert(status.state==PRESENT_FAILED&&!completed_history);risc_display_surface_v1 refused={0};assert(!output->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&refused));assert(risc_display_output_power(output)->prepare(NULL,1500)==RISC_DISPLAY_POWER_RETAINED&&!d->quiesce());
  const unsigned old_bus_calls=bus_calls,old_resets=reset_assertions;
  assert(risc_display_output_power(output)->resume(NULL,1500)==RISC_DISPLAY_POWER_RETAINED);
  assert(!d->start(deps,7)&&bus_calls==old_bus_calls&&reset_assertions==old_resets);goto done;
 }
 if(!strcmp(s,"polarity")){test_polarity();assert(d->quiesce());goto done;}
 if(!strcmp(s,"snapshot")){test_snapshot();assert(d->quiesce());goto done;}
 if(!strncmp(s,"spi-",4)||!strcmp(s,"gpio-fail")||!strcmp(s,"unlock-fail")){
  risc_display_surface_v1 f=acquire_frame();uint64_t t=submit_frame(f,NULL,0,false);(void)t;
  fail_spi_begin=!strcmp(s,"spi-begin");fail_spi_exchange=!strcmp(s,"spi-exchange");fail_spi_end=!strcmp(s,"spi-end");fail_gpio=!strcmp(s,"gpio-fail");fail_unlock=!strcmp(s,"unlock-fail");
  ((const risc_driver_poll_v2*)d)->poll(8);assert(retained&&!d->quiesce());if(fail_spi_end)assert(model_bus_held&&spi_held);else assert(!model_bus_held||fail_unlock);goto done;
 }
 if(!strcmp(s,"foreign-owner")){unsigned old=exchanges;owner=false;((const risc_driver_poll_v2*)d)->poll(8);risc_display_surface_v1 f={0};assert(!output->acquire(NULL,1,&f)&&!d->quiesce()&&old==exchanges);owner=true;}
 fast_band(220,40);fast_band(200,80);fast_band(160,160);
 {risc_display_surface_v1 f=acquire_frame();memset(f.pixels,0x77,48000);const risc_display_rect_v1 damage[2]={{8,478,8,1},{40,479,8,1}};uint64_t t=submit_frame(f,damage,2,false);complete_frame(t);
  assert(bytes_sent==4000&&update_area.y==440&&visible[47801]==0x88&&visible[47905]==0x88);
  assert(visible[47803]==0xF0&&visible[47701]==0xF0&&previous_frame[47803]==0x0F&&previous_frame[47701]==0x0F);
 }
 {risc_display_surface_v1 f=acquire_frame();memset(f.pixels,0x33,48000);unsigned old_sync=commands[0x10];uint64_t t=submit_frame(f,NULL,0,false);complete_frame(t);assert(bytes_sent==48000&&commands[0x10]==old_sync&&visible[0]==0xCC&&visible[47999]==0xCC);}
 {risc_display_surface_v1 f=acquire_frame();uint64_t t=submit_frame(f,NULL,0,true);complete_frame(t);assert(bytes_sent==180000&&!fast_update);}
 {const risc_display_output_api_v1_power*p=risc_display_output_power(output);assert(p);
  if(!strcmp(s,"hold-retry")){fail_hold=true;assert(p->prepare(NULL,1500)==RISC_DISPLAY_POWER_PLATFORM);fail_hold=false;}
  assert(p->prepare(NULL,1500)==RISC_DISPLAY_POWER_OK&&reset_held&&!completed_history);
  const unsigned resets_before=reset_assertions,tres_before=commands[0x61],gsst_before=commands[0x65];
  assert(p->resume(NULL,1500)==RISC_DISPLAY_POWER_OK&&!completed_history&&!screen_powered);
  assert(reset_assertions==resets_before+1&&commands[0x61]==tres_before+1&&commands[0x65]==gsst_before+1&&regs[0x30][0]==0x0E);
  baseline();assert(!fast_update&&regs[0x00][0]==0x17&&regs[0x30][0]==0x0E);
 }
 if(!strcmp(s,"release-retry")){fail_spi_release=true;assert(!d->quiesce()&&model_bus_token);fail_spi_release=false;}
 if(!strcmp(s,"retire-retry")){fail_retire=true;assert(!d->quiesce());fail_retire=false;}
 if(!strcmp(s,"destroy-retry")){fail_destroy=true;assert(!d->quiesce()&&!model_bus_token);fail_destroy=false;}
 assert(d->quiesce()&&!model_bus_token&&!lock_exists);assert(d->quiesce());
 done:printf("UC8279 fast %s PASS: payload=%u polls=%u max_slice_bytes=%u max_exchange=%u\n",s,payload,polls,max_poll_bytes,max_exchange);return 0;
}
