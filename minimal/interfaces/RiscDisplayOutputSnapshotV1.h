#pragma once
#include "RiscDisplayOutputMetricsV1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Optional, tagged read-only suffix after the exact history/power/metrics
 * prefixes. This grants no frame lease and performs no presentation or I/O.
 *
 * copy_completed copies the latest physically completed MONO1 image into a
 * caller-owned buffer in native display coordinates. It is available only on
 * the serialized owner while the provider is awake and ready, no frame is
 * leased or queued/active, and completed history is valid. Seeded-but-unshown
 * history does not qualify. Reset, failed/uncertain presentation, sleep and
 * retained custody invalidate availability until a new frame completes.
 *
 * The caller passes MONO1, stride >= ceil(width/8), and size >= stride*height.
 * Only visible row bytes are written; padding is preserved. Aliasing provider
 * storage, overflow, invalid format/geometry and unavailable history reject
 * without changing the destination. No pointers or frame tokens are retained.
 * Absence/refusal means that the application renders without a transition.
 */
#define RISC_DISPLAY_SNAPSHOT_TAG UINT32_C(0x44534E31) /* DSN1 */
#define RISC_DISPLAY_SNAPSHOT_VERSION 1u
typedef struct {
    risc_display_output_api_v1_metrics metrics;
    uint32_t snapshot_tag, snapshot_version;
    bool (*copy_completed)(void *context, uint32_t format, void *pixels,
                           size_t size_bytes, uint32_t stride_bytes);
} risc_display_output_api_v1_snapshot;

static inline const risc_display_output_api_v1_snapshot *risc_display_output_snapshot(
    const risc_display_output_api_v1 *api) {
    if (!risc_display_output_metrics(api) ||
        api->struct_size < sizeof(risc_display_output_api_v1_snapshot)) return NULL;
    const risc_display_output_api_v1_snapshot *ext = (const risc_display_output_api_v1_snapshot *)api;
    return ext->snapshot_tag == RISC_DISPLAY_SNAPSHOT_TAG &&
        ext->snapshot_version == RISC_DISPLAY_SNAPSHOT_VERSION && ext->copy_completed ? ext : NULL;
}

#ifdef __cplusplus
}
#endif
