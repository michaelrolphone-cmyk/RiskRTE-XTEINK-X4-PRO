#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Point to the shared RiscRTE source checkout}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -pedantic)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
"${CC:-cc}" "${flags[@]}" -I"$RISCRTE_RUNTIME_ROOT/sdk/driver" -I"$RISCRTE_RUNTIME_ROOT/sdk/hardware" \
 "$root/minimal/test/board_power_test.c" "$root/minimal/drivers/x4pro_board_power/driver.c" -o "$build/test"
for scenario in validation lifecycle reentrant-restore retained-claim create-failure start-lock-failure start-read start-low start-unlock legacy-gpio missing-hold missing-write owner busy-lock release-retry destroy-retry ready-read ready-low ready-unlock prepare-write prepare-read prepare-low hold-retained hold-unknown hold-unknown-negative hold-refused hold-refused-invalid hold-refused-context hold-refused-busy hold-refused-active hold-refused-unsupported hold-refused-read hold-refused-unlock prepared-stop restore-unhold restore-ordinary-error restore-unknown restore-read restore-low restore-unlock; do "$build/test" "$scenario"; done
