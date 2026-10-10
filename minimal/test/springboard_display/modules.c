#ifdef CADENCE_PROVIDER
#include <RiscProviderV2.h>
extern void panel_runtime_poll(uint32_t);
extern void panel_handoff_service(uint32_t);
static const unsigned api[2]={1,sizeof(api)};
static bool start(const risc_provider_dependency_v1 *deps,size_t count){(void)deps;return !count;}
static bool quiesce(void){return true;}
static void stop(void){}
static const risc_driver_service_v2 driver={
    {{{2,sizeof(driver),"cadence-proxy","test.cadence",1,&api,start,stop,quiesce},0,0},panel_runtime_poll},
    RISC_DRIVER_SERVICE_TAG_V1,RISC_DRIVER_SERVICE_VERSION_V1,panel_handoff_service};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi){return abi==2?&driver.poll.streams.driver:0;}
#else
extern void springboard_display_run(void);
__attribute__((visibility("default"))) void app_main(void){springboard_display_run();}
#endif
