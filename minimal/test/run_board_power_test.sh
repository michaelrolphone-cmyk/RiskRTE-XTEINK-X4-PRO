#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Point to the shared RiscRTE source checkout}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -pedantic)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
"${CC:-cc}" "${flags[@]}" -I"$RISCRTE_RUNTIME_ROOT/sdk/driver" -I"$RISCRTE_RUNTIME_ROOT/sdk/hardware" \
 "$root/minimal/test/board_power_test.c" "$root/minimal/drivers/x4pro_board_power/driver.c" -o "$build/test"
for scenario in validation lifecycle readback retained-claim; do "$build/test" "$scenario"; done
