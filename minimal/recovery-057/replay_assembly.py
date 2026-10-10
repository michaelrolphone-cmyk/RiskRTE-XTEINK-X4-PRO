#!/usr/bin/env python3
"""Offline exact .57 assembly replay from bundled source and source-bound inputs.
This uses the authorized post-recovery cache. For compilation use the source
bundles and recorded commands; inherited-055 retains the full cold-build recipe.
"""
import argparse,hashlib,json,pathlib,subprocess,sys,tarfile
D=pathlib.Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--work',type=pathlib.Path,required=True)
p.add_argument('--compiler',type=pathlib.Path,required=True)
p.add_argument('--mkspiffs',type=pathlib.Path,required=True)
a=p.parse_args();work=a.work.resolve()
if work.exists():raise FileExistsError(work)
work.mkdir(parents=True);spec=json.loads((D/'recipe/assembly-spec.json').read_text());catalog=json.loads((D/'source-map.json').read_text())
sha=lambda path:hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
mapping={old:str(D/rel) for old,rel in json.loads((D/'artifact-path-map.json').read_text()).items()}
for name,row in catalog['sources'].items():
 bundle=D/row['bundle'];assert sha(bundle)==row['sha256']
 dest=work/'sources'/name;dest.parent.mkdir(exist_ok=True)
 subprocess.run(['git','clone','--quiet',str(bundle),str(dest)],check=True)
 subprocess.run(['git','-C',str(dest),'checkout','--quiet','--detach',row['commit']],check=True)
 assert subprocess.check_output(['git','-C',str(dest),'rev-parse','HEAD^{tree}'],text=True).strip()==row['tree']
 mapping[row['original_path']]=str(dest)
watch=catalog['watch_snapshot'];archive=D/'inherited-055'/watch['archive'];assert sha(archive)==watch['sha256']
with tarfile.open(archive,'r:gz') as tf:tf.extractall(work/'tools',filter='data')
watch_root=work/'tools/watchsrc'
for name,digest in watch['files'].items():assert sha(watch_root/name)==digest,name
mapping[watch['original_path']]=str(watch_root)
for key,new in [('compiler',a.compiler.resolve()),('mkspiffs',a.mkspiffs.resolve())]:
 old=spec['tools'][key];assert sha(new)==spec['tool_inputs'][old],key+' differs from pinned tool';mapping[old]=str(new)
mapping[spec['native_directory']]=str(D/'inputs/native')
pairs=sorted(mapping.items(),key=lambda row:len(row[0]),reverse=True)
def replace(value):
 if isinstance(value,str):
  for old,new in pairs:
   if value==old:return new
   if value.startswith(old+'/'):return new+value[len(old):]
  return value
 if isinstance(value,list):return [replace(x) for x in value]
 if isinstance(value,dict):return {replace(k):replace(v) for k,v in value.items()}
 return value
spec=replace(spec);path=work/'assembly-spec.json';path.write_text(json.dumps(spec,indent=2)+'\n')
x4=work/'sources/x4';out=work/'image'
subprocess.run([sys.executable,str(x4/'minimal/scripts/package_wifi_ui_057.py'),'--spec',str(path),'--output',str(out),'--compiler',str(a.compiler.resolve()),'--mkspiffs',str(a.mkspiffs.resolve())],check=True)
expected=json.loads((D/'delivery.json').read_text())['image'];actual=out/expected['name'];assert actual.stat().st_size==expected['bytes'] and sha(actual)==expected['sha256']
print('Byte-identical offline .57 assembly replay PASS')
