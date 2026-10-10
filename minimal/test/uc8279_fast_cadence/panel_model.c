#define main fast_fixture_main
#include "../uc8279_fast_test.c"
#undef main

static garden_gpio_v1 gpio_model;
static garden_spi_v1 spi_model;
static risc_hw_spi_display_v1 config_model;
static risc_hardware_device_v1 device_model;
static risc_platform_clock_api_v1 clock_model;
static risc_provider_sync_api_v1 sync_model;
static x4_power_ready_api_v1 power_model;
static risc_frontlight_api_v1 light_model;
static unsigned measured_writes, measured_polls, measured_exchanges, measured_payload;
static unsigned measured_settle_writes;
static unsigned max_slice_ms, max_slice_writes, max_slice_bytes, min_budget, max_budget;
static uint64_t measured_at, wire_us;
static uint32_t model_hz;
static bool timed_begin(void*c,uint64_t t,uint32_t hz,uint8_t mode,uint32_t ms){model_hz=hz;return spi_begin(c,t,hz,mode,ms);}
static bool timed_exchange(void*c,uint64_t t,const uint8_t*tx,uint8_t*rx,size_t n){
    bool ok=spi_exchange(c,t,tx,rx,n);
    wire_us+=(uint64_t)n*8u*1000000u/model_hz;tick+=wire_us/1000u;wire_us%=1000u;
    return ok;
}
const risc_display_output_api_v1 *panel_cadence_start(unsigned unused) {
    (void)unused;
    gpio_model=(garden_gpio_v1){.api_version=1,.struct_size=sizeof(gpio_model),.claim=gpio_claim,.write=gpio_write,.read=gpio_read,.release=gpio_release,.deep_sleep_hold=gpio_hold,.retire_held_output=gpio_retire};
    spi_model=(garden_spi_v1){.api_version=1,.struct_size=sizeof(spi_model),.begin=timed_begin,.exchange=timed_exchange,.end=spi_end,.release=spi_release,.claim_three_wire=spi_claim};
    clock_model=(risc_platform_clock_api_v1){1,sizeof(clock_model),NULL,time_now,delay};
    sync_model=(risc_provider_sync_api_v1){1,sizeof(sync_model),NULL,own,create,lock,unlock,destroy};
    power_model=(x4_power_ready_api_v1){1,sizeof(power_model),NULL,ready};
    light_model=(risc_frontlight_api_v1){1,sizeof(light_model),NULL,light,NULL};
    config_model=(risc_hw_spi_display_v1){.struct_size=sizeof(config_model),.bus={.struct_size=sizeof(config_model.bus),.kind=1,.instance_id=101,.controller=0,.frequency_hz=20000000,.sclk=12,.mosi=11,.miso=-1,.sda=-1,.scl=-1},.width=800,.height=480,.offset_y=120,.cs=13,.dc=18,.reset=14,.backlight=-1,.busy=6,.reset_assert_ms=50,.reset_recovery_ms=50};
    device_model=(risc_hardware_device_v1){1,sizeof(device_model),3,"ultrachip,uc8279","unspecified","display.spi",1,sizeof(config_model),&config_model};
    risc_provider_dependency_v1 deps[]={{"hardware.device",1,&device_model},{"platform.gpio",1,&gpio_model},{"platform.clock",1,&clock_model},{"platform.sync",1,&sync_model},{"board.power.ready",1,&power_model},{"display.frontlight",1,&light_model},{"spi.bus",1,&spi_model}};
    assert(t5_driver_get(2)->start(deps,7));output=t5_driver_get(2)->capability;return output;
}
uint32_t panel_cadence_clock(void){return (uint32_t)tick;}
void panel_cadence_delay(uint32_t ms){tick+=ms;}
void panel_cadence_poll(uint32_t budget){
    const uint64_t began=tick;const unsigned before=gpio_writes,bytes=payload;
    const bool background=settle_stage!=SETTLE_NONE;
    ((const risc_driver_poll_v2*)t5_driver_get(2))->poll(budget);++measured_polls;
    if(background)measured_settle_writes+=gpio_writes-before;
    if(!min_budget||budget<min_budget)min_budget=budget;if(budget>max_budget)max_budget=budget;
    if(tick-began>max_slice_ms)max_slice_ms=(unsigned)(tick-began);
    if(gpio_writes-before>max_slice_writes)max_slice_writes=gpio_writes-before;
    if(payload-bytes>max_slice_bytes)max_slice_bytes=payload-bytes;
    assert(payload-bytes<=16384u&&!locked);
}
void panel_cadence_reset_metrics(void){
    measured_at=tick;measured_writes=gpio_writes;measured_exchanges=exchanges;measured_payload=payload;
    measured_polls=measured_settle_writes=max_slice_ms=max_slice_writes=max_slice_bytes=min_budget=max_budget=0;
}
void panel_cadence_report(const char*label){
    const risc_display_present_metrics_v1 m=snapshot();assert(m.state==PRESENT_COMPLETE&&m.valid_times==63);
    assert(m.bytes_sent==payload-measured_payload&&m.gpio_write_calls==gpio_writes-measured_writes-measured_settle_writes);
    printf("\"%s\":{\"elapsed_ms\":%llu,\"transfer_ms\":%llu,\"bytes\":%u,\"gpio_writes\":%u,\"native_exchanges\":%u,\"provider_polls\":%u,\"max_slice_ms\":%u,\"max_slice_bytes\":%u,\"max_slice_writes\":%u,\"partial\":%s,\"directional\":%s,\"absolute\":%s,\"lut_frames\":%u,\"damage\":[%d,%d,%u,%u],\"state\":%u,\"budget_ms\":[%u,%u]}",
        label,(unsigned long long)(tick-measured_at),(unsigned long long)(m.transfer_end_ms-m.transfer_start_ms),m.bytes_sent,m.gpio_write_calls,exchanges-measured_exchanges,measured_polls,max_slice_ms,max_slice_bytes,max_slice_writes,partial_update?"true":"false",directional_overdrive?"true":"false",absolute_update?"true":"false",fast_lut_frames,update_area.x,update_area.y,update_area.width,update_area.height,m.state,min_budget,max_budget);
}
void panel_cadence_stop(void){assert(t5_driver_get(2)->quiesce());}
static risc_display_present_metrics_v1 completed_metrics;
static unsigned measured_refreshes;
bool panel_cadence_settle_active(void){return settle_stage!=SETTLE_NONE;}
void panel_cadence_settle_begin(void){
    assert(settle_stage!=SETTLE_NONE);completed_metrics=snapshot();
    measured_refreshes=refreshes-settle_refreshes;panel_cadence_reset_metrics();measured_at=busy_done_ms;
}
void panel_cadence_settle_report(void){
    const risc_display_present_metrics_v1 m=snapshot();
    assert(!settle_stage&&!ptin&&!model_bus_held&&!presentation_fault);
    assert(!memcmp(&m,&completed_metrics,sizeof(m))&&payload-measured_payload==180000u);
    assert(dtm1_synced&&!screen_powered);
    assert(settle_completed==settle_refreshes&&settle_completed==refreshes-measured_refreshes&&settle_completed>1);
    assert(tick>=settle_until&&last_refresh_at>=settle_until);
    printf("\"elapsed_ms\":%llu,\"bytes\":%u,\"repeats\":%u,\"completed_repeats\":%u,\"provider_polls\":%u,\"max_slice_ms\":%u,\"max_slice_bytes\":%u,\"budget_ms\":[%u,%u]",
        (unsigned long long)(tick-measured_at),payload-measured_payload,settle_refreshes,settle_completed,measured_polls,max_slice_ms,max_slice_bytes,min_budget,max_budget);
}
static uint64_t measured_maintenance_due;
void panel_cadence_maintenance_begin(void){
    /* The deployed 0.1.9 baseline finalizes both planes and powers off.
     * Retain this fixture hook to prove thirty seconds stays completely quiet. */
    assert(dtm1_synced&&!screen_powered&&!settle_stage);
    measured_maintenance_due=tick+30000u;measured_refreshes=refreshes;
    completed_metrics=snapshot();panel_cadence_reset_metrics();
}
bool panel_cadence_maintenance_done(void){return !settle_stage&&tick>=measured_maintenance_due;}
void panel_cadence_maintenance_report(void){
    const risc_display_present_metrics_v1 m=snapshot();
    assert(panel_cadence_maintenance_done()&&!ptin&&!model_bus_held&&!presentation_fault);
    assert(!memcmp(&m,&completed_metrics,sizeof(m))&&payload==measured_payload);
    assert(refreshes==measured_refreshes&&dtm1_synced&&!screen_powered);
    assert(exchanges==measured_exchanges&&gpio_writes==measured_writes);
    printf("\"elapsed_ms\":%llu,\"bytes\":%u,\"repeats\":%u,\"provider_polls\":%u,\"max_slice_ms\":%u,\"max_slice_bytes\":%u,\"budget_ms\":[%u,%u]",
        (unsigned long long)(tick-measured_at),payload-measured_payload,refreshes-measured_refreshes,measured_polls,max_slice_ms,max_slice_bytes,min_budget,max_budget);
}
