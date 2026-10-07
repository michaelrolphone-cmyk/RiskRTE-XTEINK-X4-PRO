#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime retained-wake SDK required}"
: "${RISCRTE_APPS_ROOT:?System Apps sparse desk-clock source required}"
: "${RISCRTE_DESK_SDK_ROOT:?Canonical typed-power SDK source required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
mkdir "$build/include"
cp "$RISCRTE_APPS_ROOT"/lib/PortableApps/include/*.h "$build/include/"
for name in RiscDisplayOutputV1 RiscDisplayOutputPowerV1 RiscTouchV1 RiscTouchPowerV1 RiscStorageVolumeV1;do
 cp "$RISCRTE_DESK_SDK_ROOT/sdk/driver/$name.h" "$build/include/"
done
flags=(-std=c11 -Wall -Wextra -Werror -pedantic -DPORTABLE_DESK_CLOCK -DPORTABLE_DESK_CLOCK_SPARSE_START -DPORTABLE_ALARM_CLIENT)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize="${SANITIZERS:-address,undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
cases=(foreground-light foreground-acquire-prefs foreground-acquire-power foreground-release-prefs foreground-release-power
 refused terminal repeat held key-failure key-paint key-prepared clock-backwards health-initial health-stage
 acquire-{1..3} acquire-output release-{1..3} display-identity
 bad-panel bad-panel-history short-panel null-panel bad-power short-power null-power null-key bad-wake null-wake null-alarm
 dark-failed brightness-rollback catchup record-changed entry-expired entry-delay stage-delay
 alarm-due alarm-past alarm-near alarm-expired alarm-future alarm-pending alarm-stuck alarm-busy alarm-output
 alarm-step-busy alarm-step-output alarm-status-output alarm-status-busy alarm-uncertain
 boot-timer boot-gpio boot-corrupt boot-acquire boot-release)
for rc in -1 -2 -3 -4 -5 1 -99;do cases+=("panel-$rc" "resume-$rc");done
for rc in -1 -2 1 2 99 -99;do cases+=("stage-$rc" "clear-$rc" "boot-read-$rc");done
for rc in -1 -2 -3 -4 -5 -6 -7 -8 0 1 -99;do cases+=("native-$rc");done
for phase in 1 2 3 4 5 6 7;do cases+=("cancel-$phase");done
for quick in 0 1;do
 extra=();if [[ "$quick" == 1 ]];then extra+=(-DPORTABLE_QUICK_ACTIONS);fi
 "${CC:-cc}" "${flags[@]}" "${extra[@]}" -I"$build/include" -I"$RISCRTE_RUNTIME_ROOT/sdk/driver" -I"$RISCRTE_RUNTIME_ROOT/sdk/app" "$root/minimal/test/sparse_desk_clock_sleep_test.c" "$root/minimal/apps/portable_sleep.c" -o "$build/test"
 for scenario in "${cases[@]}";do ASAN_OPTIONS=detect_leaks=0 "$build/test" "$scenario";done
done
printf 'Sparse X4 client: %s scenarios x 2 brightness profiles PASS\n' "${#cases[@]}"
