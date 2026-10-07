#!/usr/bin/env bash
# Exercise the actual provider with bus/GPIO fakes; no board MMIO or gauge writes.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cat > "$build/x4pro_mmio.h" <<'HEADER'
#pragma once
#include <stdbool.h>
#include <stdint.h>
bool x4pro_pin_read(uint32_t pin);
void x4pro_pin_input(uint32_t pin, bool pullup);
HEADER
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$build" -I"$repo/sdk/driver" -I"$repo/Drivers/x4pro_board" \
  "$repo/Drivers/x4pro_battery/driver.c" \
  "$repo/test/drivers/x4pro_battery_test.c" -o "$build/x4pro-battery-test"
"$build/x4pro-battery-test"
