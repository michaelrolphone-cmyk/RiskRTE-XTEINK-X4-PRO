#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../drivers/x4pro_i2c/driver.c"
static bool owned=true,guard_held,guard_exists,unlock_ok=true,destroy_ok=true,power_ok=true,open_ok=true,close_ok=true,transfer_ok=true,reenter;
static uint64_t now,admission_cost,transfer_cost;
static unsigned opens,closes,transfers,power_reads;
static uint32_t forwarded;
static const risc_i2c_bus_api_v1 *bus;
static bool is_owner(void *c){(void)c;return owned;}
static bool create_lock(void *c,uint64_t *out){(void)c;*out=0;if(!owned||guard_exists)return false;guard_exists=true;*out=51;return true;}
static bool take_lock(void *c,uint64_t t){(void)c;assert(t==51);if(!owned||!guard_exists||guard_held)return false;guard_held=true;now+=admission_cost;return true;}
static bool give_lock(void *c,uint64_t t){(void)c;assert(t==51);if(!owned||!guard_held||!unlock_ok)return false;guard_held=false;return true;}
static bool destroy_lock(void *c,uint64_t t){(void)c;assert(t==51);if(!owned||guard_held||!guard_exists||!destroy_ok)return false;guard_exists=false;return true;}
static uint64_t time_ms(void *c){(void)c;return now;}
static void sleep_ms(void *c,uint32_t n){(void)c;now+=n;}
static bool rail_ready(void *c){(void)c;++power_reads;return power_ok;}
static bool open_bus(void *c,uint8_t unit,uint8_t sda,uint8_t scl,uint32_t hz,uint64_t *out){(void)c;++opens;assert(unit==0&&sda==39&&scl==38&&hz==100000);*out=open_ok?61:0;return open_ok;}
static bool close_bus(void *c,uint64_t t){(void)c;++closes;assert(t==61);return close_ok;}
static bool transfer(void *c,uint64_t t,uint8_t a,const uint8_t *tx,size_t tn,uint8_t *rx,size_t rn,uint32_t ms){
 (void)c;assert(t==61&&a==0x51&&tx&&tn==1&&rx&&rn==2&&ms);++transfers;forwarded=ms;
 if(reenter){uint64_t rejected=99;assert(!bus->claim_device(NULL,0x63,&rejected)&&!rejected);assert(!bus->release_device(NULL,tokens[0]));}
 now+=transfer_cost;if(transfer_ok){rx[0]=0x12;rx[1]=0x34;}return transfer_ok;
}
int main(int argc,char **argv){
 assert(argc==2);
 const twatch_i2c_controller_v1 hw={1,sizeof(hw),NULL,open_bus,transfer,close_bus};
 const risc_platform_clock_api_v1 clock={1,sizeof(clock),NULL,time_ms,sleep_ms};
 const risc_provider_sync_api_v1 sync={1,sizeof(sync),NULL,is_owner,create_lock,take_lock,give_lock,destroy_lock};
 const x4_power_ready_api_v1 power={1,sizeof(power),NULL,rail_ready};
 tw_hw_i2c_controller_v1 config={.struct_size=sizeof(config),.bus={.struct_size=sizeof(config.bus),.kind=2,.instance_id=101,.controller=0,.frequency_hz=100000,.sclk=-1,.mosi=-1,.miso=-1,.sda=39,.scl=38}};
 risc_hardware_device_v1 device={1,sizeof(device),2,"espressif,esp32s3-i2c","unspecified","controller.i2c",1,sizeof(config),&config};
 risc_provider_dependency_v1 deps[]={{"hardware.device",1,&device},{"platform.i2c.controller",1,&hw},{"platform.clock",1,&clock},{"platform.sync",1,&sync},{"board.power.ready",1,&power}};
 const risc_driver_v2 *driver=t5_driver_get(2);bus=driver->capability;assert(risc_i2c_bus_has_safe_contract(bus));
 if(!strcmp(argv[1],"validation")){
  assert(!driver->start(NULL,5)&&!driver->start(deps,4));
  config.bus.sda=1;assert(!driver->start(deps,5));config.bus.sda=39;
  config.bus.reserved[0]=1;assert(!driver->start(deps,5));config.bus.reserved[0]=0;
  owned=false;assert(!driver->start(deps,5));owned=true;
  power_ok=false;assert(!driver->start(deps,5));power_ok=true;
  deps[4]=deps[0];assert(!driver->start(deps,5));
  assert(!opens&&!guard_exists&&driver->quiesce());
 }else if(!strcmp(argv[1],"open-retained")){
  open_ok=false;assert(!driver->start(deps,5)&&opens==1);assert(!driver->quiesce());driver->stop();assert(!driver->start(deps,5));
 }else{
  assert(driver->start(deps,5)&&opens==1&&power_reads==1);
  uint64_t token=0;assert(bus->claim_device(NULL,0x51,&token)&&token);
  uint64_t duplicate=99;assert(!bus->claim_device(NULL,0x51,&duplicate)&&!duplicate);
  assert(!driver->quiesce()&&!closes);
  uint8_t tx=0,rx[2]={0};
  if(!strcmp(argv[1],"deadline")){
   admission_cost=4;transfer_cost=5;reenter=true;
   assert(bus->transact(NULL,token,&tx,1,rx,2,10)&&forwarded==6&&rx[0]==0x12);
   admission_cost=4;transfer_cost=6;
   assert(!bus->transact(NULL,token,&tx,1,rx,2,10));
   admission_cost=10;transfer_cost=0;unsigned before=transfers;
   assert(!bus->transact(NULL,token,&tx,1,rx,2,10)&&transfers==before);
   admission_cost=0;now=UINT64_MAX-2;
   assert(!bus->transact(NULL,token,&tx,1,rx,2,10)&&transfers==before);now=0;
  }else if(!strcmp(argv[1],"capacity")){
   uint64_t extra[7]={0};for(unsigned i=0;i<7;++i)assert(bus->claim_device(NULL,(uint8_t)(0x20+i),&extra[i]));
   uint64_t over=99;assert(!bus->claim_device(NULL,0x63,&over)&&!over);
   for(unsigned i=0;i<7;++i)assert(bus->release_device(NULL,extra[i]));
   next_token=UINT64_MAX;assert(!bus->claim_device(NULL,0x63,&over)&&!over);next_token=1000;
  }else if(!strcmp(argv[1],"release-retained")){
   unlock_ok=false;assert(!bus->release_device(NULL,token)&&tokens[0]==token);
   assert(!driver->quiesce()&&retained&&guard_held);puts("X4 typed I2C retained-release PASS");return 0;
  }else{
   assert(!strcmp(argv[1],"lifecycle"));
   owned=false;assert(!bus->transact(NULL,token,&tx,1,rx,2,10)&&!bus->release_device(NULL,token));owned=true;
   assert(!bus->transact(NULL,token,&tx,257,rx,2,10));
   assert(bus->transact(NULL,token,&tx,1,rx,2,10));
  }
  assert(bus->release_device(NULL,token));assert(!bus->release_device(NULL,token));
  close_ok=false;assert(!driver->quiesce()&&controller==61&&guard_exists);
  close_ok=true;destroy_ok=false;assert(!driver->quiesce()&&!controller&&guard_exists);
  destroy_ok=true;assert(driver->quiesce()&&!guard_exists);driver->stop();
  assert(driver->start(deps,5));uint64_t newer=0;assert(bus->claim_device(NULL,0x51,&newer)&&newer>token);
  assert(!bus->release_device(NULL,token)&&bus->release_device(NULL,newer));assert(driver->quiesce());
 }
 puts("X4 typed I2C provider PASS");
}
