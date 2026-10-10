#!/usr/bin/env python3
"""Build recovered alarm-service 0.4.8 only after the product source freeze.

The upstream historical builder is unchanged. This focused recipe uses its
native-UTC flags and exact verified source inputs without impersonating its
historical Runtime Git checkout. Product admission remains the integrator's job.
"""
import argparse, hashlib, json, os, shutil, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cc', default=os.environ.get('NATIVE_APP_CC') or shutil.which('xtensa-esp32s3-elf-gcc'))
    a = p.parse_args()
    if not a.cc: p.error('Set NATIVE_APP_CC or --cc to the pinned GCC8.4 toolchain')
    public = json.loads((ROOT/'recovery-remote-blobs.json').read_text())
    for item in public['files']:
        raw = (ROOT/item['path']).read_bytes()
        assert hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest() == item['sha'], item['path']
    external = json.loads((ROOT/'external-input-custody.json').read_text())
    for path, item in external['files'].items():
        assert sha(ROOT/path) == item['sha256'], path
    compiler = subprocess.check_output([a.cc, '--version'], text=True).splitlines()[0]
    if '8.4.0' not in compiler: raise ValueError('Pinned GCC8.4 required')
    out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    mapping = out/'exports.map'; mapping.write_text('{ global: t5_driver_get; local: *; };\n')
    flags = ['-std=c11','-Os','-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden',
        '-ffreestanding','-fno-builtin','-nostdlib','-nostartfiles','-shared','-Wl,--no-relax',
        '-Wl,--hash-style=sysv','-Wall','-Wextra','-Werror','-Wl,--version-script='+str(mapping)]
    defines = ['-DPOINTS_IN_TIME_SERVICE','-DALARM_DND_CONTROL','-DALARM_NATIVE_UTC',
        '-DALARM_VISUAL_ONLY','-DALARM_SERVICE_TAGGED_V2','-DPOINTS_CATALOG_SERVICE']
    includes = [ROOT/'lib/Alarm/include',ROOT/'external/runtime/sdk/driver',ROOT/'external/runtime/sdk/app',
        ROOT/'external/system/lib/PortableApps/include']
    sources = [ROOT/'Services/alarm_service/service.c',ROOT/'external/system/lib/PortableApps/src/PortableTimeZone.c',
        ROOT/'external/system/lib/PortableApps/src/PortableTimeZoneCatalog.c']
    elf = out/'driver.elf'
    command = [a.cc,*flags,*defines,*['-I'+str(i) for i in includes],*map(str,sources),'-lgcc','-o',str(elf)]
    subprocess.run(command,check=True)
    symbols = subprocess.check_output([a.cc.removesuffix('gcc')+'nm','-D',str(elf)],text=True)
    imports = {s.split()[-1] for s in symbols.splitlines() if ' U ' in ' '+s}
    exports = {s.split()[-1] for s in symbols.splitlines() if len(s.split())>=3 and s.split()[-2] in ('T','D','B','R')}
    allowed = {'memcpy','memset','memcmp','strcmp','strlen','malloc','free','memmove'}
    assert imports <= allowed and exports == {'t5_driver_get'}, (imports,exports)
    manifest = ROOT/'Services/alarm_service/catalog-native-utc-manifest.json'
    assert json.loads(manifest.read_text())['version'] == '0.4.8'
    shutil.copyfile(manifest,out/'manifest.json')
    (out/'build-evidence.json').write_text(json.dumps({'source_commit':public['commit'],'compiler':compiler,
        'command':command,'elf_sha256':sha(elf),'elf_bytes':elf.stat().st_size,'imports':sorted(imports),
        'exports':sorted(exports),'allowed_imports':sorted(allowed),'hardware_verified':False,
        'product_admission':'pending integrator validation','precompiled_product_inputs':False,
        'source_custody':public,'external_custody':external},indent=2)+'\n')
    print(json.dumps({'elf':str(elf),'sha256':sha(elf),'bytes':elf.stat().st_size}))
if __name__ == '__main__': main()
