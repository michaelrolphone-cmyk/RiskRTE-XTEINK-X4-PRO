#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${RISCRTE_RUNTIME_ROOT:?Shared Runtime checkout required}"
: "${RISCRTE_READER_ROOT:?Reader checkout with checked sleep recovery required}"
: "${NATIVE_DRIVER_CC:?Xtensa ESP32-S3 compiler required}"
python="${PYTHON:-python3}"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
"$python" "$root/minimal/scripts/prepare_sdk.py" --runtime "$RISCRTE_RUNTIME_ROOT" --reader "$RISCRTE_READER_ROOT" --output "$build/sdk"
"$NATIVE_DRIVER_CC" -std=c11 -O2 -fno-ivopts -fPIC -mtext-section-literals -mlongcalls \
  -fvisibility=hidden -fno-builtin -nostdlib -nostartfiles -shared -Wall -Wextra -Werror \
  -I"$build/sdk" -I"$RISCRTE_READER_ROOT/Drivers/storage_fatfs" \
  -Wl,--hash-style=sysv -Wl,--exclude-libs,ALL -Wl,--no-relax \
  "$root/minimal/drivers/x4pro_sd/driver.c" \
  "$RISCRTE_READER_ROOT/Drivers/storage_fatfs/fatfs/ff.c" \
  "$RISCRTE_READER_ROOT/Drivers/storage_fatfs/fatfs/ffunicode.c" -lgcc -o "$build/driver.elf"
"$python" "$RISCRTE_READER_ROOT/scripts/normalize_xtensa_relocations.py" "$build/driver.elf"
"$python" "$RISCRTE_READER_ROOT/scripts/validate_xtensa_relative_targets.py" "$build/driver.elf"
"${NATIVE_DRIVER_CC%gcc}readelf" --dyn-syms --wide "$build/driver.elf" > "$build/symbols.txt"
"${NATIVE_DRIVER_CC%gcc}objdump" -d "$build/driver.elf" > "$build/disassembly.txt"
"$python" - "$build" <<'PY'
import hashlib
from pathlib import Path
import sys
root = Path(sys.argv[1])
blob = (root/'driver.elf').read_bytes()
assert blob[:7] == b'\x7fELF\x01\x01\x01' and int.from_bytes(blob[18:20], 'little') == 94
rows = [line.split() for line in (root/'symbols.txt').read_text().splitlines()]
exports = {r[7] for r in rows if len(r) >= 8 and r[4] == 'GLOBAL' and r[6] != 'UND' and r[3] == 'FUNC'}
imports = {r[7] for r in rows if len(r) >= 8 and r[4] == 'GLOBAL' and r[6] == 'UND'}
assert exports == {'t5_driver_get'}, exports
assert imports <= {'strcmp', 'memcpy', 'memmove', 'memset', 'memcmp', 'strlen', 'strchr'}, imports
assert 's32c1i' not in (root/'disassembly.txt').read_text().lower()
print('X4 SD Xtensa structure PASS:', len(blob), hashlib.sha256(blob).hexdigest(), 'imports:', ','.join(sorted(imports)))
PY
if [[ -n "${X4_SD_TARGET_OUT:-}" ]]; then
  mkdir "$X4_SD_TARGET_OUT"
  cp "$build/driver.elf" "$build/symbols.txt" "$build/disassembly.txt" "$X4_SD_TARGET_OUT/"
  cp "$build/sdk/source-hashes.json" "$X4_SD_TARGET_OUT/sdk-source-hashes.json"
  cp "$root/minimal/drivers/x4pro_sd/manifest.json" "$X4_SD_TARGET_OUT/manifest.json"
  "$python" - "$root" "$X4_SD_TARGET_OUT" "$NATIVE_DRIVER_CC" <<'PY'
import hashlib,json,subprocess,sys
from pathlib import Path
root,out,cc=Path(sys.argv[1]),Path(sys.argv[2]),sys.argv[3]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
record={'source_commit':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
        'working_tree_dirty':bool(subprocess.check_output(['git','-C',str(root),'status','--porcelain'],text=True).strip()),
        'compiler':subprocess.check_output([cc,'--version'],text=True).splitlines()[0],
        'elf_sha256':sha(out/'driver.elf'),'elf_bytes':(out/'driver.elf').stat().st_size,
        'source_sha256':{p:sha(root/p) for p in ('minimal/drivers/x4pro_sd/driver.c','minimal/drivers/x4pro_sd/BootLog.h','minimal/drivers/x4pro_sd/manifest.json','minimal/test/run_sd_target_test.sh')},
        'target_structure_passed':True,'device_tested':False}
(out/'target-proof.json').write_text(json.dumps(record,indent=2)+'\n')
PY
fi
