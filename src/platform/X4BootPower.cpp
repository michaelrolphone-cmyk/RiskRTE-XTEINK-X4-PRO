#include "X4BootPower.h"

#if defined(BOARD_XTEINK_X4_PRO)
#include <driver/gpio.h>
#include "x4pro_pins.h"

bool x4PrepareBootPower() {
  // The X4 Pro reference asserts this board-alive rail before any peripheral
  // initialization. Do not leave it dependent on starting the touch ELF: that
  // occurs seconds after boot and is deliberately absent on minute wakes.
  // Restore both output-enable/mux and HIGH while the old hold remains set.
  // Only then release/re-hold the RTC-capable pad, avoiding a floating/LOW gap
  // after deep reset. GPIO1 stays defined through sleep and boot. SD and
  // touch keep their independently switched OFF rails (GPIO5 / GPIO2).
  const auto pin = static_cast<gpio_num_t>(X4PRO_PIN_PERIPH_EN);
  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << X4PRO_PIN_PERIPH_EN;
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  return gpio_set_level(pin, 1) == ESP_OK &&
         gpio_config(&config) == ESP_OK &&
         gpio_set_level(pin, 1) == ESP_OK &&
         gpio_hold_dis(pin) == ESP_OK &&
         gpio_hold_en(pin) == ESP_OK;
}
#else
bool x4PrepareBootPower() { return true; }
#endif
