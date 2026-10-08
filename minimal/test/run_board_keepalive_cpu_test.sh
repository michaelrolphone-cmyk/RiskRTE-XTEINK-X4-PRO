#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
runtime="$RISCRTE_RUNTIME_ROOT"
flags=(-Wall -Wextra -Werror -Wno-missing-field-initializers)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize="${SANITIZERS:-address,undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
for provider in board_power power;do
 name=power;if [[ "$provider" == board_power ]];then name=board;fi
 "${CC:-cc}" -std=c11 "${flags[@]}" -I"$runtime/sdk/driver" -I"$runtime/sdk/hardware" -Dt5_driver_get="${name}_get" -c "$root/minimal/drivers/x4pro_$provider/driver.c" -o "$build/$name.o"
done
c++ -std=c++17 "${flags[@]}" -I"$runtime/test/native_sleep_shim" -I"$runtime/src" -I"$runtime/sdk/app" -I"$runtime/sdk/driver" -I"$runtime/sdk/hardware" -I"$runtime/lib/ArduinoJson/src" -I"$runtime/test/drivers/stubs" \
 "$runtime/src/bootstrap/Json.cpp" "$runtime/src/bootstrap/Board.cpp" "$runtime/src/bootstrap/Runtime.cpp" \
 "$runtime/src/runtime/drivers/ProviderGraphV2.cpp" "$runtime/src/runtime/drivers/ProviderModuleV2.cpp" "$runtime/src/ports/esp32s3/CpuPort.cpp" \
 "$root/minimal/test/board_keepalive_cpu_test.cpp" "$build/board.o" "$build/power.o" -ldl -o "$build/keepalive"
for panel in ssd1677 uc8279;do
 python3 "$root/minimal/scripts/generate_profile.py" --panel "$panel" --sleep --output "$build/$panel"
 for case in refused arm-refusal timer-refusal arm-retained timer-retained restore-read native-return-retained terminal forced-low startup-hold-refusal startup-hold-retained startup-unhold-retained;do "$build/keepalive" "$build/$panel" "$case";done
done
