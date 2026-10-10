#pragma once
/* Optional append-only observation after the frozen export-prepare prefix.
 * This does not grant raw export authority and changes no existing operation.
 * A live grant's owner task may observe between serialized volume calls,
 * including after a failed call. No lock, I/O, log drain, scheduler call,
 * callback, allocation, cleanup or state mutation is allowed in observe().
 * READY is a current snapshot, not a lease; callers recheck after each call.
 * UNAVAILABLE includes absent/exported/sleeping media with clean custody.
 * RETAINED means uncertain resource custody: preserve handles, provider and
 * dependencies; do not invoke more volume calls or release the grant.
 * Missing/invalid suffix cannot prove custody and must not enable an exporter.
 */
#include "RiscStorageExportV1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_STORAGE_STATE_TAG UINT32_C(0x53565331) /* SVS1 */
enum {
    RISC_STORAGE_STATE_READY = 0,
    RISC_STORAGE_STATE_UNAVAILABLE = 1,
    RISC_STORAGE_STATE_RETAINED = -2
};
typedef struct {
    risc_storage_volume_api_v1_export_prepare prepared;
    uint32_t state_tag, state_version;
    int32_t (*observe)(void *context);
} risc_storage_volume_api_v1_state;
static inline const risc_storage_volume_api_v1_state *risc_storage_volume_state(
    const risc_storage_volume_api_v1 *api) {
    if (!api || api->api_version != RISC_STORAGE_VOLUME_API_V1 ||
        api->struct_size < sizeof(risc_storage_volume_api_v1_state)) return NULL;
    const risc_storage_volume_api_v1_state *state = (const risc_storage_volume_api_v1_state *)api;
    return state->state_tag == RISC_STORAGE_STATE_TAG && state->state_version == 1u &&
        state->observe ? state : NULL;
}
#ifdef __cplusplus
}
#endif
