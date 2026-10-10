/* Explicit sleep-settling readiness over the real shipped provider. */
#include "../interfaces/RiscDisplayOutputSettledV1.h"
_Static_assert(offsetof(risc_display_output_api_v1_settled,frontlight)==0,"tone prefix retained");
_Static_assert(offsetof(risc_display_output_api_v1_settled,settled_tag)==sizeof(risc_display_output_api_v1_frontlight),"settled suffix");
static void settled_expect(uint64_t token,int32_t wanted) {
 const risc_display_output_api_v1_settled *ext=risc_display_output_settled(output);assert(ext);
 const tone_display_state before=tone_display_snapshot();
 assert(ext->settled_status(NULL,token)==wanted);
 const tone_display_state after=tone_display_snapshot();assert(!memcmp(&before,&after,sizeof(before)));
}
static void test_settled_status(const char *scenario) {
 const risc_driver_v2 *d=t5_driver_get(2);
 const risc_display_output_api_v1_settled *ext=risc_display_output_settled(output);assert(ext);
 if(!strcmp(scenario,"settled-descriptors")) {
  risc_display_output_api_v1_settled copy=*ext;
#define SETTLED_BASE frontlight.snapshot.metrics.power.history.base
#define BAD_SETTLED(change) do {copy=*ext;change;assert(!risc_display_output_settled(&copy.SETTLED_BASE));} while(0)
  assert(risc_display_output_settled(&copy.SETTLED_BASE));
  BAD_SETTLED(copy.SETTLED_BASE.struct_size=sizeof(copy.frontlight));
  BAD_SETTLED(copy.SETTLED_BASE.api_version=2);
  BAD_SETTLED(copy.settled_tag^=1);BAD_SETTLED(copy.settled_version=2);BAD_SETTLED(copy.settled_status=NULL);
  BAD_SETTLED(copy.request_settle=NULL);
  BAD_SETTLED(copy.frontlight.frontlight_tag^=1);BAD_SETTLED(copy.frontlight.get_tone=NULL);
  assert(!risc_display_output_settled(NULL));
#undef BAD_SETTLED
#undef SETTLED_BASE
  assert(d->quiesce());return;
 }
 if(!strcmp(scenario,"settled-burst-request")||!strcmp(scenario,"settled-preempt")) {
  for(unsigned n=0;n<17;++n){
   risc_display_surface_v1 frame=acquire_frame();memset(frame.pixels,(int)n,FRAME_BYTES);
   complete_frame(submit_frame(frame,NULL,0,false));
  }
  assert(absolute_update&&!settle_update&&settle_stage==SETTLE_READY);const uint64_t token=pending_token;
  const unsigned io=exchanges;const uint64_t now=tick;
  assert(ext->request_settle(NULL,token)&&settle_stage==SETTLE_READY&&io==exchanges&&tick==now);
  if(!strcmp(scenario,"settled-preempt")){
   for(unsigned n=0;n<100&&settle_stage!=SETTLE_DONE;++n){owner_poll(8);++tick;}
   assert(settle_stage==SETTLE_DONE);const uint64_t queued=tick;
   risc_display_surface_v1 frame=acquire_frame();memset(frame.pixels,0x69,FRAME_BYTES);
   const uint64_t newer=submit_frame(frame,NULL,0,false);complete_frame(newer);
   assert(tick-queued<100u);settled_expect(token,RISC_DISPLAY_SETTLED_FAILED);
   printf("Explicit sleep-settle preempted: new frame complete after %llu ms (20 ms BUSY fixture)\n",(unsigned long long)(tick-queued));
  }
  drain_settle();settled_expect(pending_token,RISC_DISPLAY_SETTLED_COMPLETE);assert(d->quiesce());return;
 }
 const uint64_t previous=pending_token;
 settled_expect(previous,RISC_DISPLAY_SETTLED_COMPLETE);
 settled_expect(0,RISC_DISPLAY_SETTLED_FAILED);settled_expect(previous+1,RISC_DISPLAY_SETTLED_FAILED);
 assert(!ext->request_settle(NULL,0)&&!ext->request_settle(NULL,previous+1));
 owner=false;settled_expect(previous,RISC_DISPLAY_SETTLED_FAILED);owner=true;
 risc_display_surface_v1 f=acquire_frame();memcpy(f.pixels,previous_frame,FRAME_BYTES);
 ((uint8_t*)f.pixels)[0]^=0xFF;settled_expect(previous,RISC_DISPLAY_SETTLED_PENDING);
 assert(!ext->request_settle(NULL,previous));
 const uint64_t token=submit_frame(f,NULL,0,false);
 settled_expect(previous,RISC_DISPLAY_SETTLED_FAILED);settled_expect(token,RISC_DISPLAY_SETTLED_PENDING);
 assert(!ext->request_settle(NULL,token));
 owner_poll(8);settled_expect(token,RISC_DISPLAY_SETTLED_PENDING);complete_frame(token);
 settled_expect(token,RISC_DISPLAY_SETTLED_PENDING);
 if(!strcmp(scenario,"settled-request-unlock")){
  fail_unlock=true;assert(!ext->request_settle(NULL,token));assert(retained&&locked);return;
 }
 assert(ext->request_settle(NULL,token));
 if(!strcmp(scenario,"settled-unlock")) {
  fail_unlock=true;assert(ext->settled_status(NULL,token)==RISC_DISPLAY_SETTLED_FAILED);
  assert(retained&&locked);const unsigned io=exchanges;
  assert(ext->settled_status(NULL,token)==RISC_DISPLAY_SETTLED_FAILED&&io==exchanges);return;
 }
 if(!strcmp(scenario,"settled-fault")) {
  fail_spi_exchange=true;owner_poll(8);assert(presentation_fault);
  settled_expect(token,RISC_DISPLAY_SETTLED_FAILED);assert(!d->quiesce());return;
 }
 assert(!strcmp(scenario,"settled-lifecycle"));
 while(settle_stage){settled_expect(token,RISC_DISPLAY_SETTLED_PENDING);owner_poll(8);++tick;}
 settled_expect(token,RISC_DISPLAY_SETTLED_COMPLETE);assert(!screen_powered&&dtm1_synced);
 const risc_display_output_api_v1_power *power=risc_display_output_power(output);
 assert(power->prepare(NULL,1500)==RISC_DISPLAY_POWER_OK);settled_expect(token,RISC_DISPLAY_SETTLED_FAILED);
 assert(power->resume(NULL,1500)==RISC_DISPLAY_POWER_OK);settled_expect(token,RISC_DISPLAY_SETTLED_FAILED);
 baseline();settled_expect(pending_token,RISC_DISPLAY_SETTLED_COMPLETE);assert(d->quiesce());
 settled_expect(pending_token,RISC_DISPLAY_SETTLED_FAILED);
}
#ifdef TEST_X4_IDLE_POLICY
static void settled_planes(void) {
 for(unsigned i=0;i<12000u;++i)assert(old_ram[i]==0xFF&&ram[i]==0xFF);
 for(unsigned i=0;i<FRAME_BYTES;++i){
  assert(old_ram[i+12000u]==(uint8_t)~previous_frame[i]);
  assert(ram[i+12000u]==(uint8_t)~previous_frame[i]);
  assert(visible[i]==(uint8_t)~previous_frame[i]);
 }
}
#endif
