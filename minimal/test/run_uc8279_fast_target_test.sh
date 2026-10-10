#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Shared Runtime checkout required}"
: "${RISCRTE_READER_ROOT:?Pinned Reader SDK checkout required}"
: "${NATIVE_DRIVER_CC:?Xtensa ESP32-S3 compiler required}"
python="${PYTHON:-python3}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
"$python" "$root/minimal/scripts/prepare_sdk.py" --runtime "$RISCRTE_RUNTIME_ROOT" --reader "$RISCRTE_READER_ROOT" --output "$build/sdk"
"$NATIVE_DRIVER_CC" -std=c11 -O2 -fno-ivopts -fPIC -mtext-section-literals -mlongcalls \
  -fvisibility=hidden -fno-builtin -nostdlib -nostartfiles -shared -Wall -Wextra -Werror \
  -I"$build/sdk" -Wl,--hash-style=sysv -Wl,--exclude-libs,ALL -Wl,--no-relax \
  "$root/minimal/drivers/x4pro_uc8279_fast/driver.c" -lgcc -o "$build/driver.elf"
"$python" "$RISCRTE_READER_ROOT/scripts/normalize_xtensa_relocations.py" "$build/driver.elf"
"$python" "$RISCRTE_READER_ROOT/scripts/validate_xtensa_relative_targets.py" "$build/driver.elf"
"${NATIVE_DRIVER_CC%gcc}readelf" --dyn-syms --wide "$build/driver.elf" > "$build/symbols.txt"
"$python" - "$build/driver.elf" "$build/symbols.txt" <<'PY'
import hashlib
from pathlib import Path
import sys
path = Path(sys.argv[1])
blob = path.read_bytes()
assert blob[:7] == b'\x7fELF\x01\x01\x01' and int.from_bytes(blob[18:20], 'little') == 94
entries = [line.split() for line in Path(sys.argv[2]).read_text().splitlines()]
exports = {r[7] for r in entries if len(r) >= 8 and r[4] == 'GLOBAL' and r[6] != 'UND' and r[3] == 'FUNC'}
imports = {r[7] for r in entries if len(r) >= 8 and r[4] == 'GLOBAL' and r[6] == 'UND'}
assert exports == {'t5_driver_get'}, exports
assert imports <= {'memcpy', 'memset', 'strcmp'}, imports
print('x4 UC8279 fast Xtensa: PASS', len(blob), hashlib.sha256(blob).hexdigest(), 'imports:', ','.join(sorted(imports)))
PY
