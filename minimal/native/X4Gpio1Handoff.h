#pragma once
#include <driver/gpio.h>
#include <hal/gpio_ll.h>
#include <soc/gpio_reg.h>
#include <soc/gpio_sig_map.h>
#include <soc/io_mux_reg.h>

// X4 / pinned ESP32-S3 only. Stage the hidden digital path without touching
// RTC mux or pad hold. gpio_config cannot do this: it deinitializes RTC first.
// No provider owns GPIO1 at this app_main entry point.
__attribute__((noinline)) static esp_err_t stageDigitalRail() {
  constexpr gpio_num_t pin = GPIO_NUM_1;
  constexpr uint32_t output = SIG_GPIO_OUT_IDX | GPIO_FUNC1_OEN_SEL;
  // Route GPIO_OUT and GPIO_ENABLE, both non-inverted, before enabling output.
  // The caller has already staged HIGH through gpio_set_level.
  REG_WRITE(GPIO_FUNC1_OUT_SEL_CFG_REG, output);
  gpio_ll_od_disable(&GPIO, pin);
  gpio_ll_input_enable(&GPIO, pin);
  gpio_ll_pullup_dis(&GPIO, pin);
  gpio_ll_pulldown_dis(&GPIO, pin);
  gpio_ll_set_intr_type(&GPIO, pin, GPIO_INTR_DISABLE);
  gpio_ll_intr_disable(&GPIO, pin);
  gpio_ll_output_enable(&GPIO, pin);
  gpio_ll_func_sel(&GPIO, pin, PIN_FUNC_GPIO);
  // Check digital registers, not held-pad input, before selecting the path.
  const uint32_t muxMask = MCU_SEL_M | FUN_IE | FUN_PU | FUN_PD;
  const uint32_t mux = (PIN_FUNC_GPIO << MCU_SEL_S) | FUN_IE;
  return (REG_READ(GPIO_OUT_REG) & BIT(1)) &&
         (REG_READ(GPIO_ENABLE_REG) & BIT(1)) &&
         (REG_READ(GPIO_FUNC1_OUT_SEL_CFG_REG) & 0xfffu) == output &&
         (REG_READ(IO_MUX_GPIO1_REG) & muxMask) == mux ? ESP_OK : ESP_FAIL;
}
