#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Runtime source with RiscDiagnosticSourceV1.h required}"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
mkdir -p "$build/results"
flags=(-std=c++17 -Wall -Wextra -Werror -I"$root/minimal/test/bootlog/stubs" -I"$RISCRTE_RUNTIME_ROOT/sdk/driver" "-DX4_BOOTLOG_INTERNAL_ROOT=\"$build/appdata\"")
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined); else flags+=(-O2); fi
g++ "${flags[@]}" "$root/minimal/test/bootlog/logger_test.cpp" -o "$build/test"
(cd "$build" && ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./test)
python3 "$root/minimal/test/bootlog/test_recovery.py" "$build/results/simulated-flash-records.bin"
