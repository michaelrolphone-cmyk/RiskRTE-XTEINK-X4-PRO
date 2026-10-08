// X4-only native composition. Ordinary providers still require full admission.
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include "X4NativeBuildIdentity.h"

extern "C" const char risc_x4_native_composition_identity[] = X4_NATIVE_COMPOSITION_IDENTITY;

namespace {
const char* startupError = "x4-gpio1-not-initialized";
bool attempted = false;
constexpr gpio_num_t boardAlive = GPIO_NUM_1;
}

extern "C" const char* risc_native_startup_error(void) { return startupError; }

extern "C" void initVariant(void) {
  if (attempted) return;
  attempted = true;
  // Leave the old pad hold engaged while restoring the digital mux, HIGH latch
  // and output/input configuration. No error path resets/releases the pad.
  startupError = "x4-gpio1-rtc-deinit";
  if (rtc_gpio_deinit(boardAlive) != ESP_OK) return;
  startupError = "x4-gpio1-stage-high";
  if (gpio_set_level(boardAlive, 1) != ESP_OK) return;
  gpio_config_t config{};
  config.pin_bit_mask = uint64_t(1) << boardAlive;
  config.mode = GPIO_MODE_INPUT_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  startupError = "x4-gpio1-configure";
  if (gpio_config(&config) != ESP_OK) return;
  startupError = "x4-gpio1-confirm-high";
  if (gpio_set_level(boardAlive, 1) != ESP_OK) return;
  startupError = "x4-gpio1-unhold";
  if (gpio_hold_dis(boardAlive) != ESP_OK) return;
  startupError = "x4-gpio1-hold";
  if (gpio_hold_en(boardAlive) != ESP_OK) return;
  startupError = "x4-gpio1-readback-low";
  if (gpio_get_level(boardAlive) != 1) return;
  startupError = nullptr;
}
