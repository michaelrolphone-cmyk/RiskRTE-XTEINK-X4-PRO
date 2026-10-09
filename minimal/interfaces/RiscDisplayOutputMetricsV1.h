#pragma once
#include "RiscDisplayOutputPowerV1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Optional display.output@1 descriptor suffix after the exact power/history
 * prefixes. This is diagnostic observation, not presentation or bus authority.
 * A successful snapshot describes the latest accepted token, queued/active or
 * finished, until another submission or provider reset/resume. The getter runs
 * on the serialized owner and only copies bounded provider-owned state: no
 * hardware/clock I/O, allocation, formatting, progress, sleep or locking.
 * The caller initializes api_version=1 and struct_size=sizeof(snapshot).
 * Rejection leaves the caller's snapshot unchanged. Providers may reject while
 * unstarted, preparing/asleep or retained; consumers must handle absence.
 */
#define RISC_DISPLAY_METRICS_TAG 0x444D5431u /* DMT1 */
#define RISC_DISPLAY_METRICS_VERSION 1u
enum {
    RISC_DISPLAY_METRICS_QUEUED = 1u << 0,
    RISC_DISPLAY_METRICS_TRANSFER_START = 1u << 1,
    RISC_DISPLAY_METRICS_TRANSFER_END = 1u << 2,
    RISC_DISPLAY_METRICS_REFRESH = 1u << 3,
    RISC_DISPLAY_METRICS_BUSY_ASSERT = 1u << 4,
    RISC_DISPLAY_METRICS_BUSY_DONE = 1u << 5
};
enum { RISC_DISPLAY_METRICS_FULL = 0u, RISC_DISPLAY_METRICS_PARTIAL = 1u };
typedef struct {
    uint32_t api_version, struct_size;
    risc_display_present_token_v1 token;
    uint32_t state, mode;
    /* Monotonic milliseconds in the provider's platform clock domain. Only
     * timestamps with the corresponding valid_times bit have been observed. */
    /* bytes_sent is provider-accounted payload progress. It is not proof that
     * the controller accepted bytes after an incomplete/uncertain transfer. */
    uint32_t valid_times, bytes_sent;
    uint64_t queued_ms, transfer_start_ms, transfer_end_ms, refresh_ms;
    uint64_t busy_assert_ms, busy_done_ms;
    /* Scoped GPIO write calls during this token's ACTIVE phase, including a
     * rejected/uncertain call. Payload and command/sideband calls are included;
     * startup/probe, power preparation and other providers are excluded. */
    uint32_t gpio_write_calls;
    uint32_t damage_count;
    risc_display_rect_v1 submitted_damage[RISC_DISPLAY_MAX_DAMAGE_RECTS];
    /* Native display coordinates; zero submitted rectangles means full image.
     * Alignment and a forced clean waveform can expand effective_update. */
    risc_display_rect_v1 effective_update;
} risc_display_present_metrics_v1;
typedef struct {
    risc_display_output_api_v1_power power;
    uint32_t metrics_tag, metrics_version;
    bool (*snapshot)(void *context, risc_display_present_metrics_v1 *out);
} risc_display_output_api_v1_metrics;

static inline const risc_display_output_api_v1_metrics *risc_display_output_metrics(
    const risc_display_output_api_v1 *api) {
    if (!risc_display_output_power(api) ||
        api->struct_size < sizeof(risc_display_output_api_v1_metrics)) return NULL;
    const risc_display_output_api_v1_metrics *ext = (const risc_display_output_api_v1_metrics *)api;
    return ext->metrics_tag == RISC_DISPLAY_METRICS_TAG &&
        ext->metrics_version == RISC_DISPLAY_METRICS_VERSION && ext->snapshot ? ext : NULL;
}
#ifdef __cplusplus
}
#endif
