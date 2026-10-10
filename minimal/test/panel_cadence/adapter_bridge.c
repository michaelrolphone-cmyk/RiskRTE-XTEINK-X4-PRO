/* Compile the exact supplied System adapter and its existing scoped fixtures. */
#define main adapter_original_fixture_main
#define risc_runtime_get_api cadence_fixture_runtime_api
#include PANEL_ADAPTER_FIXTURE
#undef risc_runtime_get_api
#undef main

extern const risc_display_output_api_v1 *panel_cadence_start(unsigned);
extern uint32_t panel_cadence_clock(void);
extern void panel_cadence_reset_metrics(void);
extern void panel_cadence_report(const char *);
extern void panel_cadence_stop(void);
extern void panel_runtime_yield(uint32_t);
extern void panel_runtime_reset_metrics(void);
extern void panel_runtime_report(void);
extern void panel_runtime_expect_idle(uint32_t);
#ifdef PANEL_RESIDENT_SETTLE
extern bool panel_cadence_settle_active(void);
extern void panel_cadence_settle_begin(void);
extern void panel_cadence_settle_report(void);
#endif
#ifdef PANEL_RESIDENT_MAINTENANCE
extern void panel_cadence_maintenance_begin(void);
extern bool panel_cadence_maintenance_done(void);
extern void panel_cadence_maintenance_report(void);
#endif
static const risc_display_output_api_v1 *real_panel;
static unsigned input_polls,status_polls,max_input_gap;
static uint32_t last_input_at;
static unsigned touch_samples,max_touch_gap;
static uint32_t last_touch_at;
static risc_touch_api_v1 cadence_touch;
#ifdef PORTABLE_RASTER_SNAPSHOT
static unsigned software_model_steps,busy_model_steps,software_yields,busy_yields;
static unsigned software_touch_samples,busy_touch_samples,software_acquires;
static unsigned lease_live,lease_acquires,lease_submits;
static unsigned software_pending_returns,provider_pending_returns;
static unsigned max_software_model_gap,max_busy_model_gap;
static bool cadence_frame_measurement;
static bool cadence_frame(void *c,uint32_t format,risc_display_surface_v1 *out) {
    assert(!lease_live);
    if(raster_sealed){assert(raster_replaying&&!raster_cursor);++software_acquires;}
    bool ok=real_panel->acquire(c,format,out);
    if(ok){++lease_live;++lease_acquires;}
    return ok;
}
static void cadence_frame_release(void *c,risc_display_frame_v1 frame) {
    assert(lease_live==1);real_panel->release(c,frame);--lease_live;
}
static bool cadence_submit(void *c,risc_display_frame_v1 frame,const risc_display_rect_v1 *rect,size_t n,
    const risc_display_present_options_v1 *options,risc_display_present_token_v1 *token) {
    assert(lease_live==1);bool ok=real_panel->submit(c,frame,rect,n,options,token);
    if(ok){--lease_live;++lease_submits;}return ok;
}
void panel_adapter_before_yield(void) {
    if(!cadence_frame_measurement)return;
    assert(!lease_live&&!surface.frame);
    if(raster_sealed)++software_yields;
    if(paper_token)++busy_yields;
}
#else
void panel_adapter_before_yield(void) {}
#endif
static bool cadence_health(risc_runtime_health_v1 *out){out->uptime_ms=panel_cadence_clock();return true;}
static bool cadence_status(void *c,risc_display_present_token_v1 t,risc_display_present_status_v1 *out){
    ++status_polls;return real_panel->present_status(c,t,out);
}
static risc_display_output_api_v1 panel_api;
static bool cadence_snapshot(void *c,risc_touch_snapshot_v1 *out){
    const uint32_t now=panel_cadence_clock();++touch_samples;
#ifdef PORTABLE_RASTER_SNAPSHOT
    if(cadence_frame_measurement){
        if(raster_sealed){assert(!lease_live&&!surface.frame);++software_touch_samples;}
        if(paper_token)++busy_touch_samples;
    }
#endif
    if(now-last_touch_at>max_touch_gap)max_touch_gap=now-last_touch_at;
    last_touch_at=now;return fx_snapshot(c,out);
}
static bool cadence_acquire(const char *name,uint32_t version,uint64_t instance,risc_runtime_capability_v1 *out){
    bool ok=fx_acquire(name,version,instance,out);
    if(ok&&!strcmp(name,"display.output"))out->api=&panel_api;
    if(ok&&!strcmp(name,"input.touch.raw"))out->api=&cadence_touch;
    return ok;
}
static void measure_frame(unsigned interval,bool partial) {
    panel_cadence_reset_metrics();panel_runtime_reset_metrics();input_polls=status_polls=max_input_gap=0;
    last_input_at=last_touch_at=panel_cadence_clock();touch_samples=max_touch_gap=0;
    const paper_presentation *v=paper_presentation_get();assert(v);
#ifdef PORTABLE_RASTER_SNAPSHOT
    software_model_steps=busy_model_steps=software_yields=busy_yields=0;
    software_touch_samples=busy_touch_samples=software_acquires=lease_acquires=lease_submits=0;
    max_software_model_gap=max_busy_model_gap=0;software_pending_returns=provider_pending_returns=0;cadence_frame_measurement=true;assert(!lease_live);
#endif
#ifndef PANEL_BASELINE_ADAPTER
    assert(portable_paper_frame_ready());
#endif
    v->begin();v->text(20,20,300,"PANEL CADENCE",1,false,true);
    if(partial)v->circle(350,350,2,true);
    present(false);
#ifndef PANEL_BASELINE_ADAPTER
    while((paper_token
#ifdef PORTABLE_RASTER_SNAPSHOT
        ||raster_sealed
#endif
        )&&!failed){
#ifdef PORTABLE_RASTER_SNAPSHOT
        const bool was_software=raster_sealed,was_busy=paper_token!=0;
        assert(!lease_live&&!surface.frame);
#endif
        t5_app_input_t input={0};assert(poll_input(&input,interval));++input_polls;
        const uint32_t now=panel_cadence_clock();
        if(now-last_input_at>max_input_gap)max_input_gap=now-last_input_at;
#ifdef PORTABLE_RASTER_SNAPSHOT
        assert(!lease_live&&!surface.frame);
        if(raster_sealed)++software_pending_returns;
        if(paper_token)++provider_pending_returns;
        if(was_software){++software_model_steps;if(now-last_input_at>max_software_model_gap)max_software_model_gap=now-last_input_at;}
        if(was_busy){++busy_model_steps;if(now-last_input_at>max_busy_model_gap)max_busy_model_gap=now-last_input_at;}
#endif
        last_input_at=now;
    }
#else
    (void)interval;
#endif
    assert(!failed);
#ifdef PORTABLE_RASTER_SNAPSHOT
    cadence_frame_measurement=false;
    assert(software_model_steps>0&&software_yields>0&&software_touch_samples>0);
    /* A short BUSY pulse may complete within one requested owner-loop wait. */
    assert(busy_yields>0&&busy_touch_samples>0);
    assert(!lease_live&&lease_acquires==1&&lease_submits==1&&software_acquires==1);
#endif
    panel_cadence_report(partial?"partial":"full");printf(",");panel_runtime_report();
    printf(",\"controller_polls\":%u,\"status_polls\":%u,\"max_controller_gap_ms\":%u,"
        "\"touch_samples\":%u,\"max_touch_gap_ms\":%u",input_polls,status_polls,max_input_gap,touch_samples,max_touch_gap);
#ifdef PORTABLE_RASTER_SNAPSHOT
    printf(",\"software_phase_entry_steps\":%u,\"provider_busy_model_steps\":%u,\"software_runtime_yields\":%u,\"provider_busy_runtime_yields\":%u,"
        "\"software_touch_samples\":%u,\"provider_busy_touch_samples\":%u,\"max_software_phase_entry_gap_ms\":%u,\"max_provider_busy_model_gap_ms\":%u,"
        "\"provider_frame_acquires\":%u,\"provider_submits\":%u,\"lease_free_model_returns\":true,\"lease_free_software_runtime_yields\":true",
        software_model_steps,busy_model_steps,software_yields,busy_yields,software_touch_samples,busy_touch_samples,
        max_software_model_gap,max_busy_model_gap,lease_acquires,lease_submits);
    printf(",\"software_pending_model_returns\":%u,\"provider_pending_model_returns\":%u",software_pending_returns,provider_pending_returns);
#endif
}
void panel_adapter_cadence(void) {
    const unsigned interval=(unsigned)atoi(getenv("PANEL_APP_WAIT_MS"));
    const unsigned cost=(unsigned)atoi(getenv("PANEL_GPIO_WRITES_PER_MS"));
    real_panel=panel_cadence_start(cost);panel_api=*real_panel;panel_api.struct_size=sizeof(panel_api);panel_api.present_status=cadence_status;
#ifdef PORTABLE_RASTER_SNAPSHOT
    panel_api.acquire=cadence_frame;panel_api.release=cadence_frame_release;panel_api.submit=cadence_submit;
#endif
    cadence_touch=fx_touch;cadence_touch.snapshot=cadence_snapshot;
    fx_runtime.health=cadence_health;fx_runtime.yield_ms=panel_runtime_yield;fx_runtime.acquire=cadence_acquire;
    assert(app_module_init()==0);
    printf("{\"app_wait_ms\":%u,\"gpio_writes_per_ms\":%u,\"frames\":[{",interval,cost);
    measure_frame(interval,false);printf("},{");measure_frame(interval,true);printf("}],\"idle\":{");
#ifdef PANEL_RESIDENT_SETTLE
    panel_cadence_settle_begin();panel_runtime_reset_metrics();
    last_input_at=last_touch_at=panel_cadence_clock();input_polls=max_input_gap=touch_samples=max_touch_gap=0;
    for(unsigned n=0;panel_cadence_settle_active()&&n<3000;++n){
        assert(!paper_token);t5_app_input_t input={0};assert(poll_input(&input,interval));++input_polls;
        const uint32_t now=panel_cadence_clock();
        if(now-last_input_at>max_input_gap)max_input_gap=now-last_input_at;
        last_input_at=now;
    }
    assert(!panel_cadence_settle_active()&&!paper_token&&!failed);
    panel_cadence_settle_report();printf(",");panel_runtime_report();
    printf(",\"controller_polls\":%u,\"max_controller_gap_ms\":%u,\"touch_samples\":%u,\"max_touch_gap_ms\":%u},\"after_settle\":{",
        input_polls,max_input_gap,touch_samples,max_touch_gap);
#endif
    panel_runtime_reset_metrics();last_poll_at=panel_cadence_clock();
    t5_app_input_t input={0};assert(poll_input(&input,interval));panel_runtime_expect_idle(interval);
    panel_runtime_report();
#ifdef PANEL_RESIDENT_MAINTENANCE
    printf("},\"maintenance\":{");
    panel_cadence_maintenance_begin();panel_runtime_reset_metrics();
    last_input_at=last_touch_at=panel_cadence_clock();input_polls=max_input_gap=touch_samples=max_touch_gap=0;
    for(unsigned n=0;!panel_cadence_maintenance_done()&&n<31000;++n){
        assert(!paper_token);t5_app_input_t idle_input={0};assert(poll_input(&idle_input,interval));++input_polls;
        const uint32_t now=panel_cadence_clock();
        if(now-last_input_at>max_input_gap)max_input_gap=now-last_input_at;
        last_input_at=now;
    }
    assert(panel_cadence_maintenance_done()&&!paper_token&&!failed);
    panel_cadence_maintenance_report();printf(",");panel_runtime_report();
    printf(",\"controller_polls\":%u,\"max_controller_gap_ms\":%u,\"touch_samples\":%u,\"max_touch_gap_ms\":%u",
        input_polls,max_input_gap,touch_samples,max_touch_gap);
#endif
    printf("}}\n");
    app_module_fini();panel_cadence_stop();
}
