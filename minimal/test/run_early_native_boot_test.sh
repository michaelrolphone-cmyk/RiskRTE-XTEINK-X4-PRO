#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
printf '%s\n' '#define X4_NATIVE_COMPOSITION_IDENTITY "X4_NATIVE_COMPOSITION:host-fixture"' > "$work/X4NativeBuildIdentity.h"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  -I"$root/minimal/test/early_native_fake" -I"$work" \
  "$root/minimal/native/X4EarlyBoot.cpp" "$root/minimal/test/early_native_boot_test.cpp" -o "$work/test"
for held in 0 1; do
  for failure in 0 1 2 3 4 5 6 7; do "$work/test" "$failure" "$held"; done
done
