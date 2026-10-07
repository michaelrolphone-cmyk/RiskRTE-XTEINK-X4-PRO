#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -pedantic)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize="${SANITIZERS:-address,undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
"${CC:-cc}" "${flags[@]}" -I"$RISCRTE_RUNTIME_ROOT/sdk/driver" -I"$RISCRTE_RUNTIME_ROOT/sdk/hardware" "$root/minimal/test/power_test.c" "$root/minimal/drivers/x4pro_power/driver.c" -o "$build/test"
for case in validation held wake refused retained read-failure clock-backwards context release-retry destroy-retry unlock-retained;do "$build/test" "$case";done

"${CC:-cc}" "${flags[@]}" -I"$RISCRTE_RUNTIME_ROOT/sdk/driver" -I"$RISCRTE_RUNTIME_ROOT/sdk/hardware" "$root/minimal/test/power_deep_test.c" "$root/minimal/drivers/x4pro_power/driver.c" -o "$build/deep"
for case in missing-binding bad-binding wrong-instance bad-ready-prefix legacy-board-prefix legacy-board-tag legacy-board-version legacy-board-prepare legacy-board-restore prepare-refused prepare-unsupported prepare-retained prepare-unknown prepare-negative restore-retained restore-refused restore-unknown contract bounds native-invalid native-context native-active native-platform native-unsupported refused terminal retained unexpected-zero unexpected-positive unexpected-negative unsupported-prefix unsupported-callback held read-failure clock-backwards owner unlock-retained release-retry;do "$build/deep" "$case";done
