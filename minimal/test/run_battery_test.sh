#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Shared Runtime checkout required}"
: "${RISCRTE_READER_ROOT:?Pinned Reader SDK checkout required}"
python="${PYTHON:-python3}"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
"$python" "$root/minimal/scripts/prepare_sdk.py" --runtime "$RISCRTE_RUNTIME_ROOT" --reader "$RISCRTE_READER_ROOT" --output "$build/sdk"
flags=(-std=c11 -Wall -Wextra -Werror -pedantic)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
fi
"${CC:-cc}" "${flags[@]}" -I"$build/sdk" \
  "$root/minimal/test/battery_test.c" "$root/minimal/drivers/x4pro_battery/driver.c" -o "$build/test"
for scenario in validation startup samples lifecycle unlock-retained start-unlock-retained; do
  "$build/test" "$scenario"
done
# Optional target check uses the same canonical SDK, not a second include order.
if [[ -n "${NATIVE_DRIVER_CC:-}" ]]; then
  elf="$build/battery.elf"
  "$NATIVE_DRIVER_CC" -std=c11 -O2 -fno-ivopts -fPIC -mtext-section-literals -mlongcalls \
    -fvisibility=hidden -fno-builtin -nostdlib -nostartfiles -shared -Wall -Wextra -Werror \
    -I"$build/sdk" -Wl,--hash-style=sysv -Wl,--exclude-libs,ALL -Wl,--no-relax \
    "$root/minimal/drivers/x4pro_battery/driver.c" -lgcc -o "$elf"
  "$python" "$RISCRTE_READER_ROOT/scripts/normalize_xtensa_relocations.py" "$elf"
  "$python" "$RISCRTE_READER_ROOT/scripts/validate_xtensa_relative_targets.py" "$elf"
  "${NATIVE_DRIVER_CC%gcc}readelf" --dyn-syms --wide "$elf" > "$build/symbols.txt"
  "${NATIVE_DRIVER_CC%gcc}objdump" -d "$elf" > "$build/disassembly.txt"
  "$python" - "$elf" "$build/symbols.txt" "$build/disassembly.txt" <<'PY'
import hashlib
from pathlib import Path
import sys
filename, symbols, disassembly = sys.argv[1:]
blob = Path(filename).read_bytes()
assert blob[:7] == b'\x7fELF\x01\x01\x01' and int.from_bytes(blob[18:20], 'little') == 94
entries = [line.split() for line in Path(symbols).read_text().splitlines()]
exports = {r[7] for r in entries if len(r) >= 8 and r[4] == 'GLOBAL' and r[6] != 'UND' and r[3] == 'FUNC'}
imports = {r[7] for r in entries if len(r) >= 8 and r[4] == 'GLOBAL' and r[6] == 'UND'}
assert exports == {'t5_driver_get'}, exports
assert imports <= {'strcmp'}, imports
assert 's32c1i' not in Path(disassembly).read_text().lower(), 'PSRAM-unsafe compare-and-set'
print('X4 ordinary battery Xtensa PASS:', len(blob), hashlib.sha256(blob).hexdigest(), 'imports:', ','.join(sorted(imports)))
PY
fi
