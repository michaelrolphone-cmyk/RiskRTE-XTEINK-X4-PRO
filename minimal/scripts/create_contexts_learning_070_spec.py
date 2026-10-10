#!/usr/bin/env python3
"""Bind Contexts learning 0.4.1 and its providers to the qualified X4 .69 image."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--workspace',type=Path,required=True);p.add_argument('--baseline-workspace',type=Path,required=True);a=p.parse_args();B=a.workspace.resolve();prior=a.baseline_workspace.resolve()
def record(path):
 path=Path(path).resolve();raw=path.read_bytes();return {'path':str(path),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
def source(path):
 path=Path(path).resolve()
 def git(*args):return subprocess.check_output(['git','-C',str(path),*args],text=True).strip()
 assert not git('status','--porcelain','--untracked-files=no'),str(path)
 return {'path':str(path),'commit':git('rev-parse','HEAD'),'tree':git('rev-parse','HEAD^{tree}')}
spec=json.loads((prior/'assembly-spec-069.json').read_text())
spec['sources']={k:v for k,v in spec['sources'].items() if k in ('runtime','native_platform')}
spec['sources'].update(system=source(B/'system'),utilities=source(B/'utilities'),x4=source(ROOT),drivers=source('/workspace/scratch/fa8487f96167/RiscRTE-Drivers'))
spec['x4_source']=spec['sources']['x4']['commit']
spec['baseline']=record(prior/'firmware-069/X4-0.1.69-Home-Settling-Panel-0.1.16-full-0x0.bin')
spec['baseline_custody']=record(prior/'firmware-069/build-custody.json')
def component(folder,elf,manifest,receipt):
 return {'elf':record(folder/elf),'manifest':record(folder/manifest),'receipt':record(folder/receipt),'version':json.loads((folder/manifest).read_text())['version']}
spec['apps']={'contexts':component(B/'build/release-contexts-x4/contexts','contexts.elf','contexts.json','build.json')}
spec['providers']={'contexts':component(B/'utilities/dist/contexts-service-rf-only','driver.elf','manifest.json','build-evidence.json'),'ui-scene':component(B/'build/release-scene/scene-host','driver.elf','manifest.json','build.json')}
spec['compiled_inputs']={}
for entry in spec['sources'].values():
 root=Path(entry['path'])
 for rel in subprocess.check_output(['git','-C',str(root),'ls-files'],text=True).splitlines():
  path=root/rel
  if path.is_file() and rel.startswith(('Apps/','lib/','sdk/','Services/','src/','scripts/','Drivers/','minimal/scripts/')):spec['compiled_inputs'][str(path)]=record(path)['sha256']
spec['compiled_inputs'][spec['compiler']]=record(spec['compiler'])['sha256']
spec['qualifications']=[record(B/name) for name in ('build-app-release.log','build-learning-verified.log','build-component-final.log','build-host-final.log','build-scene-release.log','build-x4-release.log','build-rf-release.log')]
spec['source_public_mapping']={'status':'exact source commits; repository publication verified separately','sources':spec['sources']}
(B/'assembly-spec-070.json').write_text(json.dumps(spec,indent=2)+'\n')
print('Pinned Contexts learning .70:',len(spec['compiled_inputs']),'inputs')
