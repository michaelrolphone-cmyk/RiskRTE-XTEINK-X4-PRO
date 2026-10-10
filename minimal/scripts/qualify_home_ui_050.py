#!/usr/bin/env python3
"""Exercise all final ELFs through selected production loader host hooks."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('runtime','store','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
original=(a.runtime/'test/run_resident_object_export_test.sh').read_text()
# Keep its original malformed-object tests, while also loading every provider
# and the sole legacy app through uncached/cache-miss/cache-hit paths.
script=original.replace('repo="$(cd "$(dirname "$0")/.." && pwd)"','repo="${RUNTIME_ROOT:?}"')
script=script.replace('files=[]','files=[];others=[]').replace("store.glob('*.elf')","store.rglob('*.elf')")
old="  if d and d.get_symbol_by_name('risc_resident_app_descriptor_v1'):files.append(str(p))"
assert old in script
script=script.replace(old,old+"\n  else:others.append(str(p))")
script=script.replace("assert files","assert len(files)==20 and len(others)==24\n(out/'others.txt').write_text('\\n'.join(others)+'\\n')")
script+='\nmapfile -t others < "$build/others.txt"\n"$build/test" 0 providers-and-legacy "${others[@]}"\n'
runner=a.output/'run_complete_loader.sh';runner.write_text(script)
rows=[]
for sanitize in ('0','1'):
 log=a.output/('loader-san.log' if sanitize=='1' else 'loader-normal.log')
 env=dict(os.environ,RUNTIME_ROOT=str(a.runtime),RESIDENT_STORE=str(a.store),SANITIZE=sanitize,ASAN_OPTIONS='detect_leaks=0')
 with log.open('w') as f:subprocess.run(['bash',str(runner)],stdout=f,stderr=subprocess.STDOUT,env=env,check=True)
 text=log.read_text();rows.append({'sanitize':sanitize=='1','pass_lines':text.count(' PASS'),'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
require=lambda ok:None if ok else (_ for _ in ()).throw(ValueError('Missing final ELF coverage'))
for row in rows:require(row['pass_lines']==183)
report={'target_instructions_executed':False,'architecture_relocation_and_RTOS':'host hooks; production reader, validator, relocator dispatch, dlfcn and image cache run','final_ELFs':44,'final_elf_paths_per_mode':{'uncached':44,'cache-miss':44,'cache-hit':44},'additional_malformed_and_valid_object_cases_per_run':51,'production_runner_sha256':hashlib.sha256(original.encode()).hexdigest(),'adapted_runner_sha256':hashlib.sha256(script.encode()).hexdigest(),'runs':rows}
(a.output/'loader-qualification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
