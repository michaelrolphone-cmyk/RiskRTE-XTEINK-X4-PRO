#pragma once
#include <stdbool.h>
#include <stdint.h>
extern uint32_t x4_fake_pressed_pins;
extern uint32_t x4_fake_input_pins;
static inline bool x4pro_pin_read(uint32_t pin) {
    return (x4_fake_pressed_pins & (1u << pin)) == 0;
}
static inline void x4pro_pin_input(uint32_t pin, bool pullup) {
    if (pullup) x4_fake_input_pins |= 1u << pin;
}
