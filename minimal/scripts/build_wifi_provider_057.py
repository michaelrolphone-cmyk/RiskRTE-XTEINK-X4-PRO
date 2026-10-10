#!/usr/bin/env python3
"""Fresh target build of the public compatible async Wi-Fi provider0.2.1."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('watch','cc','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();out=a.output.resolve();assert not out.exists();out.mkdir(parents=True)
root=a.watch.resolve();source=root/'drivers/twatch_wifi';cc=str(a.cc.resolve());prefix=cc.removesuffix('gcc')
compiler=subprocess.check_output([cc,'--version'],text=True).splitlines()[0];assert '8.4.0' in compiler and '2021r2-patch5' in compiler
meta=json.loads((source/'manifest.json').read_text());assert meta['id']=='wifi' and meta['version']=='0.2.1'
mapping=out/'exports.map';mapping.write_text('{ global: t5_driver_get; local: *; };\n')
dep=out/'driver.d';elf=out/'driver.elf'
cmd=[cc,'-std=c11','-shared','-fPIC','-fvisibility=hidden','-nostdlib','-mlongcalls','-Os','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Wno-misleading-indentation','-Wno-unused-function','-Werror','-I'+str(root/'sdk/driver'),'-I'+str(root/'include'),'-Wl,--version-script='+str(mapping),'-Wl,-soname,driver.elf','-MMD','-MF',str(dep),str(source/'driver.c'),'-lgcc','-o',str(elf)]
subprocess.run(cmd,check=True)
rows=[r.split() for r in subprocess.check_output([prefix+'readelf','--dyn-syms','--wide',str(elf)],text=True).splitlines()]
imports={r[7] for r in rows if len(r)>=8 and r[4]=='GLOBAL' and r[6]=='UND'};exports={r[7] for r in rows if len(r)>=8 and r[4]=='GLOBAL' and r[6]!='UND' and r[3]=='FUNC'}
assert imports<={'memcpy','memset','strcmp','strlen'} and exports=={'t5_driver_get'},(imports,exports)
data=elf.read_bytes();assert data[:7]==b'\x7fELF\x01\x01\x01' and data[16:20]==b'\x03\x00\x5e\x00'
deps={}
for name in dep.read_text().replace('\\\n',' ').split(':',1)[1].split():
 path=Path(name).resolve();assert path.suffix not in ('.o','.a','.elf','.bin');deps[str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
(out/'manifest.json').write_bytes((source/'manifest.json').read_bytes())
record={'id':'wifi','version':'0.2.1','compiler':compiler,'commands':[cmd],'source_dependencies':deps,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'imports':sorted(imports),'exports':sorted(exports),'source_public_commit':'a304597bf26491483bb0a567569f0ca0267cdf9c','source_local_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'prior_product_inputs':[],'hardware_tested':False}
(out/'build.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps({k:record[k] for k in ('version','bytes','sha256','imports','exports')}))
