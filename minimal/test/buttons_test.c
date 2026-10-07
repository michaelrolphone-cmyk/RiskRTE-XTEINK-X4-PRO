#include <RiscInputNavigationV1.h>
#include <RiscProviderV2.h>
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include <RiscPlatformClockV1.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const risc_driver_v2 *driver;
static const risc_input_navigation_api_v1 *api;
static const uint8_t expected_pins[3]={0,7,3};
static const uint32_t expected_bits[3]={RISC_NAV_LEFT,RISC_NAV_RIGHT,RISC_NAV_CONFIRM};
static uint64_t tokens[3],next_token=1,lock_token;
static uint32_t physical;
static unsigned claims,reads,releases,creates,destroys,takes;
static bool owner=true,locked,create_ok=true,unlock_ok=true,destroy_ok=true,recurse;
static int fail_claim=-1,fail_read=-1,fail_release=-1;
static uint64_t now=100;
static bool clock_bad;
static uint64_t clock_now(void*c){(void)c;return clock_bad?UINT64_MAX:now;}
static const risc_platform_clock_api_v1 clock_api={1,sizeof(clock_api),NULL,clock_now,NULL};
static unsigned index_of(uint64_t token) {
    for(unsigned i=0;i<3;++i) if(token && tokens[i]==token) return i;
    assert(!"Unknown button token"); return 0;
}
static bool is_owner(void *c) { (void)c; return owner; }
static bool create(void *c,uint64_t *out) {
    (void)c; ++creates; assert(owner && !lock_token); *out=0;
    if(!create_ok) return false;
    *out=lock_token=next_token++; return true;
}
static bool take(void *c,uint64_t token) {
    (void)c; ++takes; assert(owner && token==lock_token);
    if(locked) return false;
    locked=true; return true;
}
static bool give(void *c,uint64_t token) {
    (void)c; assert(owner && token==lock_token && locked);
    if(!unlock_ok) return false;
    locked=false; return true;
}
static bool destroy(void *c,uint64_t token) {
    (void)c; ++destroys; assert(owner && token==lock_token && !locked);
    if(!destroy_ok) return false;
    lock_token=0; return true;
}
static bool claim(void *c,uint8_t pin,bool output,bool initial,bool pullup,uint64_t *out) {
    (void)c; assert(owner && locked && !output && !initial && pullup); *out=0;
    unsigned i=claims++%3; assert(pin==expected_pins[i] && !tokens[i]);
    if((int)i==fail_claim) return false;
    *out=tokens[i]=next_token++; return true;
}
static bool read_pin(void *c,uint64_t token,bool *high) {
    (void)c; ++reads; unsigned i=index_of(token); assert(owner && locked);
    if(recurse) {
        risc_input_navigation_frame_v1 frame={91,92,93};
        assert(!api->poll(NULL,&frame) && !api->reset(NULL) && !api->foreground(NULL,NULL,0));
        assert(!driver->quiesce() && frame.buttons==91 && frame.pressed==92 && frame.released==93);
    }
    if((int)i==fail_read) return false;
    *high=!(physical&expected_bits[i]); return true;
}
static bool release_pin(void *c,uint64_t token) {
    (void)c; ++releases; unsigned i=index_of(token); assert(owner && locked);
    if((int)i==fail_release) return false;
    tokens[i]=0; return true;
}
static garden_gpio_v1 gpio={.api_version=1,.struct_size=sizeof(gpio),.claim=claim,.read=read_pin,.release=release_pin};
static risc_provider_sync_api_v1 sync_api={1,sizeof(sync_api),NULL,is_owner,create,take,give,destroy};
static risc_hw_gpio_bank_v1 config={.struct_size=sizeof(config),.count=3,.pull_up=1,.pins={0,7,3}};
static risc_hardware_device_v1 hardware={1,sizeof(hardware),1,"xteink,x4-pro-buttons","unspecified","gpio.bank",1,sizeof(config),&config};
static risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hardware},{"platform.gpio",1,&gpio},{"platform.sync",1,&sync_api},{"platform.clock",1,&clock_api}};
static bool start(void) { return driver->start(deps,config.long_press_us?4:3); }
static void expect(uint32_t buttons,uint32_t pressed,uint32_t released) {
    risc_input_navigation_frame_v1 frame={91,92,93};
    now+=5;
    assert(api->poll(NULL,&frame));
    assert(frame.buttons==buttons && frame.pressed==pressed && frame.released==released);
}
static void clean(void) { assert(!lock_token && !locked && !tokens[0] && !tokens[1] && !tokens[2]); }
/* Independent source-0.1.5 state machine oracle, fed the same raw physical
 * frame; every adapter result must preserve its polling-count semantics. */
