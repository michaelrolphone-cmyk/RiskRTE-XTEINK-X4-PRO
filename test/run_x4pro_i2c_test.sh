#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cat > "$build/x4pro_mmio.h" <<'HEADER'
#pragma once
#include <stdbool.h>
#include <stdint.h>
void x4pro_pin_release(uint32_t pin);
void x4pro_pin_output(uint32_t pin, bool high);
bool x4pro_pin_read(uint32_t pin);
HEADER
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$build" -I"$repo/sdk/driver" -I"$repo/Drivers/x4pro_board" \
  "$repo/Drivers/x4pro_i2c/driver.c" "$repo/test/drivers/x4pro_i2c_test.c" \
  -o "$build/x4pro-i2c-test"
timeout 20s "$build/x4pro-i2c-test"
timeout 20s "$build/x4pro-i2c-test" context-refusal
