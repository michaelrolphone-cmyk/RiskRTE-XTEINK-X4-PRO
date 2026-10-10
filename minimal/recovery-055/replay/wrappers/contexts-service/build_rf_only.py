#!/usr/bin/env python3
"""Fresh source-only RF-only Contexts build after the integration recovery gate."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
PIN='918c730a10c6a80b116a6a3aa01e891641eeadd5'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--cc',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();src=a.source.resolve();out=a.output.resolve();sdk=HERE/'sdk'
def run(args,**kw):return subprocess.run(list(map(str,args)),check=True,**kw)
def capture(args,**kw):return subprocess.check_output(list(map(str,args)),text=True,**kw)
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
assert capture(['git','rev-parse','HEAD'],cwd=src).strip()==PIN,'Unqualified source revision'
assert not capture(['git','status','--porcelain','--untracked-files=no'],cwd=src).strip(),'Modified source'
closure=json.loads((HERE/'source-closure.json').read_text())
expected={}
for name,item in closure['sources'].items():
 path=sdk/Path(name).name if name.startswith('sdk/') else src/name
 assert sha(path)==item['sha256'],f'Source hash changed: {name}'
 expected[str(path.resolve())]=item['sha256']
compiler=capture([a.cc,'--version']).splitlines()[0]
assert '8.4.0' in compiler,'Pinned GCC 8.4 required'
assert not out.exists(),'Output must be a fresh, absent directory'
out.mkdir(parents=True)
mapfile=out/'exports.map';mapfile.write_text('{ global: t5_driver_get; local: *; };\n')
elf=out/'driver.elf';deps=out/'driver.d'
command=[a.cc,'-std=c11','-Os','-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden','-ffreestanding','-fno-builtin','-nostdlib','-nostartfiles','-shared','-Wl,--no-relax','-Wl,--hash-style=sysv','-Wall','-Wextra','-Werror','-DCONTEXTS_RF_ONLY=1','-MMD','-MF',deps,*['-I'+str(v) for v in [src/'Apps',src/'lib/Contexts/include',sdk]],'-Wl,--version-script='+str(mapfile),src/'Services/contexts/service.c','-lgcc','-o',elf]
run(command)
actual={str(Path(v).resolve()):sha(v) for v in deps.read_text().replace('\\\n',' ').split(':',1)[1].split()}
assert actual==expected,{'unexpected':list(actual.keys()-expected.keys()),'missing':list(expected.keys()-actual.keys())}
prefix=str(a.cc).removesuffix('gcc')
symbols=capture([prefix+'nm','-D',elf])
imports={s.split()[-1] for s in symbols.splitlines() if ' U ' in ' '+s}
exports={s.split()[-1] for s in symbols.splitlines() if len(s.split())>=3 and s.split()[-2] in ('T','D','B','R')}
assert imports=={'memcpy','memset','memcmp','memchr','strcmp','strlen'} and exports=={'t5_driver_get'}
validator=out/'validate-elf'
run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-I'+str(src/'test/native_apps/stubs'),'-I'+str(src/'lib/elf_loader/include'),src/'lib/elf_loader/src/esp_elf_validate.c',src/'test/native_apps/validate_test.c','-o',validator])
run([validator,elf])
raw=elf.read_bytes();assert raw[:7]==b'\x7fELF\x01\x01\x01' and raw[16:20]==b'\x03\x00\x5e\x00'
manifest=src/'Services/contexts/rf-only-manifest.json';(out/'manifest.json').write_bytes(manifest.read_bytes())
record={'schema':1,'source':PIN,'source_dirty':False,'profile':'rf-only','source_mask':2,'audio_available':False,'version':'0.1.2','requires':json.loads(manifest.read_text())['requires'],'compiler':compiler,'command':list(map(str,command)),'compiled_dependencies':actual,'manifest_sha256':sha(manifest),'elf':{'sha256':sha(elf),'size':elf.stat().st_size},'imports':sorted(imports),'exports':sorted(exports),'hardware_verified':False,'saved_product_binaries_used':False,'private_historical_revision_recovered':False,'comparison_053_elf_sha256':'6701c8b8a5bb16065147ee880e77e42a3d0fd0b57e127092ce6e40717b3c19f8'}
record['byte_identical_to_installed_053']=record['elf']['sha256']==record['comparison_053_elf_sha256']
(out/'build-evidence.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record['elf']));print('fresh RF-only source build and native loader validation PASS')
