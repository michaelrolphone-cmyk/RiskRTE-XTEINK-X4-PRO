#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Shared Runtime checkout required}"
: "${RISCRTE_READER_ROOT:?Pinned Reader SDK checkout required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
python3 "$root/minimal/scripts/prepare_sdk.py" --runtime "$RISCRTE_RUNTIME_ROOT" --reader "$RISCRTE_READER_ROOT" --output "$build/sdk"
flags=(-std=c11 -Wall -Wextra -Werror -pedantic)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
"${CC:-cc}" "${flags[@]}" -I"$build/sdk" "$root/minimal/test/frontlight_test.c" "$root/minimal/drivers/x4pro_frontlight/driver.c" -o "$build/test"
for scenario in validation lifecycle ratios retire-retry write-retry hold-retry pwm-failure destroy-retry claim-retained unlock-retained hold-retained retire-start-retry retire-off-retry relight-claim-retained; do "$build/test" "$scenario"; done
