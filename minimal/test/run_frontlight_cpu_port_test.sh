#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Shared Runtime checkout required}"
: "${RISCRTE_READER_ROOT:?Pinned Reader SDK checkout required}"
runtime="$RISCRTE_RUNTIME_ROOT"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
python3 "$root/minimal/scripts/prepare_sdk.py" --runtime "$runtime" --reader "$RISCRTE_READER_ROOT" --output "$build/sdk"
cflags=(-std=c11 -Wall -Wextra -Werror -pedantic)
cxxflags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers)
san=()
if [[ "${SANITIZE:-0}" == 1 ]]; then
    san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
includes=(-I"$build/sdk" -I"$runtime/src" -I"$runtime/sdk/app" -I"$runtime/lib/ArduinoJson/src" -I"$runtime/test/drivers/stubs")
sources=("$runtime/src/bootstrap/Json.cpp" "$runtime/src/bootstrap/Board.cpp" "$runtime/src/bootstrap/Runtime.cpp"
         "$runtime/src/runtime/streams/ProviderQueueHost.cpp" "$runtime/src/runtime/streams/AppStreamSessions.cpp"
         "$runtime/src/runtime/drivers/ProviderGraphV2.cpp" "$runtime/src/runtime/drivers/ProviderModuleV2.cpp"
         "$runtime/src/ports/esp32s3/CpuPort.cpp")
"${CC:-cc}" "${cflags[@]}" "${san[@]}" -I"$build/sdk" -c "$root/minimal/drivers/x4pro_frontlight/driver.c" -o "$build/frontlight.o"
"${CXX:-c++}" "${cxxflags[@]}" "${san[@]}" -rdynamic "${includes[@]}" "${sources[@]}" \
    "$root/minimal/test/frontlight_cpu_port_test.cpp" "$build/frontlight.o" -ldl -o "$build/test"
ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0" "$build/test"
