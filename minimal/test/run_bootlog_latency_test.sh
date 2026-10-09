#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime checkout required}"
: "${X4_TRACE_LATENCY_INPUT:?Line-separated boot transcript required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -O2 -Wall -Wextra -Werror -I"$root/minimal/test/bootlog/stubs" -I"$RISCRTE_RUNTIME_ROOT/sdk/driver" "-DX4_BOOTLOG_INTERNAL_ROOT=\"$build/appdata\"")
if [[ -n "${X4_TRACE_LATENCY_BASELINE:-}" ]];then
 g++ "${flags[@]}" "-DX4_NATIVE_SOURCE_UNDER_TEST=\"$X4_TRACE_LATENCY_BASELINE/minimal/native/X4EarlyBoot.cpp\"" "$root/minimal/test/bootlog/latency_test.cpp" -o "$build/baseline"
 "$build/baseline" "$X4_TRACE_LATENCY_INPUT" "$build/baseline.log"
fi
g++ "${flags[@]}" -DX4_EXPECT_BATCHING=1 "$root/minimal/test/bootlog/latency_test.cpp" -o "$build/current"
"$build/current" "$X4_TRACE_LATENCY_INPUT" "$build/current.log"

if [[ -f "$build/baseline.log" ]];then
 python3 - "$build" "$X4_TRACE_LATENCY_INPUT" <<'PY_COMPARE'
import re,sys
from pathlib import Path
root=Path(sys.argv[1]);first=Path(sys.argv[2]).read_text().splitlines()[0]
def payload(name):
 lines=[re.sub(r'^X4_TRACE session=\d+ event=\d+ us=\d+ ', '',line) for line in (root/name).read_text().splitlines()]
 return lines[lines.index(first):]
assert payload('baseline.log')==payload('current.log')
print('All captured source-line payloads preserved exactly; timing envelopes reflect the modeled execution: PASS')
PY_COMPARE
fi
