#!/usr/bin/env python3
"""Rebuild X4 launchers from pinned component sources and product catalogue."""
import argparse,hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
for n in ('system','runtime','utilities','display-sdk','output'):p.add_argument('--'+n,type=Path,required=True)
a=p.parse_args();values={k:str(v.resolve()) for k,v in vars(a).items()};values['product']=str(ROOT)
for name,version,receipt in [('default','0.4.6','build-evidence.json'),('springboard','1.7.31','springboard-build-record.json')]:
 cmd=[x.format(**values) for x in json.loads((ROOT/'minimal'/('canvas-'+name+'-command.json')).read_text())]
 subprocess.run(cmd,check=True)
 folder=a.output/name;m=folder/(name+'.json');v=json.loads(m.read_text());v['version']=version;m.write_text(json.dumps(v,indent=2)+'\n')
 r=folder/receipt;v=json.loads(r.read_text());v.update(version=version,product_catalog_sha256=hashlib.sha256((ROOT/'minimal/canvas-catalog.json').read_bytes()).hexdigest(),composition_command=cmd,version_reason='Recompiled installed-app catalogue includes Model Viewer and Hollow Trail')
 r.write_text(json.dumps(v,indent=2)+'\n')
