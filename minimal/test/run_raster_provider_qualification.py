#!/usr/bin/env python3
"""Isolated host-only snapshot/Runtime/provider qualification and complete source custody.

No hardware, raster CPU speed, product composition or deployment claim is made.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT=Path(__file__).resolve().parents[2]
def git(root,*args):
    return subprocess.check_output(['git','-C',str(root),*args],text=True).strip()
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def inventory(root):
    root=root.resolve()
    files=git(root,'ls-files','-z').split('\0')
    hashes={name:digest(root/name) for name in files if name and (root/name).is_file()}
    return {'root':str(root),'commit':git(root,'rev-parse','HEAD'),'tree':git(root,'rev-parse','HEAD^{tree}'),
        'status':git(root,'status','--porcelain'),'tracked_sha256':hashes}
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ['runtime','reader','system','output']:
        ap.add_argument('--'+name,type=Path,required=True)
    args=ap.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    roots={'x4_fixture':ROOT,'runtime':args.runtime,'reader':args.reader,'system':args.system}
    before={name:inventory(root) for name,root in roots.items()}
    wrapper=Path(os.environ['CC']).resolve()
    receipt={'host_only':True,'hardware_tested':False,'raster_cpu_time_modeled':False,
        'started_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'sources':before,
        'cc':{'path':str(wrapper),'sha256':digest(wrapper),'contents':wrapper.read_text(),
        'native_version':subprocess.check_output(['cc','--version'],text=True)},'jobs':[]}
    (out/'sources-before.json').write_text(json.dumps(receipt,indent=2)+'\n')
    jobs=[]
    common=['--runtime',str(args.runtime.resolve()),'--reader',str(args.reader.resolve()),'--system',str(args.system.resolve())]
    for sanitize in [False,True]:
        flavor='asan-ubsan' if sanitize else 'normal'
        san=['--sanitize'] if sanitize else []
        for ms in [0,17,2300]:
            name=f'ssd-{ms}-{flavor}'
            jobs.append((name,['python3',str(ROOT/'minimal/test/run_panel_cadence_test.py'),*common,'--ssd','--refresh-ms',str(ms),*san,'--output',str(out/name)]))
        name=f'uc-normal-17-{flavor}'
        jobs.append((name,['python3',str(ROOT/'minimal/test/run_panel_cadence_test.py'),*common,'--refresh-ms','17',*san,'--output',str(out/name)]))
        for transitions in [False,True]:
            name=f'uc-fast13-{"transitions" if transitions else "default"}-{flavor}'
            jobs.append((name,['python3',str(ROOT/'minimal/test/run_uc8279_fast_cadence_test.py'),*common,*(['--paper-transitions'] if transitions else []),*san,'--output',str(out/name)]))
    def execute(job):
        name,command=job;start=time.monotonic();log=out/(name+'.log')
        with log.open('w') as f:
            result=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT)
        entry={'name':name,'command':command,'returncode':result.returncode,'seconds':round(time.monotonic()-start,3),'log':str(log),'log_sha256':digest(log)}
        evidence=out/name/'evidence.json'
        if evidence.exists():
            entry.update(evidence=str(evidence),evidence_sha256=digest(evidence))
        print(json.dumps(entry),flush=True)
        return entry
    with ThreadPoolExecutor(max_workers=2) as pool:
        receipt['jobs']=list(pool.map(execute,jobs))
    after={name:inventory(root) for name,root in roots.items()}
    receipt['sources_unchanged']=before==after
    receipt['finished_utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
    receipt['passed']=all(job['returncode']==0 for job in receipt['jobs']) and receipt['sources_unchanged']
    (out/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    assert receipt['passed'],'Qualification failed; see per-job logs and qualification.json'
if __name__=='__main__':main()
