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
version="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["version"])' "$root/minimal/drivers/x4pro_uc8279_fast/manifest.json")"
if [[ "$version" == "0.1.18" ]]; then
 # Exact upstream diagnostic profile: older absolute/power-off cases do not apply.
 scenarios=(selective-nopof validation floating zero-probe unstable lut-bad claim-fail probe-spi-fail busy-absent busy-stuck clock-fail clock-rollback spi-begin spi-exchange spi-end gpio-fail unlock-fail)
else
 scenarios=(storage-upload-gap storage-power-gap storage-power-stuck storage-refresh-gap storage-refresh-short-gap storage-refresh-stuck storage-refresh-unseen storage-refresh-read-error settle-finalize settle-replace idle-default-differential idle-wake-profile idle-burst-boundary quality-cold quality-seeded-cold quality-cycle quality-busy-absent quality-busy-stuck quality-transfer-failure validation happy busy-boundary busy-short polarity snapshot lut69 floating zero-probe unstable lut-bad claim-fail probe-spi-fail busy-absent busy-stuck clock-fail clock-rollback spi-begin spi-exchange spi-end gpio-fail unlock-fail foreign-owner hold-retry release-retry retire-retry destroy-retry)
fi
for scenario in "${scenarios[@]}";do
 "$build/test" "$scenario"
done
