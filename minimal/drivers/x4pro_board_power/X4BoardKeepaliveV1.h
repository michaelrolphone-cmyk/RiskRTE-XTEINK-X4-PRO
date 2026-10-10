#pragma once
#include "PowerReadyV1.h"
#include <RiscDeepSleepV1.h>
#include <stddef.h>
#define X4_BOARD_KEEPALIVE_TAG UINT32_C(0x58344231)
/* Private append-only board authority. Existing ready consumers keep their
 * exact prefix and gain no GPIO or sleep entry authority. Only x4pro-power
 * invokes this suffix. Zero means prepared/restored; negative values use
 * RISC_DEEP_SLEEP_*. RETAINED and unknown results require external restart.
 * prepare retains the board owner lock until checked restore. GPIO1 HIGH is
 * permanently held in CPU custody from successful board start, including after
 * ordinary refusal and provider quiescence; neither method writes or unholds it.
 * restore is ONLY for an ordinary native entry refusal, never after terminal
 * entry, RETAINED or an unknown native return. A fresh boot claims HIGH before
 * the scoped CPU port removes the old pad hold. No rail-OFF policy is implied. */
typedef struct {
    x4_power_ready_api_v1 power;
    uint32_t extension_tag, extension_version;
    int32_t (*prepare)(void *context);
    int32_t (*restore)(void *context);
} x4_board_keepalive_v1;
static inline const x4_board_keepalive_v1 *x4_board_keepalive(const x4_power_ready_api_v1 *api) {
    if(!api || api->api_version!=1 || api->struct_size<sizeof(x4_board_keepalive_v1))return NULL;
    const x4_board_keepalive_v1 *ext=(const x4_board_keepalive_v1 *)api;
    return ext->extension_tag==X4_BOARD_KEEPALIVE_TAG && ext->extension_version==1 && ext->prepare && ext->restore?ext:NULL;
}
