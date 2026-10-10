#!/usr/bin/env python3
"""Bind original panel 0.1.12 and recovered boot animation to exact Contexts .71."""
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
folder=B/'components-072/apps/default'
apps={'default':{'elf':record(folder/'default.elf'),'manifest':record(folder/'default.json'),'receipt':record(folder/'build-evidence.json'),'version':'0.4.7'}}
app_receipt=json.loads((folder/'build-evidence.json').read_text());assert not app_receipt['working_tree_dirty'] and app_receipt['repository_commit']==sources['system']['commit']
folder=B/'components-072/panel'
providers={'panel':{'elf':record(folder/'driver.elf'),'manifest':record(folder/'manifest.json'),'receipt':record(folder/'receipt.json'),'version':'0.1.12'}}
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
for path in (B/'components-072').rglob('*'):
 if path.is_file() and path.suffix in ('.h','.c','.inc'):dependencies[str(path)]=record(path)['sha256']
for name in ('portable_sleep.c','portable_idle_sleep.c'):
 path=Path('/workspace/shared/x4-bugfix-composition-053/minimal/apps')/name;dependencies[str(path)]=record(path)['sha256']
for path,digest in app_receipt['boot_animation']['relocation_tools'].items():
 assert record(path)['sha256']==digest;dependencies[path]=digest
path=B/'components-072/apps-default-command.json';dependencies[str(path)]=record(path)['sha256']
path=Path('/root/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc');dependencies[str(path)]=record(path)['sha256']
compiler=str(path)
for path in (Path(compiler.removesuffix('gcc')+'objcopy'),B/'build-sources/watch/watchsrc/scripts/compact_current_elf.py'):
 dependencies[str(path)]=record(path)['sha256']
names=['panel-012-072.log','panel-012-072-san.log','idle-panel012-072.log','profile-panel012-072.log','panel-build-072.log','cadence-panel012-072/evidence.json','cadence-panel012-072-san/evidence.json','boot-animation-072/evidence.json','boot-startup-072/evidence.json','apps-build-072.log','home-transition-072-springboard/evidence.json']
qualifications=[record(B/name) for name in names]
spec={'x4_source':sources['x4']['commit'],'sources':sources,'runtime':str(B/'integration-runtime-064'),'tools':str(B/'build-sources/watch/watchsrc'),'native':str(B/'native-064'),'native_platform':str(B/'native-platform-064'),'native_candidate':record(B/'native-064/candidate.json'),'baseline':record(B/'firmware-071/X4-0.1.71-Panel-0.1.18-full-0x0.bin'),'baseline_custody':record(B/'firmware-071/build-custody.json'),'apps':apps,'providers':providers,'compiled_inputs':dependencies,'qualifications':qualifications,'mkspiffs_sha256':previous['mkspiffs_sha256'],'source_public_mapping':json.loads((ROOT/'minimal/boot-animation-072-sources.json').read_text()),'panel_source':json.loads((ROOT/'minimal/panel-012-selection-065.json').read_text())}
spec['compiler']=compiler
(B/'assembly-spec-072.json').write_text(json.dumps(spec,indent=2)+'\n')
print('Pinned .72:',len(dependencies),'compiled inputs;',len(qualifications),'qualifications')
