#!/usr/bin/env python3
"""Build the byte-identical selected 0.1.16 provider against the explicit SDK."""
import argparse,hashlib,json,os,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
for n in ('runtime','reader','output'):p.add_argument('--'+n,type=Path,required=True)
a=p.parse_args();R=a.runtime.resolve();Q=a.reader.resolve();O=a.output.resolve();O.mkdir()
CC=os.environ.get('NATIVE_DRIVER_CC','xtensa-esp32s3-elf-gcc');CC=Path(shutil.which(CC) or CC).resolve()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
commands=[]
def run(cmd):
 cmd=list(map(str,cmd));commands.append(cmd);return subprocess.check_output(cmd,text=True)
selection=json.loads((ROOT/'minimal/panel-016-selection-066.json').read_text())
for name,digest in selection['files'].items():assert sha(ROOT/name)==digest,name
print(run([sys.executable,ROOT/'minimal/scripts/prepare_sdk.py','--runtime',R,'--reader',Q,'--output',O/'sdk']))
# GCC 8.4/binutils aborts in ld on this exact source at O2. O1 keeps the
# selected driver byte-identical and passes relocation/symbol/admission gates.
elf=O/'driver.elf';source=ROOT/'minimal/drivers/x4pro_uc8279_fast/driver.c'
run([CC,'-std=c11','-O1','-fno-ivopts','-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden','-fno-builtin','-nostdlib','-nostartfiles','-shared','-Wall','-Wextra','-Werror','-I'+str(O/'sdk'),'-Wl,--hash-style=sysv','-Wl,--exclude-libs,ALL','-Wl,--no-relax',source,'-lgcc','-o',elf])
for script in ['normalize_xtensa_relocations.py','validate_xtensa_relative_targets.py']:print(run([sys.executable,Q/'scripts'/script,elf]))
prefix=str(CC).removesuffix('gcc')
rows=[r.split() for r in run([prefix+'readelf','--dyn-syms','--wide',elf]).splitlines()]
imports={r[7] for r in rows if len(r)>=8 and r[4]=='GLOBAL' and r[6]=='UND'}
exports={r[7] for r in rows if len(r)>=8 and r[4]=='GLOBAL' and r[6]!='UND' and r[3]=='FUNC'}
assert imports<={'memcpy','memset','strcmp'} and exports=={'t5_driver_get'}
assert 's32c1i' not in run([prefix+'objdump','-d',elf]).lower()
shutil.copyfile(source.with_name('manifest.json'),O/'manifest.json')
inputs=[source,source.with_name('manifest.json'),ROOT/'minimal/scripts/prepare_sdk.py',Path(__file__),Q/'scripts/normalize_xtensa_relocations.py',Q/'scripts/validate_xtensa_relative_targets.py',*sorted((O/'sdk').glob('*.h'))]
receipt=dict(id='x4pro-uc8279-fast',version='0.1.16',elf_sha256=sha(elf),elf_bytes=elf.stat().st_size,source_selection=selection,runtime_commit=run(['git','-C',R,'rev-parse','HEAD']).strip(),source_inputs={str(p):sha(p) for p in inputs},compiler_sha256=sha(CC),compiler_version=run([CC,'--version']),commands=commands,imports=sorted(imports),exports=sorted(exports),hardware_tested=False)
(O/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({k:receipt[k] for k in ('id','version','elf_sha256','elf_bytes')}))
