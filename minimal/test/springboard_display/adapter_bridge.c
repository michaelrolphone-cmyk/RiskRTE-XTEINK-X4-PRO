/* Reuse strict scoped fixtures while compiling the production adapter and
 * selected Springboard paper/scroll/crossfade display path. */
#define main original_toolbar_main
#define risc_runtime_get_api cadence_fixture_runtime_api
#define TEST_CUSTOM_CATALOG
#include PANEL_ADAPTER_FIXTURE
#undef risc_runtime_get_api
#undef main
void __real_free(void *p){free(p);}

extern void springboard_controller_main(void);
extern const risc_display_output_api_v1 *panel_handoff_start(const char *);
extern uint32_t panel_cadence_clock(void);
extern void panel_cadence_delay(uint32_t);
extern bool panel_handoff_plane_active(void);
extern void panel_handoff_report(void);
extern void panel_runtime_yield(uint32_t);
static const char *test_case;
static const risc_display_output_api_v1 *real_panel;
static unsigned touch_logs,slow_logs,complete_logs;
static char failure_log[224];
static bool diagnostic(const char *line) {
    if(strstr(line,"stage=touch-picked-up")){
        ++touch_logs;
        if(!strcmp(test_case,"slow-log")){++slow_logs;panel_cadence_delay(1200);}
    }
    if(strstr(line,"stage=display-complete"))++complete_logs;
    if(strstr(line,"stage=display-failed"))snprintf(failure_log,sizeof(failure_log),"%s",line);
    return true;
}
static bool health(risc_runtime_health_v1 *out){out->uptime_ms=panel_cadence_clock();return true;}
static bool nav_poll(void*c,risc_input_navigation_frame_v1*out){
    (void)c;*out=(risc_input_navigation_frame_v1){0};
    if(complete_logs){out->buttons=out->pressed=RISC_NAV_BACK;}
    assert(panel_cadence_clock()<20000);return true;
}
static bool nav_foreground(void*c,const risc_input_foreground_v1*f,size_t n){(void)c;(void)f;(void)n;return true;}
static bool nav_reset(void*c){(void)c;return true;}
static const risc_input_navigation_api_v1 nav={1,sizeof(nav),NULL,nav_poll,nav_foreground,nav_reset};
static bool touch_snapshot(void*c,risc_touch_snapshot_v1*out){
    (void)c;*out=(risc_touch_snapshot_v1){.width=480,.height=800};
    if(panel_handoff_plane_active()){
        out->contact_count=1;out->contacts[0]=(risc_touch_contact_v1){.id=1,.x=240,.y=350};
    }
    return true;
}
static risc_touch_api_v1 touch_api;
static bool acquire(const char*name,uint32_t version,uint64_t instance,risc_runtime_capability_v1*out){
    if(!strcmp(name,"input.navigation")){
        assert(version==1&&!instance);++acquires;++live;
        *out=(risc_runtime_capability_v1){.struct_size=sizeof(*out),.slot=acquires,.generation=1,.api=&nav};return true;
    }
    bool ok=fx_acquire(name,version,instance,out);
    if(ok&&!strcmp(name,"display.output"))out->api=real_panel;
    if(ok&&!strcmp(name,"input.touch.raw"))out->api=&touch_api;
    return ok;
}
void springboard_display_run(void) {
    test_case=getenv("DISPLAY_CASE");assert(test_case);
    real_panel=panel_handoff_start(test_case);
    touch_api=fx_touch;touch_api.snapshot=touch_snapshot;
    fx_runtime.health=health;fx_runtime.diagnostic=diagnostic;fx_runtime.yield_ms=panel_runtime_yield;fx_runtime.acquire=acquire;
    assert(app_module_init()==0);
    springboard_controller_main();
    assert(complete_logs || retained);
    printf("{\"case\":\"%s\",\"app_retained\":%s,\"completed_frames\":%u,\"touch_logs\":%u,\"slow_logs\":%u,\"app_failure\":\"%s\",",
        test_case,retained?"true":"false",complete_logs,touch_logs,slow_logs,failure_log);
    panel_handoff_report();puts("}");
    if(!retained){app_module_fini();assert(!live&&!subscriptions);}
}
