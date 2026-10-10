#!/usr/bin/env bash
# Exact no-new-flag source/object compatibility against the public X4 baseline.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Pinned Runtime SDK required}"
: "${RISCRTE_APPS_ROOT:?Shared sparse Clock header required}"
: "${RISCRTE_DESK_SDK_ROOT:?Canonical typed-power SDK required}"
: "${XTENSA_CC:?Pinned Xtensa ESP32-S3 GCC8.4 compiler required}"
base=f6c12e00e54a85fb2c09517ffad77c67c5b1851b
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
mkdir -p "$build/include" "$build/baseline"
cp "$RISCRTE_APPS_ROOT"/lib/PortableApps/include/*.h "$build/include/"
for name in RiscDisplayOutputV1 RiscDisplayOutputPowerV1 RiscTouchV1 RiscTouchPowerV1 RiscStorageVolumeV1;do
 cp "$RISCRTE_DESK_SDK_ROOT/sdk/driver/$name.h" "$build/include/"
done
git -C "$root" show "$base:minimal/apps/portable_sleep.c" > "$build/baseline/portable_sleep.c"
includes=(-iquote "$root/minimal/apps" -I"$build/include" -I"$RISCRTE_RUNTIME_ROOT/sdk/driver" -I"$RISCRTE_RUNTIME_ROOT/sdk/app")
flags=(-std=c11 -Os -Wall -Wextra -Werror -pedantic -DPORTABLE_ALARM_CLIENT)
for compiler in "${CC:-cc}" "$XTENSA_CC";do
 "$compiler" --version | head -1
 for desk in 0 1;do for quick in 0 1;do
  extra=();[[ "$desk" == 0 ]] || extra+=(-DPORTABLE_DESK_CLOCK)
  [[ "$quick" == 0 ]] || extra+=(-DPORTABLE_QUICK_ACTIONS)
  for pair in baseline current;do
   source="$build/baseline/portable_sleep.c";[[ "$pair" != current ]] || source="$root/minimal/apps/portable_sleep.c"
   "$compiler" "${flags[@]}" "${extra[@]}" "${includes[@]}" -E -P "$source" -o "$build/$pair.i"
   "$compiler" "${flags[@]}" "${extra[@]}" "${includes[@]}" -c "$source" -o "$build/$pair.o"
  done
  cmp "$build/baseline.i" "$build/current.i"
  cmp "$build/baseline.o" "$build/current.o"
  printf 'Unchanged desk=%s quick=%s object: ' "$desk" "$quick";sha256sum "$build/current.o" | cut -d' ' -f1
 done;done
 for quick in 0 1;do
  extra=();[[ "$quick" == 0 ]] || extra+=(-DPORTABLE_QUICK_ACTIONS)
  "$compiler" "${flags[@]}" "${extra[@]}" -DPORTABLE_DESK_CLOCK -DPORTABLE_DESK_CLOCK_SPARSE_START "${includes[@]}" -c "$root/minimal/apps/portable_sleep.c" -o "$build/sparse.o"
  printf 'Sparse compile quick=%s object: ' "$quick";sha256sum "$build/sparse.o" | cut -d' ' -f1
 done
done
echo 'Host + pinned Xtensa: 4 unchanged preprocess/object profiles and 2 sparse compiles each PASS'
