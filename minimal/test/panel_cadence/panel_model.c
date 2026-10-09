/* The unmodified production provider over its existing scoped GPIO fixture. */
#define main panel_original_fixture_main
#include "../panel_test.c"
#undef main

static garden_gpio_v1 native;
static risc_hw_spi_display_v1 config;
static risc_hardware_device_v1 device;
static risc_platform_clock_api_v1 clock_api_model;
static risc_provider_sync_api_v1 sync_model;
static x4_power_ready_api_v1 power_model;
static risc_frontlight_api_v1 light_model;
static unsigned measured_writes, measured_polls, max_slice_writes, max_slice_ms;
static unsigned payload_polls,min_budget,max_budget;
static uint64_t measured_at;

const risc_display_output_api_v1 *panel_cadence_start(unsigned gpio_writes_per_ms) {
    async_model = true; chip = PROBE_UC8279;
    native = (garden_gpio_v1){.api_version=1,.struct_size=sizeof(native),.claim=fake_claim,
        .write=fake_write,.read=fake_read,.release=fake_release,.deep_sleep_hold=fake_hold};
#ifdef GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE
    native.retire_held_output = fake_retire;
#endif
    config = (risc_hw_spi_display_v1){.struct_size=sizeof(config),
        .bus={.struct_size=sizeof(config.bus),.kind=1,.instance_id=101,.controller=0,
        .frequency_hz=1000000,.sclk=12,.mosi=11,.miso=-1,.sda=-1,.scl=-1},
        .width=800,.height=480,.offset_y=120,.cs=13,.dc=18,.reset=14,.backlight=-1,
        .busy=6,.reset_assert_ms=50,.reset_recovery_ms=50};
    device = (risc_hardware_device_v1){1,sizeof(device),2,"ultrachip,uc8279",
        "unspecified","display.spi",1,sizeof(config),&config};
    clock_api_model = (risc_platform_clock_api_v1){1,sizeof(clock_api_model),NULL,fake_time,fake_sleep};
    sync_model = (risc_provider_sync_api_v1){1,sizeof(sync_model),NULL,fake_owner,fake_create,fake_lock,fake_unlock,fake_destroy};
    power_model = (x4_power_ready_api_v1){1,sizeof(power_model),NULL,fake_ready};
    light_model = (risc_frontlight_api_v1){1,sizeof(light_model),NULL,fake_light,NULL};
    risc_provider_dependency_v1 deps[] = {{"hardware.device",1,&device},{"platform.gpio",1,&native},
        {"platform.clock",1,&clock_api_model},{"platform.sync",1,&sync_model},
        {"board.power.ready",1,&power_model},{"display.frontlight",1,&light_model}};
    assert(t5_driver_get(2)->start(deps,6));
    display=t5_driver_get(2)->capability; charge_every=gpio_writes_per_ms;
    return display;
}
uint32_t panel_cadence_clock(void) { return (uint32_t)fake_time(NULL); }
void panel_cadence_delay(uint32_t ms) { fake_now += ms; }
void panel_cadence_poll(uint32_t budget) {
    const uint64_t began=fake_now;const unsigned before=writes;
    const uint32_t old_bytes=present_state==PRESENT_QUEUED?0:bytes_sent;
    assert(budget && budget<=8);
    ((const risc_driver_poll_v2 *)t5_driver_get(2))->poll(budget);
    ++measured_polls;
    if(bytes_sent>old_bytes)++payload_polls;
    if(!min_budget||budget<min_budget)min_budget=budget;
    if(budget>max_budget)max_budget=budget;
    if(writes-before>max_slice_writes)max_slice_writes=writes-before;
    if(fake_now-began>max_slice_ms)max_slice_ms=(unsigned)(fake_now-began);
    assert(!lock_held && writes-before<=512u*24u+1000u);
}
void panel_cadence_reset_metrics(void) {
    measured_at=fake_now;measured_writes=writes;measured_polls=max_slice_writes=max_slice_ms=0;
    payload_polls=min_budget=max_budget=0;
}
void panel_cadence_report(const char *label) {
    const risc_display_present_metrics_v1 observed=snapshot_metrics();
    assert(observed.token==pending_token && observed.state==PRESENT_COMPLETE && observed.valid_times==63);
    assert(observed.bytes_sent==bytes_sent && observed.gpio_write_calls==writes-measured_writes);
    printf("\"%s\":{\"elapsed_ms\":%llu,\"transfer_ms\":%llu,\"bytes\":%u,\"gpio_writes\":%u,"
        "\"provider_polls\":%u,\"max_slice_ms\":%u,\"max_slice_writes\":%u,\"partial\":%s,"
        "\"damage\":[%d,%d,%u,%u],\"state\":%u,\"payload_polls\":%u,\"budget_ms\":[%u,%u],\"wire_hash\":\"%016llx\","
        "\"metric_timestamps_ms\":[%llu,%llu,%llu,%llu,%llu,%llu]}",label,(unsigned long long)(fake_now-measured_at),
        (unsigned long long)(transfer_end_ms-transfer_start_ms),bytes_sent,writes-measured_writes,
        measured_polls,max_slice_ms,max_slice_writes,partial_update?"true":"false",
        update_area.x,update_area.y,update_area.width,update_area.height,present_state,
        payload_polls,min_budget,max_budget,(unsigned long long)wire_hash,
        (unsigned long long)observed.queued_ms,(unsigned long long)observed.transfer_start_ms,
        (unsigned long long)observed.transfer_end_ms,(unsigned long long)observed.refresh_ms,
        (unsigned long long)observed.busy_assert_ms,(unsigned long long)observed.busy_done_ms);
}
void panel_cadence_stop(void) { assert(t5_driver_get(2)->quiesce()); }
