#!/usr/bin/env python3
"""Build ordinary X4 provider ELFs against explicit shared source checkouts."""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROVIDERS = ('board_power', 'i2c', 'panel', 'frontlight', 'buttons', 'rtc', 'sd', 'battery', 'gt911')
IMPORTS = {'strcmp', 'memcpy', 'memmove', 'memset', 'memcmp', 'strlen', 'strchr'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime', type=Path, required=True)
    p.add_argument('--reader', type=Path, required=True)
    p.add_argument('--cc', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--source-lock', type=Path, default=ROOT/'minimal/sources.lock.json',
                   help='Explicit exact shared-source lock; defaults to the selected product lock')
    p.add_argument('--sleep', action='store_true', help='Build explicit power and split-navigation packages')
    p.add_argument('--source-fixture', action='store_true', help='Allow non-Git host fixture sources; never production packaging')
    p.add_argument('--panel-driver', choices=['fallback', 'uc8279-fast'], default='fallback')
    args = p.parse_args()
    runtime, reader, out = args.runtime.resolve(), args.reader.resolve(), args.output.resolve()
    lock_bytes = args.source_lock.read_bytes()
    lock = json.loads(lock_bytes)
    origin = {'source_fixture': args.source_fixture, 'sources': {},
              'source_lock': {'path': str(args.source_lock.resolve()),
                              'sha256': hashlib.sha256(lock_bytes).hexdigest()}}
    for name, checkout in (('runtime', runtime), ('shared', reader)):
        try:
            revision = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True, stderr=subprocess.DEVNULL).strip()
            dirty = bool(subprocess.check_output(['git', '-C', str(checkout), 'status', '--porcelain', '--untracked-files=no'], text=True))
        except subprocess.CalledProcessError:
            revision, dirty = None, True
        if not args.source_fixture and (revision != lock[name]['commit'] or dirty):
            raise SystemExit(f'{name}: clean locked source checkout required')
        origin['sources'][name] = {'commit': revision, 'dirty': dirty, 'expected': lock[name]['commit']}
    out.mkdir(parents=True, exist_ok=True)
    (out/'build-origin.json').write_text(json.dumps(origin, indent=2)+'\n')
    subprocess.run([sys.executable, str(ROOT/'minimal/scripts/prepare_sdk.py'), '--runtime', str(runtime), '--reader', str(reader), '--output', str(out/'sdk')], check=True)
    tools = args.cc.removesuffix('gcc')
    products = []
    providers = tuple('uc8279_fast' if name == 'panel' and args.panel_driver == 'uc8279-fast' else name for name in PROVIDERS)
    for name in providers + (('power', 'power_buttons') if args.sleep else ()):
        source = ROOT/'minimal/drivers'/('x4pro_'+name)
        manifest = json.loads((source/'manifest.json').read_text())
        target = out/manifest['id']; target.mkdir(exist_ok=True)
        elf = target/'driver.elf'
        command = [args.cc, '-std=c11', '-O2', '-fno-ivopts', '-fPIC', '-mtext-section-literals', '-mlongcalls', '-fvisibility=hidden', '-fno-builtin', '-nostdlib', '-nostartfiles', '-shared', '-Wall', '-Wextra', '-Werror', '-I'+str(out/'sdk'), '-I'+str(reader/'Drivers/common'), '-I'+str(reader/'Drivers/storage_fatfs'), '-Wl,--hash-style=sysv', '-Wl,--exclude-libs,ALL', '-Wl,--no-relax', str(source/'driver.c')]
        if name == 'sd':
            command += [str(reader/'Drivers/storage_fatfs/fatfs/ff.c'), str(reader/'Drivers/storage_fatfs/fatfs/ffunicode.c')]
        subprocess.run(command+['-lgcc', '-o', str(elf)], check=True)
        for script in ('normalize_xtensa_relocations.py', 'validate_xtensa_relative_targets.py'):
            subprocess.run([sys.executable, str(reader/'scripts'/script), str(elf)], check=True)
        rows = [x.split() for x in subprocess.check_output([tools+'readelf', '--dyn-syms', '--wide', str(elf)], text=True).splitlines()]
        imports = {r[7] for r in rows if len(r)>=8 and r[4]=='GLOBAL' and r[6]=='UND'}
        exports = {r[7] for r in rows if len(r)>=8 and r[4]=='GLOBAL' and r[6]!='UND' and r[3]=='FUNC'}
        if not imports <= IMPORTS or exports != {'t5_driver_get'}:
            raise SystemExit(f'{name}: invalid symbol boundary {imports=} {exports=}')
        if 's32c1i' in subprocess.check_output([tools+'objdump', '-d', str(elf)], text=True).lower():
            raise SystemExit(f'{name}: provider-BSS compare-and-set is forbidden')
        raw = elf.read_bytes()
        (target/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
        products.append({'id': manifest['id'], 'version': manifest['version'], 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(), 'imports': sorted(imports), 'source_sha256': hashlib.sha256((source/'driver.c').read_bytes()).hexdigest(), 'local_source_hashes': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in ([source/'driver.c'] + ([source/'BootLog.h'] if name=='sd' else []) + ([ROOT/'minimal/interfaces/RiscDisplayOutputMetricsV1.h'] if name in ('panel','uc8279_fast') else []) + ([ROOT/'minimal/interfaces/RiscDisplayOutputSnapshotV1.h'] if name=='uc8279_fast' else []) + ([ROOT/'minimal/drivers/x4pro_buttons/driver.c'] if name=='power_buttons' else []) + ([ROOT/'minimal/drivers/x4pro_power/X4PowerV1.h'] if name in ('power','buttons','power_buttons') else []) + ([ROOT/'minimal/drivers/x4pro_power/X4PowerDeepV1.h'] if name=='power' else []) + ([ROOT/'minimal/drivers/x4pro_board_power/PowerReadyV1.h', ROOT/'minimal/drivers/x4pro_board_power/X4BoardKeepaliveV1.h'] if name in ('power','board_power') else []))}})
    (out/'products.json').write_text(json.dumps(products, indent=2)+'\n')
    print(json.dumps(products, indent=2))


if __name__ == '__main__':
    main()
