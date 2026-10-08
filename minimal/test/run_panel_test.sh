#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Shared Runtime checkout required}"
: "${RISCRTE_READER_ROOT:?Pinned Reader SDK checkout required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
python3 "$root/minimal/scripts/prepare_sdk.py" --runtime "$RISCRTE_RUNTIME_ROOT" --reader "$RISCRTE_READER_ROOT" --output "$build/sdk"
flags=(-std=c11 -Wall -Wextra -Werror -pedantic)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
"${CC:-cc}" "${flags[@]}" -I"$build/sdk" "$root/minimal/test/panel_test.c" -o "$build/test"
scenarios=(validation pullup-retained ssd uc uc-async uc-async-history uc-async-retained uc-async-deadline ssd-poweroff uc-poweroff hold-retry legacy-hold foreign-owner unlock-retained deadline admission-deadline clock-failure clock-rollback busy-stuck busy-absent write-retained read-retained claim-retained scope-retained ambiguous mismatch unstable-probe)
if grep -q '^#define GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE' "$build/sdk/GardenPlatformV1.h"; then
  scenarios+=(retire-retry release-retry destroy-retry)
else
  echo 'Retirement suffix absent: successful unload and retirement retry checks NOT RUN'
fi
for chip in ssd uc; do
  for case in cycle owner clock gpio read unlock pof frozen hold-retry hold-resume hold-retained unhold unhold-platform resume-timeout resume-gpio resume-busy wire-budget retire release; do
    scenarios+=("$chip-sleep-$case")
  done
done
uc_wire=''
for scenario in "${scenarios[@]}"; do
  result="$("$build/test" "$scenario")";echo "$result"
  if [[ "$scenario" == uc ]];then uc_wire="${result##*wire=}";fi
  if [[ "$scenario" == uc-async ]];then [[ "${result##*wire=}" == "$uc_wire" ]];fi
done
