#pragma once
#include <stdbool.h>
#include <stdint.h>
extern uint8_t x4_fake_busy;
extern uint32_t x4_fake_refresh_count;
extern uint32_t x4_fake_bytes;
static uint8_t x4_fake_shift;
static bool x4_fake_dc;
static bool x4_fake_cs;
extern void x4_fake_on_level(void);
extern uint32_t x4_fake_output_mask;
extern uint32_t x4_fake_level_before_config;
extern uint32_t x4_fake_cmd46;
extern uint32_t x4_fake_cmd47;
extern uint32_t x4_fake_cmd24;
extern uint32_t x4_fake_cmd26;
extern uint8_t x4_fake_hold_busy;
extern uint8_t x4_fake_uc_stage;
extern uint32_t x4_fake_uc_cmd13;
extern uint32_t x4_fake_uc_cmd10;
extern uint32_t x4_fake_uc_pon_early;
extern uint32_t x4_fake_hold_mask, x4_fake_poweroffs, x4_fake_deep_sleeps;
extern bool x4_fake_poweroff_stuck, x4_fake_poweroff_active;
extern uint8_t x4_fake_old_pixel, x4_fake_new_pixel, x4_fake_window[9], x4_fake_registers[256][4];
static uint8_t x4_fake_command;
static unsigned x4_fake_data_bits, x4_fake_data_bytes;

static inline void x4pro_pin_level(uint32_t pin, bool level) {
    if (((x4_fake_output_mask >> pin) & 1u) == 0) x4_fake_level_before_config |= 1u << pin;
    if (pin == 11) x4_fake_shift = (uint8_t)((x4_fake_shift << 1) | (level ? 1u : 0u));
    if (pin == 12 && level && x4_fake_dc && !x4_fake_cs) {
        if (++x4_fake_data_bits == 8u) {
            if (x4_fake_data_bytes < 4u) x4_fake_registers[x4_fake_command][x4_fake_data_bytes] = x4_fake_shift;
            if (x4_fake_busy != 6 && x4_fake_data_bytes == 0) {
                if (x4_fake_command == 0x26) x4_fake_old_pixel = x4_fake_shift;
                if (x4_fake_command == 0x24) x4_fake_new_pixel = x4_fake_shift;
            }
            if (x4_fake_command == 0x90 && x4_fake_data_bytes < 9u) x4_fake_window[x4_fake_data_bytes] = x4_fake_shift;
            if (x4_fake_busy == 6 && x4_fake_data_bytes == 12000u) {
                if (x4_fake_command == 0x10) x4_fake_old_pixel = x4_fake_shift;
                if (x4_fake_command == 0x13) x4_fake_new_pixel = x4_fake_shift;
            }
            ++x4_fake_data_bytes; x4_fake_data_bits = 0;
        }
    }
    if (pin == 13) {
        if (x4_fake_cs && !level) x4_fake_shift = 0;
        if (!x4_fake_cs && level && !x4_fake_dc) {
            x4_fake_command = x4_fake_shift; x4_fake_data_bits = x4_fake_data_bytes = 0;
            if (x4_fake_busy == 6 && x4_fake_shift == 0x02) { ++x4_fake_poweroffs; x4_fake_poweroff_active = true; }
            if (x4_fake_busy == 6 && x4_fake_shift == 0x07) ++x4_fake_deep_sleeps;
            if (x4_fake_shift == 0x20) ++x4_fake_refresh_count;
            if (x4_fake_shift == 0x46) ++x4_fake_cmd46;
            if (x4_fake_shift == 0x47) ++x4_fake_cmd47;
            if (x4_fake_shift == 0x24) ++x4_fake_cmd24;
            if (x4_fake_shift == 0x26) ++x4_fake_cmd26;
            if (x4_fake_busy == 6 && x4_fake_shift == 0x13) ++x4_fake_uc_cmd13;
            if (x4_fake_busy == 6 && x4_fake_shift == 0x10) ++x4_fake_uc_cmd10;
            if (x4_fake_busy == 6 && x4_fake_shift == 0x04) x4_fake_uc_stage = 4;
            if (x4_fake_busy == 6 && x4_fake_shift == 0x00 && x4_fake_uc_stage == 4) ++x4_fake_uc_pon_early;
            if (x4_fake_busy == 6 && x4_fake_shift == 0x12) x4_fake_uc_stage = 1;
            if (x4_fake_busy == 4 || (x4_fake_busy == 5 && x4_fake_shift == 0x46)) x4_fake_hold_busy = 1;
        }
        x4_fake_cs = level;
    }
    if (pin == 18) x4_fake_dc = level;
    ++x4_fake_bytes;
    x4_fake_on_level();
    (void)pin;
}
static inline void x4pro_pin_output(uint32_t pin, bool level) {
    x4_fake_output_mask |= 1u << pin;
    x4pro_pin_level(pin, level);
}
static inline void x4pro_pin_input(uint32_t pin, bool pullup) { (void)pin; (void)pullup; }
static inline void x4pro_pin_release(uint32_t pin) { (void)pin; }
static inline void x4pro_epd_reset_unhold(void) { x4_fake_hold_mask &= ~(1u << 14); }
static inline bool x4pro_pin_read(uint32_t pin) {
    (void)pin;
    if (x4_fake_busy == 6 && x4_fake_poweroff_active && x4_fake_poweroff_stuck) return false;
    if (x4_fake_busy == 6) return x4_fake_uc_stage == 0 || x4_fake_uc_stage == 3 || x4_fake_uc_stage == 4;
    if (x4_fake_hold_busy) return true;
    if (x4_fake_busy == 1 && x4_fake_refresh_count) return true;
    if (x4_fake_busy == 3 && x4_fake_refresh_count) return true;
    return x4_fake_busy == 2;
}
static inline void x4_fake_sleep_hook(void) {
    if (x4_fake_busy == 6 && x4_fake_uc_stage == 1) x4_fake_uc_stage = 3;
    if (x4_fake_busy == 6 && x4_fake_uc_stage == 4) x4_fake_uc_stage = 0;
    if (x4_fake_busy == 1 && x4_fake_refresh_count) x4_fake_busy = 0;
    if (x4_fake_busy == 4) x4_fake_hold_busy = 0;
}

static inline void x4pro_pin_hold(uint32_t pin, bool hold) { if (hold) x4_fake_hold_mask |= 1u << pin; else x4_fake_hold_mask &= ~(1u << pin); }
