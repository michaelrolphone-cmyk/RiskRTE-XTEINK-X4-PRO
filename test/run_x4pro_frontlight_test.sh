#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-fsanitize=address,undefined -fno-omit-frame-pointer)
cc -std=c11 -Wall -Wextra -Werror -pedantic "${san[@]}" \
  -I"$repo/test/frontlight/x4_stubs" -I"$repo/sdk/driver" -I"$repo/Drivers/x4pro_board" \
  -c "$repo/Drivers/x4pro_frontlight/driver.c" -o "$build/frontlight.o"
c++ -std=c++17 -pthread -Wall -Wextra -Werror "${san[@]}" \
  -I"$repo/test/frontlight/x4_stubs" -I"$repo/test/native_battery/stubs" \
  -I"$repo/sdk/driver" -I"$repo/lib/Board_X4Pro" -I"$repo/lib/Board" \
  "$repo/lib/Board_X4Pro/BoardX4Pro.cpp" "$repo/test/frontlight/x4_test.cpp" \
  "$build/frontlight.o" -o "$build/x4-frontlight"
timeout 20s "$build/x4-frontlight"
for scenario in start-give give get-give quiesce-give owner; do
  timeout 20s "$build/x4-frontlight" "$scenario"
done
