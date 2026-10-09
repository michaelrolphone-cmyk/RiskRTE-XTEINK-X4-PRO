/* X4 board-alive rail policy; GPIO1 stays solely in this provider's scope.
 * Ordinary ABI2 provider: no firmware pin branch, MMIO or native imports. */
#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include "X4BoardKeepaliveV1.h"
#include <stddef.h>
#include <string.h>
static const garden_gpio_v1 *gpio;
static const risc_provider_sync_api_v1 *sync_api;
static uint64_t token, lock;
static bool started, retained, prepared, restoring, persistent;
static const char *reason;
static bool fail(const char *message){reason=message;return false;}
static int32_t retain(const char *message){retained=true;fail(message);return RISC_DEEP_SLEEP_RETAINED;}
static bool enter(void) {
    return !retained && !prepared && sync_api && lock && sync_api->is_owner(sync_api->context) &&
        sync_api->try_lock(sync_api->context,lock);
}
static bool leave(void) {
    if(!sync_api->unlock(sync_api->context,lock)){
        retain("board owner unlock failed; ownership retained");return false;
    }
    return true;
}
/* Failure cannot establish whether the board-alive rail is safe. Keep the
 * exact claim and lock; never hide this uncertainty with release/reclaim. */
static bool high(void) {
    bool level=false;
    if(!(persistent ? gpio->read_retired_output(gpio->context,1,&level) :
         gpio->read(gpio->context,token,&level))){
        retain("gpio1 read failed; ownership retained");return false;
    }
    if(!level){retain("gpio1 readback LOW after HIGH claim; ownership retained");return false;}
    return true;
}
static bool ready(void *context) {
    (void)context;
    if(!enter())return false;
    bool ok=started && persistent && high();
    if(retained)return false;
    return leave() && ok;
}
static int32_t prepare(void *context) {
    (void)context;
    if(retained)return RISC_DEEP_SLEEP_RETAINED;
    if(!enter())return RISC_DEEP_SLEEP_CONTEXT;
    if(started && persistent) {
        /* The CPU already owns a permanent HIGH hold. Deep entry needs only
         * truthful readback and this existing owner's transaction lock. */
        if(!high())return RISC_DEEP_SLEEP_RETAINED;
        prepared=true;return 0;
    }
    return leave()?RISC_DEEP_SLEEP_BUSY:RISC_DEEP_SLEEP_RETAINED;
}
static int32_t restore(void *context) {
    (void)context;
    if(retained)return RISC_DEEP_SLEEP_RETAINED;
    if(!sync_api || !lock || !sync_api->is_owner(sync_api->context))return RISC_DEEP_SLEEP_CONTEXT;
    if(!prepared || restoring)return RISC_DEEP_SLEEP_BUSY;
    /* Successful prepare still owns the lock. A recursive restore cannot
     * consume that transaction while checked readback is in progress. */
    restoring=true;
    /* Ordinary refusal restores the transaction, never releases board power. */
    if(!high())return RISC_DEEP_SLEEP_RETAINED;
    prepared=false;restoring=false;
    return leave()?0:RISC_DEEP_SLEEP_RETAINED;
}
static bool start(const risc_provider_dependency_v1 *deps,size_t count) {
    if(gpio || sync_api || token || lock || retained || started)return fail("already owned or retained");
    if(!deps || count!=3)return fail("dependency count");
    reason=NULL;
    const risc_hardware_device_v1 *hardware=NULL;
    const garden_gpio_v1 *candidate=NULL;
    const risc_provider_sync_api_v1 *sync=NULL;
    for(size_t i=0;i<count;++i) {
        if(!deps[i].capability_id || deps[i].api_version!=1 || !deps[i].api)return fail("dependency header");
        if(!strcmp(deps[i].capability_id,"hardware.device") && !hardware)hardware=deps[i].api;
        else if(!strcmp(deps[i].capability_id,"platform.gpio") && !candidate)candidate=deps[i].api;
        else if(!strcmp(deps[i].capability_id,"platform.sync") && !sync)sync=deps[i].api;
        else return fail("unknown or duplicate dependency");
    }
    if(!hardware || hardware->api_version!=1 || hardware->struct_size<sizeof(*hardware) ||
       hardware->instance_id!=1 || !hardware->compatible || !hardware->revision || !hardware->config_type ||
       strcmp(hardware->compatible,"xteink,x4-pro-peripheral-enable") ||
       strcmp(hardware->revision,"unspecified") || strcmp(hardware->config_type,"gpio.bank") ||
       hardware->config_version!=1 || hardware->config_size!=sizeof(risc_hw_gpio_bank_v1) || !hardware->config ||
       !candidate || candidate->api_version!=1 || candidate->struct_size<GARDEN_GPIO_READ_RETIRED_OUTPUT_V1_SIZE ||
       !candidate->claim || !candidate->read || !candidate->deep_sleep_hold ||
       !candidate->retire_held_output || !candidate->read_retired_output ||
       !sync || sync->api_version!=1 || sync->struct_size<sizeof(*sync) || !sync->is_owner || !sync->create ||
       !sync->try_lock || !sync->unlock || !sync->destroy || !sync->is_owner(sync->context))
        return fail("hardware, GPIO or sync API mismatch");
    const risc_hw_gpio_bank_v1 *config=hardware->config;
    if(config->struct_size!=sizeof(*config) || config->count!=1 || config->pins[0]!=1 ||
       config->active_high!=1 || config->pull_up || config->reserved || config->debounce_us ||
       config->long_press_us || config->click_min_us)return fail("GPIO bank config mismatch");
    gpio=candidate;sync_api=sync;
    if(!sync_api->create(sync_api->context,&lock) || !lock)return fail("board owner lock creation failed");
    if(!enter())return fail("board owner lock unavailable");
    /* CPU claim stages configured HIGH before releasing an old boot hold. */
    if(!gpio->claim(gpio->context,1,true,true,false,&token) || !token) {
        retain("gpio1 claim failed; ownership retained");return false;
    }
    if(!high())return false;
    /* A live hold blocks Runtime storage and app handoff. Transfer this static
     * rail to CPU custody before publishing readiness; no mutable token lives
     * across an invocation, provider teardown or an ordinary sleep refusal. */
    if(gpio->deep_sleep_hold(gpio->context,token,true)!=0) {
        retain("gpio1 startup hold failed; ownership retained");return false;
    }
    if(!gpio->retire_held_output(gpio->context,token)) {
        retain("gpio1 hold retirement failed; ownership retained");return false;
    }
    token=0;persistent=true;
    if(!high())return false;
    started=true;return leave();
}
static bool quiesce(void) {
    if(retained || prepared)return fail("GPIO claim ownership retained");
    if(!lock){gpio=NULL;sync_api=NULL;return !token;}
    if(!enter())return fail("board owner lock unavailable");
    started=false;
    if(token)return fail("gpio1 live ownership retained");
    if(persistent && !high())return false;
    if(!leave())return false;
    if(!sync_api->destroy(sync_api->context,lock))return fail("board owner lock destruction pending");
    lock=0;persistent=false;gpio=NULL;sync_api=NULL;return true;
}
static void stop(void) { /* quiesce owns all checked cleanup. */ }
static const x4_board_keepalive_v1 api={{1,sizeof(api),NULL,ready},X4_BOARD_KEEPALIVE_TAG,1,prepare,restore};
static bool last_error(char *out,size_t capacity){
    if(!out || !capacity || !reason)return false;
    size_t i=0;while(reason[i] && i+1<capacity){out[i]=reason[i];++i;}out[i]=0;return true;
}
static const risc_driver_diagnostics_v2 driver={{2,sizeof(driver),"x4pro-board-power",X4_POWER_READY_CAPABILITY,1,&api,start,stop,quiesce},last_error};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi){return abi==2?&driver.base:NULL;}
