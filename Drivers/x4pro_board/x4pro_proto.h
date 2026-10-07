#pragma once
/* Host-testable X4 Pro protocol facts. Derived from CrossPoint/FreeInk app1
 * notes. Nothing here talks to a device. */
#include "x4pro_pins.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline uint16_t x4pro_cw2017_millivolts(uint8_t hi, uint8_t lo) {
    uint16_t raw = (uint16_t)(((uint16_t)hi << 8) | lo) & 0x3fffu;
    return (uint16_t)(((uint32_t)raw * 5u + 8u) >> 4);
}

/* X4 Reader runs portrait 480x800. The controller reports that native frame,
 * with coordinates beginning at byte 0 of the 0x8150 contact record. */
static inline bool x4pro_gt911_map(const uint8_t raw[8], uint16_t *x, uint16_t *y, uint8_t *id) {
    uint16_t raw_x = (uint16_t)(raw[0] | ((uint16_t)raw[1] << 8));
    uint16_t raw_y = (uint16_t)(raw[2] | ((uint16_t)raw[3] << 8));
    if (raw_x >= 480u || raw_y >= 800u) return false;
    *x = raw_x;
    *y = raw_y;
    /* This X4 variant exposes coordinates without stable contact IDs. The
     * bounded Reader bootstrap admits one contact at a time. */
    *id = 1u;
    return true;
}

#include "../storage_fatfs/sd_protocol.h"
#define x4pro_crc7 risc_sd_crc7
#define x4pro_sd_command risc_sd_command
#define x4pro_sd_crc16 risc_sd_crc16
