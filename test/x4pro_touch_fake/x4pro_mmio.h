#pragma once
#include <stdbool.h>
#include <stdint.h>
void fake_pin_output(uint32_t pin, bool level);
void fake_pin_level(uint32_t pin, bool level);
void fake_pin_input(uint32_t pin, bool pullup);
static inline void x4pro_pin_output(uint32_t pin, bool level) { fake_pin_output(pin, level); }
static inline void x4pro_pin_level(uint32_t pin, bool level) { fake_pin_level(pin, level); }
static inline void x4pro_pin_input(uint32_t pin, bool pullup) { fake_pin_input(pin, pullup); }

static inline void x4pro_pin_hold(uint32_t pin, bool hold) { (void)pin; (void)hold; }
