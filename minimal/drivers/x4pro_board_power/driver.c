/* X4 peripheral-alive rail policy; raw GPIO ownership remains in the CPU port.
 * Ordinary ABI2 provider: no firmware pin branch, MMIO or privileged imports.
 * Consumers bind this provider before opening the board I2C controller. */
#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <GardenPlatformV1.h>
#include "PowerReadyV1.h"
#include <stddef.h>
#include <string.h>
static const garden_gpio_v1 *gpio;
static uint64_t token;
static bool started, retained;
static bool ready(void *context) {
    (void)context;
    bool level=false;
    return started && !retained && gpio && token && gpio->read(gpio->context,token,&level) && level;
}
static bool start(const risc_provider_dependency_v1 *deps,size_t count) {
    if(gpio || token || retained || started || !deps || count!=2)return false;
    const risc_hardware_device_v1 *hardware=NULL;
    const garden_gpio_v1 *candidate=NULL;
    for(size_t i=0;i<count;++i) {
        if(!deps[i].capability_id || deps[i].api_version!=1 || !deps[i].api)return false;
        if(!strcmp(deps[i].capability_id,"hardware.device") && !hardware)hardware=deps[i].api;
        else if(!strcmp(deps[i].capability_id,"platform.gpio") && !candidate)candidate=deps[i].api;
        else return false;
    }
    const size_t prefix=offsetof(garden_gpio_v1,release)+sizeof(candidate->release);
    if(!hardware || hardware->api_version!=1 || hardware->struct_size<sizeof(*hardware) ||
       !hardware->instance_id || !hardware->compatible || !hardware->revision || !hardware->config_type ||
       strcmp(hardware->compatible,"xteink,x4-pro-peripheral-enable") ||
       strcmp(hardware->revision,"unspecified") || strcmp(hardware->config_type,"gpio.bank") ||
       hardware->config_version!=1 || hardware->config_size!=sizeof(risc_hw_gpio_bank_v1) || !hardware->config ||
       !candidate || candidate->api_version!=1 || candidate->struct_size<prefix ||
       !candidate->claim || !candidate->read || !candidate->release)return false;
    const risc_hw_gpio_bank_v1 *config=hardware->config;
    if(config->struct_size!=sizeof(*config) || config->count!=1 || config->pins[0]!=1 ||
       config->active_high!=1 || config->pull_up || config->reserved || config->debounce_us ||
       config->long_press_us || config->click_min_us)return false;
    gpio=candidate;
    /* Claim sets HIGH before output enable. The CPU port safely restores an
     * existing held pad before releasing its old deep-sleep hold. */
    if(!gpio->claim(gpio->context,1,true,true,false,&token) || !token) {
        /* A failed native claim may leave cleanup uncertain without a token.
         * Keep the provider and table pinned rather than inventing safe stop. */
        retained=true;return false;
    }
    started=true;
    if(!ready(NULL)){started=false;return false;}
    return true;
}
static bool quiesce(void) {
    started=false;
    if(retained)return false;
    if(token && (!gpio || !gpio->release(gpio->context,token)))return false;
    token=0;gpio=NULL;return true;
}
static void stop(void) { if(!token && !retained){started=false;gpio=NULL;} }
static const x4_power_ready_api_v1 api={1,sizeof(api),NULL,ready};
static const risc_driver_v2 driver={2,sizeof(driver),"x4pro-board-power",X4_POWER_READY_CAPABILITY,1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}
