#pragma once
#include <stdbool.h>
#include <stdint.h>
#define X4_POWER_READY_CAPABILITY "board.power.ready"
#define X4_POWER_READY_API_V1 1u
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    bool (*ready)(void *context);
} x4_power_ready_api_v1;
