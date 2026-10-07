#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Shared Runtime checkout required}"
: "${RISCRTE_READER_ROOT:?Pinned Reader SDK checkout required}"
: "${NATIVE_DRIVER_CC:?Xtensa ESP32-S3 compiler required}"
python="${PYTHON:-python3}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
"$python" "$root/minimal/scripts/prepare_sdk.py" --runtime "$RISCRTE_RUNTIME_ROOT" --reader "$RISCRTE_READER_ROOT" --output "$build/sdk"
for provider in board_power frontlight buttons power power_buttons; do
  elf="$build/$provider.elf"
  "$NATIVE_DRIVER_CC" -std=c11 -O2 -fno-ivopts -fPIC -mtext-section-literals -mlongcalls \
    -fvisibility=hidden -fno-builtin -nostdlib -nostartfiles -shared -Wall -Wextra -Werror \
    -I"$build/sdk" -Wl,--hash-style=sysv -Wl,--exclude-libs,ALL -Wl,--no-relax \
    "$root/minimal/drivers/x4pro_$provider/driver.c" -lgcc -o "$elf"
  "$python" "$RISCRTE_READER_ROOT/scripts/normalize_xtensa_relocations.py" "$elf"
  "$python" "$RISCRTE_READER_ROOT/scripts/validate_xtensa_relative_targets.py" "$elf"
  "${NATIVE_DRIVER_CC%gcc}readelf" --dyn-syms --wide "$elf" > "$build/symbols.txt"
  "${NATIVE_DRIVER_CC%gcc}objdump" -d "$elf" > "$build/disassembly.txt"
  "$python" - "$provider" "$elf" "$build/symbols.txt" "$build/disassembly.txt" <<'PY'
import hashlib
from pathlib import Path
import sys
provider, filename, symbols, disassembly = sys.argv[1:]
blob = Path(filename).read_bytes()
assert blob[:7] == b'\x7fELF\x01\x01\x01' and int.from_bytes(blob[18:20], 'little') == 94
entries = [line.split() for line in Path(symbols).read_text().splitlines()]
exports = {r[7] for r in entries if len(r) >= 8 and r[4] == 'GLOBAL' and r[6] != 'UND' and r[3] == 'FUNC'}
imports = {r[7] for r in entries if len(r) >= 8 and r[4] == 'GLOBAL' and r[6] == 'UND'}
assert exports == {'t5_driver_get'}, exports
assert imports <= {'strcmp'}, imports
assert 's32c1i' not in Path(disassembly).read_text().lower(), 'PSRAM-unsafe compare-and-set'
print('X4 ordinary', provider, 'Xtensa PASS:', len(blob), hashlib.sha256(blob).hexdigest(), 'imports:', ','.join(sorted(imports)))
PY
done
