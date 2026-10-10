#!/usr/bin/env bash
set -euo pipefail
: "${SOURCE:?clean X4 source checkout required}"
: "${SDK:?canonical SDK directory required}"
: "${READER:?qualified Reader scripts required}"
: "${CC:?Xtensa ESP32-S3 compiler required}"
: "${PYTHON:?Python with pyelftools required}"
: "${OUTPUT:?new output directory required}"
test ! -e "$OUTPUT"; mkdir -p "$OUTPUT"
test -z "$(git -C "$SOURCE" status --porcelain)"
"$CC" -std=c11 -O2 -fno-ivopts -fPIC -mtext-section-literals -mlongcalls \
 -fvisibility=hidden -fno-builtin -nostdlib -nostartfiles -shared -Wall -Wextra -Werror \
 -I"$SDK" -Wl,--hash-style=sysv -Wl,--exclude-libs,ALL -Wl,--no-relax \
 "$SOURCE/minimal/drivers/x4pro_gt911/driver.c" -lgcc -o "$OUTPUT/driver.elf"
"$PYTHON" "$READER/scripts/normalize_xtensa_relocations.py" "$OUTPUT/driver.elf"
"$PYTHON" "$READER/scripts/validate_xtensa_relative_targets.py" "$OUTPUT/driver.elf"
"${CC%gcc}readelf" --dyn-syms --wide "$OUTPUT/driver.elf" > "$OUTPUT/symbols.txt"
"${CC%gcc}objdump" -d "$OUTPUT/driver.elf" > "$OUTPUT/disassembly.txt"
cp "$SOURCE/minimal/drivers/x4pro_gt911/manifest.json" "$OUTPUT/manifest.json"
"$PYTHON" - "$OUTPUT" <<'PY'
import hashlib,json,os,subprocess,sys
from pathlib import Path
out=Path(sys.argv[1]);source=Path(os.environ['SOURCE']);sdk=Path(os.environ['SDK'])
rows=[l.split() for l in (out/'symbols.txt').read_text().splitlines()]
i=sorted({r[7] for r in rows if len(r)>=8 and r[4]=='GLOBAL' and r[6]=='UND'})
e=sorted({r[7] for r in rows if len(r)>=8 and r[4]=='GLOBAL' and r[6]!='UND' and r[3]=='FUNC'})
assert set(i)<={'strcmp','memcpy','memset'} and e==['t5_driver_get']
assert 's32c1i' not in (out/'disassembly.txt').read_text().lower()
hash=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
b=(out/'driver.elf').read_bytes();assert b[:7]==b'\x7fELF\x01\x01\x01' and int.from_bytes(b[18:20],'little')==94
r={'source_commit':subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip(),'source_tree':subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD^{tree}'],text=True).strip(),'driver_sha256':hash(source/'minimal/drivers/x4pro_gt911/driver.c'),'version':json.loads((out/'manifest.json').read_text())['version'],'bytes':len(b),'sha256':hash(out/'driver.elf'),'imports':i,'exports':e,'sdk_sha256':{p.name:hash(p) for p in sorted(sdk.glob('*.h'))},'compiler':subprocess.check_output([os.environ['CC'],'--version'],text=True).splitlines()[0],'physical_verification':False}
(out/'qualification.json').write_text(json.dumps(r,indent=2)+'\n');print(r['sha256'],r['bytes'])
PY
