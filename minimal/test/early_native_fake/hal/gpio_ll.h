#pragma once
#include <driver/gpio.h>
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
struct gpio_dev_t {};
inline gpio_dev_t GPIO;
void x4_test_stage(unsigned);
void x4_test_reg_write(unsigned, uint32_t);
#define REG_WRITE(reg,value) x4_test_reg_write(reg,value)
inline void gpio_ll_od_disable(gpio_dev_t*,gpio_num_t){x4_test_stage(1);}
inline void gpio_ll_input_enable(gpio_dev_t*,gpio_num_t){x4_test_stage(2);}
inline void gpio_ll_pullup_dis(gpio_dev_t*,gpio_num_t){x4_test_stage(3);}
inline void gpio_ll_pulldown_dis(gpio_dev_t*,gpio_num_t){x4_test_stage(4);}
inline void gpio_ll_set_intr_type(gpio_dev_t*,gpio_num_t,int){x4_test_stage(5);}
inline void gpio_ll_intr_disable(gpio_dev_t*,gpio_num_t){x4_test_stage(6);}
inline void gpio_ll_output_enable(gpio_dev_t*,gpio_num_t){x4_test_stage(7);}
inline void gpio_ll_func_sel(gpio_dev_t*,uint8_t,uint32_t){x4_test_stage(8);}
