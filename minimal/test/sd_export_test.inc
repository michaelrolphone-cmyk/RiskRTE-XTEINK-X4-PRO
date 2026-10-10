/* Exercise the published tail through the real GPIO protocol/FatFs owner. */
static const risc_storage_volume_api_v1_export *export_api(void) {
    const risc_storage_volume_api_v1 *base=driver.poll.streams.driver.capability;
    const risc_storage_volume_api_v1_export *p=risc_storage_volume_export(base);
    assert(p && p==&logging_api.prepared.base && p->export_begin==export_begin && p->export_end==export_end);
    assert(risc_storage_volume_sleep(base)==&p->sleep);
    assert(offsetof(risc_storage_volume_api_v1_export,sleep)==0);
    assert(offsetof(risc_storage_volume_api_v1_export,export_tag)==sizeof(risc_storage_volume_api_v1_sleep));
    risc_storage_volume_api_v1_export copy=*p;
    risc_storage_volume_api_v1 *b=&copy.sleep.terminal.power.volume.base;
    b->struct_size=sizeof(copy)-1;assert(!risc_storage_volume_export(b));b->struct_size=sizeof(copy);
    copy.export_tag=0;assert(!risc_storage_volume_export(b));copy.export_tag=p->export_tag;
    copy.export_version=2;assert(!risc_storage_volume_export(b));copy.export_version=1;
    copy.export_read=NULL;assert(!risc_storage_volume_export(b));
    assert(!risc_storage_volume_export((const risc_storage_volume_api_v1 *)&api));
    return p;
}
static void assert_export_frozen(void) {
    const unsigned before=calls;
    frozen_io();
    assert(!prepare_power_down(NULL) && !cancel_power_down(NULL) && !commit_power_down(NULL));
    assert(!prepare_sleep(NULL) && !commit_sleep(NULL));
    const int32_t resumed=resume_sleep(NULL);
    assert(resumed==RISC_STORAGE_SLEEP_REFUSED || resumed==RISC_STORAGE_SLEEP_RETAINED);
    assert(!quiesce() && calls==before);
}
static void assert_export_retained(risc_storage_export_token_t token) {
    const unsigned before=calls,writes=card_writes;
    uint64_t saved[49];memcpy(saved,tokens,sizeof(saved));
    uint8_t bytes[512];
    assert(export_read(NULL,token,0,1,bytes)==RISC_STORAGE_EXPORT_RETAINED);
    assert(export_write(NULL,token,0,1,bytes)==RISC_STORAGE_EXPORT_RETAINED);
    assert(export_sync(NULL,token)==RISC_STORAGE_EXPORT_RETAINED);
    assert(export_end(NULL,token)==RISC_STORAGE_EXPORT_RETAINED);
    assert_export_frozen();assert(!START());
    assert(calls==before && card_writes==writes && fixture_lock && operation_mutex);
    assert(!memcmp(saved,tokens,sizeof(saved)) && !destroys);
}
static void csd_parser_cases(void) {
    uint8_t r[17]={0x3f};uint64_t n=1;r[1]=0x40;r[6]=9;
    r[8]=0x3f;r[9]=r[10]=0xff;r[16]=(uint8_t)((risc_sd_crc7(r+1,15)<<1)|1);
    assert(decode_csd(r,true,&n) && n==(UINT64_C(1)<<32));
    assert(!decode_csd(r,false,&n) && !n);
    r[16]^=2;assert(!decode_csd(r,true,&n) && !n);r[16]^=2;
    r[16]&=0xfe;assert(!decode_csd(r,true,&n) && !n);
    memset(r,0,sizeof(r));r[0]=0x3f;r[6]=9;
    /* C_SIZE=4095, C_SIZE_MULT=7, READ_BL_LEN=9: 1GiB SDSC. */
    r[7]=3;r[8]=0xff;r[9]=0xc0;r[10]=3;r[11]=0x80;
    r[16]=(uint8_t)((risc_sd_crc7(r+1,15)<<1)|1);
    assert(decode_csd(r,false,&n) && n==(UINT64_C(1)<<21));
    assert(!decode_csd(r,true,&n) && !n);
    r[1]=0x80;r[16]=(uint8_t)((risc_sd_crc7(r+1,15)<<1)|1);
    assert(!decode_csd(r,true,&n) && !n);
}
static void export_cases(const char *scenario) {
    const risc_storage_volume_api_v1_export *v=export_api();
    risc_storage_export_token_t token=0;uint64_t blocks=0;uint32_t size=0;
    uint8_t bytes[4096]={0},readback[4096]={0};
    if(!strcmp(scenario,"export-csd")){csd_parser_cases();verify_cleanup();return;}
    if(!strcmp(scenario,"export-absent"))absent=true;
    if(!strcmp(scenario,"export-unformatted"))card_image[510]=0;
    if(!strcmp(scenario,"export-bad-csd"))export_bad_csd=true;
    if(!strcmp(scenario,"export-log") || !strcmp(scenario,"export-log-close") || !strcmp(scenario,"export-log-retained"))source_enabled=true;
    if(!strcmp(scenario,"export-log-retained"))log_fail_close=true;
    const bool started_ok=START();
    if(!strcmp(scenario,"export-log-retained")){
        assert(!started_ok && bootlog_retained);
        assert(v->export_begin(NULL,&token,&blocks,&size)==RISC_STORAGE_EXPORT_RETAINED);
        assert_export_retained(token);return;
    }
    assert(started_ok);
    if(!strcmp(scenario,"export-absent") || !strcmp(scenario,"export-bad-csd")){
        assert(!ready(NULL) && !card_block_count && !card_writes);
        assert(v->export_begin(NULL,&token,&blocks,&size)==RISC_STORAGE_EXPORT_REFUSED);
        assert(!token && !blocks && !size && !bootlog_paused);verify_cleanup();return;
    }
    if(!strcmp(scenario,"export-owner")){
        owner=false;rejected_calls();owner=true;
        assert(enter());rejected_calls();assert(leave());
        fail_take=true;assert(v->export_begin(NULL,&token,&blocks,&size)==RISC_STORAGE_EXPORT_REFUSED);fail_take=false;
    }
    if(!strcmp(scenario,"export-generation")){
        export_generation=UINT64_MAX;
        assert(v->export_begin(NULL,&token,&blocks,&size)==RISC_STORAGE_EXPORT_REFUSED);
        assert(!token && ready(NULL));verify_cleanup();return;
    }
    if(!strcmp(scenario,"export-handles") || !strcmp(scenario,"export-close-retained")){
        write_sample();uint64_t file_size;uint32_t file=file_open_read(NULL,"/sample.bin",&file_size);assert(file);
        unsigned before=calls;
        assert(v->export_begin(NULL,&token,&blocks,&size)==RISC_STORAGE_EXPORT_REFUSED && calls==before);
        assert(file_close(NULL,file,true));
        file=file_open(NULL,"/sample.bin",RISC_STORAGE_OPEN_WRITE);assert(file);
        if(!strcmp(scenario,"export-close-retained")){
            assert(file_write(NULL,file,"x",1)==1);card_reject_write=true;
            assert(!file_close(NULL,file,true) && has_handles());
            before=calls;
            assert(v->export_begin(NULL,&token,&blocks,&size)==RISC_STORAGE_EXPORT_REFUSED && calls==before);
            assert(!quiesce());return;
        }
        before=calls;assert(v->export_begin(NULL,&token,&blocks,&size)==RISC_STORAGE_EXPORT_REFUSED && calls==before);
        assert(file_close(NULL,file,true));uint32_t dir=dir_open(NULL,"/");assert(dir);before=calls;
        assert(v->export_begin(NULL,&token,&blocks,&size)==RISC_STORAGE_EXPORT_REFUSED && calls==before);
        assert(dir_close_checked(NULL,dir));
    }
    if(!strcmp(scenario,"export-sleep")){
        assert(prepare_sleep(NULL));assert(v->export_begin(NULL,&token,&blocks,&size)==RISC_STORAGE_EXPORT_REFUSED);
        assert(resume_sleep(NULL)==RISC_STORAGE_SLEEP_READY);
        assert(prepare_power_down(NULL));assert(v->export_begin(NULL,&token,&blocks,&size)==RISC_STORAGE_EXPORT_REFUSED);
        assert(cancel_power_down(NULL));
    }
    if(!strcmp(scenario,"export-log") || !strcmp(scenario,"export-log-close"))source_revision=2;
    if(!strcmp(scenario,"export-log-close"))log_fail_close=true;
    if(!strcmp(scenario,"export-begin-sync"))force_dat_busy=true;
    if(!strcmp(scenario,"export-unmount"))export_fail_unmount=true;
    if(!strcmp(scenario,"export-begin-unlock"))fail_unlock=true;
    if(!strcmp(scenario,"export-log-close") || !strcmp(scenario,"export-begin-sync") ||
       !strcmp(scenario,"export-unmount") || !strcmp(scenario,"export-begin-unlock")){
        assert(v->export_begin(NULL,&token,&blocks,&size)==RISC_STORAGE_EXPORT_RETAINED);
        fail_unlock=false;force_dat_busy=false;export_fail_unmount=false;log_fail_close=false;
        assert_export_retained(token);return;
    }
    assert(v->export_begin(NULL,&token,&blocks,&size)==RISC_STORAGE_EXPORT_READY);
    assert(token && blocks==card_sectors && size==512 && !mounted && bootlog_paused && export_state==EXPORT_HOST);
    assert_export_frozen();
    risc_storage_export_token_t another=0;uint64_t other_blocks=0;uint32_t other_size=0;
    assert(v->export_begin(NULL,&another,&other_blocks,&other_size)==RISC_STORAGE_EXPORT_REFUSED && !another);
    if(!strcmp(scenario,"export-owner")){owner=false;rejected_calls();owner=true;assert(enter_lifecycle());rejected_calls();assert(leave());}
    if(!strcmp(scenario,"export-bounds")){
        const unsigned before=calls;
        assert(v->export_read(NULL,token,0,0,bytes)==RISC_STORAGE_EXPORT_REFUSED);
        assert(v->export_read(NULL,token,0,9,bytes)==RISC_STORAGE_EXPORT_REFUSED);
        assert(v->export_read(NULL,token,blocks,1,bytes)==RISC_STORAGE_EXPORT_REFUSED);
        assert(v->export_read(NULL,token,blocks-1,2,bytes)==RISC_STORAGE_EXPORT_REFUSED);
        assert(v->export_read(NULL,token,UINT64_MAX,2,bytes)==RISC_STORAGE_EXPORT_REFUSED);
        assert(v->export_write(NULL,token,0,1,NULL)==RISC_STORAGE_EXPORT_REFUSED);
        assert(v->export_write(NULL,token,UINT64_MAX,UINT32_MAX,bytes)==RISC_STORAGE_EXPORT_REFUSED);
        assert(v->export_read(NULL,token+1,0,1,bytes)==RISC_STORAGE_EXPORT_REFUSED);
        assert(v->export_write(NULL,0,0,1,bytes)==RISC_STORAGE_EXPORT_REFUSED);
        assert(v->export_sync(NULL,token+1)==RISC_STORAGE_EXPORT_REFUSED);
        assert(v->export_end(NULL,token+1)==RISC_STORAGE_EXPORT_REFUSED);
        assert(calls==before);
    }
    if(!strcmp(scenario,"export-read-crc")){
        card_bad_crc=true;assert(v->export_read(NULL,token,0,1,bytes)==RISC_STORAGE_EXPORT_RETAINED);
        card_bad_crc=false;assert_export_retained(token);return;
    }
    if(!strcmp(scenario,"export-write-fail")){
        card_reject_write=true;const unsigned before=card_writes;
        assert(v->export_write(NULL,token,blocks-1,1,bytes)==RISC_STORAGE_EXPORT_RETAINED);
        assert(card_writes==before+1);card_reject_write=false;assert_export_retained(token);return;
    }
    if(!strcmp(scenario,"export-sync")){
        force_dat_busy=true;assert(v->export_sync(NULL,token)==RISC_STORAGE_EXPORT_RETAINED);
        force_dat_busy=false;assert_export_retained(token);return;
    }
    assert(v->export_read(NULL,token,0,1,bytes)==RISC_STORAGE_EXPORT_READY);
    assert(!memcmp(bytes,card_image,512));
    if(!strcmp(scenario,"export-basic") || !strcmp(scenario,"export-bounds")){
        memset(bytes,0xa6,sizeof(bytes));assert(v->export_write(NULL,token,blocks-8,8,bytes)==RISC_STORAGE_EXPORT_READY);
        assert(v->export_read(NULL,token,blocks-8,8,readback)==RISC_STORAGE_EXPORT_READY);
        assert(!memcmp(bytes,readback,sizeof(bytes)));
    }
    assert(v->export_sync(NULL,token)==RISC_STORAGE_EXPORT_READY);
    if(!strcmp(scenario,"export-log")){
        assert(bootlog_seen[0].revision==2);source_revision=3;
        const unsigned before=log_writes;assert_export_frozen();
        char error_text[200];(void)bootlog_last_error(NULL,error_text,sizeof(error_text));
        assert(v->export_read(NULL,token,0,1,bytes)==RISC_STORAGE_EXPORT_READY);
        assert(v->export_sync(NULL,token)==RISC_STORAGE_EXPORT_READY && log_writes==before);
    }
    if(!strcmp(scenario,"export-end-unformatted")){
        assert(v->export_read(NULL,token,0,1,bytes)==RISC_STORAGE_EXPORT_READY);
        bytes[510]=0;assert(v->export_write(NULL,token,0,1,bytes)==RISC_STORAGE_EXPORT_READY);
    }
    if(!strcmp(scenario,"export-end-removed"))absent=true;
    if(!strcmp(scenario,"export-end-crc"))card_bad_crc=true;
    if(!strcmp(scenario,"export-end-unlock"))fail_unlock=true;
    if(!strcmp(scenario,"export-end-sync"))force_dat_busy=true;
    if(!strcmp(scenario,"export-end-crc") || !strcmp(scenario,"export-end-unlock") || !strcmp(scenario,"export-end-sync")){
        assert(v->export_end(NULL,token)==RISC_STORAGE_EXPORT_RETAINED);
        fail_unlock=false;force_dat_busy=false;card_bad_crc=false;assert_export_retained(token);return;
    }
    const bool unavailable=!strcmp(scenario,"export-unformatted") || !strcmp(scenario,"export-end-unformatted") || !strcmp(scenario,"export-end-removed");
    assert(v->export_end(NULL,token)==(unavailable?RISC_STORAGE_EXPORT_MEDIA_UNAVAILABLE:RISC_STORAGE_EXPORT_READY));
    assert(!export_token && export_state==EXPORT_LOCAL && !bootlog_paused);
    if(unavailable){assert(!ready(NULL));absent=false;card_image[510]=0x55;assert(refresh(NULL));}
    assert(ready(NULL));
    if(!strcmp(scenario,"export-log"))assert(bootlog_seen[0].revision==3);
    const unsigned before=calls;
    assert(v->export_end(NULL,token)==RISC_STORAGE_EXPORT_REFUSED);
    assert(v->export_read(NULL,token,0,1,bytes)==RISC_STORAGE_EXPORT_REFUSED && calls==before);
    if(!strcmp(scenario,"export-stale")){
        write_sample();uint64_t file_size;uint32_t old=file_open_read(NULL,"/sample.bin",&file_size);assert(old && file_close(NULL,old,true));
        uint32_t old_dir=dir_open(NULL,"/");assert(old_dir && dir_close_checked(NULL,old_dir));
        verify_cleanup();assert(START());
        risc_storage_export_token_t fresh=0;assert(v->export_begin(NULL,&fresh,&blocks,&size)==RISC_STORAGE_EXPORT_READY && fresh!=token);
        assert(v->export_read(NULL,token,0,1,bytes)==RISC_STORAGE_EXPORT_REFUSED);
        assert(v->export_end(NULL,fresh)==RISC_STORAGE_EXPORT_READY);
        assert(!file_read(NULL,old,bytes,1) && !file_close(NULL,old,true) && !dir_close_checked(NULL,old_dir));
        uint32_t now=file_open_read(NULL,"/sample.bin",&file_size);assert(now && now!=old && file_close(NULL,now,true));
    }
    verify_cleanup();
}
