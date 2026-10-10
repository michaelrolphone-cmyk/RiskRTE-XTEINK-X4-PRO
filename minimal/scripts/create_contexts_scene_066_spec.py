#!/usr/bin/env python3
"""Bind the corrected Contexts app to the delivered .65 store and native image."""
import argparse,hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def record(p):
 p=Path(p).resolve();b=p.read_bytes();return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def source(p):
 p=Path(p).resolve();assert not subprocess.check_output(['git','-C',str(p),'status','--porcelain'],text=True).strip(),str(p)
 return dict(path=str(p),commit=subprocess.check_output(['git','-C',str(p),'rev-parse','HEAD'],text=True).strip(),tree=subprocess.check_output(['git','-C',str(p),'rev-parse','HEAD^{tree}'],text=True).strip())
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('prior-spec','baseline','baseline-custody','system','utilities','app','scene','stage','evidence','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();old=json.loads(a.prior_spec.read_text());sources={n:source(p) for n,p in [('system',a.system),('utilities',a.utilities),('x4',ROOT)]}
 spec={k:old[k] for k in ('runtime','tools','native','native_platform','native_candidate','mkspiffs_sha256','compiler')}
 spec.update(x4_source=sources['x4']['commit'],sources=sources,baseline=record(a.baseline),baseline_custody=record(a.baseline_custody),boot=record(a.stage/'store/boot.json'))
 def package(p,name,version):return dict(elf=record(p/(name+'.elf')),manifest=record(p/('manifest.json' if name=='driver' else name+'.json')),receipt=record(p/'build.json'),version=version)
 spec['apps']={'contexts':package(a.app,'contexts','0.4.0')};spec['providers']={'ui-scene':package(a.scene,'driver','0.3.0')}
 spec['qualifications']=[record(a.evidence/n) for n in ('scene-app-tests.log','service-tests.log','rf-retained-tests.log','scene-host-tests.log','x4-build.log','watch-build.log')]
 closure={}
 for r in sources.values():
  root=Path(r['path'])
  for n in subprocess.check_output(['git','-C',str(root),'ls-files'],text=True).splitlines():
   if n.startswith(('Apps/','Services/','lib/','sdk/','scripts/','test/','tests/','minimal/scripts/')) and (root/n).is_file():closure[str(root/n)]=record(root/n)['sha256']
 spec['compiled_inputs']=closure;a.output.write_text(json.dumps(spec,indent=2)+'\n')
