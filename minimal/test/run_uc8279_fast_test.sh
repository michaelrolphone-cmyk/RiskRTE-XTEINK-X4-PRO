#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime with explicit three-wire SPI required}"
: "${RISCRTE_READER_ROOT:?Reader SDK checkout required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
python3 "$root/minimal/scripts/prepare_sdk.py" --runtime "$RISCRTE_RUNTIME_ROOT" --reader "$RISCRTE_READER_ROOT" --output "$build/sdk"
flags=(-std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -pedantic)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
"${CC:-cc}" "${flags[@]}" -I"$build/sdk" "$root/minimal/test/uc8279_fast_test.c" -o "$build/test"
for scenario in settle-repeat settle-short settle-expiry settle-replace-ready settle-replace-window settle-replace-refresh settle-replace-assert settle-replace-busy settle-replace-clean settle-replace-wait settle-sleep settle-sleep-budget settle-cleanup settle-no-budget settle-busy-absent settle-busy-stuck settle-spi-begin settle-spi-exchange settle-spi-end settle-gpio settle-clock-fail settle-clock-rollback settle-queued-failure validation happy busy-boundary busy-short polarity snapshot lut69 floating zero-probe unstable lut-bad claim-fail probe-spi-fail busy-absent busy-stuck clock-fail clock-rollback spi-begin spi-exchange spi-end gpio-fail unlock-fail foreign-owner hold-retry release-retry retire-retry destroy-retry;do
 "$build/test" "$scenario"
done
