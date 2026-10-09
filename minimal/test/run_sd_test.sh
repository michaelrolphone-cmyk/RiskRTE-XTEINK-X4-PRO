#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Shared Runtime checkout required}"
: "${RISCRTE_READER_ROOT:?Reader checkout with external volume guard required}"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
python3 "$root/minimal/scripts/prepare_sdk.py" --runtime "$RISCRTE_RUNTIME_ROOT" --reader "$RISCRTE_READER_ROOT" --output "$build/sdk"
export X4_TRACE_FIXTURE="$build/full-boot-trace.txt"
bash "$root/minimal/test/run_bootlog_test.sh"
flags=(-std=c11 -O1 -g -Wall -Wextra -Werror -Wno-overflow)
if [[ -n "${X4_SD_DRIVER_SOURCE:-}" ]]; then flags+=("-DX4_SD_DRIVER_SOURCE=\"$X4_SD_DRIVER_SOURCE\""); else flags+=(-DX4_EXPECT_BATCHING=1); fi
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer); fi
"${CC:-cc}" "${flags[@]}" -I"$build/sdk" -I"$RISCRTE_READER_ROOT/Drivers/storage_fatfs" \
 -I"$RISCRTE_READER_ROOT/Drivers/x4pro_board" -I"$RISCRTE_READER_ROOT" \
 "$root/minimal/test/sd_test.c" "$RISCRTE_READER_ROOT/Drivers/storage_fatfs/fatfs/ff.c" \
 "$RISCRTE_READER_ROOT/Drivers/storage_fatfs/fatfs/ffunicode.c" -o "$build/test"
for scenario in validation absent lifetime mbr files-paths stale-handles reentry nonowner create-fail take-fail unlock-fail destroy-fail release-retained shutdown-write claim-retained gpio-read-fail clock-stuck crc write-rejected busy-timeout budgets generation power power-fail \
 sleep-empty sleep-absent sleep-handles sleep-busy sleep-refusal sleep-legacy sleep-sync sleep-prepare-unlock sleep-reentry \
 sleep-repeat-prepare-unlock sleep-repeat-commit-unlock sleep-repeat-resume-unlock \
 sleep-commit-clock sleep-commit-cmd-release sleep-commit-cmd-claim sleep-commit-dat-release sleep-commit-dat-claim sleep-commit-rail sleep-commit-hold sleep-commit-hold-retained sleep-commit-unlock \
 sleep-resume-unhold sleep-resume-unhold-retained sleep-resume-rail-off sleep-resume-rail-on sleep-resume-cmd-claim sleep-resume-dat-claim sleep-resume-unlock sleep-resume-crc sleep-unformatted sleep-removed \
 export-basic export-handles export-close-retained export-owner export-sleep export-bounds export-stale export-generation export-csd export-absent export-unformatted export-bad-csd \
 export-begin-sync export-unmount export-begin-unlock export-read-crc export-write-fail export-sync export-end-sync export-end-crc export-end-unlock export-end-unformatted export-end-removed export-log export-log-close export-log-retained \
 log-history log-absent log-unformatted log-readonly log-full log-partial log-close log-write-fail log-ownership log-sleep log-repeated log-rotation log-full-trace log-trace-close log-trace-invalid log-trace-timeout log-trace-export log-batching-latency; do
 if [[ -n "${X4_SD_ONLY_SCENARIO:-}" && "$scenario" != "$X4_SD_ONLY_SCENARIO" ]];then continue;fi
 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 120s "$build/test" "$scenario"
done
