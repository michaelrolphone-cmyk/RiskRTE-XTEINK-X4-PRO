#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime retained-wake SDK required}"
: "${RISCRTE_APPS_ROOT:?System Apps desk-clock source required}"
: "${RISCRTE_DESK_SDK_ROOT:?Canonical typed-power SDK source required}"
settings="${RISCRTE_SETTINGS_ROOT:-$RISCRTE_APPS_ROOT}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
mkdir "$build/include"
# Copied application headers have quoted sibling includes: overlay the canonical
# driver set into ONE include root, never mix multiple copies of pragma-once APIs.
cp "$RISCRTE_APPS_ROOT"/lib/PortableApps/include/*.h "$build/include/"
for name in RiscDisplayOutputV1 RiscDisplayOutputPowerV1 RiscTouchV1 RiscTouchPowerV1 RiscStorageVolumeV1;do
 cp "$RISCRTE_DESK_SDK_ROOT/sdk/driver/$name.h" "$build/include/"
done
cp "$settings/lib/PortableApps/include/PortableSleepPolicy.h" "$build/include/"
flags=(-std=c11 -Wall -Wextra -Werror -pedantic -DPORTABLE_DESK_CLOCK -DPORTABLE_ALARM_CLIENT)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize="${SANITIZERS:-address,undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
cases=(refusal terminal deep-retained deep-zero deep-positive deep-unknown
 light hybrid future-mode settings-missing settings-io settings-corrupt
 missing-{1..8} bad-panel bad-touch bad-storage bad-power bad-wake
 short-panel short-touch short-storage short-power short-wifi short-bt null-wifi null-bt bt-status-failure
 null-panel null-touch null-storage null-power null-wake null-alarm bad-panel-history bad-storage-terminal
 held key-failure key-paint key-after-prepare clock-backwards release-prefs release-deep release-slot-{1..8} health-initial health-stage
 alarm-future alarm-near alarm-expired alarm-due alarm-past alarm-pending alarm-stuck
 alarm-step-busy alarm-step-output alarm-status-output alarm-uncertain
 entry-expired entry-delay stage-delay record-changed catchup
 boot-timer boot-gpio boot-reset boot-power boot-absent boot-mismatch boot-corrupt
 boot-type boot-schema boot-size boot-release boot-missing)
# Every ordinary/retained/unknown typed prepare and every failed rollback code.
for rc in -1 -2 -3 -4 -5 -6 1 -99;do cases+=("fail-2-$rc");done
for rc in -1 -2 -3 -4 -5 1 -99;do cases+=("fail-3-$rc");done
for op in 1 4 5 9 18 20;do cases+=("fail-$op-0");done
for op in 6 7 8;do for rc in -1 -2 -3 -4 -5 1 -99;do cases+=("fail-$op-$rc");done;done
for op in 10 11;do cases+=("fail-$op--1" "fail-$op--2" "fail-$op-99");done
for rc in -1 -2 -3 -4 -5 -6 -7 -8;do cases+=("fail-14-$rc");done
for op in 1 2 3 4 5 18 19 20 21 98 99;do cases+=("cancel-$op");done
for rc in 1 2 3 4 5 7;do cases+=("deep-code-$rc");done
for op in 19 21;do for rc in 1 2 99;do cases+=("fail-$op-$rc");done;done
for quick in 0 1;do
 extra=();if [[ "$quick" == 1 ]];then extra+=(-DPORTABLE_QUICK_ACTIONS);fi
 "${CC:-cc}" "${flags[@]}" "${extra[@]}" -I"$build/include" -I"$RISCRTE_RUNTIME_ROOT/sdk/driver" -I"$RISCRTE_RUNTIME_ROOT/sdk/app" "$root/minimal/test/desk_clock_sleep_test.c" "$root/minimal/apps/portable_sleep.c" -o "$build/test"
 for scenario in "${cases[@]}";do ASAN_OPTIONS=detect_leaks=0 "$build/test" "$scenario";done
done
printf 'Desk-clock lifecycle: %s scenarios x 2 brightness profiles PASS\n' "${#cases[@]}"
