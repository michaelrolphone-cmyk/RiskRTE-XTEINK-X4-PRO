#include <RiscProviderV2.h>
#include <RiscTouchV1.h>
#include <RiscTouchPowerV1.h>
#include <RiscTouchI2cV2.h>
#include <RiscI2cBusV1.h>
#include <RiscPlatformClockV1.h>
#include <RiscProviderSyncV1.h>
#include <GardenPlatformV1.h>
#include "../drivers/x4pro_board_power/PowerReadyV1.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const risc_driver_v2 *driver;
static const risc_touch_api_v1 *api;
static const risc_touch_power_api_v1 *power;
static uint64_t serial=1,mutex,bus_token,time_ms;
static uint8_t bus_address,good_address=0x5d,status,raw[8];
static struct { uint64_t token; bool output,level,held; } pins[49];
static unsigned creates,takes,destroys,claims,writes,releases,holds,retires,bus_claims,bus_releases,transacts,power_checks;
static bool owner=true,locked,power_ready=true,create_ok=true,unlock_ok=true,destroy_ok=true;
static bool bus_release_ok=true,retire_ok=true,recurse,malformed_claim,ack_ok=true,read_ok=true,point_ok=true;
static int fail_gpio_claim,fail_write_pin=-1,fail_release_pin=-1,hold_result;
static int fail_write_call,fail_release_call,fail_bus_claim_call,fail_bus_release_call,fail_transact_call;
static int expire_operation;
static unsigned operations;
static bool reverse_clock,stalled_clock,ack_reaches,empty_claim,claim_token_on_failure;
static uint32_t sleep_overrun;
static char trace[32768];
static size_t trace_size;
static void record(const char *format,...) {
    ++operations;if((int)operations==expire_operation)time_ms+=2000;
    if(trace_size+100>=sizeof(trace))return;
    va_list args;va_start(args,format);
    int n=vsnprintf(trace+trace_size,sizeof(trace)-trace_size,format,args);va_end(args);
    assert(n>=0 && (size_t)n<sizeof(trace)-trace_size);trace_size+=(size_t)n;
}
static unsigned pin_of(uint64_t token) {
    for(unsigned i=0;i<49;++i)if(token && pins[i].token==token)return i;
    assert(!"Unknown GPIO token");return 0;
}
static void require_locked(void) { assert(owner && locked && mutex); }
static bool is_owner(void *context) { (void)context;return owner; }
static bool create(void *context,uint64_t *out) {
    (void)context;assert(owner && !mutex);++creates;*out=0;
    if(!create_ok)return false;
    *out=mutex=serial++;return true;
}
static bool take(void *context,uint64_t lock) {
    (void)context;assert(owner && lock==mutex);++takes;
    if(locked)return false;
    locked=true;return true;
}
static bool give(void *context,uint64_t lock) {
    (void)context;require_locked();assert(lock==mutex);
    if(!unlock_ok)return false;
    locked=false;return true;
}
static bool destroy(void *context,uint64_t lock) {
    (void)context;assert(owner && !locked && lock==mutex);++destroys;
    if(!destroy_ok)return false;
    mutex=0;return true;
}
static bool gpio_claim(void *context,uint8_t pin,bool output,bool initial,bool pull_up,uint64_t *out) {
    (void)context;require_locked();assert(pin==2 || pin==4 || pin==10);assert(!pins[pin].token && !pull_up);
    ++claims;*out=0;record("c%u%c%u;",pin,output?'o':'i',initial);
    if((int)claims==fail_gpio_claim && !claim_token_on_failure)return false;
    pins[pin].output=output;pins[pin].level=initial;pins[pin].held=false;
    *out=pins[pin].token=serial++;return (int)claims!=fail_gpio_claim;
}
static bool gpio_write(void *context,uint64_t token,bool level) {
    (void)context;require_locked();unsigned pin=pin_of(token);++writes;
    assert(pins[pin].output && !pins[pin].held);record("w%u:%u;",pin,level);
    if((int)pin==fail_write_pin || (int)writes==fail_write_call)return false;
    pins[pin].level=level;return true;
}
static bool gpio_release(void *context,uint64_t token) {
    (void)context;require_locked();unsigned pin=pin_of(token);++releases;
    assert(!pins[pin].held);record("r%u;",pin);
    if((int)pin==fail_release_pin || (int)releases==fail_release_call)return false;
    pins[pin].token=0;return true;
}
static int32_t gpio_hold(void *context,uint64_t token,bool enable) {
    (void)context;require_locked();unsigned pin=pin_of(token);++holds;
    assert(pin==2 && pins[pin].output && pins[pin].level);record(enable?"h%u;":"H%u;",pin);
    if(hold_result)return hold_result;
    pins[pin].held=enable;return 0;
}
static bool gpio_retire(void *context,uint64_t token) {
    (void)context;require_locked();unsigned pin=pin_of(token);++retires;
    assert(pin==2 && pins[pin].held && pins[pin].level && !pins[4].token && !pins[10].token);
    record("t%u;",pin);
    if(!retire_ok)return false;
    pins[pin].token=0;return true;
}
static void check_reentry(void) {
    if(!recurse)return;
    risc_touch_snapshot_v1 snapshot;memset(&snapshot,0xa5,sizeof(snapshot));
    risc_touch_event_v1 event;memset(&event,0xa5,sizeof(event));
    assert(!api->subscribe(NULL) && !api->unsubscribe(NULL,1));
    assert(!api->poll(NULL,1) && api->next(NULL,1,&event)==-1 && !api->snapshot(NULL,&snapshot));
    assert(snapshot.width==0xa5a5 && event.x==0xa5a5 && !driver->quiesce());
    assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_BUSY);
    assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_BUSY);
    driver->stop();
}
static bool claim_bus(void *context,uint8_t address,uint64_t *out) {
    (void)context;require_locked();assert(!bus_token && (address==0x5d || address==0x14));
    ++bus_claims;record("b%02x;",address);*out=0;
    if((int)bus_claims==fail_bus_claim_call)return false;
    if(empty_claim)return true;
    *out=bus_token=serial++;bus_address=address;
    return !malformed_claim;
}
static bool transact(void *context,uint64_t token,const uint8_t *tx,size_t tn,uint8_t *rx,size_t rn,uint32_t timeout) {
    (void)context;require_locked();assert(token==bus_token && tx && timeout>0 && timeout<=20);++transacts;
    check_reentry();if((int)transacts==fail_transact_call)return false;
    assert(tn==2 || tn==3);uint16_t reg=(uint16_t)(((uint16_t)tx[0]<<8)|tx[1]);
    if(rn) {
        assert(tn==2 && rx);record("d%04x;",reg);
        if(reg==0x8140) {
            assert(rn==4);
            memcpy(rx,bus_address==good_address?"9110":"bad!",4);return true;
        }
        if(reg==0x814e) { assert(rn==1);if(!read_ok)return false;*rx=status;return true; }
        assert(reg==0x8150 && rn==8);if(!point_ok)return false;memcpy(rx,raw,8);return true;
    }
    assert(tn==3 && !rx && reg==0x814e && tx[2]==0);record("a;");
    if(!ack_ok){if(ack_reaches)status=0;return false;}
    status=0;return true;
}
static bool release_bus(void *context,uint64_t token) {
    (void)context;require_locked();assert(token==bus_token);++bus_releases;record("u;");
    if(!bus_release_ok || (int)bus_releases==fail_bus_release_call)return false;
    bus_token=0;return true;
}
static uint64_t now(void *context) { (void)context;require_locked();if(reverse_clock)--time_ms;return time_ms; }
static void sleep_ms(void *context,uint32_t ms) { (void)context;require_locked();record("s%u;",ms);if(!stalled_clock)time_ms+=ms+sleep_overrun; }
static bool ready(void *context) { (void)context;assert(owner);++power_checks;return power_ready; }
static garden_gpio_v1 gpio={.api_version=1,.struct_size=sizeof(gpio),.claim=gpio_claim,.write=gpio_write,
    .release=gpio_release,.deep_sleep_hold=gpio_hold,.retire_held_output=gpio_retire};
