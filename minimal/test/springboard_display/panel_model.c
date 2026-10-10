/* The selected production provider and its existing GPIO/SPI panel model. */
#include "../uc8279_fast_cadence/panel_model.c"

static const char *failure_case;
static unsigned injected_gaps, slow_services, deadline_rejections;
static uint64_t target_busy_from,target_busy_until;
static bool deadline_exchange(void*c,uint64_t t,const uint8_t*tx,uint8_t*rx,size_t n) {
    /* Match CpuPort::spiTransfer: the begin deadline includes time between
     * owner polls. A denied transfer is an I/O failure, not a BUSY result. */
    if(tick>=bus_deadline){++deadline_rejections;return false;}
    return timed_exchange(c,t,tx,rx,n);
}
const risc_display_output_api_v1 *panel_handoff_start(const char *name) {
    const risc_display_output_api_v1 *result=panel_cadence_start(0);
    spi_model.exchange=deadline_exchange;
    baseline();fast_band(190,80);
    while(settle_stage){panel_cadence_poll(8);tick+=1;}
    failure_case=name;panel_cadence_reset_metrics();
    refresh_pulse_ms=82;
    if(!strcmp(name,"missed-busy"))refresh_assert_delay=1;
    if(!strcmp(name,"absent-busy"))no_busy=true;
    if(!strcmp(name,"stuck-busy"))stuck_busy=true;
    return result;
}
bool panel_handoff_plane_active(void) {
    return present_state==PRESENT_ACTIVE && spi_held && async_stage==UC_ASYNC_NEW;
}
void panel_handoff_after_poll(void) {
    if(!failure_case)return;
    if(refresh_ms && !target_busy_from){target_busy_from=busy_from;target_busy_until=busy_until;}
    if(injected_gaps)return;
    if((!strcmp(failure_case,"missed-busy") && async_stage==UC_ASYNC_ASSERT) ||
       (!strcmp(failure_case,"observed-busy-gap") && async_stage==UC_ASYNC_DONE)) {
        ++injected_gaps;tick+=120;
    }
}
void panel_handoff_service(uint32_t budget) {
    assert(budget==1000);
    if(failure_case && !strcmp(failure_case,"slow-service") && panel_handoff_plane_active()) {
        ++slow_services;tick+=600;
    }
}
void panel_handoff_report(void) {
    char text[512];assert(last_error(text,sizeof(text)));
    printf("\"provider\":{\"state\":%u,\"retained\":%s,\"presentation_fault\":%s,"
        "\"bytes\":%u,\"deadline_rejections\":%u,\"slow_services\":%u,\"poll_gaps\":%u,"
        "\"refresh_ms\":%llu,\"assert_ms\":%llu,\"physical_busy_from\":%llu,\"physical_busy_until\":%llu,"
        "\"diagnostic\":\"%s\"}",
        present_state,retained?"true":"false",presentation_fault?"true":"false",bytes_sent,
        deadline_rejections,slow_services,injected_gaps,(unsigned long long)refresh_ms,
        (unsigned long long)busy_assert_ms,(unsigned long long)target_busy_from,(unsigned long long)target_busy_until,text);
    if(presentation_fault){risc_display_surface_v1 f={0};assert(!output->acquire(NULL,RISC_DISPLAY_FORMAT_MONO1,&f));}
}
