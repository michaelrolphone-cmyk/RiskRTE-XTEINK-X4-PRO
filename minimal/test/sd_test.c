/* Production SD transport + shared FatFs, replacing only typed GPIO/clock/sync.
 * The pinned Reader native-card wire model remains the single implementation. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static uint32_t fixture_cycles(void);
#define X4PRO_SD_CYCLE_COUNT() fixture_cycles()
#include "../drivers/x4pro_sd/driver.c"
#undef x4pro_sd_command
#undef x4pro_sd_crc16
#define x4pro_pin_output wire_pin_output
#define x4pro_pin_release wire_pin_release
#define x4pro_pin_read wire_pin_read
#define x4pro_pin_input wire_pin_input
#define x4pro_pin_hold wire_pin_hold
#undef X4PRO_SD_CYCLE_COUNT
#include <test/storage_volume/fake/x4pro_mmio.h>
#undef x4pro_pin_output
#undef x4pro_pin_release
#undef x4pro_pin_read
#undef x4pro_pin_input
#undef x4pro_pin_hold
uint8_t *card_image;
uint32_t card_sectors = 131072;
bool card_bad_crc, card_reject_write, card_busy_forever, card_bad_pin;
unsigned card_reads, card_writes;
bool card_sleep_off, card_power_off;
unsigned card_sleep_commits;
static uint64_t now_ms, next_token=100, fixture_lock;
static uint32_t cycles;
static unsigned calls, claims, releases, locks, unlocks, destroys, sleeps, clock_edges;
static bool owner=true, locked, fail_create, fail_take, fail_unlock, fail_destroy;
static bool fail_claim, fail_release, fail_read, fail_write, stuck_cycles, absent, reenter, nested;
static bool clock_level, power_ready=true;
static uint64_t tokens[49];
static bool outputs[49], levels[49], holds[49];
static uint32_t fixture_cycles(void) { if (!stuck_cycles) cycles+=8; return cycles; }
uint32_t x4pro_sd_test_cycle_count(void) { return fixture_cycles(); }
static int token_pin(uint64_t token) { for (unsigned i=0;i<49;++i) if(tokens[i]==token) return (int)i; assert(!"invalid GPIO token"); return -1; }
static bool gpio_claim(void *ctx,uint8_t pin,bool output,bool initial,bool pullup,uint64_t *out) {
    (void)ctx; ++calls; ++claims; assert(owner && locked && fixture_lock && out);
    assert(pin==5 || pin==40 || pin==41 || pin==42); assert(!tokens[pin]); assert(!pullup || (!output && (pin==40 || pin==42))); *out=0;
    if(fail_claim)return false;
    tokens[pin]=*out=++next_token;outputs[pin]=output;levels[pin]=initial;holds[pin]=false;
    if(pin==5){wire_pin_hold(5,false);wire_pin_output(5,initial);}
    else if(output){if(pin==41){clock_level=initial;++clock_edges;}wire_pin_output(pin,initial);}
    else if(pullup)wire_pin_release(pin);else wire_pin_input(pin,false);
    return true;
}
static bool gpio_write(void *ctx,uint64_t token,bool level) {
    (void)ctx; ++calls; assert(owner && locked);int pin=token_pin(token);assert(outputs[pin]);
    if(fail_write || holds[pin])return false;
    levels[pin]=level;
    if(pin==41){clock_level=level;++clock_edges;}
    wire_pin_output((uint32_t)pin,level);return true;
}
static bool gpio_read(void *ctx,uint64_t token,bool *level) {
    (void)ctx; ++calls;assert(owner && locked);int pin=token_pin(token);
    if(fail_read)return false;
    *level=pin==41?clock_level:pin==5?levels[pin]:absent?true:wire_pin_read((uint32_t)pin);return true;
}
static bool gpio_release(void *ctx,uint64_t token) {
    (void)ctx; ++calls;++releases;assert(owner && locked);int pin=token_pin(token);
    if(fail_release || holds[pin])return false;
    tokens[pin]=0;return true;
}
static int32_t gpio_hold(void *ctx,uint64_t token,bool hold) {
    (void)ctx;++calls;assert(owner && locked);int pin=token_pin(token);assert(pin==5 && outputs[pin]);
    if(fail_write)return RISC_DEEP_SLEEP_PLATFORM;
    holds[pin]=hold;wire_pin_hold((uint32_t)pin,hold);return 0;
}
static bool sync_owner(void *ctx){(void)ctx;return owner;}
static bool sync_create(void *ctx,uint64_t *out){(void)ctx;assert(owner && !fixture_lock);*out=0;if(fail_create)return false;*out=fixture_lock=++next_token;return true;}
static bool sync_take(void *ctx,uint64_t token){(void)ctx;++locks;assert(token==fixture_lock);if(!owner || locked || fail_take)return false;locked=true;return true;}
static bool sync_unlock(void *ctx,uint64_t token){(void)ctx;++unlocks;assert(token==fixture_lock);if(!owner || !locked || fail_unlock)return false;locked=false;return true;}
static bool sync_destroy(void *ctx,uint64_t token){(void)ctx;++destroys;assert(owner && !locked && token==fixture_lock);if(fail_destroy)return false;fixture_lock=0;return true;}
static bool power_is_ready(void *ctx){(void)ctx;return power_ready;}
static uint64_t monotonic(void *ctx){(void)ctx;return now_ms;}
static void rejected_calls(void) {
    const unsigned before=calls;char text[80]="untouched",byte=0;uint64_t size=0,position=0;bool directory=false;risc_storage_dirent_v1 entry;
    assert(!refresh(NULL) && !ready(NULL) && !label(NULL,text,sizeof(text)));
    assert(!stat_path(NULL,"/",&size,&directory));assert(!dir_open(NULL,"/") && !dir_next(NULL,1,&entry));
    assert(!dir_rewind(NULL,1) && !dir_close_checked(NULL,1));dir_close(NULL,1);
    assert(!file_open_read(NULL,"/a",&size) && !file_open_write(NULL,"/a") && !file_open(NULL,"/a",1));
    assert(!file_read(NULL,1,&byte,1) && !file_write(NULL,1,&byte,1) && !file_close(NULL,1,true));
    assert(!file_seek(NULL,1,0) && !file_info(NULL,1,&size,&position) && !file_sync(NULL,1));
    assert(handle_error(NULL,1,false)==FR_LOCKED);assert(!remove_path(NULL,"/a") && !mkdir_path(NULL,"/a") && !rename_path(NULL,"/a","/b"));
    assert(!prepare_power_down(NULL) && !cancel_power_down(NULL) && !commit_power_down(NULL));
    assert(!last_error_api(NULL,text,sizeof(text)) && !strcmp(text,"untouched"));assert(!quiesce());stop();assert(calls==before);
}
static void sleep_ms(void *ctx,uint32_t ms){(void)ctx;++sleeps;now_ms+=ms;assert(owner && locked);if(reenter && !nested){nested=true;rejected_calls();nested=false;}}
static const garden_gpio_v1 fixture_gpio={.api_version=1,.struct_size=sizeof(fixture_gpio),.claim=gpio_claim,.write=gpio_write,.read=gpio_read,.release=gpio_release,.deep_sleep_hold=gpio_hold};
static const risc_provider_sync_api_v1 fixture_sync={1,sizeof(fixture_sync),NULL,sync_owner,sync_create,sync_take,sync_unlock,sync_destroy};
static const risc_platform_clock_api_v1 fixture_clock={1,sizeof(fixture_clock),NULL,monotonic,sleep_ms};
static const x4_power_ready_api_v1 fixture_power={1,sizeof(fixture_power),NULL,power_is_ready};
static risc_hw_gpio_bank_v1 fixture_config={sizeof(fixture_config),4,1,1,0,{5,41,42,40,-1,-1,-1,-1},0,0,0};
static const risc_hardware_device_v1 fixture_hardware={1,sizeof(fixture_hardware),55,"xteink,x4-pro-sd-native1","unspecified","gpio.bank",1,sizeof(fixture_config),&fixture_config};
static risc_provider_dependency_v1 deps[]={{"hardware.device",1,&fixture_hardware},{"platform.gpio",1,&fixture_gpio},{"platform.clock",1,&fixture_clock},{"platform.sync",1,&fixture_sync},{"board.power.ready",1,&fixture_power}};
#define START() start(deps,sizeof(deps)/sizeof(deps[0]))
static void put16(uint8_t *p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));}
static void format(bool partitioned){
    card_image=calloc(card_sectors,512);assert(card_image);const uint32_t base=partitioned?2048:0;
    if(base){card_image[450]=0x0c;put32(card_image+454,base);put32(card_image+458,card_sectors-base);card_image[510]=0x55;card_image[511]=0xaa;}
    uint8_t *b=card_image+(size_t)base*512;b[0]=0xeb;b[1]=0x58;b[2]=0x90;memcpy(b+3,"MSDOS5.0",8);put16(b+11,512);b[13]=1;put16(b+14,32);b[16]=2;b[21]=0xf8;
    put32(b+32,card_sectors-base);put32(b+36,1024);put32(b+44,2);put16(b+48,1);b[66]=0x29;memcpy(b+82,"FAT32   ",8);b[510]=0x55;b[511]=0xaa;
    for(unsigned fat=0;fat<2;++fat){uint8_t *p=card_image+(size_t)(base+32+fat*1024)*512;put32(p,0xffffff8);put32(p+4,0xffffffff);put32(p+8,0xfffffff);}
}
static void verify_cleanup(void){assert(quiesce());stop();assert(!fixture_lock && !gpio_api && !clock_api && !sync_api);for(unsigned i=0;i<49;++i)assert(!tokens[i]);assert(!card_bad_pin);}
static void write_sample(void){const uint32_t file=file_open_write(NULL,"/sample.bin");assert(file);uint8_t bytes[2048];for(unsigned i=0;i<sizeof(bytes);++i)bytes[i]=(uint8_t)i;assert(file_write(NULL,file,bytes,sizeof(bytes))==sizeof(bytes));assert(file_sync(NULL,file));assert(file_close(NULL,file,true));}
int main(int argc,char **argv){
    assert(argc==2);const char *scenario=argv[1];format(!strcmp(scenario,"mbr"));
    assert(t5_driver_get(2)==&driver && !t5_driver_get(1));
    if(!strcmp(scenario,"validation")){
        assert(!start(NULL,0));const void *saved=deps[0].api;deps[0].api=NULL;assert(!START());deps[0].api=saved;
        fixture_config.pins[3]=39;assert(!START());fixture_config.pins[3]=40;fixture_config.pins[7]=0;assert(!START());fixture_config.pins[7]=-1;
        power_ready=false;assert(!START());power_ready=true;owner=false;assert(!START());owner=true;
        assert(!calls && !fixture_lock && quiesce());goto done;
    }
    if(!strcmp(scenario,"create-fail")){fail_create=true;assert(!START());assert(!calls && !fixture_lock && quiesce());goto done;}
    if(!strcmp(scenario,"take-fail")){fail_take=true;assert(!START());assert(!calls && operation_mutex);fail_take=false;verify_cleanup();goto done;}
    if(!strcmp(scenario,"claim-retained")){fail_claim=true;assert(!START());assert(gpio_retained && operation_mutex);fail_claim=false;assert(!quiesce());assert(!START());goto done;}
    if(!strcmp(scenario,"absent"))absent=true;
    if(!strcmp(scenario,"unlock-fail"))fail_unlock=true;
    if(!strcmp(scenario,"reentry"))reenter=true;
    const bool result=START();
    if(!strcmp(scenario,"unlock-fail")){assert(!result && mutex_poisoned && locked);fail_unlock=false;rejected_calls();assert(!START() && !destroys);goto done;}
    assert(result);
    if(!strcmp(scenario,"absent")){assert(!ready(NULL));char text[80];assert(last_error_api(NULL,text,sizeof(text)) && !strcmp(text,"CMD8 no response"));assert(refresh(NULL));assert(now_ms==400 && clock_edges<2000);verify_cleanup();goto done;}
    assert(ready(NULL));
    if(!strcmp(scenario,"nonowner")){owner=false;rejected_calls();owner=true;verify_cleanup();goto done;}
    if(!strcmp(scenario,"destroy-fail")){fail_destroy=true;const uint64_t lock=operation_mutex;assert(!quiesce() && quiescing && operation_mutex==lock);assert(!refresh(NULL));fail_destroy=false;verify_cleanup();goto done;}
    if(!strcmp(scenario,"shutdown-write")){fail_write=true;const uint64_t power_token=pins[0].token;assert(!quiesce() && pins[0].token==power_token && operation_mutex);fail_write=false;verify_cleanup();goto done;}
    if(!strcmp(scenario,"release-retained")){fail_release=true;const uint64_t power_token=pins[0].token;assert(!quiesce() && pins[0].token==power_token && operation_mutex);fail_release=false;verify_cleanup();goto done;}
    if(!strcmp(scenario,"gpio-read-fail")){fail_read=true;uint8_t sector[512];assert(enter());assert(!read_sector(0,sector));assert(leave());assert(gpio_fault);fail_read=false;verify_cleanup();goto done;}
    if(!strcmp(scenario,"clock-stuck")){stuck_cycles=true;const unsigned before=calls;uint8_t sector[512];assert(enter());assert(!read_sector(0,sector));assert(leave());assert(calls-before<10);stuck_cycles=false;verify_cleanup();goto done;}
    if(!strcmp(scenario,"budgets")){assert(enter());operation_start=0;now_ms=OP_BUDGET_MS;assert(!risc_fatfs_checkpoint() && io_failed && !mounted);assert(leave());verify_cleanup();goto done;}
    if(!strcmp(scenario,"generation")){next_generation=0xffffff;assert(!dir_open(NULL,"/") && !file_open_write(NULL,"/new"));assert(!has_handles());verify_cleanup();goto done;}
    write_sample();
    if(!strcmp(scenario,"crc")){card_bad_crc=true;assert(!refresh(NULL) || !ready(NULL));assert(io_failed);card_bad_crc=false;verify_cleanup();goto done;}
    if(!strcmp(scenario,"write-rejected") || !strcmp(scenario,"busy-timeout")){
        const uint32_t writer=file_open(NULL,"/sample.bin",RISC_STORAGE_OPEN_WRITE);assert(writer);
        card_reject_write=!strcmp(scenario,"write-rejected");card_busy_forever=!card_reject_write;const uint64_t before=now_ms;
        uint8_t bytes[512]={0};assert(!file_write(NULL,writer,bytes,sizeof(bytes)) || !file_sync(NULL,writer));
        assert(io_failed && !file_close(NULL,writer,true) && !quiesce() && has_handles());assert(now_ms-before<16000);goto done;
    }
    uint64_t size=0;uint32_t reader=file_open_read(NULL,"/sample.bin",&size);assert(reader && size==2048);
    if(!strcmp(scenario,"power") || !strcmp(scenario,"power-fail")){
        assert(prepare_power_down(NULL));uint8_t byte;assert(!file_read(NULL,reader,&byte,1));assert(cancel_power_down(NULL));assert(prepare_power_down(NULL));
        if(!strcmp(scenario,"power-fail")){fail_write=true;assert(!commit_power_down(NULL) && !power_down_committed && gpio_fault);fail_write=false;assert(!cancel_power_down(NULL) && !quiesce());}
        else{assert(commit_power_down(NULL) && power_down_committed && card_sleep_off && card_power_off);assert(commit_power_down(NULL));assert(!cancel_power_down(NULL) && !quiesce());}
        goto done;
    }
    uint8_t bytes[2048]={0};assert(file_read(NULL,reader,bytes,sizeof(bytes))==sizeof(bytes));for(unsigned i=0;i<sizeof(bytes);++i)assert(bytes[i]==(uint8_t)i);
    assert(!refresh(NULL) && !quiesce());assert(file_close(NULL,reader,true));
    uint32_t replacement=file_open_read(NULL,"/sample.bin",&size);assert(replacement && replacement!=reader);assert(!file_read(NULL,reader,bytes,1));assert(file_close(NULL,replacement,true));
    uint32_t dir=dir_open(NULL,"/");assert(dir);assert(!quiesce());assert(dir_close_checked(NULL,dir));uint32_t replacement_dir=dir_open(NULL,"/");assert(replacement_dir && replacement_dir!=dir);risc_storage_dirent_v1 entry;assert(!dir_next(NULL,dir,&entry));assert(dir_next(NULL,replacement_dir,&entry));assert(dir_rewind(NULL,replacement_dir));assert(dir_close_checked(NULL,replacement_dir));
    assert(!file_open(NULL,"/../escape",1));assert(!card_bad_pin && sleeps>0);verify_cleanup();assert(START());assert(ready(NULL));verify_cleanup();
 done:
    assert(!card_bad_pin);free(card_image);printf("minimal X4 SD %s PASS\n",scenario);return 0;
}