static uint32_t ref_previous,ref_pending;
static uint8_t ref_stable;
static bool ref_neutral;
static void ref_reset(void) { ref_previous=ref_pending=0;ref_stable=0;ref_neutral=physical!=0; }
static risc_input_navigation_frame_v1 ref_poll(void) {
    risc_input_navigation_frame_v1 out={0,0,0};
    if(ref_neutral) {
        if(physical) ref_stable=0;
        else if(ref_stable<3) ++ref_stable;
        if(ref_stable>=3) { ref_neutral=false;ref_stable=0; }
        return out;
    }
    if(physical!=ref_pending) { ref_pending=physical;ref_stable=0; }
    else if(ref_stable<3) ++ref_stable;
    if(ref_stable>=3 && ref_pending!=ref_previous) {
        out.pressed=ref_pending&~ref_previous;out.released=ref_previous&~ref_pending;ref_previous=ref_pending;
    }
    out.buttons=ref_previous;return out;
}
int main(int argc,char **argv) {
    assert(argc==2);driver=t5_driver_get(2);assert(driver && !t5_driver_get(1));api=driver->capability;
    assert(!strcmp(driver->driver_id,"x4pro-buttons") && !strcmp(driver->capability_id,"input.navigation"));
    assert(api->api_version==1 && risc_input_navigation_has_physical_page_pair(api));
    risc_input_navigation_frame_v1 frame={91,92,93};
    assert(!api->poll(NULL,&frame) && !api->reset(NULL) && frame.buttons==91);
    if(!strcmp(argv[1],"validation")) {
        assert(!driver->start(NULL,3) && !driver->start(deps,2));
        hardware.instance_id=0;assert(!start());hardware.instance_id=1;
        hardware.compatible="wrong";assert(!start());hardware.compatible="xteink,x4-pro-buttons";
        hardware.revision="wrong";assert(!start());hardware.revision="unspecified";
        hardware.config_type="wrong";assert(!start());hardware.config_type="gpio.bank";
        hardware.config_version=2;assert(!start());hardware.config_version=1;
        hardware.config_size--;assert(!start());hardware.config_size++;
        config.count=2;assert(!start());config.count=3;
        config.pins[1]=3;assert(!start());config.pins[1]=7;
        config.active_high=1;assert(!start());config.active_high=0;
        config.pull_up=0;assert(!start());config.pull_up=1;
        config.debounce_us=1;assert(!start());config.debounce_us=0;
        config.long_press_us=1;assert(!start());config.long_press_us=0;
        gpio.struct_size=offsetof(garden_gpio_v1,release);assert(!start());gpio.struct_size=sizeof(gpio);
        gpio.read=NULL;assert(!start());gpio.read=read_pin;
        sync_api.try_lock=NULL;assert(!start());sync_api.try_lock=take;
        deps[2]=deps[1];assert(!start());deps[2]=(risc_provider_dependency_v1){"platform.sync",1,&sync_api};
        owner=false;assert(!start());owner=true;
        assert(!creates && !claims && !reads && !releases && driver->quiesce());
        create_ok=false;assert(!start() && driver->quiesce() && !claims);create_ok=true;
        assert(start() && driver->quiesce());clean();
    } else if(!strcmp(argv[1],"crown")) {
        config.long_press_us=1000000;assert(!driver->start(deps,3));
        assert(start());expect(0,0,0);
        physical=RISC_NAV_CONFIRM;for(unsigned j=0;j<5;++j)expect(0,0,0);
        now+=100;physical=0;for(unsigned j=0;j<3;++j)expect(0,0,0);
        expect(0,RISC_NAV_HOME,RISC_NAV_HOME);expect(0,0,0);
        /* Long, combined, reset-boundary, and failed samples never become Home. */
        physical=RISC_NAV_CONFIRM;for(unsigned j=0;j<4;++j)expect(0,0,0);
        now+=1000;physical=0;for(unsigned j=0;j<5;++j)expect(0,0,0);
        physical=RISC_NAV_CONFIRM;for(unsigned j=0;j<4;++j)expect(0,0,0);
        physical|=RISC_NAV_LEFT;expect(0,0,0);physical=RISC_NAV_CONFIRM;expect(0,0,0);
        physical=0;for(unsigned j=0;j<5;++j)expect(0,0,0);
        physical=RISC_NAV_CONFIRM;for(unsigned j=0;j<4;++j)expect(0,0,0);
        assert(api->reset(NULL));physical=0;for(unsigned j=0;j<5;++j)expect(0,0,0);
        physical=RISC_NAV_CONFIRM;for(unsigned j=0;j<4;++j)expect(0,0,0);
        clock_bad=true;assert(!api->poll(NULL,&frame));clock_bad=false;
        physical=0;for(unsigned j=0;j<5;++j)expect(0,0,0);
        physical=RISC_NAV_CONFIRM;for(unsigned j=0;j<4;++j)expect(0,0,0);
        fail_read=1;assert(!api->poll(NULL,&frame));fail_read=-1;
        physical=0;for(unsigned j=0;j<5;++j)expect(0,0,0);
        physical=RISC_NAV_LEFT;for(unsigned j=0;j<3;++j)expect(0,0,0);expect(RISC_NAV_LEFT,RISC_NAV_LEFT,0);
        assert(driver->quiesce());clean();
        physical=RISC_NAV_CONFIRM;assert(start());for(unsigned j=0;j<5;++j)expect(0,0,0);
        physical=0;for(unsigned j=0;j<5;++j)expect(0,0,0);assert(driver->quiesce());clean();
    } else if(!strcmp(argv[1],"mapping")) {
        assert(start() && !start() && claims==3);expect(0,0,0);
        for(unsigned i=0;i<3;++i) {
            physical=expected_bits[i];
            for(unsigned j=0;j<3;++j) expect(0,0,0);
            expect(physical,physical,0);expect(physical,0,0);
            physical=0;
            for(unsigned j=0;j<3;++j) expect(expected_bits[i],0,0);
            expect(0,0,expected_bits[i]);expect(0,0,0);
        }
        physical=RISC_NAV_LEFT|RISC_NAV_RIGHT|RISC_NAV_CONFIRM;
        for(unsigned j=0;j<3;++j) expect(0,0,0);
        expect(physical,physical,0);
        assert(api->foreground(NULL,NULL,0));
        assert(!api->poll(NULL,NULL));recurse=true;expect(physical,0,0);recurse=false;
        unsigned old_takes=takes,old_reads=reads;owner=false;
        assert(!api->poll(NULL,&frame) && !api->reset(NULL) && !api->foreground(NULL,NULL,0) && !driver->quiesce());
        driver->stop();assert(takes==old_takes && reads==old_reads);owner=true;
        assert(driver->quiesce());clean();driver->stop();assert(driver->quiesce());
        assert(!api->poll(NULL,&frame) && !api->reset(NULL));physical=0;
        assert(start());expect(0,0,0);assert(driver->quiesce());clean();
    } else if(!strcmp(argv[1],"neutral-reset")) {
        physical=RISC_NAV_CONFIRM;assert(start());
        for(unsigned i=0;i<8;++i) expect(0,0,0);
        physical=0;expect(0,0,0);expect(0,0,0);
        physical=RISC_NAV_LEFT;expect(0,0,0); /* Two neutral polls do not rearm. */
        physical=0;for(unsigned i=0;i<3;++i) expect(0,0,0);
        physical=RISC_NAV_LEFT;for(unsigned i=0;i<3;++i) expect(0,0,0);expect(physical,physical,0);
        assert(api->reset(NULL));expect(0,0,0); /* No synthetic release at reset. */
        physical=RISC_NAV_RIGHT;for(unsigned i=0;i<5;++i) expect(0,0,0);
        physical=0;for(unsigned i=0;i<3;++i) expect(0,0,0);
        physical=RISC_NAV_RIGHT;for(unsigned i=0;i<3;++i) expect(0,0,0);expect(physical,physical,0);
        assert(driver->quiesce());clean();
    } else if(!strcmp(argv[1],"source-equivalence")) {
        physical=RISC_NAV_CONFIRM;assert(start());ref_reset();uint32_t random=0x521041u;
        for(unsigned i=0;i<20000;++i) {
            random=random*1664525u+1013904223u;
            if(i%5==0) physical=(random>>16)&(RISC_NAV_LEFT|RISC_NAV_RIGHT|RISC_NAV_CONFIRM);
            if(i%53==0) { assert(api->reset(NULL));ref_reset(); }
            if(i%47==0) assert(api->foreground(NULL,NULL,0));
            risc_input_navigation_frame_v1 want=ref_poll();expect(want.buttons,want.pressed,want.released);
        }
        assert(driver->quiesce());clean();
    } else if(!strcmp(argv[1],"read-failure")) {
        assert(start());physical=RISC_NAV_LEFT;expect(0,0,0);expect(0,0,0);expect(0,0,0);
        fail_read=1;assert(!api->poll(NULL,&frame) && frame.buttons==91 && frame.pressed==92 && frame.released==93);
        assert(!api->reset(NULL));fail_read=-1;expect(physical,physical,0);
        assert(driver->quiesce());clean();
    } else if(!strcmp(argv[1],"start-read-failure")) {
        fail_read=1;assert(!start() && claims==3 && lock_token);
        assert(!api->poll(NULL,&frame));fail_read=-1;
        assert(driver->quiesce());clean();assert(start() && driver->quiesce());clean();
    } else if(!strcmp(argv[1],"release-retry")) {
        assert(start());uint64_t saved=tokens[1];fail_release=1;
        assert(!driver->quiesce() && !tokens[0] && tokens[1]==saved && !tokens[2] && lock_token);
        assert(!api->poll(NULL,&frame) && !api->reset(NULL) && !api->foreground(NULL,NULL,0));
        driver->stop();assert(!start());fail_release=-1;
        assert(driver->quiesce() && releases==4);clean();
    } else if(!strcmp(argv[1],"destroy-retry")) {
        assert(start());destroy_ok=false;assert(!driver->quiesce() && releases==3 && lock_token);
        destroy_ok=true;assert(driver->quiesce() && releases==3);clean();
    } else if(!strcmp(argv[1],"claim-retained")) {
        fail_claim=1;assert(!start() && claims==2 && tokens[0] && !tokens[1] && lock_token);
        assert(!driver->quiesce() && !releases);driver->stop();fail_claim=-1;assert(!start());
    } else if(!strcmp(argv[1],"unlock-retained")) {
        assert(start());unlock_ok=false;assert(!api->poll(NULL,&frame));
        assert(frame.buttons==91 && frame.pressed==92 && frame.released==93);
        unlock_ok=true;assert(!driver->quiesce() && !releases && locked && lock_token);
        driver->stop();assert(!start() && !destroys);
    } else assert(!"Unknown scenario");
    printf("X4 ordinary buttons PASS: %s\n",argv[1]);
}