static risc_provider_sync_api_v1 sync_api={1,sizeof(sync_api),NULL,is_owner,create,take,give,destroy};
static risc_platform_clock_api_v1 clock_api={1,sizeof(clock_api),NULL,now,sleep_ms};
static x4_power_ready_api_v1 power_api={1,sizeof(power_api),NULL,ready};
static risc_i2c_bus_contract_v1 bus={{1,sizeof(bus),NULL,claim_bus,transact,release_bus},
    RISC_I2C_BUS_CONTRACT_TAG,RISC_I2C_BUS_CONTRACT_V1,RISC_I2C_BUS_SAFE_CONTRACT_FLAGS};
static risc_hw_i2c_touch_v2 config={
    .base={.struct_size=sizeof(config),.bus={.struct_size=sizeof(risc_hw_bus_v1),.kind=RISC_HW_BUS_I2C,
        .instance_id=1,.frequency_hz=400000,.sclk=-1,.mosi=-1,.miso=-1,.sda=39,.scl=38},
        .width=480,.height=800,.address=0x5d,.reset=4,.irq=10,.reset_assert_ms=10,.reset_recovery_ms=10},
    .power=2,.irq_output=1,.alternate_address=0x14
};
static risc_hardware_device_v1 hardware={1,sizeof(hardware),1,"goodix,gt911","unspecified","touch.i2c",2,sizeof(config),&config};
static risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hardware},{"platform.gpio",1,&gpio},
    {"platform.sync",1,&sync_api},{"i2c.bus",1,&bus.base},{"platform.clock",1,&clock_api},{"board.power.ready",1,&power_api}};
static bool start(void) { return driver->start(deps,sizeof(deps)/sizeof(deps[0])); }
static void clean(void) {
    assert(!locked && !mutex && !bus_token && !pins[2].token && !pins[4].token && !pins[10].token);
    assert(pins[2].level && pins[2].held);
}
static void packet(uint8_t contact_count,uint16_t x,uint16_t y,bool home) {
    status=(uint8_t)(0x80u|contact_count|(home?0x10u:0));
    raw[0]=(uint8_t)x;raw[1]=(uint8_t)(x>>8);raw[2]=(uint8_t)y;raw[3]=(uint8_t)(y>>8);
    raw[4]=0xa5;raw[5]=0x5a;raw[6]=0xff;raw[7]=0xff;++time_ms;
}
static risc_touch_snapshot_v1 snapshot(void) {
    risc_touch_snapshot_v1 out={0};assert(api->snapshot(NULL,&out));assert(out.width==480 && out.height==800);return out;
}
static void expect_event(uint64_t token,uint8_t kind,uint16_t x,uint16_t y,uint64_t seq) {
    risc_touch_event_v1 out={0};assert(api->next(NULL,token,&out)==1);
    assert(out.kind==kind && out.x==x && out.y==y && out.sequence==seq && out.timestamp_ms<=time_ms);
    assert(out.id==((kind==RISC_TOUCH_EVENT_BUTTON_DOWN || kind==RISC_TOUCH_EVENT_BUTTON_UP)?0:1));
}
static void no_event(uint64_t token,int32_t result) {
    risc_touch_event_v1 out;memset(&out,0xa5,sizeof(out));
    assert(api->next(NULL,token,&out)==result && out.x==0xa5a5);
}
static void done(uint64_t token) {
    if(token)assert(api->unsubscribe(NULL,token));
    assert(driver->quiesce());clean();driver->stop();assert(driver->quiesce());
}
/* C11-friendly scalar rejection helper: save the whole envelope/config first. */
static void validation(void) {
    const risc_hardware_device_v1 h=hardware;
    const risc_hw_i2c_touch_v2 c=config;
    assert(!driver->start(NULL,6) && !driver->start(deps,5));
    hardware.api_version=2;assert(!start());hardware=h;
    hardware.struct_size--;assert(!start());hardware=h;
    hardware.instance_id=0;assert(!start());hardware=h;
    hardware.compatible="wrong";assert(!start());hardware=h;
    hardware.revision="wrong";assert(!start());hardware=h;
    hardware.config_type="wrong";assert(!start());hardware=h;
    hardware.config_version=1;assert(!start());hardware=h;
    hardware.config_size--;assert(!start());hardware=h;
    hardware.config=NULL;assert(!start());hardware=h;
    config.base.struct_size=sizeof(config.base);assert(!start());config=c;
    config.base.bus.struct_size--;assert(!start());config=c;
    config.base.bus.kind=RISC_HW_BUS_SPI;assert(!start());config=c;
    config.base.bus.instance_id=0;assert(!start());config=c;
    config.base.bus.controller=2;assert(!start());config=c;
    config.base.bus.frequency_hz=0;assert(!start());config=c;
    config.base.bus.frequency_hz=400001;assert(!start());config=c;
    config.base.bus.mode=1;assert(!start());config=c;
    config.base.bus.reserved[1]=1;assert(!start());config=c;
    config.base.bus.sda=38;assert(!start());config=c;
    config.base.bus.scl=39;assert(!start());config=c;
    config.base.bus.sclk=0;assert(!start());config=c;
    config.base.width=800;assert(!start());config=c;
    config.base.height=480;assert(!start());config=c;
    config.base.address=0x14;assert(!start());config=c;
    config.base.reset=10;assert(!start());config=c;
    config.base.irq=4;assert(!start());config=c;
    config.base.reset_active_high=1;assert(!start());config=c;
    config.base.irq_active_high=1;assert(!start());config=c;
    config.base.irq_pull_up=1;assert(!start());config=c;
    config.base.reset_assert_ms=11;assert(!start());config=c;
    config.base.reset_recovery_ms=11;assert(!start());config=c;
    config.power=1;assert(!start());config=c;
    config.power_active_high=1;assert(!start());config=c;
    config.irq_output=0;assert(!start());config=c;
    config.alternate_address=0;assert(!start());config=c;
    config.reserved[2]=1;assert(!start());config=c;
    bus.base.struct_size=sizeof(bus.base);assert(!start());bus.base.struct_size=sizeof(bus);
    bus.contract_tag=0;assert(!start());bus.contract_tag=RISC_I2C_BUS_CONTRACT_TAG;
    bus.contract_version=2;assert(!start());bus.contract_version=1;
    bus.contract_flags^=RISC_I2C_BUS_RETAINED_RELEASE;assert(!start());bus.contract_flags=RISC_I2C_BUS_SAFE_CONTRACT_FLAGS;
    bus.base.transact=NULL;assert(!start());bus.base.transact=transact;
    gpio.struct_size=GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE-1;assert(!start());gpio.struct_size=sizeof(gpio);
    gpio.retire_held_output=NULL;assert(!start());gpio.retire_held_output=gpio_retire;
    gpio.deep_sleep_hold=NULL;assert(!start());gpio.deep_sleep_hold=gpio_hold;
    gpio.claim=NULL;assert(!start());gpio.claim=gpio_claim;
    gpio.write=NULL;assert(!start());gpio.write=gpio_write;
    gpio.release=NULL;assert(!start());gpio.release=gpio_release;
    clock_api.sleep_ms=NULL;assert(!start());clock_api.sleep_ms=sleep_ms;
    clock_api.monotonic_ms=NULL;assert(!start());clock_api.monotonic_ms=now;
    sync_api.try_lock=NULL;assert(!start());sync_api.try_lock=take;
    power_api.ready=NULL;assert(!start());power_api.ready=ready;
    risc_provider_dependency_v1 saved=deps[5];deps[5]=deps[0];assert(!start());deps[5]=saved;
    deps[5].api_version=2;assert(!start());deps[5]=saved;
    owner=false;assert(!start());owner=true;
    assert(!power_checks && !creates && !claims && !bus_claims && !transacts && driver->quiesce());
    power_ready=false;assert(!start());power_ready=true;
    assert(power_checks==1 && !creates && !claims && !bus_claims);
    create_ok=false;assert(!start() && driver->quiesce() && !claims);create_ok=true;
    assert(start());done(0);
}
/* Independent source-0.1.5 state/queue oracle. The same report is processed
 * without GPIO, bus calls or the adapter's parsing/state-update functions. */
