/* Compile the exact supplied System adapter and its existing scoped fixtures. */
#define main adapter_original_fixture_main
#define risc_runtime_get_api cadence_fixture_runtime_api
#include PANEL_ADAPTER_FIXTURE
#undef risc_runtime_get_api
#undef main

extern const risc_display_output_api_v1 *panel_cadence_start(unsigned);
extern uint32_t panel_cadence_clock(void);
extern void panel_cadence_delay(uint32_t);
extern void panel_cadence_reset_metrics(void);
extern void panel_cadence_report(const char *);
extern void panel_cadence_stop(void);
extern void panel_runtime_yield(uint32_t);
extern void panel_runtime_reset_metrics(void);
extern void panel_runtime_report(void);
extern void panel_runtime_expect_idle(uint32_t);
extern void panel_runtime_expect_idle_sliced(uint32_t);
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
static bool cadence_health(risc_runtime_health_v1 *out){out->uptime_ms=panel_cadence_clock();return true;}
static bool cadence_status(void *c,risc_display_present_token_v1 t,risc_display_present_status_v1 *out){
    ++status_polls;return real_panel->present_status(c,t,out);
}
static risc_display_output_api_v1 panel_api;
static bool cadence_snapshot(void *c,risc_touch_snapshot_v1 *out){
    const uint32_t now=panel_cadence_clock();++touch_samples;
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
#ifndef PANEL_BASELINE_ADAPTER
    assert(portable_paper_frame_ready());
#endif
    v->begin();v->text(20,20,300,"PANEL CADENCE",1,false,true);
    if(partial)v->circle(350,350,2,true);
    present(false);
#ifndef PANEL_BASELINE_ADAPTER
#ifdef PORTABLE_RASTER_SNAPSHOT
    while(!portable_paper_frame_idle()&&!failed){
#else
    while(paper_token&&!failed){
#endif
        t5_app_input_t input={0};assert(poll_input(&input,interval));++input_polls;
        const uint32_t now=panel_cadence_clock();
        if(now-last_input_at>max_input_gap)max_input_gap=now-last_input_at;
        last_input_at=now;
    }
#else
    (void)interval;
#endif
    assert(!failed);
    panel_cadence_report(partial?"partial":"full");printf(",");panel_runtime_report();
    printf(",\"controller_polls\":%u,\"status_polls\":%u,\"max_controller_gap_ms\":%u,"
        "\"touch_samples\":%u,\"max_touch_gap_ms\":%u",input_polls,status_polls,max_input_gap,touch_samples,max_touch_gap);
}
void panel_adapter_cadence(void) {
    const unsigned interval=(unsigned)atoi(getenv("PANEL_APP_WAIT_MS"));
    const unsigned cost=(unsigned)atoi(getenv("PANEL_GPIO_WRITES_PER_MS"));
    const char *work_env=getenv("PANEL_SETTLE_WORK_MS");
    const unsigned settle_work=work_env?(unsigned)atoi(work_env):0;
    real_panel=panel_cadence_start(cost);panel_api=*real_panel;panel_api.struct_size=sizeof(panel_api);panel_api.present_status=cadence_status;
    cadence_touch=fx_touch;cadence_touch.snapshot=cadence_snapshot;
    fx_runtime.health=cadence_health;fx_runtime.yield_ms=panel_runtime_yield;fx_runtime.acquire=cadence_acquire;
    assert(app_module_init()==0);
    printf("{\"app_wait_ms\":%u,\"gpio_writes_per_ms\":%u,\"settle_work_ms\":%u,\"frames\":[{",interval,cost,settle_work);
    measure_frame(interval,false);printf("},{");measure_frame(interval,true);printf("}],\"idle\":{");
#ifdef PANEL_RESIDENT_SETTLE
    panel_cadence_settle_begin();panel_runtime_reset_metrics();
    last_input_at=last_touch_at=panel_cadence_clock();input_polls=max_input_gap=touch_samples=max_touch_gap=0;
    for(unsigned n=0;panel_cadence_settle_active()&&n<3000;++n){
        /* Model synchronous Home work after the first completed image. It
         * consumes the app's poll interval without advancing providers. */
        panel_cadence_delay(settle_work);
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
    t5_app_input_t input={0};assert(poll_input(&input,interval));
#ifdef PORTABLE_RASTER_SNAPSHOT
    panel_runtime_expect_idle_sliced(interval);
#else
    panel_runtime_expect_idle(interval);
#endif
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
