#pragma once
#include "X4PowerV1.h"
#include <RiscTimedSleepV1.h>
#include <stddef.h>
#define X4_POWER_DEEP_TAG UINT32_C(0x58344431)
/* Optional append-only product authority. The light-sleep prefix is unchanged.
 * Caller must close input sessions, complete frames, prepare peripherals and
 * stage its app-owned retained record first. This provider owns only GPIO3;
 * it does not silently change panel, touch, SD, rails, radios or alarm policy.
 * Duration is 1..RISC_TIMED_SLEEP_MAX_MS. GPIO3 and timer wake remain armed.
 * Successful entry NEVER RETURNS. Only ordinary native refusal returns for
 * checked rollback. RETAINED requires keeping the invocation/resources intact.
 */
typedef struct {
    x4_power_v1 power;
    uint32_t extension_tag, extension_version;
    int32_t (*deep_sleep_for)(void *context, uint32_t duration_ms);
} x4_power_deep_v1;
static inline const x4_power_deep_v1 *x4_power_deep(const x4_power_v1 *api) {
    if(!api || api->api_version!=1 || api->struct_size<sizeof(x4_power_deep_v1))return NULL;
    const x4_power_deep_v1 *ext=(const x4_power_deep_v1 *)api;
    return ext->extension_tag==X4_POWER_DEEP_TAG && ext->extension_version==1 && ext->deep_sleep_for?ext:NULL;
}
