#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
python3 "$root/minimal/scripts/prepare_sdk.py" --runtime "$RISCRTE_RUNTIME_ROOT" --reader "${RISCRTE_READER_ROOT:?Reader required}" --output "$build/sdk"
flags=(-std=c11 -Wall -Wextra -Werror -I"$build/sdk")
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize="${SANITIZERS:-address,undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
"${CC:-cc}" "${flags[@]}" -Dt5_driver_get=power_get -c "$root/minimal/drivers/x4pro_power/driver.c" -o "$build/power.o"
"${CC:-cc}" "${flags[@]}" "$root/minimal/test/power_navigation_test.c" "$root/minimal/drivers/x4pro_power_buttons/driver.c" "$build/power.o" -o "$build/test"
"$build/test"
