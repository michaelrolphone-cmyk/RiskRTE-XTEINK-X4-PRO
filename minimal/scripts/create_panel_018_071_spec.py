#!/usr/bin/env python3
"""Bind the exact panel 0.1.18 experiment to the qualified .70 image."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--workspace',type=Path,required=True);a=p.parse_args();B=a.workspace.resolve()
def record(p):
 p=Path(p).resolve();b=p.read_bytes();return {'path':str(p),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def source(p):
 p=Path(p).resolve()
 def git(arg):return subprocess.check_output(['git','-C',str(p),'rev-parse',arg],text=True).strip()
 assert not subprocess.check_output(['git','-C',str(p),'status','--porcelain'],text=True).strip(),str(p)
 return {'path':str(p),'commit':git('HEAD'),'tree':git('HEAD^{tree}')}
previous=json.loads((B/'assembly-spec-065.json').read_text())
sources={n:source(B/f'integration-{n}-064') for n in ('system','utilities','runtime','productivity','drivers')}
sources['native_platform']=source(B/'native-platform-064');sources['x4']=source(ROOT)
apps={}
folder=B/'components-071/panel'
providers={'panel':{'elf':record(folder/'driver.elf'),'manifest':record(folder/'manifest.json'),'receipt':record(folder/'receipt.json'),'version':'0.1.18'}}
dependencies={}
for path,digest in json.loads((folder/'receipt.json').read_text())['source_inputs'].items():
 assert record(path)['sha256']==digest,'Panel build input changed: '+path
 dependencies[path]=digest
for row in sources.values():
 root=Path(row['path'])
 for rel in subprocess.check_output(['git','-C',str(root),'ls-files'],text=True).splitlines():
  if rel.startswith(('Apps/','lib/','sdk/','Services/','src/','scripts/','minimal/native/','minimal/drivers/','minimal/interfaces/','minimal/apps/','minimal/scripts/','minimal/test/')):
   path=root/rel
   if path.is_file():dependencies[str(path)]=record(path)['sha256']
for path in (B/'components-071').rglob('*'):
 if path.is_file() and path.suffix in ('.h','.c','.inc'):dependencies[str(path)]=record(path)['sha256']
path=Path('/root/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc');dependencies[str(path)]=record(path)['sha256']
compiler=str(path)
for path in (Path(compiler.removesuffix('gcc')+'objcopy'),B/'build-sources/watch/watchsrc/scripts/compact_current_elf.py'):
 dependencies[str(path)]=record(path)['sha256']
names=['panel-tests-070/evidence.json','cadence-panel018-070/evidence.json','cadence-panel018-070-san/evidence.json','idle-panel018-070.log','profile-panel018-070.log','panel-build-071.log','panel-runner-071.log','panel-runner-071-san.log']
qualifications=[record(B/name) for name in names]
spec={'x4_source':sources['x4']['commit'],'sources':sources,'runtime':str(B/'integration-runtime-064'),'tools':str(B/'build-sources/watch/watchsrc'),'native':str(B/'native-064'),'native_platform':str(B/'native-platform-064'),'native_candidate':record(B/'native-064/candidate.json'),'baseline':record(B/'baseline-contexts-070/X4-0.1.70-Contexts-Learning-Progress-full-0x0.bin'),'baseline_custody':record(B/'baseline-contexts-070/build-custody.json'),'apps':apps,'providers':providers,'compiled_inputs':dependencies,'qualifications':qualifications,'mkspiffs_sha256':previous['mkspiffs_sha256'],'source_public_mapping':json.loads((ROOT/'minimal/panel-018-071-sources.json').read_text()),'panel_source':json.loads((ROOT/'minimal/panel-018-selection-071.json').read_text())}
spec['compiler']=compiler
(B/'assembly-spec-071.json').write_text(json.dumps(spec,indent=2)+'\n')
print('Pinned .71:',len(dependencies),'compiled inputs;',len(qualifications),'qualifications')
