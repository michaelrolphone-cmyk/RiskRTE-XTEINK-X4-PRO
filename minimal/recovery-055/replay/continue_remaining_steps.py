"""Resume remaining source compile commands in the same post-recovery build run."""
import datetime,json,os,pathlib,subprocess,sys,time
P=pathlib.Path;root=P('/workspace/shared/x4-clean-product-build-055-r2');lp=root/'build-ledger.json';ledger=json.loads(lp.read_text());plan=ledger['plan']
sys.path.insert(0,plan['sources']['x4']['path']+'/minimal/scripts');from build_clean_recovery_055 import validate,require,sha,encoded,inside
sources=validate(plan,root);epoch=ledger['epoch_ns'];require(not ledger['completed'],'Already complete')
for name,row in ledger['artifacts'].items():
 p=inside(root,name);require(sha(p)==row['sha256'] and p.stat().st_size==row['bytes'] and p.stat().st_mtime_ns==row['mtime_ns'],'Changed current-run artifact '+name)
if ledger['steps'] and ledger['steps'][-1].get('returncode')!=0:
 failed=ledger['steps'].pop();failed['retained_log']=failed['log']+'.attempt'+str(len(ledger.get('failed_attempts',[]))+1)
 (root/failed['log']).rename(root/failed['retained_log']);ledger.setdefault('failed_attempts',[]).append(failed)
ledger.setdefault('continuation_scripts',[]).append({'sha256':sha(__file__),'path':str(P(__file__).resolve()),'time_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})
def save():lp.write_bytes(encoded(ledger))
save();values=dict(sources,build=str(root),**plan['tools'])
for step in plan['steps'][len(ledger['steps']):]:
 for name in step['outputs']:require(not inside(root,name).exists(),'Output exists before producer '+name)
 argv=[s.format_map(values) for s in step['argv']];cwd=step.get('cwd','{build}').format_map(values)
 env=dict(os.environ,PLATFORMIO_SETTING_ENABLE_TELEMETRY='No',PYTHONDONTWRITEBYTECODE='1');env.update({k:v.format_map(values) for k,v in plan['environment'].items()});env.update({k:v.format_map(values) for k,v in step.get('environment',{}).items()})
 row={'id':step['id'],'argv':argv,'cwd':cwd,'started_ns':time.time_ns(),'log':'logs/'+step['id']+'.log'};ledger['steps'].append(row);save()
 with (root/row['log']).open('wb') as log:r=subprocess.run(argv,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
 row.update(returncode=r.returncode,finished_ns=time.time_ns());save();require(r.returncode==0,'Failed '+step['id'])
 for name in step['outputs']:
  p=inside(root,name);require(p.is_file() and not p.is_symlink(),'Missing '+name);st=p.stat();require((st.st_ctime_ns if p.suffix=='.json' else st.st_mtime_ns)>=epoch,'Predates run '+name)
  ledger['artifacts'][name]={'producer':step['id'],'bytes':st.st_size,'sha256':sha(p),'mtime_ns':st.st_mtime_ns}
 save()
validate(plan,root);ledger.update(completed=True,finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat());save();print('Completed all47 source-built modules in original clean run')
