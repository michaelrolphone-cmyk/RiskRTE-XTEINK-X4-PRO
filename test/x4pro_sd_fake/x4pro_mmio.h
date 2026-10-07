#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "x4pro_pins.h"
extern unsigned x4_sd_fake_ticks;
extern bool x4_sd_fake_bad_pin;
static inline void x4pro_pin_output(uint32_t pin, bool level) {
    (void)level;
    if (pin != X4PRO_PIN_SD_PWR && pin != X4PRO_PIN_SD_CLK && pin != X4PRO_PIN_SD_CMD)
        x4_sd_fake_bad_pin = true;
    if (pin == X4PRO_PIN_SD_CLK) ++x4_sd_fake_ticks;
}
static inline void x4pro_pin_level(uint32_t pin, bool level) { x4pro_pin_output(pin, level); }
static inline void x4pro_pin_release(uint32_t pin) {
    if (pin != X4PRO_PIN_SD_CMD && pin != X4PRO_PIN_SD_DAT0) x4_sd_fake_bad_pin = true;
}
static inline bool x4pro_pin_read(uint32_t pin) {
    if (pin != X4PRO_PIN_SD_CMD) x4_sd_fake_bad_pin = true;
    return true; /* No card drives CMD low. */
}

static inline void x4pro_pin_input(uint32_t pin, bool pullup) {
    if ((pin != X4PRO_PIN_SD_CMD && pin != X4PRO_PIN_SD_DAT0) || pullup) x4_sd_fake_bad_pin = true;
}
static inline void x4pro_pin_hold(uint32_t pin, bool hold) {
    (void)hold; if (pin != X4PRO_PIN_SD_PWR) x4_sd_fake_bad_pin = true;
}

uint32_t x4pro_sd_test_cycle_count(void);
#define X4PRO_SD_CYCLE_COUNT() x4pro_sd_test_cycle_count()
