#pragma once
#include "RiscFrontlightV1.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Optional suffix; the existing display.frontlight@1 prefix is unchanged.
 * Tone is a dimensionless cool-to-warm ratio, never a Kelvin/luminance claim.
 * Zero is cool, maximum is warm, midpoint is neutral. A tone change preserves
 * the base logical level, including OFF; later level changes preserve tone.
 * Missing/invalid suffixes mean no tone control. False may mean uncertain
 * hardware state; consumers must not infer rollback or issue cleanup I/O.
 */
#define RISC_FRONTLIGHT_TONE_TAG UINT32_C(0x46544e31)
#define RISC_FRONTLIGHT_TONE_VERSION 1u
typedef struct {
    risc_frontlight_api_v1 base;
    uint32_t tone_tag, tone_version;
    bool (*set_tone)(void *context, uint16_t warm, uint16_t maximum);
    bool (*get_tone)(void *context, uint16_t *warm, uint16_t *maximum);
} risc_frontlight_api_v1_tone;
static inline const risc_frontlight_api_v1_tone *risc_frontlight_tone(const risc_frontlight_api_v1 *api) {
    if (!api || api->api_version!=RISC_FRONTLIGHT_API_V1 ||
        api->struct_size<sizeof(risc_frontlight_api_v1_tone)) return NULL;
    const risc_frontlight_api_v1_tone *ext=(const risc_frontlight_api_v1_tone *)api;
    return ext->tone_tag==RISC_FRONTLIGHT_TONE_TAG && ext->tone_version==RISC_FRONTLIGHT_TONE_VERSION &&
        ext->set_tone && ext->get_tone ? ext : NULL;
}
#ifdef __cplusplus
}
#endif
