/* Production SD transport + shared FatFs, replacing only typed GPIO/clock/sync.
 * The pinned Reader native-card wire model remains the single implementation. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static uint32_t fixture_cycles(void);
#define X4PRO_SD_CYCLE_COUNT() fixture_cycles()
#define f_open fixture_f_open
#define f_write fixture_f_write
#define f_close fixture_f_close
#define f_mount fixture_f_mount
#ifndef X4_SD_DRIVER_SOURCE
#define X4_SD_DRIVER_SOURCE "../drivers/x4pro_sd/driver.c"
#endif
#include X4_SD_DRIVER_SOURCE
#undef f_open
#undef f_write
#undef f_close
#undef f_mount
FRESULT f_open(FIL*,const TCHAR*,BYTE);
FRESULT f_write(FIL*,const void*,UINT,UINT*);
FRESULT f_close(FIL*);
FRESULT f_mount(FATFS*,const TCHAR*,BYTE);
static bool export_fail_unmount;
FRESULT fixture_f_mount(FATFS *fs,const TCHAR *path,BYTE opt) {
    if (!fs && export_fail_unmount) return FR_DISK_ERR;
    return f_mount(fs,path,opt);
}
static bool log_deny_open,log_partial_write,log_fail_close,log_opened;
static unsigned log_opens,log_writes,log_closes;
FRESULT fixture_f_open(FIL *file,const TCHAR *path,BYTE mode) {
    const bool log=!strcmp(path,X4_BOOTLOG_SD_PATH) && (mode & FA_WRITE);
    if(log){++log_opens;if(log_deny_open)return FR_WRITE_PROTECTED;}
    FRESULT result=f_open(file,path,mode);
    if(log && result==FR_OK)log_opened=true;
    return result;
}
FRESULT fixture_f_write(FIL *file,const void *bytes,UINT size,UINT *written) {
    if(bytes==bootlog_text){++log_writes;if(log_partial_write)return f_write(file,bytes,size/2,written);}
    return f_write(file,bytes,size,written);
}
FRESULT fixture_f_close(FIL *file) {
    if(log_opened && file==&files[0].object){++log_closes;if(log_fail_close)return FR_DISK_ERR;log_opened=false;}
    return f_close(file);
}
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
static int fail_claim_pin=-1, fail_release_pin=-1, fail_write_pin=-1, fail_write_level=-1;
static int32_t fail_hold, fail_unhold;
static bool clock_level, power_ready=true, force_dat_busy, export_bad_csd;
static void fixture_wire_release(uint32_t pin) {
    const bool csd = pin == 42 && cmd_count == 48 && cmd_bits[2] == 0 && cmd_bits[3] == 0 &&
        cmd_bits[4] == 1 && cmd_bits[5] == 0 && cmd_bits[6] == 0 && cmd_bits[7] == 1;
    wire_pin_release(pin);
    if (csd && export_bad_csd) reply_bits[135] = 0; /* Invalid CSD end bit. */
}
static uint64_t tokens[49];
static bool outputs[49], levels[49], holds[49];
static uint32_t fixture_cycles(void) { if (!stuck_cycles) cycles+=8; return cycles; }
uint32_t x4pro_sd_test_cycle_count(void) { return fixture_cycles(); }
static int token_pin(uint64_t token) { for (unsigned i=0;i<49;++i) if(tokens[i]==token) return (int)i; assert(!"invalid GPIO token"); return -1; }
static bool gpio_claim(void *ctx,uint8_t pin,bool output,bool initial,bool pullup,uint64_t *out) {
    (void)ctx; ++calls; ++claims; assert(owner && locked && fixture_lock && out);
    assert(pin==5 || pin==40 || pin==41 || pin==42); assert(!tokens[pin]); assert(!pullup || (!output && (pin==40 || pin==42))); *out=0;
    if(fail_claim || pin==fail_claim_pin)return false;
    tokens[pin]=*out=++next_token;outputs[pin]=output;levels[pin]=initial;holds[pin]=false;
    if(pin==5){wire_pin_hold(5,false);wire_pin_output(5,initial);}
    else if(output){if(pin==41){clock_level=initial;++clock_edges;}wire_pin_output(pin,initial);}
    else if(pullup)fixture_wire_release(pin);else wire_pin_input(pin,false);
    return true;
}
static bool gpio_write(void *ctx,uint64_t token,bool level) {
    (void)ctx; ++calls; assert(owner && locked);int pin=token_pin(token);assert(outputs[pin]);
    if(fail_write || holds[pin] || (pin==fail_write_pin && (fail_write_level<0 || level==fail_write_level)))return false;
    levels[pin]=level;
    if(pin==41){clock_level=level;++clock_edges;}
    wire_pin_output((uint32_t)pin,level);return true;
}
static bool gpio_read(void *ctx,uint64_t token,bool *level) {
    (void)ctx; ++calls;assert(owner && locked);int pin=token_pin(token);
    if(fail_read)return false;
    *level=pin==40 && force_dat_busy?false:pin==41?clock_level:pin==5?levels[pin]:absent?true:wire_pin_read((uint32_t)pin);return true;
}
static bool gpio_release(void *ctx,uint64_t token) {
    (void)ctx; ++calls;++releases;assert(owner && locked);int pin=token_pin(token);
    if(fail_release || holds[pin] || pin==fail_release_pin)return false;
    tokens[pin]=0;return true;
}
static int32_t gpio_hold(void *ctx,uint64_t token,bool hold) {
    (void)ctx;++calls;assert(owner && locked);int pin=token_pin(token);assert(pin==5 && outputs[pin]);
    if(fail_write)return RISC_DEEP_SLEEP_PLATFORM;
    if(hold && fail_hold){if(fail_hold==RISC_DEEP_SLEEP_RETAINED)holds[pin]=true;return fail_hold;}
    if(!hold && fail_unhold)return fail_unhold;
    holds[pin]=hold;wire_pin_hold((uint32_t)pin,hold);return 0;
}
static bool sync_owner(void *ctx){(void)ctx;return owner;}
static bool sync_create(void *ctx,uint64_t *out){(void)ctx;assert(owner && !fixture_lock);*out=0;if(fail_create)return false;*out=fixture_lock=++next_token;return true;}
static bool sync_take(void *ctx,uint64_t token){(void)ctx;++locks;assert(token==fixture_lock);if(!owner || locked || fail_take)return false;locked=true;return true;}
static bool sync_unlock(void *ctx,uint64_t token){(void)ctx;++unlocks;assert(token==fixture_lock);if(!owner || !locked || fail_unlock)return false;locked=false;return true;}
static bool sync_destroy(void *ctx,uint64_t token){(void)ctx;++destroys;assert(owner && !locked && token==fixture_lock);if(fail_destroy)return false;fixture_lock=0;return true;}
static bool power_is_ready(void *ctx){(void)ctx;return power_ready;}
static bool budget_jump,realistic_latency;
static unsigned timed_reads,timed_writes;
static uint64_t modeled_sd_ms;
static uint64_t monotonic(void *ctx){
 (void)ctx;
 if(realistic_latency){const uint64_t cost=(card_reads-timed_reads)*8u+(card_writes-timed_writes)*20u;now_ms+=cost;modeled_sd_ms+=cost;timed_reads=card_reads;timed_writes=card_writes;}
 if(budget_jump && bootlog_servicing)now_ms+=250;
 return now_ms;
}
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
    assert(!prepare_sleep(NULL) && !commit_sleep(NULL));
    const int32_t sleep_result=resume_sleep(NULL);
    assert(sleep_result==RISC_STORAGE_SLEEP_REFUSED || sleep_result==RISC_STORAGE_SLEEP_RETAINED);
    risc_storage_export_token_t lease=0;uint64_t blocks=0;uint32_t block_size=0;
    int32_t er=export_begin(NULL,&lease,&blocks,&block_size);
    assert(er==RISC_STORAGE_EXPORT_REFUSED || er==RISC_STORAGE_EXPORT_RETAINED);
    er=export_read(NULL,1,0,1,&byte);assert(er==RISC_STORAGE_EXPORT_REFUSED || er==RISC_STORAGE_EXPORT_RETAINED);
    er=export_write(NULL,1,0,1,&byte);assert(er==RISC_STORAGE_EXPORT_REFUSED || er==RISC_STORAGE_EXPORT_RETAINED);
    er=export_sync(NULL,1);assert(er==RISC_STORAGE_EXPORT_REFUSED || er==RISC_STORAGE_EXPORT_RETAINED);
    er=export_end(NULL,1);assert(er==RISC_STORAGE_EXPORT_REFUSED || er==RISC_STORAGE_EXPORT_RETAINED);
    assert(!last_error_api(NULL,text,sizeof(text)) && !strcmp(text,"untouched"));assert(!quiesce());stop();assert(calls==before);
}
static void sleep_ms(void *ctx,uint32_t ms){(void)ctx;++sleeps;now_ms+=ms;assert(owner && locked);if(reenter && !nested){nested=true;rejected_calls();nested=false;}}
static const garden_gpio_v1 fixture_gpio={.api_version=1,.struct_size=sizeof(fixture_gpio),.claim=gpio_claim,.write=gpio_write,.read=gpio_read,.release=gpio_release,.deep_sleep_hold=gpio_hold};
static const risc_provider_sync_api_v1 fixture_sync={1,sizeof(fixture_sync),NULL,sync_owner,sync_create,sync_take,sync_unlock,sync_destroy};
static const risc_platform_clock_api_v1 fixture_clock={1,sizeof(fixture_clock),NULL,monotonic,sleep_ms};
static bool source_enabled,source_history;
static uint32_t source_revision=1;
static uint64_t source_sequence=23;
static unsigned source_reads;
static int32_t fixture_diagnostic_read(void *ctx, uint32_t slot, char *text, uint32_t capacity,
    uint32_t *written, uint64_t *sequence, uint32_t *revision) {
    (void)ctx; ++source_reads; text[0] = 0;
    *written = 0; *sequence = 0; *revision = 0;
    if(!source_enabled || (slot && !source_history))return 0;
    *sequence=source_sequence-slot;*revision=slot?1:source_revision;
    int n=snprintf(text,capacity,"X4_BOOTLOG seq=%llu revision=%u origin=%s last_us=1300\n"
      "last_line=RTE_STAGE us=1300 provider sd begin\nfirst_failure=%s\nX4_BOOTLOG record_end=complete\n",
      (unsigned long long)*sequence,*revision,slot?"recovered-flash":"current",slot?"RTE_BOOT error=previous-mount":"none-recorded");
    assert(n>0 && (uint32_t)n<capacity);*written=(uint32_t)n;return 1;
}
static char source_trace[262144];static size_t source_trace_size,source_trace_available;
static bool source_trace_invalid;
static int32_t fixture_trace_read(void *context,uint64_t after,char *text,uint32_t capacity,uint32_t *written,uint64_t *next){
 (void)context;++source_reads;*written=0;*next=0;text[0]=0;
 assert(owner && !bootlog_reporting);
 if(after==source_trace_available)return 0;
 if(after>source_trace_available)return -1;
 uint32_t count=0,last=0;
 while(after+count<source_trace_available && count+1<capacity){text[count]=source_trace[after+count];if(text[count++]=='\n')last=count;}
 if(!last)return -1;
 text[last]=0;*written=last;*next=source_trace_invalid?after:after+last;return 1;
}
static const risc_diagnostic_source_api_v1_trace fixture_trace={{1,sizeof(fixture_trace),NULL,fixture_diagnostic_read},fixture_trace_read};
static const risc_diagnostic_source_api_v1 fixture_diagnostic={1,sizeof(fixture_diagnostic),NULL,fixture_diagnostic_read};
static const x4_power_ready_api_v1 fixture_power={1,sizeof(fixture_power),NULL,power_is_ready};
static risc_hw_gpio_bank_v1 fixture_config={sizeof(fixture_config),4,1,1,0,{5,41,42,40,0,0,0,0},0,0,0};
static const risc_hardware_device_v1 fixture_hardware={1,sizeof(fixture_hardware),55,"xteink,x4-pro-sd-native1","unspecified","gpio.bank",1,sizeof(fixture_config),&fixture_config};
static risc_provider_dependency_v1 deps[]={{"hardware.device",1,&fixture_hardware},{"platform.gpio",1,&fixture_gpio},{"platform.clock",1,&fixture_clock},{"platform.sync",1,&fixture_sync},{"board.power.ready",1,&fixture_power},{RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,&fixture_diagnostic}};
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
static void files_paths(void){
    /* Files passes volume-relative paths. /sd is a broker/VFS prefix, never
     * prepended to ordinary storage.volume operations. Exercise the exported
     * production API over native SD transport and shared FatFs. */
    const risc_storage_volume_api_v1 *v=(const risc_storage_volume_api_v1 *)&api;
    const risc_storage_volume_api_v1_ext *ext=risc_storage_volume_extension(v);
    assert(ext&&ext->mkdir&&ext->rename&&ext->dir_close_checked);
    uint64_t size=0;bool directory=false;char bytes[6]={0};
    assert(v->stat(v->context,"/",&size,&directory)&&directory);
    assert(ext->mkdir(v->context,"/Books")&&ext->mkdir(v->context,"/Archive"));
    uint32_t writer=v->file_open_write(v->context,"/Books/read.txt");assert(writer);
    assert(v->file_write(v->context,writer,"hello",5)==5&&v->file_close(v->context,writer,true));
    uint32_t dir=v->dir_open(v->context,"/");assert(dir);
    risc_storage_dirent_v1 entry;bool books=false;
    while(v->dir_next(v->context,dir,&entry))if(!strcmp(entry.name,"Books")){assert(entry.is_directory);books=true;}
    assert(books&&!ext->handle_error(v->context,dir,true)&&ext->dir_close_checked(v->context,dir));
    dir=v->dir_open(v->context,"/Books");assert(dir);
    assert(v->dir_next(v->context,dir,&entry)&&!strcmp(entry.name,"read.txt")&&!entry.is_directory&&entry.size==5);
    assert(!v->dir_next(v->context,dir,&entry)&&!ext->handle_error(v->context,dir,true)&&ext->dir_close_checked(v->context,dir));
    assert(!v->file_open_read(v->context,"/sd/Books/read.txt",&size));
    owner=false;const unsigned before=calls;
    assert(!v->file_open_read(v->context,"/Books/read.txt",&size)&&calls==before);owner=true;
    uint32_t reader=v->file_open_read(v->context,"/Books/read.txt",&size);assert(reader&&size==5);
    assert(v->file_read(v->context,reader,bytes,5)==5&&!strcmp(bytes,"hello"));
    assert(v->file_close(v->context,reader,true));
    assert(ext->rename(v->context,"/Books/read.txt","/Books/renamed.txt"));
    assert(ext->rename(v->context,"/Books/renamed.txt","/Archive/renamed.txt"));
    assert(!v->stat(v->context,"/Books/read.txt",&size,&directory));
    assert(v->stat(v->context,"/Archive/renamed.txt",&size,&directory)&&size==5&&!directory);
    writer=v->file_open_write(v->context,"/Books/copy.txt");assert(writer);
    assert(v->file_write(v->context,writer,bytes,5)==5&&v->file_close(v->context,writer,true));
    assert(v->remove(v->context,"/Archive/renamed.txt")&&v->remove(v->context,"/Books/copy.txt"));
    assert(v->remove(v->context,"/Archive")&&v->remove(v->context,"/Books")&&!has_handles());
    verify_cleanup();
}
static void frozen_io(void){
    const unsigned before=calls;char byte=0,text[80];uint64_t size=0,position=0;bool directory=false;risc_storage_dirent_v1 entry;
    assert(!refresh(NULL) && !ready(NULL) && !label(NULL,text,sizeof(text)));
    assert(!stat_path(NULL,"/",&size,&directory) && !dir_open(NULL,"/") && !dir_next(NULL,1,&entry));
    assert(!dir_rewind(NULL,1) && !dir_close_checked(NULL,1));dir_close(NULL,1);
    assert(!file_open(NULL,"/sample.bin",1) && !file_open_write(NULL,"/blocked"));
    assert(!file_open_read(NULL,"/sample.bin",&size) && !file_read(NULL,1,&byte,1) && !file_write(NULL,1,&byte,1));
    assert(!file_seek(NULL,1,0) && !file_sync(NULL,1) && !file_info(NULL,1,&size,&position) && !file_close(NULL,1,true));
    assert(handle_error(NULL,1,false)==FR_LOCKED);
    assert(!mkdir_path(NULL,"/blocked") && !remove_path(NULL,"/sample.bin") && !rename_path(NULL,"/a","/b"));
    assert(calls==before);
}
static void retained_sleep(void){
    const unsigned before=calls;const uint64_t mutex=operation_mutex;
    uint64_t saved[49];memcpy(saved,tokens,sizeof(saved));
    assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_RETAINED);frozen_io();
    assert(!prepare_sleep(NULL) && !commit_sleep(NULL) && !prepare_power_down(NULL));
    assert(!cancel_power_down(NULL) && !commit_power_down(NULL) && !quiesce() && !START());
    assert(operation_mutex==mutex && fixture_lock && !destroys && calls==before);
    assert(!memcmp(saved,tokens,sizeof(saved)));
}
static void sleep_cases(const char *scenario){
    const risc_storage_volume_api_v1 *base=(const risc_storage_volume_api_v1 *)&api;
    const risc_storage_volume_api_v1_sleep *extension=risc_storage_volume_sleep(base);
    assert(extension==&api && risc_storage_volume_power_commit(base)==&api.terminal);
    assert(extension->prepare_sleep==prepare_sleep && extension->commit_sleep==commit_sleep && extension->resume_sleep==resume_sleep);
    assert(!commit_sleep(NULL));
    if(!strcmp(scenario,"sleep-absent")){
        assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_MEDIA_UNAVAILABLE && !ready(NULL));
        assert(prepare_sleep(NULL));assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_MEDIA_UNAVAILABLE);
        assert(prepare_sleep(NULL) && commit_sleep(NULL));frozen_io();
        assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_MEDIA_UNAVAILABLE && !ready(NULL));
        assert(!card_sleep_off && !card_power_off && !holds[5]);
        absent=false;assert(refresh(NULL) && ready(NULL));verify_cleanup();return;
    }
    assert(ready(NULL));
    if(!strcmp(scenario,"sleep-busy")){
        owner=false;assert(!prepare_sleep(NULL) && resume_sleep(NULL)==RISC_STORAGE_SLEEP_REFUSED);owner=true;
        owner=false;mutex_poisoned=true;assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_REFUSED);mutex_poisoned=false;owner=true;
        assert(enter());const unsigned before=calls;assert(!prepare_sleep(NULL) && !commit_sleep(NULL));
        assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_REFUSED && calls==before);assert(leave());
    }
    if(!strcmp(scenario,"sleep-handles")){
        write_sample();uint64_t size;uint32_t handle=file_open_read(NULL,"/sample.bin",&size);assert(handle);
        assert(!prepare_sleep(NULL) && !power_down_prepared && ready(NULL));assert(file_close(NULL,handle,true));
        handle=file_open(NULL,"/sample.bin",RISC_STORAGE_OPEN_WRITE);assert(handle);
        assert(!prepare_sleep(NULL) && !power_down_prepared);assert(file_close(NULL,handle,true));
        handle=dir_open(NULL,"/");assert(handle);assert(!prepare_sleep(NULL) && !power_down_prepared);assert(dir_close_checked(NULL,handle));
    }
    if(!strcmp(scenario,"sleep-legacy")){
        assert(prepare_power_down(NULL));assert(!prepare_sleep(NULL) && !commit_sleep(NULL));
        assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_REFUSED);assert(cancel_power_down(NULL));
        assert(prepare_sleep(NULL));assert(!prepare_power_down(NULL) && !cancel_power_down(NULL) && !commit_power_down(NULL));
        assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_READY);
        assert(prepare_power_down(NULL) && commit_power_down(NULL));
        assert(!prepare_sleep(NULL) && !commit_sleep(NULL) && resume_sleep(NULL)==RISC_STORAGE_SLEEP_REFUSED);
        assert(!cancel_power_down(NULL) && !quiesce());return;
    }
    if(!strcmp(scenario,"sleep-sync")){
        force_dat_busy=true;assert(!prepare_sleep(NULL));force_dat_busy=false;retained_sleep();return;
    }
    if(!strcmp(scenario,"sleep-prepare-unlock")){
        fail_unlock=true;assert(!prepare_sleep(NULL));fail_unlock=false;retained_sleep();return;
    }
    assert(prepare_sleep(NULL) && prepare_sleep(NULL));frozen_io();assert(!quiesce());
    assert(!prepare_power_down(NULL) && !cancel_power_down(NULL) && !commit_power_down(NULL));
    if(!strcmp(scenario,"sleep-repeat-prepare-unlock")){
        fail_unlock=true;assert(!prepare_sleep(NULL));fail_unlock=false;retained_sleep();return;
    }
    if(!strcmp(scenario,"sleep-refusal")){
        const unsigned before=calls;assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_READY && calls==before);
        assert(ready(NULL) && !card_sleep_commits);verify_cleanup();return;
    }
    if(!strcmp(scenario,"sleep-commit-clock"))fail_write_pin=41;
    if(!strcmp(scenario,"sleep-commit-cmd-release"))fail_release_pin=42;
    if(!strcmp(scenario,"sleep-commit-cmd-claim"))fail_claim_pin=42;
    if(!strcmp(scenario,"sleep-commit-dat-release"))fail_release_pin=40;
    if(!strcmp(scenario,"sleep-commit-dat-claim"))fail_claim_pin=40;
    if(!strcmp(scenario,"sleep-commit-rail"))fail_write_pin=5;
    if(!strcmp(scenario,"sleep-commit-hold"))fail_hold=RISC_DEEP_SLEEP_PLATFORM;
    if(!strcmp(scenario,"sleep-commit-hold-retained"))fail_hold=RISC_DEEP_SLEEP_RETAINED;
    if(!strcmp(scenario,"sleep-commit-unlock"))fail_unlock=true;
    if(strstr(scenario,"sleep-commit-")){
        assert(!commit_sleep(NULL));fail_unlock=false;retained_sleep();return;
    }
    assert(commit_sleep(NULL) && sleep_state==SLEEP_COMMITTED && !mounted && !card_ready);
    assert(card_power_off && card_sleep_off && holds[5]);
    const unsigned committed_calls=calls;assert(commit_sleep(NULL) && calls==committed_calls);
    assert(!prepare_sleep(NULL) && !prepare_power_down(NULL) && !cancel_power_down(NULL) && !commit_power_down(NULL));
    assert(!quiesce());frozen_io();
    if(!strcmp(scenario,"sleep-busy")){
        fail_take=true;assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_REFUSED);assert(calls==committed_calls);
        fail_take=false;assert(card_power_off && card_sleep_off && !mounted);
    }
    if(!strcmp(scenario,"sleep-repeat-commit-unlock")){
        fail_unlock=true;assert(!commit_sleep(NULL));fail_unlock=false;retained_sleep();return;
    }
    if(!strcmp(scenario,"sleep-resume-unhold"))fail_unhold=RISC_DEEP_SLEEP_PLATFORM;
    if(!strcmp(scenario,"sleep-resume-unhold-retained"))fail_unhold=RISC_DEEP_SLEEP_RETAINED;
    if(!strcmp(scenario,"sleep-resume-rail-off")){fail_write_pin=5;fail_write_level=1;}
    if(!strcmp(scenario,"sleep-resume-rail-on")){fail_write_pin=5;fail_write_level=0;}
    if(!strcmp(scenario,"sleep-resume-cmd-claim"))fail_claim_pin=42;
    if(!strcmp(scenario,"sleep-resume-dat-claim"))fail_claim_pin=40;
    if(!strcmp(scenario,"sleep-resume-unlock"))fail_unlock=true;
    if(!strcmp(scenario,"sleep-resume-crc"))card_bad_crc=true;
    if(strstr(scenario,"sleep-resume-")){
        assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_RETAINED);fail_unlock=false;retained_sleep();return;
    }
    if(!strcmp(scenario,"sleep-unformatted"))card_image[510]=0;
    if(!strcmp(scenario,"sleep-removed"))absent=true;
    if(!strcmp(scenario,"sleep-unformatted") || !strcmp(scenario,"sleep-removed")){
        assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_MEDIA_UNAVAILABLE && !ready(NULL));
        assert(!card_power_off && !card_sleep_off && !holds[5]);
        char byte;uint64_t size;assert(!file_open_read(NULL,"/sample.bin",&size) && !file_read(NULL,1,&byte,1));
        assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_MEDIA_UNAVAILABLE);
        absent=false;card_image[510]=0x55;assert(refresh(NULL) && ready(NULL));verify_cleanup();return;
    }
    assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_READY && ready(NULL));
    assert(!card_power_off && !card_sleep_off && !holds[5]);
    const unsigned resumed_calls=calls;assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_READY && calls==resumed_calls);
    if(!strcmp(scenario,"sleep-repeat-resume-unlock")){
        fail_unlock=true;assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_RETAINED);fail_unlock=false;retained_sleep();return;
    }
    if(strcmp(scenario,"sleep-handles"))write_sample();
    uint64_t size;uint32_t stale=file_open_read(NULL,"/sample.bin",&size);assert(stale);assert(file_close(NULL,stale,true));
    uint32_t stale_dir=dir_open(NULL,"/");assert(stale_dir);assert(dir_close_checked(NULL,stale_dir));
    for(unsigned cycle=0;cycle<3;++cycle){
        assert(prepare_sleep(NULL) && commit_sleep(NULL));assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_READY);
        uint32_t fresh=file_open_read(NULL,"/sample.bin",&size);assert(fresh && fresh!=stale);char byte;
        assert(!file_read(NULL,stale,&byte,1) && !file_close(NULL,stale,true));assert(handle_error(NULL,stale,false)==FR_INVALID_OBJECT);
        assert(file_read(NULL,fresh,&byte,1)==1 && file_close(NULL,fresh,true));
        uint32_t fresh_dir=dir_open(NULL,"/");assert(fresh_dir && fresh_dir!=stale_dir);risc_storage_dirent_v1 entry;
        assert(!dir_next(NULL,stale_dir,&entry) && !dir_close_checked(NULL,stale_dir));assert(handle_error(NULL,stale_dir,true)==FR_INVALID_OBJECT);
        assert(dir_next(NULL,fresh_dir,&entry) && dir_close_checked(NULL,fresh_dir));
    }
    verify_cleanup();assert(START() && ready(NULL));assert(handle_error(NULL,stale,false)==FR_INVALID_OBJECT);verify_cleanup();
}
static size_t read_log(char *text,size_t capacity) {
    uint64_t size=0;uint32_t file=file_open_read(NULL,X4_BOOTLOG_SD_PATH,&size);assert(file && size<capacity);
    size_t n=0;
    while(n<size){uint32_t chunk=(uint32_t)(size-n);if(chunk>4096)chunk=4096;uint32_t copied=file_read(NULL,file,text+n,chunk);assert(copied);n+=copied;}
    text[n]=0;assert(n==size && file_close(NULL,file,true));return n;
}
static void bootlog_cases(const char *scenario) {
    source_enabled=true;source_history=!strcmp(scenario,"log-history");
    if(!strcmp(scenario,"log-absent"))absent=true;
    if(!strcmp(scenario,"log-unformatted"))card_image[510]=0;
    if(!strcmp(scenario,"log-readonly"))log_deny_open=true;
    if(!strcmp(scenario,"log-partial"))log_partial_write=true;
    if(!strcmp(scenario,"log-close"))log_fail_close=true;
    if(!strcmp(scenario,"log-full")){
        for(unsigned fat=0;fat<2;++fat)
            for(unsigned entry=3;entry<1024u*128u;++entry)
                put32(card_image+(size_t)(32+fat*1024)*512+entry*4,0xfffffff);
    }
    assert(START() == (strcmp(scenario,"log-close") != 0));
    if(!strcmp(scenario,"log-absent") || !strcmp(scenario,"log-unformatted")){
        assert(!mounted && !log_opens && !bootlog_disabled);
        absent=false;card_image[510]=0x55;assert(refresh(NULL) && ready(NULL));
        assert(log_writes==1 && bootlog_seen[0].revision==1);verify_cleanup();return;
    }
    if(!strcmp(scenario,"log-readonly") || !strcmp(scenario,"log-partial") || !strcmp(scenario,"log-full") || !strcmp(scenario,"log-close")){
        assert(bootlog_disabled && bootlog_error && !bootlog_seen[0].revision);
        const unsigned attempts=log_opens;
        char message[200]; const risc_storage_volume_api_v1 *published=driver.poll.streams.driver.capability;
        assert(published->last_error(NULL,message,sizeof(message)) && strstr(message,"boot-log:"));
        for(unsigned i=0;i<8;++i)(void)ready(NULL);
        assert(log_opens==attempts);
        if(!strcmp(scenario,"log-close")){
            assert(has_handles() && io_failed && !mounted && !quiesce() && !prepare_sleep(NULL) && !refresh(NULL));
            const unsigned before=calls;
            assert(driver.poll.streams.last_error(message,sizeof(message)) && strstr(message,"writable log retained"));
            assert(calls==before); /* Diagnostic suffix is RAM-only after revocation. */
            assert(files[0].handle && files[0].flags==RISC_STORAGE_OPEN_WRITE);return;
        }
        assert(!has_handles() && !io_failed && ready(NULL));
        if(!strcmp(scenario,"log-partial")){char text[512];read_log(text,sizeof(text));assert(!strstr(text,"record_end=complete"));}
        verify_cleanup();return;
    }
    assert(bootlog_seen[0].revision==1);
    if(source_history)assert(log_writes==9); /* Complete recovered batch before START returns. */
    assert(ready(NULL));
    if(!strcmp(scenario,"log-history")){
        for(unsigned i=0;i<12;++i)assert(ready(NULL));
        assert(log_writes==9);char text[4096];read_log(text,sizeof(text));
        assert(strstr(text,"recovered-flash") && strstr(text,"previous-mount"));
        const unsigned before=log_writes;for(unsigned i=0;i<12;++i)assert(ready(NULL));assert(log_writes==before);
    } else if(!strcmp(scenario,"log-ownership")){
        uint64_t size=0;uint32_t reader=file_open_read(NULL,X4_BOOTLOG_SD_PATH,&size);assert(reader);
        char prefix[8];assert(file_read(NULL,reader,prefix,sizeof(prefix))==sizeof(prefix));
        const unsigned before=log_writes;source_revision=2;
        uint32_t dir=dir_open(NULL,"/");assert(dir);assert(ready(NULL));assert(log_writes==before);
        uint64_t bytes=0,position=0;assert(file_info(NULL,reader,&bytes,&position) && position==sizeof(prefix));
        assert(file_for(reader)->error==0 && file_for(reader)->handle==reader);
        fail("caller marker");assert(file_close(NULL,reader,true) && log_writes==before);
        assert(dir_close_checked(NULL,dir) && log_writes==before+1 && !strcmp(error,"caller marker"));
    } else if(!strcmp(scenario,"log-sleep")){
        const unsigned before=log_writes;assert(prepare_sleep(NULL));source_revision=2;
        assert(commit_sleep(NULL) && log_writes==before);assert(!ready(NULL));
        assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_READY && log_writes==before+1);
        assert(bootlog_seen[0].revision==2);
    } else if(!strcmp(scenario,"log-repeated")){
        const unsigned before=log_writes;for(unsigned i=0;i<20;++i)assert(ready(NULL));
        assert(refresh(NULL) && log_writes==before);
        source_sequence=24;source_revision=1;assert(ready(NULL) && log_writes==before+1);
        char text[1024];read_log(text,sizeof(text));assert(strstr(text,"seq=23") && strstr(text,"seq=24"));
    } else if(!strcmp(scenario,"log-rotation")){
        source_enabled=false;
        const uint32_t file=file_open(NULL,X4_BOOTLOG_SD_PATH,RISC_STORAGE_OPEN_WRITE|RISC_STORAGE_OPEN_APPEND);assert(file);
        char bytes[4096];memset(bytes,'x',sizeof(bytes));
        for(unsigned i=0;i<X4_BOOTLOG_SD_MAX_BYTES/sizeof(bytes);++i)assert(file_write(NULL,file,bytes,sizeof(bytes))==sizeof(bytes));
        assert(file_close(NULL,file,true));source_revision=2;source_enabled=true;assert(ready(NULL));
        uint64_t size;bool directory;assert(stat_path(NULL,X4_BOOTLOG_SD_PREVIOUS,&size,&directory) && !directory && size>=X4_BOOTLOG_SD_MAX_BYTES);
        assert(stat_path(NULL,X4_BOOTLOG_SD_PATH,&size,&directory) && size<X4_BOOTLOG_SD_MAX_BYTES);
    } else if(!strcmp(scenario,"log-write-fail")){
        source_revision=2;card_reject_write=true;const unsigned before=log_writes;
        (void)ready(NULL);assert(bootlog_disabled && bootlog_seen[0].revision==1 && log_writes==before+1);
        assert(io_failed && has_handles() && !quiesce() && !prepare_sleep(NULL));return;
    }
    verify_cleanup();
}
static void trace_cases(const char *scenario) {
 const char* fixture=getenv("X4_TRACE_FIXTURE");assert(fixture);
 FILE *input=fopen(fixture,"rb");assert(input);
 source_trace_size=fread(source_trace,1,sizeof(source_trace)-1,input);assert(!ferror(input) && feof(input));fclose(input);
 assert(source_trace_size>20000);source_trace[source_trace_size]=0;
 deps[5].api=&fixture_trace.base;
 // Mount with no data, then publish exact production native-logger output.
 source_trace_available=0;assert(START());assert(log_writes==0);
 source_trace_available=source_trace_size;
 if(!strcmp(scenario,"log-trace-close"))log_fail_close=true;
 if(!strcmp(scenario,"log-trace-invalid"))source_trace_invalid=true;
 if(!strcmp(scenario,"log-trace-timeout"))budget_jump=true;
 unsigned iterations=0;
 if(!strcmp(scenario,"log-trace-export")) {
  risc_storage_export_token_t token=0;uint64_t blocks=0;uint32_t block_size=0;
  assert(export_begin(NULL,&token,&blocks,&block_size)==RISC_STORAGE_EXPORT_READY);
  assert(bootlog_cursor==source_trace_available && bootlog_paused);
  const unsigned writes=card_writes;bootlog_service(1000);assert(card_writes==writes);
  assert(export_end(NULL,token)==RISC_STORAGE_EXPORT_READY);
 }
 while(bootlog_cursor<source_trace_available && !bootlog_disabled){
  const unsigned sectors=card_writes+card_reads,writes=log_writes;const uint64_t cursor=bootlog_cursor;
  bootlog_service(1000);assert(++iterations<512);
  if(bootlog_cursor==cursor)now_ms+=2001;
  assert(log_writes-writes<=1);
  assert(card_writes+card_reads-sectors<=64);
 }
 if(strcmp(scenario,"log-full-trace") && strcmp(scenario,"log-trace-export")){
  assert(bootlog_disabled && bootlog_error && bootlog_cursor==0);
  const unsigned writes=log_writes;bootlog_service(1000);assert(log_writes==writes);
  if(!strcmp(scenario,"log-trace-close"))assert(bootlog_retained && has_handles() && !quiesce());
  printf("Full trace SD %s disabled safely; cursor=%llu retained=%u sectors=%u elapsed_ms=%llu PASS\n",scenario,(unsigned long long)bootlog_cursor,(unsigned)bootlog_retained,operation_sectors,(unsigned long long)(now_ms-operation_start));
  return;
 }
 char actual[262144];const size_t count=read_log(actual,sizeof(actual));
 assert(count==source_trace_size && !memcmp(actual,source_trace,count));
 const char *artifact=getenv("X4_SD_LOG_ARTIFACT");if(artifact){FILE*f=fopen(artifact,"wb");assert(f);assert(fwrite(actual,1,count,f)==count);assert(!fclose(f));}
 assert(strstr(actual,"event=100") && strstr(actual,"panel-start-timeout") && strstr(actual,"stage=rtc-recovery result=ok") && strstr(actual,"file=clock.elf"));
 assert(strstr(actual,"session=2 event=1") && strstr(actual,"session=3 event=1"));
 unsigned before=card_writes+card_reads,opens=log_opens;
 for(unsigned i=0;i<100;++i)bootlog_service(1000);
 assert(card_writes+card_reads==before && log_opens==opens);
 owner=false;bootlog_service(1000);owner=true;assert(card_writes+card_reads==before);
 printf("Full trace SD bytes=%zu events>200 chunks=%u physical_reads=%u physical_writes=%u; exact persisted text/order/reset/results PASS\n",count,iterations,card_reads,card_writes);
 verify_cleanup();
}
static void batching_latency_case(void) {
 const char* fixture=getenv("X4_TRACE_FIXTURE");assert(fixture);
 FILE *input=fopen(fixture,"rb");assert(input);
 source_trace_size=fread(source_trace,1,sizeof(source_trace)-1,input);assert(!ferror(input) && feof(input));fclose(input);
 deps[5].api=&fixture_trace.base;source_trace_available=0;assert(START());
 realistic_latency=true;timed_reads=card_reads;timed_writes=card_writes;
 unsigned lines=0;
 // Production boot statement cadence: each acquisition boundary receives the
 // preceding four lines, rather than an already-complete artificial transcript.
 for(size_t i=0;i<source_trace_size;++i)if(source_trace[i]=='\n') {
  source_trace_available=i+1;++lines;now_ms+=2;
  if(lines%4==0)bootlog_service(1000);
  assert(!bootlog_disabled);
 }
 for(unsigned tries=0;bootlog_cursor<source_trace_size;++tries){assert(tries<128);now_ms+=2001;bootlog_service(1000);assert(!bootlog_disabled);}
 const uint64_t cost=modeled_sd_ms;const unsigned r=card_reads-timed_reads,w=card_writes-timed_writes;
 (void)r;(void)w;
 const unsigned persisted=log_closes;
 char actual[262144];assert(read_log(actual,sizeof(actual))==source_trace_size && !memcmp(actual,source_trace,source_trace_size));
 const unsigned before=card_reads+card_writes;
 for(unsigned i=0;i<1000;++i)bootlog_service(1000);
 assert(before==card_reads+card_writes);
#ifdef X4_EXPECT_BATCHING
 assert(persisted<=source_trace_size/3000+3);
#endif
 printf("Boot cadence bytes=%zu lines=%u append_close_pairs=%u modeled_sd_ms=%llu zero_work_calls=1000 PASS\n",source_trace_size,lines,persisted,(unsigned long long)cost);
 verify_cleanup();
}
#include "sd_export_test.inc"
int main(int argc,char **argv){
    assert(argc==2);const char *scenario=argv[1];format(!strcmp(scenario,"mbr"));
    const char*materialized=getenv("X4_SD_TYPED_CONFIG");
    if(materialized){FILE*f=fopen(materialized,"rb");assert(f);assert(fread(&fixture_config,1,sizeof(fixture_config),f)==sizeof(fixture_config));assert(fgetc(f)==EOF);assert(!fclose(f));}
    assert(t5_driver_get(2)==&driver.poll.streams.driver && !t5_driver_get(1));
    if(!strncmp(scenario,"export-",7)){export_cases(scenario);goto done;}
    if(!strcmp(scenario,"log-batching-latency")){batching_latency_case();return 0;}
    if(!strcmp(scenario,"log-full-trace") || !strncmp(scenario,"log-trace-",10)){trace_cases(scenario);return 0;}
    if(!strncmp(scenario,"log-",4)){bootlog_cases(scenario);goto done;}
    if(!strcmp(scenario,"validation")){
        assert(!start(NULL,0));const void *saved=deps[0].api;deps[0].api=NULL;assert(!START());deps[0].api=saved;
        fixture_config.pins[3]=39;assert(!START());fixture_config.pins[3]=40;fixture_config.pins[7]=-1;assert(!START());fixture_config.pins[7]=0;
        power_ready=false;assert(!START());power_ready=true;owner=false;assert(!START());owner=true;
        assert(!calls && !fixture_lock && quiesce());goto done;
    }
    if(!strcmp(scenario,"create-fail")){fail_create=true;assert(!START());assert(!calls && !fixture_lock && quiesce());goto done;}
    if(!strcmp(scenario,"take-fail")){fail_take=true;assert(!START());assert(!calls && operation_mutex);fail_take=false;verify_cleanup();goto done;}
    if(!strcmp(scenario,"claim-retained")){fail_claim=true;assert(!START());assert(gpio_retained && operation_mutex);fail_claim=false;assert(!quiesce());assert(!START());goto done;}
    if(!strcmp(scenario,"absent") || !strcmp(scenario,"sleep-absent"))absent=true;
    if(!strcmp(scenario,"unlock-fail"))fail_unlock=true;
    if(!strcmp(scenario,"reentry") || !strcmp(scenario,"sleep-reentry"))reenter=true;
    const bool result=START();
    if(!strcmp(scenario,"unlock-fail")){assert(!result && mutex_poisoned && locked);fail_unlock=false;rejected_calls();assert(!START() && !destroys);goto done;}
    assert(result);
    if(!strncmp(scenario,"sleep-",6)){sleep_cases(scenario);goto done;}
    if(!strcmp(scenario,"absent")){assert(!ready(NULL));char text[80];assert(last_error_api(NULL,text,sizeof(text)) && !strcmp(text,"CMD8 no response"));assert(refresh(NULL));assert(now_ms==400 && clock_edges<2000);verify_cleanup();goto done;}
    assert(ready(NULL));
    if(!strcmp(scenario,"files-paths")){files_paths();goto done;}
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
