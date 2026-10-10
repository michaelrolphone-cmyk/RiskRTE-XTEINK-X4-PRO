#pragma once
#include "RiscDisplayOutputSnapshotV1.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Optional suffix after the exact history/power/metrics/snapshot prefixes.
 * No display lease or presentation is created by these operations. Semantics
 * match the optional frontlight tone suffix: dimensionless cool-to-warm ratio,
 * independent of logical brightness and OFF. UNAVAILABLE means no underlying
 * tone suffix; FAILED means a present provider refused or could not confirm
 * its value, and must not be treated as absence. False set may follow partial output and is terminal
 * for the caller's current operation. Tone is not calibrated color temperature.
 */
#define RISC_DISPLAY_FRONTLIGHT_TAG UINT32_C(0x44464c31)
#define RISC_DISPLAY_FRONTLIGHT_VERSION 1u
enum { RISC_DISPLAY_TONE_FAILED=-1, RISC_DISPLAY_TONE_OK=0, RISC_DISPLAY_TONE_UNAVAILABLE=1 };
typedef struct {
    risc_display_output_api_v1_snapshot snapshot;
    uint32_t frontlight_tag, frontlight_version;
    bool (*set_tone)(void *context, uint16_t warm, uint16_t maximum);
    int32_t (*get_tone)(void *context, uint16_t *warm, uint16_t *maximum);
} risc_display_output_api_v1_frontlight;
static inline const risc_display_output_api_v1_frontlight *risc_display_output_frontlight(const risc_display_output_api_v1 *api) {
    if (!api || api->api_version!=RISC_DISPLAY_OUTPUT_API_V1 ||
        api->struct_size<sizeof(risc_display_output_api_v1_frontlight)) return NULL;
    if (!risc_display_output_snapshot(api)) return NULL;
    const risc_display_output_api_v1_frontlight *ext=(const risc_display_output_api_v1_frontlight *)api;
    return ext->frontlight_tag==RISC_DISPLAY_FRONTLIGHT_TAG && ext->frontlight_version==RISC_DISPLAY_FRONTLIGHT_VERSION &&
        ext->set_tone && ext->get_tone ? ext : NULL;
}
#ifdef __cplusplus
}
#endif
