#pragma once
#include <stdint.h>
typedef int esp_err_t;
typedef int gpio_num_t;
enum { ESP_OK = 0, GPIO_NUM_1 = 1, GPIO_MODE_INPUT_OUTPUT = 3,
       GPIO_PULLUP_DISABLE = 0, GPIO_PULLDOWN_DISABLE = 0, GPIO_INTR_DISABLE = 0 };
typedef struct {
  uint64_t pin_bit_mask;
  int mode, pull_up_en, pull_down_en, intr_type;
} gpio_config_t;
extern "C" {
esp_err_t gpio_set_level(gpio_num_t, uint32_t);
esp_err_t gpio_config(const gpio_config_t*);
esp_err_t gpio_hold_dis(gpio_num_t);
esp_err_t gpio_hold_en(gpio_num_t);
int gpio_get_level(gpio_num_t);
}
