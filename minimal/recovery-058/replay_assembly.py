#!/usr/bin/env python3
"""Replay .58 using this delta plus the extracted delivered .57 source package."""
import argparse,hashlib,json,os,subprocess,sys,tarfile
from pathlib import Path
D=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline-source',type=Path,required=True)
p.add_argument('--work',type=Path,required=True)
p.add_argument('--compiler',type=Path,required=True)
p.add_argument('--mkspiffs',type=Path,required=True)
a=p.parse_args();base=a.baseline_source.resolve();work=a.work.resolve()
assert not work.exists(),'Work directory must be absent';work.mkdir(parents=True)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
spec=json.load(open(D/'recipe/assembly-spec.json'));catalog=json.load(open(D/'source-map.json'));mapping={}
for name,row in catalog['sources'].items():
 bundle=(base if row['from_baseline'] else D)/row['bundle'];assert sha(bundle)==row['sha256'],bundle
 dest=work/'sources'/name;dest.parent.mkdir(exist_ok=True)
 subprocess.run(['git','clone','--quiet',str(bundle),str(dest)],check=True)
 subprocess.run(['git','-C',str(dest),'checkout','--quiet','--detach',row['commit']],check=True)
 assert subprocess.check_output(['git','-C',str(dest),'rev-parse','HEAD^{tree}'],text=True).strip()==row['tree']
 mapping[row['path']]=str(dest)
watch=catalog['watch_snapshot'];archive=base/'inherited-055'/watch['archive'];assert sha(archive)==watch['sha256']
with tarfile.open(archive,'r:gz') as tf:tf.extractall(work/'tools',filter='data')
watchroot=work/'tools/watchsrc'
for n,h in watch['files'].items():assert sha(watchroot/n)==h,n
for old,row in json.load(open(D/'path-map.json')).items():
 if 'source' in row:continue
 if 'watch_snapshot' in row:new=watchroot
 elif 'baseline' in row:new=base/row['baseline']
 elif 'delta' in row:new=D/row['delta']
 elif 'tool' in row:new=getattr(a,row['tool']).resolve();assert sha(new)==spec['tool_inputs'][old],old
 else:raise ValueError(row)
 mapping[old]=str(new)
pairs=sorted(mapping.items(),key=lambda x:len(x[0]),reverse=True)
def replace(x):
 if isinstance(x,str):
  for old,new in pairs:
   if x==old:return new
   if x.startswith(old+'/'):return new+x[len(old):]
  return x
 if isinstance(x,list):return [replace(v) for v in x]
 if isinstance(x,dict):return {replace(k):replace(v) for k,v in x.items()}
 return x
spec=replace(spec);f=work/'assembly-spec.json';f.write_text(json.dumps(spec,indent=2)+'\n')
# Host compiler intermediates use ordinary disk, not a small /tmp tmpfs.
tmp=work/'tmp';tmp.mkdir();env=dict(os.environ,TMPDIR=str(tmp),PLATFORMIO_SETTING_ENABLE_TELEMETRY='No',NATIVE_DRIVER_CC=str(a.compiler.resolve()),X4_XTENSA_OBJDUMP=str(a.compiler.resolve()).removesuffix('gcc')+'objdump')
subprocess.run([sys.executable,str(work/'sources/x4/minimal/scripts/package_latest_058.py'),'--spec',str(f),'--compiler',str(a.compiler.resolve()),'--mkspiffs',str(a.mkspiffs.resolve()),'--output',str(work/'image')],check=True,env=env)
expected=json.load(open(D/'delivery.json'))['image'];actual=work/'image'/expected['name']
assert actual.stat().st_size==expected['bytes'] and sha(actual)==expected['sha256']
(work/'replay-verification.json').write_text(json.dumps({'passed':True,'image':expected,'scope':'offline delta + exact delivered .57 source package'},indent=2)+'\n')
print('Byte-identical .58 offline source-package replay PASS')
