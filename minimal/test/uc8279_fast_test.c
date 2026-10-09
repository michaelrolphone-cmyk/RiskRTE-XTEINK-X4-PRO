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
static uint64_t tick=100, busy_from, busy_until, bus_deadline, gpio_serial=10, model_bus_token, last_refresh_at;
static unsigned phase, gpio_writes, bus_calls, exchanges, ends, probe_reads, refreshes, pons, pofs, sleeps, polls;
static uint32_t payload, max_exchange, max_poll_bytes;
static unsigned command_cost, scheduler_gap=1, refresh_pulse_ms=20, refresh_assert_delay, reset_assertions;
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
static bool gpio_read(void*c,uint64_t t,bool*v){(void)c;unsigned p=pin_for(t);assert(owner&&locked&&p==6);if(tick>=busy_until&&!stuck_busy&&!stuck_power)phase=0;*v=!((phase==1&&stuck_busy)||(phase==2&&stuck_power)||(tick>=busy_from&&tick<busy_until));return true;}
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
 if(cmd==0x04){++pons;phase=2;busy_from=tick;busy_until=tick+2;}
 if(cmd==0x02){++pofs;phase=2;busy_from=tick;busy_until=tick+2;}
 if(cmd==0x07)++sleeps;
 if(cmd==0x12){
  ++refreshes;phase=1;last_refresh_at=tick;busy_from=tick+refresh_assert_delay;
  busy_until=no_busy?tick:busy_from+refresh_pulse_ms;
  unsigned top=0,height=480,left=0,width=800;
  if(regs[0x00][0]==0x37){
   assert(ptin&&regs[0x30][0]==0x0F&&regs[0x50][0]==0xD7);
   for(unsigned r=0;r<5;++r)for(unsigned i=0;i<42;++i){uint8_t want=0;if(i==0||i==5||i==6)want=1;if(i==1)want=r==0?1:(r<=2?0x81:0x41);assert(regs[0x20+r][i]==want);}
   top=(((unsigned)regs[0x90][4]<<8)|regs[0x90][5])-120;
   height=(((unsigned)regs[0x90][6]<<8)|regs[0x90][7])-120-top+1;
  }else {
   assert(regs[0x00][0]==0x17&&regs[0x30][0]==0x0E);
   if(ptin){
    assert(regs[0x50][0]==0xD7&&regs[0xE5][0]==0x5A&&regs[0xE0][0]==2);
    left=((unsigned)regs[0x90][0]<<8)|regs[0x90][1];
    width=(((unsigned)regs[0x90][2]<<8)|regs[0x90][3])-left+1;
    top=(((unsigned)regs[0x90][4]<<8)|regs[0x90][5])-120;
    height=(((unsigned)regs[0x90][6]<<8)|regs[0x90][7])-120-top+1;
    assert(!(left&7u)&&!(width&7u)&&left+width<=800);
    /* Normal differential OTP must compare with the real completed image,
     * even after absolute A2 left stale OLD RAM or a reset destroyed it. */
    assert(!memcmp(old_ram+12000,visible,sizeof(visible)));
   }else assert(regs[0x50][0]==0x97&&regs[0xE5][0]==0x1E);
  }
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
  }else {
   for(unsigned y=top;y<top+height;++y)
    memcpy(visible+y*100+left/8,ram+(y+120)*100+left/8,width/8);
  }
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
static void owner_poll(uint32_t budget) {
 const unsigned bytes=payload;const uint64_t at=tick;
 ((const risc_driver_poll_v2*)t5_driver_get(2))->poll(budget);
 assert(!locked&&payload-bytes<=16384u&&(reverse_clock||tick-at<=8u));
}
static void drain_settle(void) {
 for(unsigned n=0;n<6000&&settle_stage;++n){owner_poll(8);++tick;}
 assert(!settle_stage&&!ptin&&!presentation_fault&&!model_bus_held);
}
static void assert_idle(void) {
 const unsigned e=exchanges,g=gpio_writes,r=refreshes;const uint64_t sampled=last_sample_ms;
 for(unsigned n=0;n<10;++n){tick+=100;owner_poll(8);}
 assert(e==exchanges&&g==gpio_writes&&r==refreshes&&sampled==last_sample_ms);
}
static uint64_t submit_quality(risc_display_surface_v1 f,const risc_display_rect_v1 *damage,size_t count){
 const risc_display_present_options_v1 options={RISC_DISPLAY_PRESENT_QUALITY,RISC_DISPLAY_QUEUE_FIFO,0};
 uint64_t token=0;assert(output->submit(NULL,f.frame,damage,count,&options,&token));return token;
}
static void assert_quality_done(unsigned old1,unsigned old2,unsigned lut_count,const risc_display_rect_v1 *damage){
 assert(!fast_update&&quality_partial&&partial_update&&!settle_stage&&!settle_coverage_valid);
 assert(commands[0x10]==old1+2&&commands[0x13]==old2+1&&commands[0x20]==lut_count&&bytes_sent==180000);
 assert(regs[0x00][0]==0x17&&regs[0x30][0]==0x0E&&regs[0xE5][0]==0x5A&&regs[0x50][0]==0xD7&&!ptin);
 assert(update_area.x==(damage->x&~7)&&update_area.y==damage->y&&update_area.height==damage->height);
 assert(!memcmp(old_ram,ram,sizeof(ram))&&!memcmp(old_ram+12000,visible,sizeof(visible)));
 assert_idle();
}
static void test_quality_cold(void){
 risc_display_surface_v1 f=acquire_frame();memset(f.pixels,0x33,48000);
 const risc_display_rect_v1 damage={17,478,1,1};uint64_t token=submit_quality(f,&damage,1);
 assert(!fast_update&&!quality_partial&&!partial_update);complete_frame(token);
 assert(bytes_sent==180000&&commands[0x10]==2&&commands[0x13]==1&&!settle_stage);
 assert(regs[0xE5][0]==0x1E&&regs[0x50][0]==0x97&&visible[0]==0xCC&&visible[47999]==0xCC);
 assert(!memcmp(old_ram,ram,sizeof(ram)));assert_idle();assert(t5_driver_get(2)->quiesce());
}
static void test_quality_seeded_cold(void){
 /* Reconstructed physical image supplied immediately after fresh start, with
  * no completed frame in this provider instance and unusable controller RAM. */
 memset(visible,0xC3,sizeof(visible));memset(ram,0,sizeof(ram));memset(old_ram,0x55,sizeof(old_ram));
 risc_display_surface_v1 f=acquire_frame();memset(f.pixels,0x3C,48000);
 assert(risc_display_output_history(output)->seed_previous(NULL,f.frame));assert(previous_seeded&&!completed_history);
 ((uint8_t*)f.pixels)[0]=0xF0;const risc_display_rect_v1 damage={0,0,8,1};
 const unsigned old1=commands[0x10],old2=commands[0x13],luts=commands[0x20];
 uint64_t token=submit_quality(f,&damage,1);assert(quality_partial&&!fast_update);complete_frame(token);
 assert_quality_done(old1,old2,luts,&damage);assert(visible[0]==0x0F&&visible[1]==0xC3&&previous_frame[0]==0xF0&&previous_frame[1]==0x3C);
 assert(t5_driver_get(2)->quiesce());
}
static void test_quality(const char *scenario){
 const risc_driver_v2 *d=t5_driver_get(2);const risc_display_output_api_v1_power *power=risc_display_output_power(output);
 /* Fast leaves OLD RAM stale and may still be repeating a resident waveform. */
 fast_band(440,40);owner_poll(8);assert(settle_stage==SETTLE_DONE);
 const uint64_t last_busy=busy_until;const unsigned bytes_before=payload;
 const unsigned old1=commands[0x10],old2=commands[0x13],lut_count=commands[0x20];
 risc_display_surface_v1 f=acquire_frame();memset(f.pixels,0x66,48000);
 const risc_display_rect_v1 damage={17,440,1,4};uint64_t token=submit_quality(f,&damage,1);
 assert(!fast_update&&quality_partial&&partial_update);
 while(tick<last_busy){owner_poll(8);assert(payload==bytes_before);++tick;}
 complete_frame(token);assert_quality_done(old1,old2,lut_count,&damage);
 const uint8_t expected_window[]={0,16,0,23,2,48,2,51,1};assert(!memcmp(regs[0x90],expected_window,sizeof(expected_window)));
 assert(visible[44002]==0x99&&visible[44001]==0xF0&&visible[44402]==0x55);
 /* Deep-sleep reconstruction: RAM and provider history are invalid after
  * resume, while visible pigment remains. Only the canonical seed authorizes
  * a differential QUALITY partial rather than the full cold baseline. */
 static uint8_t image[48000];assert(risc_display_output_snapshot(output)->copy_completed(NULL,RISC_DISPLAY_FORMAT_MONO1,image,sizeof(image),100));
 assert(power->prepare(NULL,1500)==RISC_DISPLAY_POWER_OK);assert(power->resume(NULL,1500)==RISC_DISPLAY_POWER_OK&&!completed_history);
 memset(old_ram,0,sizeof(old_ram));memset(ram,0,sizeof(ram));
 f=acquire_frame();memcpy(f.pixels,image,sizeof(image));assert(risc_display_output_history(output)->seed_previous(NULL,f.frame));
 assert(previous_seeded&&!completed_history);((uint8_t*)f.pixels)[47999]=0xAA;
 const risc_display_rect_v1 minute={799,479,1,1};
 const unsigned old1_wake=commands[0x10],old2_wake=commands[0x13],lut_wake=commands[0x20];
 token=submit_quality(f,&minute,1);assert(quality_partial&&!fast_update);
 if(!strcmp(scenario,"quality-busy-absent")||!strcmp(scenario,"quality-busy-stuck")){
  no_busy=!strcmp(scenario,"quality-busy-absent");stuck_busy=!strcmp(scenario,"quality-busy-stuck");
  risc_display_present_status_v1 status={0};assert(output->wait_present(NULL,token,5000,&status)&&status.state==PRESENT_FAILED);
  assert(!completed_history&&!settle_stage&&!d->quiesce());return;
 }
 if(!strcmp(scenario,"quality-transfer-failure")){
  while(async_stage!=UC_ASYNC_OLD){owner_poll(8);++tick;}
  fail_spi_exchange=true;owner_poll(8);assert(present_state==PRESENT_FAILED&&retained&&!completed_history&&!settle_stage&&!d->quiesce());return;
 }
 complete_frame(token);assert_quality_done(old1_wake,old2_wake,lut_wake,&minute);
 assert(visible[47999]==0x55&&visible[47998]==(uint8_t)~image[47998]&&visible[47899]==(uint8_t)~image[47899]);
 const uint8_t minute_window[]={3,24,3,31,2,87,2,87,1};assert(!memcmp(regs[0x90],minute_window,sizeof(minute_window)));
 /* Return to the interactive contract: explicit LOW_LATENCY, no OLD sync,
  * one native band upload and resident settling re-enabled. */
 f=acquire_frame();((uint8_t*)f.pixels)[47999]=0x55;
 const risc_display_present_options_v1 fast={RISC_DISPLAY_PRESENT_LOW_LATENCY,RISC_DISPLAY_QUEUE_FIFO,0};
 const unsigned synced=commands[0x10];assert(output->submit(NULL,f.frame,&minute,1,&fast,&token));complete_frame(token);
 assert(fast_update&&!quality_partial&&bytes_sent==4000&&commands[0x10]==synced&&settle_stage==SETTLE_READY&&visible[47999]==0xAA);
 assert(d->quiesce());
}
static void test_maintenance(const char *scenario) {
 const risc_driver_v2 *d=t5_driver_get(2);
 const risc_display_output_api_v1_power *p=risc_display_output_power(output);
 /* Cold OTP alone cannot repeat the absolute A2 bank. */
 assert(!maintenance_enabled&&!maintenance_due);tick+=60000;assert_idle();
 fast_band(440,40);assert(maintenance_enabled&&!maintenance_due&&!maintenance_active);
 drain_settle();assert(maintenance_enabled&&maintenance_due==last_sample_ms+30000u);
 const uint64_t due=maintenance_due;const unsigned r=refreshes,bytes=payload,e=exchanges;
 const risc_display_present_metrics_v1 original=snapshot();
 tick=due-1;owner_poll(8);assert(!settle_stage&&refreshes==r&&exchanges==e);
 if(!strcmp(scenario,"idle-sleep-wait")){
  assert(p->prepare(NULL,1500)==RISC_DISPLAY_POWER_OK&&!maintenance_enabled);
  tick+=60000;assert_idle();assert(p->resume(NULL,1500)==RISC_DISPLAY_POWER_OK);
  tick+=60000;assert_idle();assert(!maintenance_enabled&&!maintenance_due);assert(d->quiesce());return;
 }
 tick=due;
 if(!strcmp(scenario,"idle-gates")){
  owner_poll(0);assert(exchanges==e&&!settle_stage);owner=false;owner_poll(8);owner=true;assert(exchanges==e);
  risc_display_surface_v1 held_frame=acquire_frame();owner_poll(8);assert(exchanges==e&&!settle_stage);
  output->release(NULL,held_frame.frame);
 }
 if(!strcmp(scenario,"idle-replace-ready")){
  risc_display_surface_v1 f=acquire_frame();((uint8_t*)f.pixels)[0]=0x55;
  const risc_display_rect_v1 damage={0,0,8,1};uint64_t token=submit_frame(f,&damage,1,false);
  assert(!maintenance_enabled&&!maintenance_due);complete_frame(token);assert(refreshes==r+1);assert(d->quiesce());return;
 }
 if(!strcmp(scenario,"idle-busy-absent"))no_busy=true;
 if(!strcmp(scenario,"idle-busy-stuck"))stuck_busy=true;
 if(!strcmp(scenario,"idle-spi-failure"))fail_spi_exchange=true;
 if(!strcmp(scenario,"idle-clock-fail"))fail_clock=true;
 if(!strcmp(scenario,"idle-clock-rollback")){tick=last_sample_ms;reverse_clock=true;}
 if(!strcmp(scenario,"idle-replace-window"))command_cost=1;
 if(!strcmp(scenario,"idle-replace-assert"))refresh_assert_delay=5;
 owner_poll(!strcmp(scenario,"idle-replace-window")?1:8);
 if(strstr(scenario,"failure")||strstr(scenario,"busy-")||strstr(scenario,"clock-")){
  for(unsigned n=0;n<3600&&settle_stage;++n){owner_poll(8);++tick;}
  assert(presentation_fault&&!completed_history&&!maintenance_enabled&&!maintenance_due&&!settle_stage);
  assert(present_state==PRESENT_COMPLETE&&payload==bytes&&!d->quiesce());return;
 }
 assert(maintenance_active&&settle_stage);
 if(!strncmp(scenario,"idle-replace-",13)||!strcmp(scenario,"idle-quality")){
  const unsigned active_refreshes=refreshes;const uint64_t drain_at=busy_until;
  risc_display_surface_v1 f=acquire_frame();memset(f.pixels,0x66,48000);
  const risc_display_rect_v1 damage={0,3,8,1};
  const bool quality=!strcmp(scenario,"idle-quality");
  uint64_t token=quality?submit_quality(f,&damage,1):submit_frame(f,&damage,1,false);
  assert(!maintenance_enabled&&!maintenance_due);command_cost=0;
  while(settle_stage){owner_poll(8);assert(payload==bytes&&refreshes==active_refreshes);++tick;}
  if(active_refreshes>r)assert(tick>=drain_at);
  complete_frame(token);assert(refreshes==active_refreshes+1&&visible[300]==0x99);
  if(quality){assert(!settle_stage&&!maintenance_enabled);tick+=60000;assert_idle();}
  else assert(maintenance_enabled&&!maintenance_due&&settle_stage);
  assert(d->quiesce());return;
 }
 if(!strcmp(scenario,"idle-sleep-active")||!strcmp(scenario,"idle-sleep-budget")||!strcmp(scenario,"idle-cleanup")){
  assert(refreshes==r+1);const unsigned off=pofs;
  if(!strcmp(scenario,"idle-sleep-budget")){
   const uint64_t at=tick;assert(p->prepare(NULL,5)==RISC_DISPLAY_POWER_TIMEOUT);
   assert(tick-at==5&&!maintenance_enabled&&pofs==off);
  }
  if(!strcmp(scenario,"idle-cleanup")){assert(d->quiesce());assert(!model_bus_token&&!lock_exists);return;}
  assert(p->prepare(NULL,1500)==RISC_DISPLAY_POWER_OK&&pofs==off+1);
  assert(!settle_stage&&!maintenance_enabled&&refreshes==r+1&&payload==bytes);
  tick+=60000;assert_idle();assert(p->resume(NULL,1500)==RISC_DISPLAY_POWER_OK);
  tick+=60000;assert_idle();assert(!maintenance_enabled);assert(d->quiesce());return;
 }
 drain_settle();assert(refreshes==r+1&&settle_refreshes==1&&settle_completed==1);
 assert(payload==bytes&&maintenance_enabled&&!maintenance_active&&maintenance_due==last_sample_ms+30000u);
 assert(regs[0x90][4]==0&&regs[0x90][5]==120&&regs[0x90][6]==2&&regs[0x90][7]==87);
 const risc_display_present_metrics_v1 after=snapshot();assert(!memcmp(&original,&after,sizeof(after)));
 assert(visible[44002]==0x55&&visible[43902]==0xF0&&previous_frame[44002]==0xAA);
 /* Late owner polling performs one pulse, without replaying missed intervals. */
 tick=maintenance_due+90000;owner_poll(8);drain_settle();assert(refreshes==r+2&&payload==bytes);
 assert(maintenance_due==last_sample_ms+30000u);assert_idle();assert(d->quiesce());
}
static void test_settle(const char *scenario) {
 const risc_driver_v2 *d=t5_driver_get(2);
 const risc_display_output_api_v1_power *p=risc_display_output_power(output);
 assert(!settle_stage);assert_idle();
 fast_band(440,40);assert(settle_stage==SETTLE_READY);
 const uint64_t token=pending_token,until=settle_until;
 assert(until-busy_done_ms==2300u);
 const unsigned bytes=payload,dtm1=commands[0x10],dtm2=commands[0x13],r=refreshes;
 const risc_display_present_metrics_v1 original=snapshot();
 if(!strcmp(scenario,"settle-repeat")||!strcmp(scenario,"settle-short")||!strcmp(scenario,"settle-expiry")){
  if(!strcmp(scenario,"settle-short"))refresh_pulse_ms=1;
  if(!strcmp(scenario,"settle-expiry"))tick=until;
  drain_settle();
  assert(payload==bytes&&commands[0x10]==dtm1&&commands[0x13]==dtm2);
  assert(settle_completed==refreshes-r&&settle_completed==settle_refreshes);
  if(!strcmp(scenario,"settle-expiry"))assert(refreshes==r);
  else {assert(refreshes>r+1&&last_refresh_at<until&&tick>=until&&tick<=until+refresh_pulse_ms+2);}
  const risc_display_present_metrics_v1 after=snapshot();assert(!memcmp(&original,&after,sizeof(original)));
  assert(completed_history&&visible[44002]==0x55&&visible[43902]==0xF0&&previous_frame[44002]==0xAA);
  assert_idle();assert(d->quiesce());return;
 }
 if(!strncmp(scenario,"settle-replace",14)){
  if(!strcmp(scenario,"settle-replace-window")){command_cost=1;owner_poll(1);assert(settle_stage==SETTLE_WINDOW);}
  else if(!strcmp(scenario,"settle-replace-refresh")){command_cost=1;owner_poll(1);owner_poll(1);assert(settle_stage==SETTLE_REFRESH);}
  else if(!strcmp(scenario,"settle-replace-assert")){refresh_assert_delay=5;owner_poll(8);assert(settle_stage==SETTLE_ASSERT);}
  else if(strcmp(scenario,"settle-replace-ready")){owner_poll(8);assert(settle_stage==SETTLE_DONE);}
  const unsigned repeats=refreshes;const uint64_t drained_at=busy_until;
  risc_display_surface_v1 f=acquire_frame();memset(f.pixels,0x66,48000);
  const risc_display_rect_v1 replacement={16,3,8,1};
  uint64_t new_token=submit_frame(f,&replacement,1,!strcmp(scenario,"settle-replace-clean"));
  assert(new_token>token);command_cost=0;
  if(!strcmp(scenario,"settle-replace-wait")){
   risc_display_present_status_v1 status={0};assert(output->wait_present(NULL,new_token,5000,&status));
   assert(status.state==PRESENT_COMPLETE&&refreshes==repeats+1&&visible[302]==0x99);
   drain_settle();assert(d->quiesce());return;
  }
  while(settle_stage){owner_poll(8);assert(payload==bytes&&refreshes==repeats);++tick;}
  if(repeats>r)assert(tick>=drained_at);
  assert(!ptin);complete_frame(new_token);assert(refreshes==repeats+1);
  assert(visible[302]==0x99&&previous_frame[302]==0x66);
  if(!strcmp(scenario,"settle-replace-clean"))assert(!settle_stage&&payload-bytes==180000);
  else {
   assert(settle_until>until&&settle_until-busy_done_ms==2300u&&payload-bytes==4000&&visible[44002]==0x55);
   /* Old bottom-band target plus new top-band target must both settle using
    * the resident RAM, while the replacement upload remains only40 rows. */
   assert(settle_area.y==0&&settle_area.height==480);drain_settle();
   assert(visible[302]==0x99&&visible[44002]==0x55&&visible[20002]==0xF0);
  }
  assert(d->quiesce());return;
 }
 if(!strcmp(scenario,"settle-sleep")||!strcmp(scenario,"settle-sleep-budget")||!strcmp(scenario,"settle-cleanup")){
  owner_poll(8);assert(settle_stage==SETTLE_DONE);const unsigned repeats=refreshes,poweroffs=pofs;
  if(!strcmp(scenario,"settle-sleep-budget")){
   const uint64_t began=tick;assert(p->prepare(NULL,5)==RISC_DISPLAY_POWER_TIMEOUT);
   assert(tick-began==5&&settle_stop&&pofs==poweroffs&&settle_stage==SETTLE_DONE);
  }
  if(strcmp(scenario,"settle-cleanup"))assert(p->prepare(NULL,1500)==RISC_DISPLAY_POWER_OK);
  else {
   const uint64_t began=tick;assert(d->quiesce());
   assert(tick-began<1500&&refreshes==repeats&&payload==bytes&&pofs==poweroffs+1);
   assert(!model_bus_token&&!lock_exists&&!settle_stage);return;
  }
  assert(settle_stop&&pofs==poweroffs+1&&model_bus_token&&pins[14].token);
  assert(!settle_stage&&refreshes==repeats&&payload==bytes);
  if(strcmp(scenario,"settle-cleanup")){
   assert_idle();
   assert(p->resume(NULL,1500)==RISC_DISPLAY_POWER_OK&&!settle_stage&&!completed_history);
   assert_idle();baseline();assert(!settle_stage);
  }
  assert(d->quiesce()&&!model_bus_token&&!lock_exists);return;
 }
 if(!strcmp(scenario,"settle-no-budget")){
  const unsigned e=exchanges;owner_poll(0);assert(exchanges==e&&settle_stage==SETTLE_READY);
  owner=false;owner_poll(8);assert(exchanges==e&&settle_stage==SETTLE_READY);owner=true;
  drain_settle();assert(d->quiesce());return;
 }
 no_busy=!strcmp(scenario,"settle-busy-absent");stuck_busy=!strcmp(scenario,"settle-busy-stuck");
 fail_spi_begin=!strcmp(scenario,"settle-spi-begin");fail_spi_exchange=!strcmp(scenario,"settle-spi-exchange");
 fail_spi_end=!strcmp(scenario,"settle-spi-end");fail_gpio=!strcmp(scenario,"settle-gpio");
 fail_clock=!strcmp(scenario,"settle-clock-fail");reverse_clock=!strcmp(scenario,"settle-clock-rollback");
 const bool replacement=!strcmp(scenario,"settle-queued-failure");
 if(replacement){no_busy=true;owner_poll(8);risc_display_surface_v1 f=acquire_frame();submit_frame(f,NULL,0,false);}
 for(unsigned n=0;n<3600&&settle_stage;++n){owner_poll(8);++tick;}
 assert(presentation_fault&&!completed_history&&!settle_stage&&settle_completed==0&&payload==bytes);
 assert(present_state==(replacement?PRESENT_FAILED:PRESENT_COMPLETE));
 risc_display_surface_v1 refused={0};assert(!output->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&refused));
 assert(p->prepare(NULL,1500)==RISC_DISPLAY_POWER_RETAINED&&!d->quiesce());
 if(fail_spi_end)assert(spi_held&&model_bus_held&&spi_token==model_bus_token);
 else assert(!spi_held&&!model_bus_held);
}
#ifdef TEST_X4_IDLE_POLICY
#include "idle_panel_policy.inc"
#endif
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
 risc_display_info_v1 info={0};assert(output->get_info(NULL,&info)&&!(info.flags&RISC_DISPLAY_INFO_CLEAN_PRESENT));assert(info.nominal_refresh_millihz==10000&&info.typical_present_latency_us==100000);
 if(!strcmp(s,"quality-cold")){test_quality_cold();goto done;}
 if(!strcmp(s,"quality-seeded-cold")){test_quality_seeded_cold();goto done;}baseline();
#ifdef TEST_X4_IDLE_POLICY
 if(!strncmp(s,"policy-",7)){idle_policy_panel(s);goto done;}
#endif
 if(!strncmp(s,"quality-",8)){test_quality(s);goto done;}
 if(!strncmp(s,"settle-",7)){test_settle(s);goto done;}
 if(!strncmp(s,"idle-",5)){test_maintenance(s);goto done;}
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
