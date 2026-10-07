/* X4 owns GPIO3 explicitly; no Watch PMU layout or raw pin authority escapes. */
#include "X4PowerDeepV1.h"
#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include <RiscPlatformClockV1.h>
#include <string.h>
static const garden_gpio_v1 *gpio;
static const risc_provider_sync_api_v1 *sync_api;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t token, lock, neutral_at, sampled_at;
static bool started, closing, retained, neutral;
static bool enter(void) {
    return !retained && sync_api && lock && sync_api->is_owner(sync_api->context) &&
        sync_api->try_lock(sync_api->context,lock);
}
static bool leave(void) {
    if(!sync_api->unlock(sync_api->context,lock)){retained=true;return false;}
    return true;
}
static bool sample(bool *down) {
    bool high;uint64_t now=clock_api->monotonic_ms(clock_api->context);
    if(now==UINT64_MAX || now<sampled_at || !gpio->read(gpio->context,token,&high)) {
        neutral=false;return false;
    }
    sampled_at=now;*down=!high;
    if(!high)neutral=false;
    else if(!neutral){neutral=true;neutral_at=now;}
    return true;
}
static bool read_key(void *c,bool *down) {
    (void)c;if(!down || !enter())return false;
    bool value=false,ok=started && !closing && sample(&value);
    if(!leave() || !ok)return false;
    *down=value;return true;
}
static int32_t light_sleep(void *c,uint32_t duration,risc_light_sleep_result_v1 *out) {
    (void)c;
    if(!out || out->struct_size<sizeof(*out))return RISC_LIGHT_SLEEP_INVALID;
    out->wake_cause=RISC_LIGHT_SLEEP_WAKE_NONE;
    if(retained)return RISC_LIGHT_SLEEP_RETAINED;
    if(!enter())return RISC_LIGHT_SLEEP_CONTEXT;
    int32_t rc=RISC_LIGHT_SLEEP_BUSY;bool down=true;
    if(started && !closing) {
        if(!sample(&down))rc=RISC_LIGHT_SLEEP_PLATFORM;
        else if(down || !neutral || sampled_at-neutral_at<30u)rc=RISC_LIGHT_SLEEP_ACTIVE_WAKE;
        else {
            /* Consume neutrality even for native refusal. A new observation is
             * required after wake; held waking input can never re-arm sleep. */
            neutral=false;
            rc=duration?gpio->light_sleep_for(gpio->context,token,false,duration,out):
                gpio->light_sleep(gpio->context,token,false,out);
            if(rc==RISC_LIGHT_SLEEP_RETAINED){retained=true;return rc;}
        }
    }
    if(!leave())return RISC_LIGHT_SLEEP_RETAINED;
    return rc;
}
static int32_t deep_sleep_for(void *c,uint32_t duration) {
    (void)c;
    if(!duration || duration>RISC_TIMED_SLEEP_MAX_MS)return RISC_DEEP_SLEEP_INVALID;
    if(retained)return RISC_DEEP_SLEEP_RETAINED;
    if(!enter())return RISC_DEEP_SLEEP_CONTEXT;
    int32_t rc=RISC_DEEP_SLEEP_BUSY;bool down=true;
    if(started && !closing) {
        if(gpio->struct_size<GARDEN_GPIO_DEEP_SLEEP_FOR_V1_SIZE || !gpio->deep_sleep_for)
            rc=RISC_DEEP_SLEEP_UNSUPPORTED;
        else if(!sample(&down))rc=RISC_DEEP_SLEEP_PLATFORM;
        else if(down || !neutral || sampled_at-neutral_at<30u)rc=RISC_DEEP_SLEEP_ACTIVE_WAKE;
        else {
            neutral=false;
            rc=gpio->deep_sleep_for(gpio->context,token,false,duration);
            /* Even a zero/unknown return violates the terminal contract. Do
             * not unlock or touch providers after an unconfirmed entry. */
            if(rc==RISC_DEEP_SLEEP_RETAINED || rc>=0 || rc<RISC_DEEP_SLEEP_UNSUPPORTED) {
                retained=true;return RISC_DEEP_SLEEP_RETAINED;
            }
        }
    }
    if(!leave())return RISC_DEEP_SLEEP_RETAINED;
    return rc;
}
static bool start(const risc_provider_dependency_v1 *deps,size_t count) {
    if(gpio || token || lock || retained || !deps || count!=4)return false;
    const risc_hardware_device_v1 *hw=NULL;
    const garden_gpio_v1 *g=NULL;const risc_provider_sync_api_v1 *s=NULL;
    const risc_platform_clock_api_v1 *t=NULL;
    for(size_t i=0;i<count;i++) {
        if(!deps[i].capability_id || deps[i].api_version!=1 || !deps[i].api)return false;
        if(!strcmp(deps[i].capability_id,"hardware.device") && !hw)hw=deps[i].api;
        else if(!strcmp(deps[i].capability_id,"platform.gpio") && !g)g=deps[i].api;
        else if(!strcmp(deps[i].capability_id,"platform.sync") && !s)s=deps[i].api;
        else if(!strcmp(deps[i].capability_id,"platform.clock") && !t)t=deps[i].api;
        else return false;
    }
    if(!hw || hw->api_version!=1 || hw->struct_size<sizeof(*hw) || !hw->instance_id ||
       !hw->compatible || strcmp(hw->compatible,"xteink,x4-pro-power-key") ||
       !hw->revision || strcmp(hw->revision,"unspecified") || !hw->config_type ||
       strcmp(hw->config_type,"gpio.bank") || hw->config_version!=1 ||
       hw->config_size!=sizeof(risc_hw_gpio_bank_v1) || !hw->config ||
       !g || g->api_version!=1 || g->struct_size<GARDEN_GPIO_LIGHT_SLEEP_FOR_V1_SIZE ||
       !g->claim || !g->read || !g->release || !g->light_sleep || !g->light_sleep_for ||
       !s || s->api_version!=1 || s->struct_size<sizeof(*s) || !s->is_owner || !s->create ||
       !s->try_lock || !s->unlock || !s->destroy || !s->is_owner(s->context) ||
       !t || t->api_version!=1 || t->struct_size<sizeof(*t) || !t->monotonic_ms)return false;
    const risc_hw_gpio_bank_v1 *cfg=hw->config;
    if(cfg->struct_size!=sizeof(*cfg) || cfg->count!=1 || cfg->pins[0]!=3 ||
       cfg->active_high || cfg->pull_up!=1 || cfg->debounce_us || cfg->long_press_us ||
       cfg->click_min_us || cfg->reserved)return false;
    gpio=g;sync_api=s;clock_api=t;closing=false;neutral=false;sampled_at=0;
    if(!s->create(s->context,&lock) || !lock)return false;
    if(!enter())return false;
    bool ok=g->claim(g->context,3,false,false,true,&token) && token;
    bool down;if(ok)ok=sample(&down);
    started=ok;return leave() && ok;
}
static bool quiesce(void) {
    if(retained)return false;
    if(!lock)return !token;
    if(!enter())return false;
    closing=true;started=false;neutral=false;
    bool ok=!token || gpio->release(gpio->context,token);
    if(ok)token=0;
    if(!leave() || !ok)return false;
    if(!sync_api->destroy(sync_api->context,lock))return false;
    lock=0;gpio=NULL;sync_api=NULL;clock_api=NULL;return true;
}
static void stop(void){}
static const x4_power_deep_v1 api={{1,sizeof(api),NULL,read_key,light_sleep},X4_POWER_DEEP_TAG,1,deep_sleep_for};
static const risc_driver_v2 driver={2,sizeof(driver),"x4pro-power",X4_POWER_CAPABILITY,1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}
