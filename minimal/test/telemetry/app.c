#include "PortableBroadcastClient.h"
#include "RiscProviderPromotionV1.h"
#include <assert.h>
extern unsigned telemetry_fixture_entry(void);
extern const char *telemetry_fixture_mode(void);
extern void telemetry_fixture_radio(unsigned);
extern void telemetry_fixture_check(unsigned);
extern void telemetry_fixture_fail_close(void);
extern void telemetry_fixture_fini(void);
__attribute__((visibility("default"))) int app_module_init(void){return 0;}
__attribute__((visibility("default"))) void app_module_fini(void){telemetry_fixture_fini();}
__attribute__((visibility("default"))) void app_main(void){
 unsigned entry=telemetry_fixture_entry();const char*mode=telemetry_fixture_mode();
 const risc_runtime_api_v1*rt=risc_runtime_get_api(1);assert(rt);
 if(!strcmp(mode,"timer")||entry==3)return;
 if(entry==1){
  risc_runtime_capability_v1 g={.struct_size=sizeof(g)};assert(rt->acquire(RISC_PROVIDER_PROMOTION_CAPABILITY,1,0,&g));
  const risc_provider_promotion_api_v1*p=g.api;assert(p->promote(p->context)==RISC_PROVIDER_PROMOTION_OK);assert(rt->release(&g));
 }
 /* Fourteen unrelated live grants plus service and its transient storage hit
  * the real 16-slot limit. The seventeenth must be denied cleanly. */
 risc_runtime_capability_v1 slots[14]={0};for(unsigned i=0;i<14;i++){slots[i].struct_size=sizeof(slots[i]);assert(rt->acquire("storage.key-value",1,1,&slots[i]));}
 portable_broadcast_client c;assert(portable_broadcast_open(&c,rt));
 risc_runtime_capability_v1 last={.struct_size=sizeof(last)},denied={.struct_size=sizeof(denied)};
 assert(rt->acquire("storage.key-value",1,1,&last));assert(!rt->acquire("storage.key-value",1,1,&denied));assert(rt->release(&last));
 if(entry==1){
  assert(portable_broadcast_step(&c,true));telemetry_fixture_check(0);
  assert(portable_broadcast_set_enabled(&c,true));assert(portable_broadcast_step(&c,true));telemetry_fixture_check(0);
  telemetry_fixture_radio(3);assert(portable_broadcast_pause(&c));
  for(unsigned i=0;i<10;i++){assert(portable_broadcast_step(&c,true));rt->yield_ms(20);}
  telemetry_fixture_check(1);
  if(!strcmp(mode,"retained")){telemetry_fixture_fail_close();assert(!portable_broadcast_pause(&c));assert(rt->retain_invocation());return;}
  assert(portable_broadcast_close(&c,rt));telemetry_fixture_check(1);
  for(unsigned i=0;i<14;i++)assert(rt->release(&slots[i]));
  assert(rt->request_launch("child.elf"));return;
 }
 telemetry_fixture_check(1);
 telemetry_fixture_radio(!strcmp(mode,"airplane")?4:1);assert(portable_broadcast_step(&c,true));telemetry_fixture_check(0);
 telemetry_broadcast_status_v1 status={.struct_size=sizeof(status)};assert(c.api->status(c.api->context,&status)&&status.enabled&&status.state==TELEMETRY_BROADCAST_BLE_OFF);
 assert(portable_broadcast_close(&c,rt));for(unsigned i=0;i<14;i++)assert(rt->release(&slots[i]));
}
