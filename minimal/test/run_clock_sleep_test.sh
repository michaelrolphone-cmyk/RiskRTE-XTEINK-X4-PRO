#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime required}"
: "${RISCRTE_APPS_ROOT:?System Apps required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -DPORTABLE_APP_OWNS_TOUCH_CHROME -DPORTABLE_RTC_WALL_TIME -DPORTABLE_INPUT_NAVIGATION -DPORTABLE_ALARM_CLIENT -DPORTABLE_APP_SLEEP_LOCAL -DPORTABLE_CROWN_SLEEP_LOCAL -DPORTABLE_SLEEP_MANUAL_ONLY '-DPORTABLE_HOME_APP="default.elf"')
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize="${SANITIZERS:-address,undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
for quick in 0 1;do
for rotation in 0 90;do
 extra=(-DPORTABLE_DISPLAY_ROTATION=0);if [[ "$rotation" == 90 ]];then extra=(-DTEST_NATIVE_LANDSCAPE -DPORTABLE_DISPLAY_ROTATION=90);fi
 if [[ "$quick" == 1 ]];then
 extra+=(-DPORTABLE_QUICK_ACTIONS -DTEST_QUICK "$RISCRTE_APPS_ROOT/lib/PortableApps/src/quick_actions.c" "$RISCRTE_APPS_ROOT/lib/PortableApps/src/quick_render.c" "$RISCRTE_APPS_ROOT/lib/PortableApps/src/quick_session.c")
 fi
 "${CC:-cc}" "${flags[@]}" "${extra[@]}" -I"$RISCRTE_APPS_ROOT/lib/PortableApps/include" -I"$RISCRTE_APPS_ROOT/lib/NativeApps/include" -I"$RISCRTE_APPS_ROOT/test/native_apps" -I"$RISCRTE_RUNTIME_ROOT/sdk/driver" "$root/minimal/test/clock_sleep_test.c" "$root/minimal/apps/portable_sleep.c" "$RISCRTE_APPS_ROOT/Apps/paper_clock.c" "$RISCRTE_APPS_ROOT/lib/PortableApps/src/adapter.c" -o "$build/test"
 for case in 0 1 2 3 4 5 6;do ASAN_OPTIONS=detect_leaks=0 "$build/test" "$case";done
done
done
