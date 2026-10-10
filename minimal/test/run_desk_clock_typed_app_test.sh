#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime retained-wake SDK required}"
: "${RISCRTE_APPS_ROOT:?System Apps desk-clock source required}"
: "${RISCRTE_DESK_SDK_ROOT:?Canonical typed-power SDK source required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
cp -R "$RISCRTE_APPS_ROOT/lib/PortableApps/include" "$build/include"
cp -R "$RISCRTE_APPS_ROOT/lib/PortableApps/time" "$build/time"
for name in RiscDisplayOutputV1 RiscDisplayOutputPowerV1 RiscTouchV1 RiscTouchPowerV1 RiscStorageVolumeV1;do
 cp "$RISCRTE_DESK_SDK_ROOT/sdk/driver/$name.h" "$build/include/"
done
cp "${RISCRTE_SETTINGS_ROOT:-$RISCRTE_APPS_ROOT}/lib/PortableApps/include/PortableSleepPolicy.h" "$build/include/"
flags=(-std=c11 -Wall -Wextra -Werror -DTEST_NATIVE_LANDSCAPE -DPORTABLE_DISPLAY_ROTATION=90 -DPORTABLE_APP_OWNS_TOUCH_CHROME -DPORTABLE_RTC_WALL_TIME -DPORTABLE_ALARM_CLIENT -DPORTABLE_INPUT_NAVIGATION -DPORTABLE_APP_SLEEP_LOCAL -DPORTABLE_CROWN_SLEEP_LOCAL -DPORTABLE_SLEEP_MANUAL_ONLY -DPORTABLE_DESK_CLOCK)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize="${SANITIZERS:-address,undefined}" -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
for quick in 0 1;do
extra=();if [[ "$quick" == 1 ]];then extra+=(-DPORTABLE_QUICK_ACTIONS -DPORTABLE_QUICK_RADIOS "$RISCRTE_APPS_ROOT/lib/PortableApps/src/quick_actions.c" "$RISCRTE_APPS_ROOT/lib/PortableApps/src/quick_render.c" "$RISCRTE_APPS_ROOT/lib/PortableApps/src/quick_session.c" "$RISCRTE_APPS_ROOT/lib/PortableApps/src/quick_radios.c");fi
"${CC:-cc}" "${flags[@]}" "${extra[@]}" -I"$build/include" -I"$RISCRTE_APPS_ROOT/lib/NativeApps/include" -I"$RISCRTE_APPS_ROOT/test/native_apps" -I"$RISCRTE_RUNTIME_ROOT/sdk/app" -I"$RISCRTE_RUNTIME_ROOT/sdk/driver" "$root/minimal/test/desk_clock_typed_app_test.c" "$root/minimal/apps/portable_sleep.c" "$RISCRTE_APPS_ROOT/Apps/paper_clock.c" "$RISCRTE_APPS_ROOT/lib/PortableApps/src/adapter.c" "$RISCRTE_APPS_ROOT/lib/PortableApps/src/desk_clock_faces.c" -o "$build/test"
for scenario in terminal refused retained key-retained touch-retained touch-refused sd-refused wifi-retained stage-refused alarm-due held;do
 ASAN_OPTIONS=detect_leaks=0 "$build/test" "$scenario" "$build/q$quick-$scenario.state"
done
# A separate process decodes the owned timer record and reproduces the exact old
# image before differential rendering. GPIO-classified records never seed it.
for cycle in {1..32};do ASAN_OPTIONS=detect_leaks=0 "$build/test" terminal "$build/q$quick-terminal.state";done
ASAN_OPTIONS=detect_leaks=0 "$build/test" seed-retained "$build/q$quick-terminal.state"
ASAN_OPTIONS=detect_leaks=0 "$build/test" gpio "$build/q$quick-terminal.state"
done
echo 'Real app/adapter/X4 typed client: 45 fresh processes x 2 radio profiles PASS'
