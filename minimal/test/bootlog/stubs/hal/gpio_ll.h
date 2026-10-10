#pragma once
#include "mock.h"
#define BIT(n) (1u << (n))
#define GPIO_OUT_REG 6
#define GPIO_ENABLE_REG 7
#define GPIO_FUNC1_OUT_SEL_CFG_REG 8
#define IO_MUX_GPIO1_REG 9
#define GPIO_FUNC1_OEN_SEL BIT(10)
#define SIG_GPIO_OUT_IDX 256
#define PIN_FUNC_GPIO 1
#define MCU_SEL_S 12
#define MCU_SEL_M (7u << MCU_SEL_S)
#define FUN_IE BIT(9)
#define FUN_PU BIT(8)
#define FUN_PD BIT(7)
#undef REG_READ
#define REG_READ(r) (uint32_t((r)==6 || (r)==7 ? BIT(1) : (r)==8 ? 0x500 : (r)==9 ? 0x1200 : 0x1200u+(r)))
#define REG_WRITE(reg,value) ((void)(reg),(void)(value))
struct gpio_dev_t {};
inline gpio_dev_t GPIO;
inline void gpio_ll_od_disable(gpio_dev_t*,gpio_num_t){}
inline void gpio_ll_input_enable(gpio_dev_t*,gpio_num_t){}
inline void gpio_ll_pullup_dis(gpio_dev_t*,gpio_num_t){}
inline void gpio_ll_pulldown_dis(gpio_dev_t*,gpio_num_t){}
inline void gpio_ll_set_intr_type(gpio_dev_t*,gpio_num_t,int){}
inline void gpio_ll_intr_disable(gpio_dev_t*,gpio_num_t){}
inline void gpio_ll_output_enable(gpio_dev_t*,gpio_num_t){}
inline void gpio_ll_func_sel(gpio_dev_t*,uint8_t,uint32_t){}