static risc_touch_snapshot_v1 reference;
static risc_touch_event_v1 reference_events[RISC_TOUCH_QUEUE_LENGTH];
static unsigned reference_count;
static bool reference_gap,reference_subscribed;
static void reference_emit(uint8_t kind,uint16_t x,uint16_t y) {
    risc_touch_event_v1 event={0};
    event.sequence=++reference.sequence;event.timestamp_ms=time_ms;
    event.kind=kind;event.id=(kind<=RISC_TOUCH_EVENT_UP)?1:0;event.x=x;event.y=y;
    if(!reference_subscribed || reference_gap)return;
    if(reference_count==RISC_TOUCH_QUEUE_LENGTH){reference_gap=true;reference_count=0;return;}
    reference_events[reference_count++]=event;
}
static bool reference_poll(void) {
    if(!read_ok)return false;
    if((status&128u)==0)return true;
    const unsigned count=status&15u;
    uint16_t x=0,y=0;
    if(count==1){
        if(!point_ok)return false;
        x=(uint16_t)(raw[0]+256u*raw[1]);y=(uint16_t)(raw[2]+256u*raw[3]);
    }
    if(count>1 || x>=480 || y>=800){
        ++reference.sequence;reference.contact_count=0;reference.buttons=0;
        reference.timestamp_ms=time_ms;reference_gap=true;reference_count=0;return false;
    }
    if(count==0 && reference.contact_count)reference_emit(RISC_TOUCH_EVENT_UP,reference.contacts[0].x,reference.contacts[0].y);
    if(count==1 && !reference.contact_count)reference_emit(RISC_TOUCH_EVENT_DOWN,x,y);
    if(count==1 && reference.contact_count && (reference.contacts[0].x!=x || reference.contacts[0].y!=y))
        reference_emit(RISC_TOUCH_EVENT_MOVE,x,y);
    if((status&16u)!=0 && !reference.buttons)reference_emit(RISC_TOUCH_EVENT_BUTTON_DOWN,0,0);
    if((status&16u)==0 && reference.buttons)reference_emit(RISC_TOUCH_EVENT_BUTTON_UP,0,0);
    reference.contact_count=(uint8_t)count;
    reference.contacts[0]=(risc_touch_contact_v1){(uint8_t)(count?1:0),0,x,y};
    reference.buttons=(status&16u)?1:0;reference.timestamp_ms=time_ms;
    return ack_ok;
}
static void compare_snapshot(void) {
    const risc_touch_snapshot_v1 actual=snapshot();
    assert(actual.sequence==reference.sequence && actual.timestamp_ms==reference.timestamp_ms);
    assert(actual.width==reference.width && actual.height==reference.height);
    assert(actual.contact_count==reference.contact_count && actual.buttons==reference.buttons);
    for(unsigned i=0;i<RISC_TOUCH_MAX_CONTACTS;++i)
        assert(actual.contacts[i].id==reference.contacts[i].id && actual.contacts[i].x==reference.contacts[i].x &&
            actual.contacts[i].y==reference.contacts[i].y && actual.contacts[i].reserved==reference.contacts[i].reserved);
}
static void compare_queue(uint64_t sub) {
    if(reference_gap){no_event(sub,-1);reference_gap=false;reference_count=0;}
    for(unsigned i=0;i<reference_count;++i){
        risc_touch_event_v1 actual={0};assert(api->next(NULL,sub,&actual)==1);
        const risc_touch_event_v1 *expected=&reference_events[i];
        assert(actual.sequence==expected->sequence && actual.timestamp_ms==expected->timestamp_ms &&
            actual.kind==expected->kind && actual.id==expected->id && actual.x==expected->x && actual.y==expected->y);
    }
    reference_count=0;no_event(sub,0);
}
static void source_equivalence(void) {
    assert(start());reference=snapshot();uint64_t sub=api->subscribe(NULL);assert(sub);reference_subscribed=true;
    uint32_t random=0x9911480u;
    for(unsigned i=0;i<20000;++i){
        random=random*1664525u+1013904223u;
        uint8_t contacts=(uint8_t)((random>>16)&1u);
        uint16_t x=(uint16_t)((random>>8)%480u),y=(uint16_t)((random>>12)%800u);
        if(i%101==0)contacts=2;
        if(i%103==0)x=480;
        if(i%107==0)y=800;
        packet(contacts,x,y,((random>>20)&1u)!=0);
        if(i%109==0)status&=0x7f;
        read_ok=i%179!=0;point_ok=i%181!=0;ack_ok=i%183!=0;
        bool expected=reference_poll();assert(api->poll(NULL,1u+i%16u)==expected);compare_snapshot();
        if(!ack_ok){ack_ok=true;expected=reference_poll();assert(api->poll(NULL,1)==expected);compare_snapshot();}
        if(i%127==0 || i%131==0)compare_queue(sub);
        if(i%499==0){
            assert(api->unsubscribe(NULL,sub));reference_subscribed=false;reference_count=0;reference_gap=false;
            packet(1,37,79,false);read_ok=point_ok=true;
            expected=reference_poll();assert(api->poll(NULL,1)==expected);compare_snapshot();
            sub=api->subscribe(NULL);assert(sub);reference_subscribed=true;reference_gap=false;reference_count=0;
            no_event(sub,0);
        }
    }
    compare_queue(sub);done(sub);
}
static void fenced(void) {
    const unsigned old=transacts;
    risc_touch_snapshot_v1 snap;memset(&snap,0xa5,sizeof(snap));
    assert(!api->subscribe(NULL) && !api->poll(NULL,1) && !api->snapshot(NULL,&snap));
    assert(snap.width==0xa5a5 && !api->unsubscribe(NULL,1));no_event(1,-1);
    assert(transacts==old && !start());
}
static void retained_forever(void) {
    const unsigned old=operations,locks=takes;
    assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_RETAINED);
    assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_RETAINED);
    assert(power->prepare(NULL,0)==RISC_TOUCH_POWER_RETAINED);
    assert(power->resume(NULL,0)==RISC_TOUCH_POWER_RETAINED);
    assert(!driver->quiesce());driver->stop();fenced();
    assert(operations==old && takes==locks && mutex && !destroys);
}
static void prepared(void) {
    const unsigned retired_before=retires,destroyed_before=destroys;
    assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_OK);
    assert(!bus_token && pins[2].token && pins[2].level && pins[2].held);
    assert(pins[4].token && pins[10].token && mutex && retires==retired_before && destroys==destroyed_before);
    const unsigned old=operations;
    assert(power->prepare(NULL,0)==RISC_TOUCH_POWER_OK);
    assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_OK);
    assert(power->resume(NULL,0)==RISC_TOUCH_POWER_BUSY);
    assert(operations==old);fenced();
}
static void recovered(void) {
    assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_OK);
    assert(bus_token && !pins[2].level && !pins[2].held && pins[4].level && !pins[10].output);
    const unsigned old=operations;
    assert(power->resume(NULL,0)==RISC_TOUCH_POWER_OK && power->resume(NULL,1000)==RISC_TOUCH_POWER_OK);
    assert(operations==old);risc_touch_snapshot_v1 snap=snapshot();
    assert(!snap.contact_count && !snap.buttons && !snap.contacts[0].id);
}
static void power_tests(const char *name) {
    assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_UNAVAILABLE);
    assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_UNAVAILABLE);
    assert(start());
    if(!strcmp(name,"power-subscriptions")) {
        uint64_t old=api->subscribe(NULL);packet(1,37,99,true);assert(api->poll(NULL,1));
        const unsigned count=operations;const risc_touch_snapshot_v1 before=snapshot();
        assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_BUSY);
        assert(power->prepare(NULL,0)==RISC_TOUCH_POWER_BUSY);
        assert(power->resume(NULL,0)==RISC_TOUCH_POWER_OK);
        assert(power->prepare(NULL,1001)==RISC_TOUCH_POWER_INVALID);
        assert(power->resume(NULL,1001)==RISC_TOUCH_POWER_INVALID);
        assert(operations==count && snapshot().sequence==before.sequence);
        expect_event(old,RISC_TOUCH_EVENT_DOWN,37,99,1);expect_event(old,RISC_TOUCH_EVENT_BUTTON_DOWN,0,0,2);
        assert(api->unsubscribe(NULL,old));prepared();recovered();
        uint64_t sub=api->subscribe(NULL);assert(sub>old);no_event(old,-1);no_event(sub,0);
        assert(!api->unsubscribe(NULL,old) && !driver->quiesce());done(sub);
    } else if(!strcmp(name,"power-owner")) {
        const unsigned old=operations,locks=takes;owner=false;
        assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_BUSY);
        assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_BUSY);
        assert(operations==old && takes==locks);owner=true;
        locked=true;assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_BUSY);locked=false;
        prepared();recurse=true;recovered();recurse=false;done(0);
    } else if(!strcmp(name,"power-neutral")) {
        uint64_t old=api->subscribe(NULL);packet(1,3,4,true);assert(api->poll(NULL,1));
        assert(api->unsubscribe(NULL,old));prepared();recovered();
        const uint64_t sub=api->subscribe(NULL);assert(sub>old);no_event(old,-1);
        const uint64_t sequence=snapshot().sequence;
        for(unsigned i=0;i<10;++i) {
            packet(1,(uint16_t)(3+i),4,true);assert(api->poll(NULL,1));no_event(sub,0);
            risc_touch_snapshot_v1 snap=snapshot();assert(!snap.contact_count && !snap.buttons && snap.sequence==sequence);
        }
        packet(0,0,0,true);assert(api->poll(NULL,1));no_event(sub,0);
        packet(1,1,1,false);assert(api->poll(NULL,1));no_event(sub,0);
        status=0;assert(api->poll(NULL,1));no_event(sub,0);
        packet(0,0,0,false);read_ok=false;assert(!api->poll(NULL,1));read_ok=true;
        ack_ok=false;ack_reaches=true;assert(!api->poll(NULL,1) && !status);no_event(sub,0);
        ack_ok=true;assert(api->poll(NULL,1));packet(1,17,29,true);assert(api->poll(NULL,1));no_event(sub,0);
        packet(2,0,0,false);assert(!api->poll(NULL,1));no_event(sub,-1);
        packet(1,480,0,false);assert(!api->poll(NULL,1));no_event(sub,-1);
        packet(1,5,6,true);point_ok=false;assert(!api->poll(NULL,1));point_ok=true;
        assert(api->poll(NULL,1));no_event(sub,0);
        packet(0,0,0,false);assert(api->poll(NULL,1));no_event(sub,0);
        const uint64_t fresh=snapshot().sequence;
        packet(1,17,29,true);assert(api->poll(NULL,1));
        expect_event(sub,RISC_TOUCH_EVENT_DOWN,17,29,fresh+1);expect_event(sub,RISC_TOUCH_EVENT_BUTTON_DOWN,0,0,fresh+2);
        done(sub);
    } else if(!strcmp(name,"power-prepare-release")) {
        const uint64_t saved=bus_token;const unsigned old=writes;bus_release_ok=false;
        assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_PLATFORM);
        assert(bus_token==saved && writes==old && !pins[2].level);fenced();
        assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_PLATFORM && bus_token==saved && writes==old);
        bus_release_ok=true;recovered();done(0);
    } else if(!strcmp(name,"power-off-retained")) {
        fail_write_pin=2;assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_RETAINED);
        assert(!bus_token && !holds && pins[2].token && pins[4].token && pins[10].token);
        fail_write_pin=-1;retained_forever();
    } else if(!strcmp(name,"power-hold-refusal")) {
        hold_result=RISC_DEEP_SLEEP_PLATFORM;
        assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_PLATFORM && !bus_token && pins[2].level && !pins[2].held);
        fenced();hold_result=0;recovered();done(0);
    } else if(!strcmp(name,"power-hold-retained")) {
        hold_result=RISC_DEEP_SLEEP_RETAINED;assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_RETAINED);
        hold_result=0;retained_forever();
    } else if(!strcmp(name,"power-unhold-retained") || !strcmp(name,"power-unhold-refusal")) {
        prepared();hold_result=!strcmp(name,"power-unhold-refusal")?RISC_DEEP_SLEEP_PLATFORM:RISC_DEEP_SLEEP_RETAINED;
        const unsigned old=writes;assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_RETAINED);
        assert(pins[2].held && writes==old);hold_result=0;retained_forever();
    } else if(!strncmp(name,"power-write-",12)) {
        unsigned which=(unsigned)atoi(name+12);assert(which>=1 && which<=7);
        prepared();good_address=0x14;fail_write_call=(int)writes+(int)which;
        assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_RETAINED);
        assert((int)writes==fail_write_call && pins[2].token && pins[4].token && pins[10].token);
        fail_write_call=0;retained_forever();
    } else if((!strncmp(name,"power-claim-",12) && name[12]>='1' && name[12]<='4')) {
        unsigned which=(unsigned)atoi(name+12);assert(which>=1 && which<=4);
        prepared();good_address=0x14;fail_gpio_claim=(int)claims+(int)which;
        claim_token_on_failure=which%2==0;
        assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_RETAINED);
        assert((int)claims==fail_gpio_claim && pins[2].token && pins[4].token);
        assert((pins[10].token!=0)==claim_token_on_failure);fail_gpio_claim=0;retained_forever();
    } else if(!strncmp(name,"power-pin-release-",18)) {
        unsigned which=(unsigned)atoi(name+18);assert(which>=1 && which<=4);
        prepared();good_address=0x14;fail_release_call=(int)releases+(int)which;
        assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_PLATFORM);
        const uint64_t saved=pins[10].token;const unsigned old=claims;assert(saved);fenced();
        fail_release_pin=10;assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_PLATFORM);
        assert(pins[10].token==saved && claims==old);fail_release_call=0;fail_release_pin=-1;recovered();done(0);
    } else if(!strcmp(name,"power-alternate-release")) {
        prepared();good_address=0x14;bus_release_ok=false;
        const unsigned old=bus_claims;assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_PLATFORM);
        uint64_t saved=bus_token;assert(saved && bus_address==0x5d && bus_claims==old+1);
        const unsigned old_writes=writes;
        assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_PLATFORM);
        assert(bus_token==saved && bus_claims==old+1 && writes==old_writes);
        bus_release_ok=true;recovered();assert(bus_address==0x14 && bus_claims==old+2);done(0);
    } else if(!strcmp(name,"power-second-release")) {
        prepared();good_address=0;fail_bus_release_call=(int)bus_releases+2;
        assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_PLATFORM && bus_token && bus_address==0x14);
        uint64_t saved=bus_token;unsigned old=bus_claims;bus_release_ok=false;
        assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_PLATFORM && bus_token==saved && bus_claims==old);
        bus_release_ok=true;fail_bus_release_call=0;
        assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_PLATFORM && !bus_token && bus_claims==old);
        good_address=0x5d;recovered();done(0);
    } else if(!strcmp(name,"power-primary-ack")) {
        prepared();fail_transact_call=(int)transacts+2;
        assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_PLATFORM && !bus_token);
        fenced();fail_transact_call=0;recovered();done(0);
    } else if(!strcmp(name,"power-custody")) {
        const uint64_t power_token=pins[2].token,reset_token=pins[4].token;
        uint64_t old_bus=bus_token;
        for(unsigned i=0;i<16;++i) {
            prepared();recovered();
            assert(pins[2].token==power_token && pins[4].token==reset_token && bus_token>old_bus);
            old_bus=bus_token;
        }
        done(0);
    } else if(!strcmp(name,"power-no-chip")) {
        prepared();good_address=0;const unsigned old=bus_claims;
        assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_PLATFORM && !bus_token && bus_claims==old+2);fenced();
        good_address=0x5d;recovered();done(0);
    } else if(!strcmp(name,"power-claim-refusal")) {
        prepared();good_address=0x14;fail_bus_claim_call=(int)bus_claims+1;recovered();assert(bus_address==0x14);done(0);
    } else if(!strcmp(name,"power-bus-retained") || !strcmp(name,"power-empty-claim")) {
        prepared();malformed_claim=!strcmp(name,"power-bus-retained");empty_claim=!malformed_claim;
        unsigned old=transacts;assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_RETAINED && transacts==old);
        malformed_claim=empty_claim=false;retained_forever();
    } else if(!strncmp(name,"power-transfer-",15)) {
        unsigned which=(unsigned)atoi(name+15);assert(which>=1 && which<=3);
        prepared();good_address=0x14;fail_transact_call=(int)transacts+(int)which;
        int32_t result=power->resume(NULL,1000);
        assert(result==(which==1?RISC_TOUCH_POWER_OK:RISC_TOUCH_POWER_PLATFORM));
        if(which!=1){assert(!bus_token);fenced();fail_transact_call=0;recovered();}done(0);
    } else if(!strcmp(name,"power-unlock-prepare") || !strcmp(name,"power-unlock-resume")) {
        if(!strcmp(name,"power-unlock-resume"))prepared();
        unlock_ok=false;assert((!strcmp(name,"power-unlock-resume")?power->resume(NULL,1000):power->prepare(NULL,1000))==RISC_TOUCH_POWER_RETAINED);
        unlock_ok=true;assert(locked);retained_forever();
    } else if(!strcmp(name,"power-zero")) {
        unsigned old=operations;assert(power->prepare(NULL,0)==RISC_TOUCH_POWER_BUSY);
        assert(power->resume(NULL,0)==RISC_TOUCH_POWER_OK && operations==old);
        prepared();recovered();prepared();done(0);
    } else if(!strcmp(name,"power-timeout")) {
        prepared();assert(power->resume(NULL,49)==RISC_TOUCH_POWER_TIMEOUT);
        assert(!pins[2].held && !pins[2].level && !bus_token);fenced();
        unsigned old=operations;assert(power->resume(NULL,0)==RISC_TOUCH_POWER_BUSY && operations==old);
        prepared();recovered();done(0);
    } else if(!strcmp(name,"power-clock-reverse")) {
        prepared();reverse_clock=true;unsigned old=operations;
        assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_TIMEOUT && operations==old);
        reverse_clock=false;fenced();recovered();done(0);
    } else if(!strcmp(name,"power-clock-stalled")) {
        prepared();stalled_clock=true;uint64_t old=time_ms;
        assert(power->resume(NULL,100)==RISC_TOUCH_POWER_TIMEOUT && time_ms==old);
        fenced();stalled_clock=false;recovered();done(0);
    } else if(!strcmp(name,"power-clock-overrun")) {
        prepared();sleep_overrun=1000;assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_TIMEOUT);
        sleep_overrun=0;fenced();recovered();done(0);
    } else if(!strcmp(name,"power-expiry-matrix") || !strcmp(name,"power-final-expiry-matrix")) {
        /* Each injected late clock reading covers a different completed GPIO,
         * hold, wait, claim, transfer or release stage on both addresses. */
        for(unsigned which=1;which<=33;++which) {
            prepared();good_address=0x14;expire_operation=(int)operations+(int)which;
            int32_t result=power->resume(NULL,1000);
            assert(result==RISC_TOUCH_POWER_TIMEOUT || result==RISC_TOUCH_POWER_OK);
            expire_operation=0;
            if(result){fenced();assert(power->prepare(NULL,0)==RISC_TOUCH_POWER_BUSY);
                if(!strcmp(name,"power-expiry-matrix"))recovered();}
            done(0);good_address=0x5d;if(which<33)assert(start());
        }
    } else if(!strcmp(name,"power-prepare-expiry-matrix")) {
        for(unsigned which=1;which<=3;++which) {
            expire_operation=(int)operations+(int)which;
            assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_TIMEOUT);
            expire_operation=0;fenced();recovered();done(0);if(which<3)assert(start());
        }
    } else if(!strcmp(name,"power-first-failure")) {
        prepared();good_address=0x14;bus_release_ok=false;
        expire_operation=(int)operations+17;
        const uint64_t before=time_ms;
        assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_PLATFORM && time_ms-before>1000);
        expire_operation=0;bus_release_ok=true;recovered();done(0);
    } else if(!strcmp(name,"power-final-prepared")) {
        prepared();fail_release_pin=10;uint64_t saved=pins[10].token;
        assert(!driver->quiesce() && pins[10].token==saved && pins[2].held);
        assert(power->resume(NULL,1000)==RISC_TOUCH_POWER_UNAVAILABLE);
        fail_release_pin=-1;done(0);
    } else if(!strcmp(name,"power-final-recovered")) {
        prepared();recovered();retire_ok=false;
        assert(!driver->quiesce() && pins[2].token && pins[2].held && !bus_token);
        assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_UNAVAILABLE);retire_ok=true;done(0);
    } else if(!strcmp(name,"power-final-partial")) {
        prepared();assert(power->resume(NULL,70)==RISC_TOUCH_POWER_TIMEOUT);fenced();done(0);
    } else assert(!"Unknown power scenario");
}
int main(int argc,char **argv) {
    assert(argc==2);driver=t5_driver_get(2);assert(driver && !t5_driver_get(1));api=driver->capability;
    assert(!strcmp(driver->driver_id,"x4pro-gt911") && !strcmp(driver->capability_id,"input.touch.raw"));
    assert(api->api_version==1 && api->struct_size==sizeof(risc_touch_power_api_v1));
    power=risc_touch_power(api);assert(power);
    risc_touch_snapshot_v1 out;memset(&out,0xa5,sizeof(out));
    assert(!api->subscribe(NULL) && !api->snapshot(NULL,&out) && !api->poll(NULL,1) && out.width==0xa5a5);
    if(!strncmp(argv[1],"power-",6))power_tests(argv[1]);
    else if(!strcmp(argv[1],"validation"))validation();
    else if(!strcmp(argv[1],"source-equivalence"))source_equivalence();
    else if(!strcmp(argv[1],"startup")) {
        pins[2].held=true;assert(start());
        assert(!strcmp(trace,"c2o0;s50;c10o0;c4o0;s10;w4:1;s10;w10:0;s50;r10;c10i0;s50;b5d;d8140;a;"));
        assert(time_ms==170 && bus_address==0x5d && !pins[2].held && !pins[2].level && pins[4].level && !pins[10].output);
        assert(!start());out=snapshot();assert(!out.sequence && !out.contact_count && !out.buttons && out.timestamp_ms==170);
        assert(!api->poll(NULL,0) && !api->poll(NULL,17));done(0);
        assert(strstr(trace,"u;w2:1;h2;r10;r4;t2;"));assert(start());done(0);
    } else if(!strcmp(argv[1],"fallback")) {
        good_address=0x14;assert(start() && time_ms==290 && bus_claims==2 && bus_releases==1);
        assert(strstr(trace,"b5d;d8140;u;r10;c10o1;w4:0;s10;w4:1;s10;w10:1;s50;r10;c10i0;s50;b14;d8140;a;"));done(0);
    } else if(!strcmp(argv[1],"no-chip")) {
        good_address=0;assert(!start() && bus_claims==2 && bus_releases==2 && !bus_token);
        assert(!api->snapshot(NULL,&out));done(0);
    } else if(!strcmp(argv[1],"subscriptions")) {
        assert(start());uint64_t sub=api->subscribe(NULL);assert(sub);
        uint64_t other[RISC_TOUCH_MAX_SUBSCRIBERS-1];
        for(unsigned i=0;i<RISC_TOUCH_MAX_SUBSCRIBERS-1;++i){other[i]=api->subscribe(NULL);assert(other[i]>sub);}
        assert(!api->subscribe(NULL));
        assert(!api->unsubscribe(NULL,UINT64_MAX) && !driver->quiesce());assert(api->snapshot(NULL,&out));
        for(unsigned i=0;i<RISC_TOUCH_MAX_SUBSCRIBERS-1;++i)assert(api->unsubscribe(NULL,other[i]));
        packet(1,7,9,false);assert(api->poll(NULL,1));expect_event(sub,RISC_TOUCH_EVENT_DOWN,7,9,1);
        assert(api->unsubscribe(NULL,sub));no_event(sub,-1);
        uint64_t newer=api->subscribe(NULL);assert(newer>sub);no_event(newer,0);out=snapshot();assert(out.contact_count==1);
        done(newer);assert(start());uint64_t newest=api->subscribe(NULL);assert(newest>newer);no_event(newer,-1);done(newest);
    } else if(!strcmp(argv[1],"parallel-consumers")) {
        assert(start());uint64_t ui=api->subscribe(NULL),hid=api->subscribe(NULL);assert(ui && hid && ui!=hid);
        packet(1,348,732,false);assert(api->poll(NULL,1));
        expect_event(ui,RISC_TOUCH_EVENT_DOWN,348,732,1);no_event(ui,0);
        assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_BUSY);
        assert(!driver->quiesce());
        expect_event(hid,RISC_TOUCH_EVENT_DOWN,348,732,1);no_event(hid,0);
        for(unsigned i=0;i<RISC_TOUCH_QUEUE_LENGTH+2;++i){
            packet(1,(uint16_t)(20+i),400,false);assert(api->poll(NULL,1));
            expect_event(ui,RISC_TOUCH_EVENT_MOVE,(uint16_t)(20+i),400,(uint64_t)i+2);
        }
        no_event(hid,-1);no_event(hid,0);no_event(ui,0);
        packet(0,0,0,false);assert(api->poll(NULL,1));
        risc_touch_event_v1 a={0},b={0};assert(api->next(NULL,ui,&a)==1&&api->next(NULL,hid,&b)==1);
        assert(!memcmp(&a,&b,sizeof(a))&&a.kind==RISC_TOUCH_EVENT_UP);
        assert(api->unsubscribe(NULL,ui));no_event(ui,-1);
        assert(power->prepare(NULL,1000)==RISC_TOUCH_POWER_BUSY && !driver->quiesce());
        uint64_t fresh=api->subscribe(NULL);assert(fresh>hid);no_event(fresh,0);
        packet(1,55,77,true);assert(api->poll(NULL,1));
        assert(api->next(NULL,hid,&a)==1&&api->next(NULL,fresh,&b)==1&&!memcmp(&a,&b,sizeof(a)));
        assert(api->unsubscribe(NULL,hid));done(fresh);
    } else if(!strcmp(argv[1],"events")) {
        assert(start());uint64_t sub=api->subscribe(NULL);assert(sub);
        packet(1,100,700,false);assert(api->poll(NULL,16));expect_event(sub,RISC_TOUCH_EVENT_DOWN,100,700,1);no_event(sub,0);
        packet(1,100,700,false);assert(api->poll(NULL,1));no_event(sub,0);
        packet(1,101,701,true);assert(api->poll(NULL,1));expect_event(sub,RISC_TOUCH_EVENT_MOVE,101,701,2);
        expect_event(sub,RISC_TOUCH_EVENT_BUTTON_DOWN,0,0,3);out=snapshot();assert(out.buttons==1 && out.contacts[0].id==1);
        packet(0,0,0,false);assert(api->poll(NULL,1));expect_event(sub,RISC_TOUCH_EVENT_UP,101,701,4);
        expect_event(sub,RISC_TOUCH_EVENT_BUTTON_UP,0,0,5);out=snapshot();assert(!out.contact_count && !out.buttons && out.sequence==5);
        status=0x11;unsigned old=transacts;assert(api->poll(NULL,1) && transacts==old+1);no_event(sub,0);done(sub);
    } else if(!strcmp(argv[1],"boundaries")) {
        assert(start());uint64_t sub=api->subscribe(NULL);packet(1,0,0,false);assert(api->poll(NULL,1));expect_event(sub,RISC_TOUCH_EVENT_DOWN,0,0,1);
        packet(1,479,799,false);assert(api->poll(NULL,1));expect_event(sub,RISC_TOUCH_EVENT_MOVE,479,799,2);
        packet(1,480,799,false);assert(!api->poll(NULL,1));no_event(sub,-1);out=snapshot();assert(!out.contact_count);
        packet(1,479,800,false);assert(!api->poll(NULL,1));no_event(sub,-1);
        packet(1,0xffff,0xffff,false);assert(!api->poll(NULL,1));no_event(sub,-1);done(sub);
    } else if(!strcmp(argv[1],"gaps")) {
        assert(start());uint64_t sub=api->subscribe(NULL);packet(1,8,9,true);assert(api->poll(NULL,1));
        packet(2,8,9,true);assert(!api->poll(NULL,1) && !status);no_event(sub,-1);no_event(sub,0);
        out=snapshot();assert(!out.contact_count && !out.buttons && out.sequence==3);
        packet(1,12,13,false);assert(api->poll(NULL,1));expect_event(sub,RISC_TOUCH_EVENT_DOWN,12,13,4);done(sub);
    } else if(!strcmp(argv[1],"overflow")) {
        assert(start());uint64_t sub=api->subscribe(NULL);
        for(unsigned i=0;i<RISC_TOUCH_QUEUE_LENGTH+3;++i){packet(1,(uint16_t)i,10,false);assert(api->poll(NULL,1));}
        out=snapshot();assert(out.contact_count==1 && out.sequence==35 && out.contacts[0].x==34);
        no_event(sub,-1);no_event(sub,0);packet(0,0,0,false);assert(api->poll(NULL,1));expect_event(sub,RISC_TOUCH_EVENT_UP,34,10,36);done(sub);
    } else if(!strcmp(argv[1],"ack-failure")) {
        assert(start());uint64_t sub=api->subscribe(NULL);packet(1,10,20,true);ack_ok=false;
        assert(!api->poll(NULL,1));out=snapshot();assert(out.sequence==2 && out.contact_count==1 && out.buttons==1);
        expect_event(sub,RISC_TOUCH_EVENT_DOWN,10,20,1);expect_event(sub,RISC_TOUCH_EVENT_BUTTON_DOWN,0,0,2);
        ack_ok=true;assert(api->poll(NULL,1));no_event(sub,0);out=snapshot();assert(out.sequence==2);done(sub);
    } else if(!strcmp(argv[1],"read-failure")) {
        assert(start());uint64_t sub=api->subscribe(NULL);packet(1,10,20,false);read_ok=false;
        assert(!api->poll(NULL,1));no_event(sub,0);out=snapshot();assert(!out.contact_count);
        read_ok=true;point_ok=false;assert(!api->poll(NULL,1));no_event(sub,0);out=snapshot();assert(!out.contact_count);
        point_ok=true;assert(api->poll(NULL,1));expect_event(sub,RISC_TOUCH_EVENT_DOWN,10,20,1);done(sub);
    } else if(!strcmp(argv[1],"owner-reentry")) {
        recurse=true;assert(start());uint64_t sub=api->subscribe(NULL);packet(1,100,200,false);assert(api->poll(NULL,1));recurse=false;
        unsigned old_takes=takes,old_transacts=transacts,old_writes=writes;owner=false;
        memset(&out,0xa5,sizeof(out));assert(!api->snapshot(NULL,&out) && out.width==0xa5a5 && !api->poll(NULL,1));
        assert(!api->subscribe(NULL) && !api->unsubscribe(NULL,sub) && !driver->quiesce());no_event(sub,-1);driver->stop();
        assert(takes==old_takes && transacts==old_transacts && writes==old_writes);owner=true;
        expect_event(sub,RISC_TOUCH_EVENT_DOWN,100,200,1);done(sub);
    } else if(!strcmp(argv[1],"release-retry")) {
        assert(start());uint64_t saved=bus_token;unsigned old=writes;bus_release_ok=false;
        assert(!driver->quiesce() && bus_token==saved && writes==old && !pins[2].level);
        assert(!api->subscribe(NULL) && !api->snapshot(NULL,&out) && !start());driver->stop();bus_release_ok=true;done(0);
    } else if(!strcmp(argv[1],"pin-release-retry")) {
        assert(start());uint64_t saved=pins[10].token;fail_release_pin=10;
        assert(!driver->quiesce() && !bus_token && pins[2].held && pins[10].token==saved && !retires);
        unsigned old=writes;fail_release_pin=-1;done(0);assert(writes==old);
    } else if(!strcmp(argv[1],"off-retry")) {
        assert(start());fail_write_pin=2;assert(!driver->quiesce() && !holds && pins[2].token && pins[4].token && pins[10].token);
        fail_write_pin=-1;done(0);
    } else if(!strcmp(argv[1],"hold-retry")) {
        assert(start());hold_result=RISC_DEEP_SLEEP_PLATFORM;assert(!driver->quiesce() && !retires && pins[2].level);
        hold_result=0;done(0);assert(holds==2);
    } else if(!strcmp(argv[1],"hold-retained")) {
        assert(start());hold_result=RISC_DEEP_SLEEP_RETAINED;assert(!driver->quiesce());unsigned old=writes;
        hold_result=0;assert(!driver->quiesce() && !start() && writes==old && !retires && pins[2].token);driver->stop();
    } else if(!strcmp(argv[1],"retire-retry")) {
        assert(start());retire_ok=false;uint64_t saved=pins[2].token;assert(!driver->quiesce() && pins[2].token==saved && pins[2].held);
        unsigned old=writes;retire_ok=true;done(0);assert(writes==old && retires==2);
    } else if(!strcmp(argv[1],"destroy-retry")) {
        assert(start());destroy_ok=false;assert(!driver->quiesce() && mutex && !pins[2].token && pins[2].held);
        unsigned old=writes;destroy_ok=true;done(0);assert(writes==old && destroys==2);
    } else if(!strcmp(argv[1],"claim-retained")) {
        fail_gpio_claim=1;assert(!start() && claims==1 && !bus_claims && !transacts && mutex);
        fail_gpio_claim=0;assert(!driver->quiesce() && !start() && !releases && !retires);driver->stop();
    } else if(!strcmp(argv[1],"irq-claim-retained")) {
        fail_gpio_claim=4;assert(!start() && claims==4 && !bus_claims && pins[2].token && pins[4].token && !pins[10].token);
        fail_gpio_claim=0;assert(!driver->quiesce() && !start() && releases==1);driver->stop();
    } else if(!strcmp(argv[1],"strap-release-retry")) {
        fail_release_pin=10;assert(!start() && !bus_claims && pins[10].token && pins[10].output);
        assert(!start());fail_release_pin=-1;done(0);assert(!bus_claims);
    } else if(!strcmp(argv[1],"reset-failure")) {
        fail_write_pin=4;assert(!start() && !bus_claims && claims==3 && time_ms==60);
        fail_write_pin=-1;done(0);
    } else if(!strcmp(argv[1],"probe-release-retry")) {
        good_address=0x14;bus_release_ok=false;assert(!start() && bus_token && bus_claims==1 && time_ms==170);
        assert(!start() && !driver->quiesce() && bus_claims==1 && time_ms==170);
        bus_release_ok=true;done(0);assert(bus_claims==1);
    } else if(!strcmp(argv[1],"bus-claim-retained")) {
        malformed_claim=true;assert(!start() && bus_token && bus_claims==1 && !transacts);
        assert(!driver->quiesce() && !start() && !bus_releases);driver->stop();
    } else if(!strcmp(argv[1],"unlock-retained")) {
        assert(start());unlock_ok=false;memset(&out,0xa5,sizeof(out));assert(!api->snapshot(NULL,&out) && out.width==0xa5a5);
        unlock_ok=true;assert(!driver->quiesce() && !start() && locked && mutex && !bus_releases);driver->stop();
    } else assert(!"Unknown scenario");
    printf("X4 ordinary GT911 PASS: %s\n",argv[1]);
    return 0;
}
