#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime with touch.i2c v2 required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
runtime="$RISCRTE_RUNTIME_ROOT"
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
cflags=(-std=c11 -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]]; then cflags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
"${CC:-cc}" "${cflags[@]}" -I"$runtime/sdk/driver" -I"$runtime/sdk/hardware" -c "$root/minimal/drivers/x4pro_board_power/driver.c" -o "$build/board_power.o"
c++ "${flags[@]}" -I"$runtime/test/native_sleep_shim" -I"$runtime/src" -I"$runtime/sdk/app" -I"$runtime/sdk/driver" -I"$runtime/sdk/hardware" -I"$runtime/lib/ArduinoJson/src" -I"$runtime/test/drivers/stubs" \
 "$runtime/src/bootstrap/Json.cpp" "$runtime/src/bootstrap/Board.cpp" "$runtime/src/bootstrap/Runtime.cpp" \
 "$runtime/src/runtime/streams/ProviderQueueHost.cpp" "$runtime/src/runtime/streams/AppStreamSessions.cpp" \
 "$runtime/src/runtime/drivers/ProviderGraphV2.cpp" "$runtime/src/runtime/drivers/ProviderModuleV2.cpp" "$runtime/src/ports/esp32s3/CpuPort.cpp" \
 "$root/minimal/test/native_gpio_readback.cpp" "$build/board_power.o" -ldl -o "$build/readback"
for panel in ssd1677 uc8279; do
 python3 "$root/minimal/scripts/generate_profile.py" --panel "$panel" --output "$build/$panel"
 "$build/readback" "$build/$panel" "${1:-expect-fixed}"
done
