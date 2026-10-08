#include <driver/gpio.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>

extern "C" void initVariant(void);
extern "C" const char* risc_native_startup_error(void);
extern "C" const char risc_x4_native_composition_identity[];

static int step, failure;
static bool held, configured, high, sensing;
static int operation(int expected) { assert(++step == expected); return step == failure ? -1 : ESP_OK; }
extern "C" esp_err_t rtc_gpio_deinit(gpio_num_t pin) {
  assert(pin == 1);
  return operation(1);
}
extern "C" esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level) {
  assert(pin == 1 && level == 1);
  const int result = operation(step == 1 ? 2 : 4);
  if (result == ESP_OK) high = true;
  return result;
}
extern "C" esp_err_t gpio_config(const gpio_config_t* config) {
  assert(config->pin_bit_mask == 2 && config->mode == GPIO_MODE_INPUT_OUTPUT);
  assert(config->pull_up_en == 0 && config->pull_down_en == 0 && config->intr_type == 0 && high);
  const int result = operation(3);
  if (result == ESP_OK) configured = sensing = true;
  return result;
}
extern "C" esp_err_t gpio_hold_dis(gpio_num_t pin) {
  assert(pin == 1 && high && configured && sensing);
  const int result = operation(5);
  if (result == ESP_OK) held = false;
  return result;
}
extern "C" esp_err_t gpio_hold_en(gpio_num_t pin) {
  assert(pin == 1 && high && configured && sensing && !held);
  const int result = operation(6);
  if (result == ESP_OK) held = true;
  return result;
}
extern "C" int gpio_get_level(gpio_num_t pin) {
  assert(pin == 1 && high && configured && sensing && held);
  return operation(7) == ESP_OK ? 1 : 0;
}
int main(int argc, char** argv) {
  assert(argc == 3);
  failure = std::atoi(argv[1]); held = std::atoi(argv[2]);
  assert(!std::strcmp(risc_native_startup_error(), "x4-gpio1-not-initialized"));
  assert(!std::strcmp(risc_x4_native_composition_identity, "X4_NATIVE_COMPOSITION:host-fixture"));
  initVariant();
  const char* expected[] = {nullptr, "x4-gpio1-rtc-deinit", "x4-gpio1-stage-high", "x4-gpio1-configure",
      "x4-gpio1-confirm-high", "x4-gpio1-unhold", "x4-gpio1-hold", "x4-gpio1-readback-low"};
  if (failure) {
    assert(step == failure && !std::strcmp(risc_native_startup_error(), expected[failure]));
  } else {
    assert(step == 7 && !risc_native_startup_error() && held && high && sensing);
  }
  if (failure >= 1 && failure <= 5) assert(held == bool(std::atoi(argv[2])));
  if (failure == 6) assert(!held && high);
  if (failure == 7) assert(held && high);
  const int previous = step;
  initVariant();
  assert(step == previous); // No hidden retry or low/cleanup sequence after failure.
  std::puts("X4 real native startup ordering/failure PASS");
}
