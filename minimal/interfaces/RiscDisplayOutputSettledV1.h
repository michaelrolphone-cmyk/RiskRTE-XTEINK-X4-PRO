#pragma once
#include "RiscDisplayOutputFrontlightV1.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Optional explicit final-image preparation and read-only readiness.
 * Request immediately after the matching token completes, before power prep.
 * It does not restart the quiet period or block subsequent frame submission.
 * Presentation tokens keep their
 * existing low-latency semantics. Sleep UI owners may wait for this stronger
 * condition while yielding normally; no controller policy is copied to apps. */
#define RISC_DISPLAY_SETTLED_TAG 0x53544c44u
#define RISC_DISPLAY_SETTLED_VERSION 1u
enum {
    RISC_DISPLAY_SETTLED_FAILED = -1,
    RISC_DISPLAY_SETTLED_PENDING = 0,
    RISC_DISPLAY_SETTLED_COMPLETE = 1
};
typedef struct {
    risc_display_output_api_v1_frontlight frontlight;
    uint32_t settled_tag, settled_version;
    int32_t (*settled_status)(void *context, risc_display_present_token_v1 token);
    bool (*request_settle)(void *context, risc_display_present_token_v1 token);
} risc_display_output_api_v1_settled;

static inline const risc_display_output_api_v1_settled *risc_display_output_settled(
        const risc_display_output_api_v1 *api) {
    if (!risc_display_output_frontlight(api) ||
        api->struct_size < sizeof(risc_display_output_api_v1_settled)) return NULL;
    const risc_display_output_api_v1_settled *ext =
        (const risc_display_output_api_v1_settled *)api;
    return ext->settled_tag == RISC_DISPLAY_SETTLED_TAG &&
        ext->settled_version == RISC_DISPLAY_SETTLED_VERSION && ext->settled_status && ext->request_settle ? ext : NULL;
}
#ifdef __cplusplus
}
#endif
