#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Shared Runtime checkout required}"
: "${RISCRTE_READER_ROOT:?Reader checkout with external volume guard required}"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
python3 "$root/minimal/scripts/prepare_sdk.py" --runtime "$RISCRTE_RUNTIME_ROOT" --reader "$RISCRTE_READER_ROOT" --output "$build/sdk"
flags=(-std=c11 -O1 -g -Wall -Wextra -Werror -Wno-overflow)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer); fi
"${CC:-cc}" "${flags[@]}" -I"$build/sdk" -I"$RISCRTE_READER_ROOT/Drivers/storage_fatfs" \
 -I"$RISCRTE_READER_ROOT/Drivers/x4pro_board" -I"$RISCRTE_READER_ROOT" \
 "$root/minimal/test/sd_test.c" "$RISCRTE_READER_ROOT/Drivers/storage_fatfs/fatfs/ff.c" \
 "$RISCRTE_READER_ROOT/Drivers/storage_fatfs/fatfs/ffunicode.c" -o "$build/test"
for scenario in validation absent lifetime mbr stale-handles reentry nonowner create-fail take-fail unlock-fail destroy-fail release-retained shutdown-write claim-retained gpio-read-fail clock-stuck crc write-rejected busy-timeout budgets generation power power-fail; do
 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 120s "$build/test" "$scenario"
done
