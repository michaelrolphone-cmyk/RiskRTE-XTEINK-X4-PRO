#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime SDK required}"
: "${RISCRTE_APPS_ROOT:?System Apps required}"
: "${RISCRTE_DESK_SDK_ROOT:?Typed driver SDK required}"
: "${RISCRTE_UTILITIES_ROOT:?Qualified Utilities637e13b required}"
for pair in "$RISCRTE_RUNTIME_ROOT:30dcec5ce6ce33223f2b203a2399283e1f758567" "$RISCRTE_APPS_ROOT:81f884b8a053cf917054fb1433c7850714cd0c48" "$RISCRTE_UTILITIES_ROOT:637e13b0bce62ad49b756bec2468a6271d163fc7";do
 repo="${pair%:*}";sha="${pair##*:}"
 [[ "$(git -C "$repo" rev-parse HEAD)" == "$sha" ]] || { echo "Unexpected source revision: $repo" >&2;exit 1; }
 [[ -z "$(git -C "$repo" status --porcelain --untracked-files=no)" ]] || { echo "Modified qualified input: $repo" >&2;exit 1; }
done
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
cp -R "$RISCRTE_APPS_ROOT/lib/PortableApps/include" "$build/include"
cp -R "$RISCRTE_APPS_ROOT/lib/PortableApps/time" "$build/time"
cp "$RISCRTE_UTILITIES_ROOT"/lib/Alarm/include/*.h "$build/include/"
for name in RiscDisplayOutputV1 RiscDisplayOutputPowerV1 RiscTouchV1 RiscTouchPowerV1 RiscStorageVolumeV1;do
 cp "$RISCRTE_DESK_SDK_ROOT/sdk/driver/$name.h" "$build/include/"
done
flags=(-std=c11 -O1 -g -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
inc=(-I"$build/include" -I"$RISCRTE_RUNTIME_ROOT/sdk/app" -I"$RISCRTE_RUNTIME_ROOT/sdk/driver" -I"$RISCRTE_UTILITIES_ROOT/test/native_apps")
count=0
for quick in 0 1;do
extra=();[[ "$quick" == 0 ]] || extra+=(-DPORTABLE_QUICK_ACTIONS)
"${CC:-cc}" "${flags[@]}" "${inc[@]}" "${extra[@]}" -DPORTABLE_ALARM_CLIENT -DPORTABLE_DESK_CLOCK -DPORTABLE_DESK_CLOCK_SPARSE_START -DALARM_SERVICE_TAGGED_V2 -c "$root/minimal/apps/portable_sleep.c" -o "$build/client.o"
"${CC:-cc}" "${flags[@]}" "${inc[@]}" -DPORTABLE_ALARM_CLIENT "$root/minimal/test/tagged_alarm_sleep_owner_test.c" "$RISCRTE_APPS_ROOT/lib/PortableApps/src/PortableTimeZone.c" "$RISCRTE_APPS_ROOT/lib/PortableApps/src/PortableTimeZoneCatalog.c" "$build/client.o" -o "$build/test"
for route in light timer foreground;do
 for fault in api1 short tag descriptor-version output-modes features no-resume feature-mismatch no-status no-step no-refresh no-acknowledge no-prepare no-stop prepare-retained step-retained storage-retained native-retained unsupported-resume release-retained due near future;do
  ASAN_OPTIONS=detect_leaks=0 "$build/test" "$route" "$fault";count=$((count+1))
 done
 if [[ "$route" == light ]];then cases=(ok repeat refused resume-retained resume-rtc resume-backward ticket-stale descriptor-changed restore-failure);else cases=(refused terminal status-retained copied-retained panel-resume-retained);fi
 for fault in "${cases[@]}";do ASAN_OPTIONS=detect_leaks=0 "$build/test" "$route" "$fault";count=$((count+1));done
done
done
printf 'Actual tagged alarm provider + X4 sleep owner: %s scenarios x 2 brightness profiles PASS\n' "$((count/2))"
