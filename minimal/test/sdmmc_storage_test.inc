/* Production ELF + FatFs. Only the native controller callback boundary is
 * replaced; ROM/state bytes and filesystem operations remain real. */
static bool native_live, native_absent, native_open_bad, native_close_bad, native_read_bad, native_write_bad;
static unsigned native_opens,native_closes,native_reads,native_writes,native_syncs;
static uint64_t native_generation;
static bool native_open(void *ctx,uint8_t clk,uint8_t cmd,uint8_t d0,uint32_t hz,uint64_t *token,risc_sdmmc_card_info_v1 *info){
 (void)ctx;++native_opens;assert(owner && locked && !native_live && clk==41 && cmd==42 && d0==40 && hz==20000000);
 assert(tokens[5] && outputs[5] && !levels[5] && !tokens[41] && !tokens[42] && !tokens[40]);
 *token=0;memset(info,0,sizeof(*info));if(native_absent)return false;
 native_live=true;*token=++native_generation;*info=(risc_sdmmc_card_info_v1){sizeof(*info),512,card_sectors,20000000,0};
 if(native_open_bad)info->sector_size=513;
 return !native_open_bad;
}
static void native_check(uint64_t token,uint64_t lba,uint32_t count){
 assert(owner && locked && native_live && token==native_generation && count && count<=8 && lba<card_sectors && count<=card_sectors-lba);
 assert(!tokens[41] && !tokens[42] && !tokens[40] && !levels[5]);
}
static bool native_read(void *ctx,uint64_t token,uint64_t lba,uint32_t count,void *out){
 (void)ctx;native_check(token,lba,count);++native_reads;if(native_read_bad)return false;
 memcpy(out,card_image+lba*512,count*512);return true;
}
static bool native_write(void *ctx,uint64_t token,uint64_t lba,uint32_t count,const void *in){
 (void)ctx;native_check(token,lba,count);++native_writes;if(native_write_bad)return false;
 memcpy(card_image+lba*512,in,count*512);return true;
}
static bool native_sync(void *ctx,uint64_t token){(void)ctx;native_check(token,0,1);++native_syncs;return true;}
static bool native_close(void *ctx,uint64_t token){
 (void)ctx;++native_closes;native_check(token,0,1);if(native_close_bad)return false;native_live=false;return true;
}
static void native_roundtrip(const char *path,size_t length){
 uint8_t *input=malloc(length),*output=malloc(length);assert(input&&output);
 for(size_t i=0;i<length;++i)input[i]=(uint8_t)(i*29u+(i>>9));
 uint64_t size=0;bool directory=false;uint32_t h=0;
 if(!stat_path(NULL,path,&size,&directory)) {
  h=file_open_write(NULL,path);assert(h);
  for(size_t at=0;at<length;){size_t n=length-at;if(n>4096)n=4096;assert(file_write(NULL,h,input+at,n)==n);at+=n;}
  assert(file_close(NULL,h,true));
 } else assert(!directory && size==length);
 h=file_open_read(NULL,path,&size);assert(h&&size==length);
 const unsigned gpio_before=calls,read_before=native_reads;unsigned chunks=0;
 for(size_t at=0;at<length;){size_t n=length-at;if(n>4096)n=4096;assert(file_read(NULL,h,output+at,n)==n);at+=n;++chunks;}
 assert(file_close(NULL,h,true) && !memcmp(input,output,length));
 assert(calls==gpio_before && !card_reads && !card_writes); /* No per-bit GPIO transport. */
 printf("native SDMMC exact roundtrip path=%s bytes=%zu app_reads=%u native_sector_reads=%u payload_gpio_calls=0 PASS\n",path,length,chunks,native_reads-read_before);
 free(input);free(output);
}
static void sdmmc_storage_cases(const char *scenario){
 risc_gpio_sdmmc_api_v1 extension={0};extension.base=fixture_gpio;extension.base.struct_size=sizeof(extension);
 extension.sdmmc_tag=RISC_GPIO_SDMMC_TAG_V1;extension.sdmmc_version=1;
 extension.sdmmc=(risc_sdmmc_host_api_v1){1,sizeof(extension.sdmmc),NULL,native_open,native_read,native_write,native_sync,native_close};deps[1].api=&extension.base;
 if(!strcmp(scenario,"sdmmc-absent"))native_absent=true;
 if(!strcmp(scenario,"sdmmc-open-retained")){native_open_bad=true;native_close_bad=true;assert(!START());assert(sdmmc_token && sdmmc_fault && native_live);native_close_bad=false;verify_cleanup();assert(!native_live);return;}
 if(!strcmp(scenario,"sdmmc-open-invalid"))native_open_bad=true;
 assert(START());assert(sdmmc_api && native_opens==1);
 if(native_absent || native_open_bad){assert(!ready(NULL) && !sdmmc_token && !native_live);native_absent=native_open_bad=false;assert(refresh(NULL) && ready(NULL));}
 assert(ready(NULL) && native_live && card_block_count==card_sectors);
 if(!strcmp(scenario,"sdmmc-roundtrip")){native_roundtrip("/rom512.gb",524288);native_roundtrip("/rom1m.gb",1048576);native_roundtrip("/rom512.gb.state",59320);}
 if(!strcmp(scenario,"sdmmc-sleep")){
  native_roundtrip("/rom512.gb.state",59320);assert(prepare_sleep(NULL)&&commit_sleep(NULL));
  assert(!native_live && !sdmmc_token && native_closes==1 && levels[5] && holds[5]);
  assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_READY && ready(NULL) && native_live && native_opens==2);
  native_roundtrip("/rom512.gb.state",59320);
 }
 if(!strcmp(scenario,"sdmmc-export")){
  native_roundtrip("/state",59320);risc_storage_export_token_t lease=0;uint64_t blocks=0;uint32_t block_size=0;
  assert(export_begin(NULL,&lease,&blocks,&block_size)==RISC_STORAGE_EXPORT_READY && lease && blocks==card_sectors && block_size==512);
  assert_export_frozen();uint8_t original[512],copy[512];assert(export_read(NULL,lease,card_sectors-1,1,original)==RISC_STORAGE_EXPORT_READY);
  memset(copy,0x56,sizeof(copy));assert(export_write(NULL,lease,card_sectors-1,1,copy)==RISC_STORAGE_EXPORT_READY);memset(copy,0,sizeof(copy));
  assert(export_read(NULL,lease,card_sectors-1,1,copy)==RISC_STORAGE_EXPORT_READY && copy[0]==0x56);
  assert(export_write(NULL,lease,card_sectors-1,1,original)==RISC_STORAGE_EXPORT_READY);
  assert(export_sync(NULL,lease)==RISC_STORAGE_EXPORT_READY && export_end(NULL,lease)==RISC_STORAGE_EXPORT_READY && ready(NULL));
  native_roundtrip("/state",59320);
 }
 if(!strcmp(scenario,"sdmmc-read-failure") || !strcmp(scenario,"sdmmc-write-failure")){
  assert(enter());uint8_t bytes[512]={0};native_read_bad=!strcmp(scenario,"sdmmc-read-failure");native_write_bad=!native_read_bad;
  assert(!(native_read_bad?read_sector(0,bytes):write_sector(0,bytes)) && sdmmc_fault);
  const unsigned io=native_reads+native_writes;assert(!read_sector(0,bytes)&&!write_sector(0,bytes)&&native_reads+native_writes==io);assert(leave());
 }
 if(!strcmp(scenario,"sdmmc-close-retained")){native_close_bad=true;assert(!quiesce() && sdmmc_token && native_live && !levels[5]);native_close_bad=false;}
 verify_cleanup();assert(!native_live && !sdmmc_token);
}
