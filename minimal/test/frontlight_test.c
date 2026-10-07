#include <RiscFrontlightV1.h>
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const risc_driver_v2 *driver;
static const risc_frontlight_api_v1 *api;
typedef struct { uint64_t token; bool held, high, pwm; uint16_t duty, maximum; } pad;
static pad pads[2] = {{.held=true}, {.held=true}};
static unsigned claims, writes, pwms, holds, retires, releases, creates, destroys, takes, gives;
static uint64_t next_token = 1, lock_token;
static bool owner = true, locked, unlock_ok = true, destroy_ok = true, create_ok = true, recurse;
static int fail_claim = -1, fail_write = -1, fail_pwm = -1, fail_hold = -1, fail_retire = -1;
static int hold_error = RISC_DEEP_SLEEP_PLATFORM;
static unsigned index_of(uint64_t token) {
    for (unsigned i=0; i<2; ++i) if (token && pads[i].token == token) return i;
    assert(!"Unknown GPIO token"); return 0;
}
static bool is_owner(void *c) { (void)c; return owner; }
static bool create(void *c, uint64_t *out) {
    (void)c; ++creates; assert(owner && !lock_token); *out=0;
    if (!create_ok) return false;
    *out=lock_token=next_token++; return true;
}
static bool take(void *c, uint64_t token) {
    (void)c; ++takes; assert(owner && token == lock_token);
    if (locked) return false;
    locked=true; return true;
}
static bool give(void *c, uint64_t token) {
    (void)c; ++gives; assert(owner && token == lock_token && locked);
    if (!unlock_ok) return false;
    locked=false; return true;
}
static bool destroy(void *c, uint64_t token) {
    (void)c; ++destroys; assert(owner && token == lock_token && !locked);
    if (!destroy_ok) return false;
    lock_token=0; return true;
}
static bool claim(void *c, uint8_t pin, bool output, bool initial, bool pullup, uint64_t *out) {
    (void)c; ++claims; assert(owner && locked && pin >= 8 && pin <= 9 && output && !initial && !pullup);
    unsigned i=pin-8; assert(!pads[i].token); *out=0;
    if ((int)i == fail_claim) return false;
    /* Model CPU staging LOW before releasing a previous/boot hold. */
    pads[i].high=false; pads[i].pwm=false; pads[i].held=false;
    *out=pads[i].token=next_token++; return true;
}
static bool write_pin(void *c, uint64_t token, bool high) {
    (void)c; ++writes; unsigned i=index_of(token); assert(owner && locked && !pads[i].held);
    if ((int)i == fail_write) return false;
    pads[i].high=high; pads[i].pwm=false; pads[i].duty=high?1024:0; return true;
}
static bool pwm_pin(void *c, uint64_t token, uint32_t hz, uint16_t duty, uint16_t maximum) {
    (void)c; ++pwms; unsigned i=index_of(token); assert(owner && locked && !pads[i].held);
    assert(hz == 25000 && maximum == 1024 && duty > 0 && duty < 1024);
    if (recurse) {
        uint16_t a=17,b=19;
        assert(!api->set_level(NULL,1,2) && !api->get_level(NULL,&a,&b) && a==17 && b==19);
        assert(!driver->quiesce());
    }
    pads[i].pwm=true; /* A native PWM failure may partially configure output. */
    if ((int)i == fail_pwm) return false;
    pads[i].duty=duty; pads[i].maximum=maximum; return true;
}
static bool read_pin(void *c, uint64_t token, bool *high) {
    (void)c; *high=pads[index_of(token)].high; return true;
}
static bool release_pin(void *c, uint64_t token) {
    (void)c; (void)token; ++releases; assert(!"Frontlight must retire held LOW, never release/unhold on stop"); return false;
}
static int32_t hold_pin(void *c, uint64_t token, bool enable) {
    (void)c; ++holds; unsigned i=index_of(token); assert(owner && locked);
    if (enable) assert(!pads[i].high && !pads[i].pwm); /* Static LOW before hold. */
    else assert(pads[i].held && !pads[i].high && !pads[i].pwm); /* No bright flash. */
    if ((int)i == fail_hold) return hold_error;
    pads[i].held=enable; return 0;
}
static bool retire(void *c, uint64_t token) {
    (void)c; ++retires; unsigned i=index_of(token); assert(owner && locked);
    assert(pads[i].held && !pads[i].high && !pads[i].pwm);
    if ((int)i == fail_retire) return false;
    pads[i].token=0; return true;
}
static garden_gpio_v1 gpio = {
    .api_version=1,.struct_size=sizeof(gpio),.claim=claim,.write=write_pin,.read=read_pin,.pwm=pwm_pin,
    .release=release_pin,.deep_sleep_hold=hold_pin,.retire_held_output=retire
};
static risc_provider_sync_api_v1 sync_api = {1,sizeof(sync_api),NULL,is_owner,create,take,give,destroy};
static risc_hw_gpio_bank_v1 config = {.struct_size=sizeof(config),.count=2,.active_high=1,.pins={8,9}};
static risc_hardware_device_v1 hardware = {1,sizeof(hardware),1,"xteink,x4-pro-frontlight","unspecified","gpio.bank",1,sizeof(config),&config};
static risc_provider_dependency_v1 deps[] = {{"hardware.device",1,&hardware},{"platform.gpio",1,&gpio},{"platform.sync",1,&sync_api}};
static bool start(void) { return driver->start(deps,3); }
static void check_level(uint16_t expected) {
    uint16_t value=0xffff,maximum=0;
    assert(api->get_level(NULL,&value,&maximum) && value==expected && maximum==1024);
}
static void check_dark(void) {
    for(unsigned i=0;i<2;++i) assert(pads[i].held && !pads[i].high && !pads[i].pwm);
}
static void check_clean(void) {
    assert(!lock_token && !locked && !pads[0].token && !pads[1].token && !releases);
    check_dark();
}
int main(int argc, char **argv) {
    assert(argc==2); driver=t5_driver_get(2); assert(driver && !t5_driver_get(1)); api=driver->capability;
    assert(!strcmp(driver->driver_id,"x4pro-frontlight") && !strcmp(driver->capability_id,"display.frontlight"));
    assert(api->api_version==1 && api->struct_size==sizeof(*api));
    uint16_t value=17,maximum=19;
    assert(!api->get_level(NULL,&value,&maximum) && value==17 && maximum==19);
    if (!strcmp(argv[1],"validation")) {
        assert(!driver->start(NULL,3) && !driver->start(deps,2));
        hardware.struct_size--; assert(!start()); hardware.struct_size++;
        hardware.instance_id=0; assert(!start()); hardware.instance_id=1;
        hardware.compatible="wrong"; assert(!start()); hardware.compatible="xteink,x4-pro-frontlight";
        hardware.revision="wrong"; assert(!start()); hardware.revision="unspecified";
        hardware.config_type="wrong"; assert(!start()); hardware.config_type="gpio.bank";
        hardware.config_size--; assert(!start()); hardware.config_size++;
        config.pins[1]=8; assert(!start()); config.pins[1]=9;
        config.active_high=0; assert(!start()); config.active_high=1;
        config.pull_up=1; assert(!start()); config.pull_up=0;
        config.debounce_us=1; assert(!start()); config.debounce_us=0;
        gpio.struct_size=GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE-1; assert(!start()); gpio.struct_size=sizeof(gpio);
        gpio.retire_held_output=NULL; assert(!start()); gpio.retire_held_output=retire;
        gpio.deep_sleep_hold=NULL; assert(!start()); gpio.deep_sleep_hold=hold_pin;
        sync_api.struct_size--; assert(!start()); sync_api.struct_size++;
        owner=false; assert(!start()); owner=true;
        deps[2]=deps[1]; assert(!start()); deps[2]=(risc_provider_dependency_v1){"platform.sync",1,&sync_api};
        assert(!creates && !claims && !writes && !pwms && !holds && !retires && driver->quiesce());
        create_ok=false; assert(!start() && !claims && driver->quiesce());
        create_ok=true; assert(start() && driver->quiesce()); check_clean();
    } else if (!strcmp(argv[1],"lifecycle")) {
        assert(start()); check_dark(); check_level(0); assert(!pwms && !start());
        assert(!api->set_level(NULL,0,0) && !api->set_level(NULL,2,1));
        assert(!api->get_level(NULL,NULL,&maximum) && !api->get_level(NULL,&value,NULL));
        assert(api->set_level(NULL,1,2)); check_level(512);
        for(unsigned i=0;i<2;++i) assert(pads[i].pwm && pads[i].duty==512 && !pads[i].held);
        assert(api->set_level(NULL,1,1)); check_level(1024);
        for(unsigned i=0;i<2;++i) assert(!pads[i].pwm && pads[i].high && !pads[i].held);
        assert(api->set_level(NULL,0,1)); check_level(0); check_dark();
        unsigned old_writes=writes,old_holds=holds;
        assert(api->set_level(NULL,0,65535) && writes==old_writes && holds==old_holds);
        recurse=true; assert(api->set_level(NULL,1,3)); recurse=false; check_level(341);
        unsigned old_takes=takes; owner=false;
        assert(!api->set_level(NULL,1,1) && !api->get_level(NULL,&value,&maximum) && !driver->quiesce());
        driver->stop(); assert(takes==old_takes); owner=true;
        assert(driver->quiesce()); check_clean(); driver->stop();
        old_holds=holds; assert(driver->quiesce() && holds==old_holds);
        assert(!api->set_level(NULL,1,1) && !api->get_level(NULL,&value,&maximum));
        assert(start()); check_dark(); check_level(0); assert(driver->quiesce()); check_clean();
    } else if (!strcmp(argv[1],"ratios")) {
        assert(start());
        const uint16_t maxima[]={1,2,3,255,256,1024,65535};
        for(unsigned m=0;m<sizeof(maxima)/sizeof(maxima[0]);++m) for(uint32_t n=0;n<=maxima[m];++n) {
            uint32_t expected=(n*1024u+maxima[m]/2u)/maxima[m];
            if(n && !expected) expected=1;
            if(n<maxima[m] && expected>=1024) expected=1023;
            assert(api->set_level(NULL,(uint16_t)n,maxima[m])); check_level((uint16_t)expected);
            for(unsigned i=0;i<2;++i) {
                if(!n) assert(pads[i].held && !pads[i].high && !pads[i].pwm);
                else if(n==maxima[m]) assert(pads[i].high && !pads[i].pwm && !pads[i].held);
                else assert(pads[i].pwm && pads[i].duty==expected && !pads[i].held);
            }
        }
        assert(driver->quiesce()); check_clean();
    } else if (!strcmp(argv[1],"retire-retry")) {
        assert(start() && api->set_level(NULL,3,4));
        uint64_t saved=pads[1].token; fail_retire=1;
        assert(!driver->quiesce()); check_dark(); assert(!pads[0].token && pads[1].token==saved && lock_token);
        assert(!api->set_level(NULL,1,1) && !api->get_level(NULL,&value,&maximum));
        unsigned old_writes=writes,old_holds=holds; driver->stop(); assert(!start());
        fail_retire=-1; assert(driver->quiesce() && writes==old_writes && holds==old_holds); check_clean();
    } else if (!strcmp(argv[1],"write-retry")) {
        assert(start() && api->set_level(NULL,1,1)); uint64_t saved=pads[1].token; fail_write=1;
        assert(!driver->quiesce() && !retires && pads[1].token==saved && pads[1].high);
        assert(!api->get_level(NULL,&value,&maximum)); fail_write=-1;
        assert(driver->quiesce()); check_clean();
    } else if (!strcmp(argv[1],"hold-retry")) {
        fail_hold=1; assert(!start() && pads[0].held && !pads[1].held && lock_token);
        assert(!driver->quiesce() && !retires); fail_hold=-1;
        assert(driver->quiesce()); check_clean();
    } else if (!strcmp(argv[1],"pwm-failure")) {
        assert(start()); fail_pwm=1;
        assert(!api->set_level(NULL,1,2)); check_dark(); assert(!api->get_level(NULL,&value,&maximum));
        assert(!api->set_level(NULL,1,1)); fail_pwm=-1;
        assert(driver->quiesce()); check_clean();
    } else if (!strcmp(argv[1],"destroy-retry")) {
        assert(start()); destroy_ok=false; assert(!driver->quiesce()); check_dark();
        assert(!pads[0].token && !pads[1].token && lock_token && !locked);
        unsigned old_writes=writes,old_retires=retires;
        destroy_ok=true; assert(driver->quiesce() && writes==old_writes && retires==old_retires); check_clean();
    } else if (!strcmp(argv[1],"claim-retained")) {
        fail_claim=1; assert(!start() && claims==2); check_dark();
        assert(!driver->quiesce() && !retires && lock_token && pads[0].token);
        fail_claim=-1; driver->stop(); assert(!start() && claims==2);
    } else if (!strcmp(argv[1],"unlock-retained")) {
        assert(start()); unlock_ok=false; assert(!api->set_level(NULL,0,1)); check_dark();
        assert(!driver->quiesce() && !api->get_level(NULL,&value,&maximum));
        unlock_ok=true; driver->stop(); assert(!start() && locked && lock_token && !destroys && !retires);
    } else if (!strcmp(argv[1],"hold-retained")) {
        assert(start()); fail_hold=1; hold_error=RISC_DEEP_SLEEP_RETAINED;
        assert(!api->set_level(NULL,1,2)); check_dark();
        fail_hold=-1; assert(!driver->quiesce() && !retires); driver->stop(); assert(!start());
    } else assert(!"Unknown scenario");
    printf("X4 ordinary frontlight PASS: %s\n",argv[1]);
}
