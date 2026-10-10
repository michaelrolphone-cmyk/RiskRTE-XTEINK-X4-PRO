#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <RiscLightSleepV1.h>
#define X4_POWER_CAPABILITY "x4.power"
/* Explicit product authority. Never append/cast this onto navigation or gauge.
 * GPIO3 stays private. read_key confers no token; sleep requires its own grant.
 * neutral must observe a continuous 30ms released key before every entry.
 * Native RETAINED is terminal: no restore, release, polling or cleanup I/O. */
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    bool (*read_key)(void *, bool *down);
    int32_t (*light_sleep)(void *, uint32_t duration_ms, risc_light_sleep_result_v1 *);
} x4_power_v1;
