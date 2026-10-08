#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Point to the shared RiscRTE source checkout}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -pedantic)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
"${CC:-cc}" "${flags[@]}" -I"$RISCRTE_RUNTIME_ROOT/sdk/driver" -I"$RISCRTE_RUNTIME_ROOT/sdk/hardware" \
 "$root/minimal/test/board_power_test.c" "$root/minimal/drivers/x4pro_board_power/driver.c" -o "$build/test"
for scenario in validation lifecycle reentrant-restore create-failure start-lock-failure start-claim start-read start-low start-unlock start-retire start-retired-read start-hold-platform start-hold-invalid start-hold-context start-hold-busy start-hold-active start-hold-unsupported start-hold-retained start-hold-unknown start-hold-negative owner busy-lock destroy-retry ready-read ready-low ready-unlock prepare-read prepare-low prepared-stop restore-read restore-low restore-unlock quiesce-read quiesce-low quiesce-unlock; do "$build/test" "$scenario"; done
