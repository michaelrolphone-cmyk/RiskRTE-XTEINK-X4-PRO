/* Actual SSD provider; only the existing GPIO/clock model is synthetic. */
static void ssd_async_cases(const char *scenario,const risc_driver_v2 *base,
                            risc_display_surface_v1 *surface,uint64_t token) {
    const risc_driver_poll_v2 *d=(const risc_driver_poll_v2 *)base;
    const risc_display_output_api_v1_power *power=risc_display_output_power(display);
    risc_display_present_status_v1 status={0};
    assert(chip==PROBE_SSD && async_model && power);
    if (!strcmp(scenario,"ssd-async-controls")) {
        const unsigned before=writes,reads=pin_reads,times=clock_reads;
        d->poll(0);owner=false;d->poll(8);owner=true;lock_held=true;d->poll(8);lock_held=false;
        assert(writes==before && pin_reads==reads && clock_reads==times && present_state==PRESENT_QUEUED);
        d->poll(1);assert(present_state==PRESENT_ACTIVE && bytes_sent<=512u);
        const unsigned active_writes=writes;
        assert(display->wait_present(NULL,token,1,&status) && status.state==PRESENT_ACTIVE && writes==active_writes);
        display->release(NULL,surface->frame);assert(held && !base->quiesce());
        assert(power->prepare(NULL,1500)==RISC_DISPLAY_POWER_BUSY && writes==active_writes);
        complete(token);assert(base->quiesce());return;
    }
    if (!strcmp(scenario,"ssd-async-pre-busy") || !strcmp(scenario,"ssd-async-window-busy")) {
        if (strstr(scenario,"window")) {
            charge_every=24;d->poll(1);charge_every=0;
            assert(async_stage>=SSD_ASYNC_WINDOW_X && async_stage<=SSD_ASYNC_CURSOR_Y);
        }
        phase=RAM;d->poll(8);assert(present_state==PRESENT_ACTIVE);
        const uint64_t limit=ssd_ready_deadline;
        fake_now=limit;d->poll(8);assert(present_state==PRESENT_FAILED && !refreshes && !bytes_sent);
        assert(!strcmp(reason,"pre-transfer readiness"));phase=IDLE;assert(base->quiesce());return;
    }
    if (!strcmp(scenario,"ssd-async-clock") || !strcmp(scenario,"ssd-async-rollback") || !strcmp(scenario,"ssd-async-overflow")) {
        bad_clock=!strcmp(scenario,"ssd-async-clock");rollback_clock=!strcmp(scenario,"ssd-async-rollback");
        if (!strcmp(scenario,"ssd-async-overflow")) fake_now=UINT64_MAX-9u;
        d->poll(8);assert(present_state==PRESENT_FAILED && !bytes_sent && !refreshes);
        const unsigned before=writes;d->poll(8);assert(writes==before);return;
    }
    if (!strcmp(scenario,"ssd-async-write") || !strcmp(scenario,"ssd-async-read") || !strcmp(scenario,"ssd-async-unlock")) {
        d->poll(8);assert(present_state==PRESENT_ACTIVE);
        fail_write=!strcmp(scenario,"ssd-async-write");unlock_ok=strcmp(scenario,"ssd-async-unlock")!=0;
        if (!strcmp(scenario,"ssd-async-read")) { async_stage=SSD_ASYNC_READY;fail_read=true; }
        d->poll(8);assert(retained && !base->quiesce());
        const unsigned before=writes;d->poll(8);assert(writes==before);
        assert(!display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,surface));return;
    }
    if (!strcmp(scenario,"ssd-async-admission-compat")) {
        /* Explicit synchronous callers retain their timeout/abort contract. */
        charge_every=20;assert(display->wait_present(NULL,token,5,&status));
        assert(status.state==PRESENT_FAILED && !refreshes && bytes_sent<FRAME_BYTES*2u);return;
    }
    bool never=!strcmp(scenario,"ssd-async-never"),absent=!strcmp(scenario,"ssd-async-no-assert");
    bool frozen=!strcmp(scenario,"ssd-async-frozen");
    stuck_refresh=never || frozen;absent_busy=absent;require_busy_observation=true;
    refresh_delay_ms=strstr(scenario,"2300")?2300u:strstr(scenario,"17")?17u:0u;
    charge_every=frozen?0u:2400u;
    unsigned logical_changes=0,max_writes=0,max_elapsed=0,assert_polls=0;
    const uint64_t began=fake_now;
    for (unsigned n=0;n<20000u;++n) {
        const unsigned before=writes,bytes=bytes_sent;const uint64_t at=fake_now;
        const uint32_t budget=n%3u==0u?1u:n%3u==1u?8u:UINT32_MAX;
        d->poll(budget);++async_calls;
        const unsigned elapsed=(unsigned)(fake_now-at),slice_writes=writes-before;
        if(elapsed>max_elapsed)max_elapsed=elapsed;
        if(slice_writes>max_writes)max_writes=slice_writes;
        assert(!lock_held && bytes_sent-bytes<=512u && slice_writes<=512u*24u+1000u);
        assert(elapsed<=(budget>8u?8u:budget)+1u); /* 2400 GPIO callbacks per modeled ms. */
        const unsigned observed=writes;
        assert(display->present_status(NULL,token,&status) && writes==observed);
        if(status.state==PRESENT_COMPLETE || status.state==PRESENT_FAILED)break;
        ++logical_changes; /* Owner can consume/update the input model here. */
        display->release(NULL,surface->frame);assert(held && !base->quiesce());
        assert(power->prepare(NULL,0)==RISC_DISPLAY_POWER_BUSY);
        if(async_stage==SSD_ASYNC_ASSERT || async_stage==SSD_ASYNC_DONE)++assert_polls;
        if(frozen) {
            if(n==1000u){assert(present_state==PRESENT_ACTIVE && logical_changes==1001u);fake_now=async_deadline;}
        } else ++fake_now;
    }
    assert(logical_changes>100u);
    assert(bytes_sent==FRAME_BYTES*2u && refreshes==1u);
    if(never || absent || frozen) {
        assert(status.state==PRESENT_FAILED && !(metrics.valid_times&RISC_DISPLAY_METRICS_BUSY_DONE));
        if(never || frozen)assert(!base->quiesce() && !poweroffs);
        const unsigned before=writes;d->poll(8);assert(writes==before);
    } else {
        assert(status.state==PRESENT_COMPLETE && metrics.valid_times==63u && !held);
        assert(busy_done_ms>=busy_assert_ms && busy_done_ms-busy_assert_ms>=refresh_delay_ms);
        if(refresh_delay_ms==2300u)assert(assert_polls>=2299u);
        assert(display->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,surface));
        memset(surface->pixels,(uint8_t)logical_changes,surface->size_bytes);
        display->release(NULL,surface->frame);assert(base->quiesce());
    }
    printf("SSD progress refresh_ms=%u changes=%u elapsed=%llu max_slice_ms=%u max_slice_writes=%u pending_polls=%u terminal=%u\n",
        refresh_delay_ms,logical_changes,(unsigned long long)(fake_now-began),max_elapsed,max_writes,assert_polls,status.state);
}
