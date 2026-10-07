#pragma once
#include <stdint.h>

namespace BoardX4Pro {
// Keep the saved/UI 0..10 range. Like GameBoy's fine night-light control,
// express low requests in tenths of one percent rather than whole percent.
// These are requested PWM duties, not measured brightness or LED current.
constexpr uint16_t frontlightTenthsPercent(uint8_t level) {
  constexpr uint16_t levels[] = {0, 1, 2, 5, 10, 20, 50, 100, 250, 500, 1000};
  return levels[level > 10 ? 10 : level];
}
}
