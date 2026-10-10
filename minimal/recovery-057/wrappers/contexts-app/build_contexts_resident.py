#!/usr/bin/env python3
"""Build Contexts 0.1.8 from recovered app sources and selected shared adapter with the frozen shared System."""
import argparse,hashlib,json,subprocess
from pathlib import Path
import resident_client_build as resident
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__);resident.options(p)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--runtime-revision',default='274bc66f193cbe29018d2a85c9400cb0dce8aacc')
p.add_argument('--check-only',action='store_true')
a=p.parse_args();root=a.source.resolve();baseline=json.loads((HERE/'baseline-017.json').read_text())
assert resident.sha(HERE/'resident_client_build.py')==baseline['build_helper_sha256'],'Changed recovered helper'
resident.exact(root,'918c730a10c6a80b116a6a3aa01e891641eeadd5')
for name,want in baseline['compiled_dependencies_sha256'].items():
 if name.startswith('Source/'):
  assert resident.sha(root/name.removeprefix('Source/'))==want,'Changed original app source: '+name
if a.output.exists():raise ValueError('Output must be fresh and absent')
# Runtime pin is a build dependency assertion/receipt, not generated app code.
resident.RUNTIME=a.runtime_revision
c=resident.prepare(a,p,root)
defines=baseline['build_defines'];sources=[root/'Apps/contexts.c'];grants=baseline['required_grants']
if a.check_only:
 includes=[c['inc'],c['system']/'lib/NativeApps/include',root/'lib/NativeApps/include',root/'lib/Bluetooth/include',root/'lib/Contexts/include',root/'Apps',c['system']/'Apps']
 all_sources=sources+[c['system']/'lib/PortableApps/src/adapter.c']+[c['system']/'lib/PortableApps/src'/n for n in resident.TIME]
 deps={}
 for src in all_sources:
  resident.run([c['cc'],'-std=c11','-fsyntax-only','-Wall','-Wextra','-Werror',*defines,*['-I'+str(v) for v in includes],src])
  text=subprocess.check_output([c['cc'],'-std=c11','-MM',*defines,*['-I'+str(v) for v in includes],src],text=True).replace('\\\n',' ')
  for token in text.split(':',1)[1].split():deps[str(Path(token).resolve())]=resident.sha(token)
 resident.write(c['out']/'source-only-recipe.json',dict(source=resident.git(root,'rev-parse','HEAD'),historical_app_source=baseline['source_revision'],historical_app_owned_sources_exact=True,version='0.1.8',system=resident.git(c['system'],'rev-parse','HEAD'),runtime=resident.RUNTIME,build_defines=defines,compiled_source_units=list(map(str,all_sources)),dependencies=deps,required_grants=grants,syntax_checks='passed',production_objects_built=False))
 print('Contexts 0.1.8 exact application sources, frozen shared ABI and syntax PASS; no production objects built')
else:
 record=resident.build(c,root,'contexts','0.1.8',defines,sources,grants,baseline['features'])
 record['recovery']={'historical_private_revision':baseline['source_revision'],'private_commit_recovered':False,'all_three_app_owned_compiled_inputs_byte_exact':True,'historical_helper_byte_exact':True,'shared_system_selected_by_integrator':True}
 resident.write(c['out']/'contexts/x4-native-app.json',record)
