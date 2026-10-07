#pragma once
#include <stdint.h>
#include <stdbool.h>
#define X4PRO_GPIO_MATRIX_BASE 0x60004554u
#ifdef __cplusplus
extern "C" {
#endif
uint32_t x4pro_reg_read(uint32_t);
void x4pro_reg_write(uint32_t, uint32_t);
void x4pro_pin_output(uint32_t, bool);
void x4pro_pin_hold(uint32_t, bool);
#ifdef __cplusplus
}
#endif
