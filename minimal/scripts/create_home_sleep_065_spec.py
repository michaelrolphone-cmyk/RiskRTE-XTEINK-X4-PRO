#!/usr/bin/env python3
"""Bind .65 components and qualification evidence to the exact delivered .64 image."""
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
previous=json.loads((B/'assembly-spec-064.json').read_text())
sources={n:source(B/f'integration-{n}-064') for n in ('system','utilities','runtime','productivity','drivers')}
sources['native_platform']=source(B/'native-platform-064');sources['x4']=source(ROOT)
apps={}
for name in ('default','springboard','lists'):
 folder=B/'components-065'/('lists/lists' if name=='lists' else 'apps/'+name)
 receipt={'default':'build-evidence.json','springboard':'springboard-build-record.json','lists':'build.json'}[name]
 apps[name]={'elf':record(folder/(name+'.elf')),'manifest':record(folder/(name+'.json')),'receipt':record(folder/receipt),'version':json.loads((folder/(name+'.json')).read_text())['version']}
providers={}
for name,folder,receipt in [('panel',B/'components-065/panel','receipt.json'),('ui-scene',B/'components-065/scene/scene-host','build.json')]:
 providers[name]={'elf':record(folder/'driver.elf'),'manifest':record(folder/'manifest.json'),'receipt':record(folder/receipt),'version':json.loads((folder/'manifest.json').read_text())['version']}
dependencies={}
for row in sources.values():
 root=Path(row['path'])
 for rel in subprocess.check_output(['git','-C',str(root),'ls-files'],text=True).splitlines():
  if rel.startswith(('Apps/','lib/','sdk/','Services/','src/','scripts/','minimal/native/','minimal/drivers/','minimal/interfaces/','minimal/apps/','minimal/scripts/')):
   path=root/rel
   if path.is_file():dependencies[str(path)]=record(path)['sha256']
for path in (B/'components-065').rglob('*'):
 if path.is_file() and path.suffix in ('.h','.c','.inc'):dependencies[str(path)]=record(path)['sha256']
# These two canonical inherited product helpers are named in the app build commands.
for name in ('portable_sleep.c','portable_idle_sleep.c'):
 path=Path('/workspace/shared/x4-bugfix-composition-053/minimal/apps')/name;dependencies[str(path)]=record(path)['sha256']
for name in ('default','springboard'):
 cmd=json.loads((B/f'components-065/apps-{name}-command.json').read_text());assert '--raster-snapshot' in cmd
path=Path('/root/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc');dependencies[str(path)]=record(path)['sha256']
names=['home-raster-065.log','home-transition-065/evidence.json','sleep-idle-065/evidence.json','sleep-policy-065.log','springboard-motion-065/evidence.json','raster-rows-065.log','panel-012-test-065.log','cadence-panel012-065.log','idle-panel012-065.log','lists-tests-065.log','scene-components-065.log','apps-build-065.log','scene-build-065.log','lists-build-065.log','panel-build-065.log']
qualifications=[record(B/name) for name in names]
spec={'x4_source':sources['x4']['commit'],'sources':sources,'runtime':str(B/'integration-runtime-064'),'tools':str(B/'build-sources/watch/watchsrc'),'native':str(B/'native-064'),'native_platform':str(B/'native-platform-064'),'native_candidate':record(B/'native-064/candidate.json'),'baseline':record(B/'firmware-064/X4-0.1.64-Render-Latency-Contexts-Lists-Panel-0.1.13-full-0x0.bin'),'baseline_custody':record(B/'firmware-064/build-custody.json'),'apps':apps,'providers':providers,'compiled_inputs':dependencies,'qualifications':qualifications,'mkspiffs_sha256':previous['mkspiffs_sha256'],'source_public_mapping':json.loads((ROOT/'minimal/home-sleep-065-sources.json').read_text()),'panel_source':json.loads((ROOT/'minimal/panel-012-selection-065.json').read_text())}
(B/'assembly-spec-065.json').write_text(json.dumps(spec,indent=2)+'\n')
print('Pinned .65:',len(dependencies),'compiled inputs;',len(qualifications),'qualifications')
