#define X4_SD_DRIVER_SOURCE "../drivers/x4pro_sd/driver.c"
#define main original_sd_fixture_main
#include "sd_test.c"
#undef main
#include "CrashReportSd.h"
static uint64_t review_clock(void *context) { (void)context; return now_ms; }
static bool review_safe(void *context) {
 const risc_storage_volume_api_v1_state *snapshot=context;
 const unsigned before_calls=calls,before_locks=locks,before_unlocks=unlocks,before_source=source_reads,before_reads=native_reads,before_writes=native_writes;
 int status=snapshot->observe(NULL);
 assert(calls==before_calls && locks==before_locks && unlocks==before_unlocks && source_reads==before_source && native_reads==before_reads && native_writes==before_writes);
 return status==RISC_STORAGE_STATE_READY || status==RISC_STORAGE_STATE_UNAVAILABLE;
}
int main(int argc,char **argv) {
 assert(argc==2); format(true);
 risc_gpio_sdmmc_api_v1 extension={0};extension.base=fixture_gpio;extension.base.struct_size=sizeof(extension);
 extension.sdmmc_tag=RISC_GPIO_SDMMC_TAG_V1;extension.sdmmc_version=1;
 extension.sdmmc=(risc_sdmmc_host_api_v1){1,sizeof(extension.sdmmc),NULL,native_open,native_read,native_write,native_sync,native_close};
 deps[1].api=&extension.base; if(!strcmp(argv[1],"absent"))native_absent=true; assert(START());
 const risc_driver_v2 *d=t5_driver_get(2);assert(d);
 const risc_storage_volume_api_v1_ext *volume=risc_storage_volume_extension(d->capability);assert(volume);
 const risc_storage_volume_api_v1_state *snapshot=risc_storage_volume_state(d->capability);assert(snapshot);
 assert(review_safe((void *)snapshot));
 if(!strcmp(argv[1],"unlock"))fail_unlock=true;
 else if(!strcmp(argv[1],"log-close")){source_enabled=true;log_fail_close=true;}
 else if(!strcmp(argv[1],"exported"))export_state=EXPORT_HOST;
 else if(!strcmp(argv[1],"writer-fault")){assert(file_open_write(NULL,"/writer.tmp"));io_failed=true;}
 else if(!strcmp(argv[1],"matrix")){
  const unsigned before_calls=calls,before_locks=locks,before_unlocks=unlocks,before_source=source_reads,before_reads=native_reads,before_writes=native_writes;
  bool *retained[]={&mutex_poisoned,&gpio_fault,&gpio_retained,&sdmmc_fault,&bootlog_retained};
  for(unsigned i=0;i<sizeof(retained)/sizeof(retained[0]);++i){*retained[i]=true;assert(snapshot->observe(NULL)==RISC_STORAGE_STATE_RETAINED);*retained[i]=false;}
  sleep_state=SLEEP_RETAINED;assert(snapshot->observe(NULL)==RISC_STORAGE_STATE_RETAINED);sleep_state=SLEEP_ACTIVE;
  export_state=EXPORT_RETAINED;assert(snapshot->observe(NULL)==RISC_STORAGE_STATE_RETAINED);export_state=EXPORT_LOCAL;
  bool *unavailable[]={&quiescing,&quiesced,&power_down_prepared,&power_down_committed,&io_failed};
  for(unsigned i=0;i<sizeof(unavailable)/sizeof(unavailable[0]);++i){*unavailable[i]=true;assert(snapshot->observe(NULL)==RISC_STORAGE_STATE_UNAVAILABLE);*unavailable[i]=false;}
  assert(calls==before_calls && locks==before_locks && unlocks==before_unlocks && source_reads==before_source && native_reads==before_reads && native_writes==before_writes);
  assert(snapshot->observe(NULL)==RISC_STORAGE_STATE_READY);
  puts("Snapshot latch matrix: all retained/unavailable states classified without callbacks PASS");free(card_image);return 0;
 }
 else assert(!strcmp(argv[1],"normal") || !strcmp(argv[1],"absent"));
 crash_report_sd_state state={0};const crash_report_sd_hooks hooks={(void *)snapshot,review_clock,review_safe,250};
 const char *sha="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
 int status=snapshot->observe(NULL);
 int result=status==RISC_STORAGE_STATE_UNAVAILABLE?CRASH_REPORT_SD_DEFERRED:
 crash_report_sd_export(&state,volume,sha,7,81,"CRASH / RESTART\n",16,&hooks);
 printf("Corrected SD0.2.14 provider first-ready %s: result=%d archive_retained=%d mutex_poisoned=%d bootlog_retained=%d handles=%d operations=%u\n",argv[1],result,state.retained,mutex_poisoned,bootlog_retained,has_handles(),state.operations);
 if(!strcmp(argv[1],"unlock") || !strcmp(argv[1],"log-close"))assert(result==CRASH_REPORT_SD_RETAINED && state.retained && (mutex_poisoned || bootlog_retained));
 else if(!strcmp(argv[1],"writer-fault"))assert(result==CRASH_REPORT_SD_RETAINED && state.retained && io_failed && has_handles() && !state.operations);
 else if(!strcmp(argv[1],"normal"))assert(result==CRASH_REPORT_SD_SAVED && !state.retained);
 else assert(result==CRASH_REPORT_SD_DEFERRED && !state.retained && !state.operations);
 assert(review_safe((void *)snapshot)==!state.retained);
 free(card_image);return 0;
}
