/* Ordinary X4 I2C bus: typed controller, clock, power and scoped sync.
 * Public i2c.bus@1 prefix and tagged safety suffix remain unchanged. */
#include <RiscI2cBusV1.h>
#include <RiscPlatformClockV1.h>
#include <RiscProviderSyncV1.h>
#include <TWatchPlatformV1.h>
#include <TWatchHardwareV1.h>
#include "../x4pro_board_power/PowerReadyV1.h"
#include <string.h>
#define CLAIMS 8u
#define TRANSFER_BYTES 256u
static const twatch_i2c_controller_v1 *raw;
static const risc_platform_clock_api_v1 *clock_api;
static const risc_provider_sync_api_v1 *sync_api;
static uint64_t controller,mutex,next_token=1,tokens[CLAIMS];
static uint8_t addresses[CLAIMS];
static bool started,retained;
static bool enter(void){return !retained && sync_api && mutex && sync_api->is_owner(sync_api->context) && sync_api->try_lock(sync_api->context,mutex);}
static bool leave(void){if(!sync_api->unlock(sync_api->context,mutex)){retained=true;return false;}return true;}
static bool claim_device(void *context,uint8_t address,uint64_t *out){
 (void)context;if(out)*out=0;
 if(!out || address<8 || address>0x77 || !enter())return false;
 size_t free_slot=CLAIMS;bool okay=started && next_token!=UINT64_MAX;
 for(size_t i=0;i<CLAIMS;++i){if(tokens[i] && addresses[i]==address)okay=false;if(!tokens[i] && free_slot==CLAIMS)free_slot=i;}
 okay=okay && free_slot<CLAIMS;
 if(okay){tokens[free_slot]=next_token++;addresses[free_slot]=address;*out=tokens[free_slot];}
 if(!leave()){if(okay){tokens[free_slot]=0;addresses[free_slot]=0;}*out=0;return false;}return okay;
}
static bool transact(void *context,uint64_t token,const uint8_t *tx,size_t tn,uint8_t *rx,size_t rn,uint32_t ms){
 (void)context;
 if(!token || (!tn && !rn) || tn>TRANSFER_BYTES || rn>TRANSFER_BYTES || (tn && !tx) || (rn && !rx) || !ms || ms>1000 || !clock_api || !sync_api || !sync_api->is_owner(sync_api->context))return false;
 const uint64_t began=clock_api->monotonic_ms(clock_api->context);
 if(began==UINT64_MAX || began>UINT64_MAX-ms || !enter())return false;
 const uint64_t deadline=began+ms,admitted=clock_api->monotonic_ms(clock_api->context);
 bool okay=false;
 if(started && admitted>=began && admitted<deadline){
  for(size_t i=0;i<CLAIMS;++i)if(tokens[i]==token){
   okay=raw->transfer(raw->context,controller,addresses[i],tx,tn,rx,rn,(uint32_t)(deadline-admitted));break;
  }
 }
 const uint64_t finished=clock_api->monotonic_ms(clock_api->context);
 okay=okay && finished>=admitted && finished<deadline;
 return leave() && okay;
}
static bool release_device(void *context,uint64_t token){
 (void)context;if(!token || !enter())return false;
 size_t slot=CLAIMS;uint8_t address=0;
 for(size_t i=0;i<CLAIMS;++i)if(tokens[i]==token){slot=i;address=addresses[i];tokens[i]=0;addresses[i]=0;break;}
 if(!leave()){
  /* False retains the exact public claim even if guard release failed. */
  if(slot<CLAIMS){tokens[slot]=token;addresses[slot]=address;}return false;
 }
 return slot<CLAIMS;
}
static bool quiesce(void){
 if(retained)return false;
 if(!mutex)return !controller;
 if(!enter())return false;
 for(size_t i=0;i<CLAIMS;++i)if(tokens[i]){(void)leave();return false;}
 started=false;
 if(controller){if(!raw->close(raw->context,controller)){(void)leave();return false;}controller=0;}
 if(!leave())return false;
 if(!sync_api->destroy(sync_api->context,mutex))return false;
 mutex=0;raw=NULL;clock_api=NULL;sync_api=NULL;return true;
}
static void stop(void){/* No fallible work after accepted quiescence. */}
static bool start(const risc_provider_dependency_v1 *deps,size_t count){
 if(started || retained || raw || clock_api || sync_api || mutex || controller || !deps || count!=5)return false;
 const risc_hardware_device_v1 *hardware=NULL;
 const twatch_i2c_controller_v1 *candidate=NULL;
 const risc_platform_clock_api_v1 *clock=NULL;
 const risc_provider_sync_api_v1 *sync=NULL;
 const x4_power_ready_api_v1 *power=NULL;
 for(size_t i=0;i<count;++i){
  if(!deps[i].capability_id || deps[i].api_version!=1 || !deps[i].api)return false;
  const char *name=deps[i].capability_id;
  if(!strcmp(name,"hardware.device") && !hardware)hardware=deps[i].api;
  else if(!strcmp(name,"platform.i2c.controller") && !candidate)candidate=deps[i].api;
  else if(!strcmp(name,"platform.clock") && !clock)clock=deps[i].api;
  else if(!strcmp(name,RISC_PROVIDER_SYNC_CAPABILITY) && !sync)sync=deps[i].api;
  else if(!strcmp(name,X4_POWER_READY_CAPABILITY) && !power)power=deps[i].api;
  else return false;
 }
 if(!hardware || hardware->api_version!=1 || hardware->struct_size<sizeof(*hardware) || !hardware->instance_id ||
    !hardware->compatible || strcmp(hardware->compatible,"espressif,esp32s3-i2c") || !hardware->revision || strcmp(hardware->revision,"unspecified") ||
    !hardware->config_type || strcmp(hardware->config_type,"controller.i2c") || hardware->config_version!=1 ||
    hardware->config_size!=sizeof(tw_hw_i2c_controller_v1) || !hardware->config ||
    !candidate || candidate->api_version!=1 || candidate->struct_size<sizeof(*candidate) || !candidate->open || !candidate->transfer || !candidate->close ||
    !clock || clock->api_version!=1 || clock->struct_size<sizeof(*clock) || !clock->monotonic_ms || !clock->sleep_ms ||
    !sync || sync->api_version!=1 || sync->struct_size<sizeof(*sync) || !sync->is_owner || !sync->create || !sync->try_lock || !sync->unlock || !sync->destroy ||
    !power || power->api_version!=1 || power->struct_size<sizeof(*power) || !power->ready)return false;
 const tw_hw_i2c_controller_v1 *config=hardware->config;
 if(config->struct_size!=sizeof(*config) || config->bus.struct_size!=sizeof(config->bus) ||
    config->bus.kind!=RISC_HW_BUS_I2C || !config->bus.instance_id || config->bus.controller>1 ||
    config->bus.sda!=39 || config->bus.scl!=38 || config->bus.sclk!=-1 || config->bus.mosi!=-1 || config->bus.miso!=-1 ||
    !config->bus.frequency_hz || config->bus.frequency_hz>400000 || config->bus.mode ||
    config->bus.reserved[0] || config->bus.reserved[1] || config->bus.reserved[2] ||
    !sync->is_owner(sync->context) || !power->ready(power->context))return false;
 raw=candidate;clock_api=clock;sync_api=sync;
 if(!sync_api->create(sync_api->context,&mutex) || !mutex){raw=NULL;clock_api=NULL;sync_api=NULL;return false;}
 if(!enter())return false;
 if(!raw->open(raw->context,(uint8_t)config->bus.controller,39,38,config->bus.frequency_hz,&controller) || !controller){retained=true;(void)leave();return false;}
 started=true;return leave();
}
static const risc_i2c_bus_contract_v1 api={
 {1,sizeof(api),NULL,claim_device,transact,release_device},
 RISC_I2C_BUS_CONTRACT_TAG,RISC_I2C_BUS_CONTRACT_V1,RISC_I2C_BUS_SAFE_CONTRACT_FLAGS
};
static const risc_driver_v2 driver={2,sizeof(driver),"x4pro-i2c","i2c.bus",1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}
