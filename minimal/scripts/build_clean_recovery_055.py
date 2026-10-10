#!/usr/bin/env python3
"""Execute a frozen source-only recovery plan into a new output tree.

This runner does not flash, publish or accept previous product outputs. Final
assembly consumes its hash-recorded build ledger in a separate checked step.
"""
import argparse,datetime,hashlib,json,os,subprocess,time
from pathlib import Path

def require(ok,message):
    if not ok:raise ValueError(message)
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def encoded(value):return (json.dumps(value,sort_keys=True,indent=2)+'\n').encode()
def git(path,*args):return subprocess.check_output(['git','-C',str(path),*args],text=True).strip()
def inside(root,name):
    p=Path(name)
    require(not p.is_absolute() and '..' not in p.parts and str(p)==name,'Unsafe output path: '+name)
    result=root/p
    require(result.resolve().is_relative_to(root),'Output escaped build root')
    return result

def validate(plan,root):
    require(plan.get('schema')=='x4.clean-recovery-build' and plan.get('schema_version')==1,'Unknown plan')
    require(plan.get('source_recovery_complete') is True,'Source recovery is not frozen')
    require(plan.get('product_version')=='0.1.55','Wrong product reservation')
    require(len(plan['modules'])==47,'Expected all47 modules')
    expected={x['store_elf'] for x in plan['modules']}
    require(len(expected)==47 and sum('/' in x for x in expected)==26,'Expected21 apps and26 providers')
    sources={}
    for name,row in plan['sources'].items():
        p=Path(row['path']).resolve(strict=True)
        require(not root.is_relative_to(p) and not p.is_relative_to(root),'Source/output overlap')
        require(git(p,'rev-parse','HEAD')==row['commit'],'Source commit changed: '+name)
        require(git(p,'rev-parse','HEAD^{tree}')==row['tree'],'Source tree changed: '+name)
        require(not git(p,'status','--porcelain','--untracked-files=no'),'Modified tracked source: '+name)
        sources[name]=str(p)
    for name,row in plan.get('snapshots',{}).items():
        p=Path(row['path']).resolve(strict=True)
        require(not root.is_relative_to(p) and not p.is_relative_to(root),'Snapshot/output overlap')
        require(row['files'],'Empty snapshot input inventory')
        for relative,digest in row['files'].items():
            f=inside(p,relative)
            require(f.suffix.lower() not in ('.elf','.o','.bin','.a','.so'),'Product binary listed as source: '+relative)
            require(f.is_file() and not f.is_symlink() and sha(f)==digest,'Snapshot source changed: '+name+'/'+relative)
        sources[name]=str(p)
    for path,digest in plan.get('source_files',{}).items():
        p=Path(path).resolve(strict=True)
        require(p.suffix.lower() not in ('.elf','.o','.bin','.a','.so'),'Product binary cannot be a source input: '+path)
        require(sha(p)==digest,'Source/recipe/SDK changed: '+path)
    producers={};ids=set()
    for step in plan['steps']:
        require(step['id'] not in ids,'Duplicate build step');ids.add(step['id'])
        require(isinstance(step['argv'],list) and step['argv'] and all(isinstance(x,str) for x in step['argv']),'Invalid command')
        for output in step['outputs']:
            inside(root,output);require(output not in producers,'Multiple producers: '+output);producers[output]=step['id']
    for module in plan['modules']:
        for name in ('elf','manifest'):
            require(producers.get(module[name])==module['producer'],'Module lacks compile producer: '+module['store_elf'])
        require(module['elf'].endswith('.elf') and module['manifest'].endswith('.json'),'Invalid module output types')
    for output in plan['native'].values():require(output in producers,'Native output lacks fresh producer: '+output)
    return sources

def build(plan_path,output):
    plan_path=Path(plan_path).resolve(strict=True);plan=json.loads(plan_path.read_text());root=Path(output).absolute()
    require(not root.exists() and not root.is_symlink(),'Build root must not exist')
    root=root.resolve();sources=validate(plan,root)
    epoch=time.time_ns();stamp=datetime.datetime.now(datetime.timezone.utc).isoformat()
    root.mkdir(parents=True);(root/'logs').mkdir();(root/'plan.json').write_bytes(encoded(plan))
    values=dict(sources,build=str(root),**plan.get('tools',{}))
    ledger={'schema':'x4.clean-recovery-ledger','schema_version':1,'product_version':'0.1.55','started_utc':stamp,'epoch_ns':epoch,'build_root':str(root),'plan_sha256':sha(root/'plan.json'),'input_plan_sha256':sha(plan_path),'plan':plan,'steps':[],'artifacts':{},'completed':False,'hardware_tested':False}
    def save():(root/'build-ledger.json').write_bytes(encoded(ledger))
    save()
    for step in plan['steps']:
        for name in step['outputs']:require(not inside(root,name).exists(),'Output already exists before producer: '+name)
        argv=[arg.format_map(values) for arg in step['argv']]
        cwd=step.get('cwd','{build}').format_map(values)
        env=dict(os.environ,PLATFORMIO_SETTING_ENABLE_TELEMETRY='No',PYTHONDONTWRITEBYTECODE='1')
        env.update({k:v.format_map(values) for k,v in plan.get('environment',{}).items()})
        env.update({k:v.format_map(values) for k,v in step.get('environment',{}).items()})
        start=time.time_ns();row={'id':step['id'],'argv':argv,'cwd':cwd,'started_ns':start,'log':'logs/'+step['id']+'.log'}
        ledger['steps'].append(row);save()
        with (root/row['log']).open('wb') as log:
            result=subprocess.run(argv,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
        row.update(returncode=result.returncode,finished_ns=time.time_ns());save()
        require(result.returncode==0,'Build step failed: '+step['id']+'; see '+row['log'])
        for name in step['outputs']:
            path=inside(root,name);require(path.is_file() and not path.is_symlink(),'Missing or symlink output: '+name)
            require(path.stat().st_mtime_ns>=epoch,'Output predates clean build: '+name)
            ledger['artifacts'][name]={'producer':step['id'],'bytes':path.stat().st_size,'sha256':sha(path),'mtime_ns':path.stat().st_mtime_ns}
        save()
    # A source changed mid-build invalidates every result, even if commands passed.
    validate(plan,root)
    ledger.update(completed=True,finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat());save()
    print(json.dumps({'ledger':str(root/'build-ledger.json'),'artifacts':len(ledger['artifacts']),'modules':47}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--plan',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();build(a.plan,a.output)
