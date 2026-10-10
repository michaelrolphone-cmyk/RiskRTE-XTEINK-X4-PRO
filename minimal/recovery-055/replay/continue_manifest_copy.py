"""One-run continuation after the audited source-JSON copy timestamp false positive."""
import datetime,hashlib,json,os,pathlib,subprocess,sys,time
P=pathlib.Path;root=P('/workspace/shared/x4-clean-product-build-055-r2');lp=root/'build-ledger.json';ledger=json.loads(lp.read_text());plan=ledger['plan']
sys.path.insert(0,plan['sources']['x4']['path']+'/minimal/scripts');from build_clean_recovery_055 import validate,require,sha,encoded,inside
sources=validate(plan,root);epoch=ledger['epoch_ns'];require(not ledger['completed'],'Already completed')
require(ledger['steps'][-1]['id']=='msc-provider' and ledger['steps'][-1]['returncode']==0,'Unexpected interrupted step')
for name,row in ledger['artifacts'].items():
 p=inside(root,name);require(sha(p)==row['sha256'] and p.stat().st_size==row['bytes'] and p.stat().st_mtime_ns==row['mtime_ns'],'Changed current-run artifact '+name)
manifest=inside(root,'providers/msc/manifest.json');source=P(plan['sources']['reader']['path'])/'Drivers/usb_device_msc_esp32s3/manifest.json'
require(manifest.read_bytes()==source.read_bytes(),'Source manifest differs')
require(manifest.stat().st_ctime_ns>=epoch,'Manifest destination was not created during this run')
ledger['artifacts']['providers/msc/manifest.json']={'producer':'msc-provider','bytes':manifest.stat().st_size,'sha256':sha(manifest),'mtime_ns':manifest.stat().st_mtime_ns,'source_json_copy':{'path':str(source),'sha256':sha(source),'destination_ctime_ns':manifest.stat().st_ctime_ns}}
ledger['continuation']={'reason':'Canonical MSC builder copy2 retains JSON source mtime; destination creation and exact pinned source bytes verified. All executable artifacts were already freshly compiled in this same run.','runner_fix_commit':'fd43f2c87c3eeab1abcc861ea74a71c690978834','script_sha256':sha(__file__),'script_path':str(P(__file__).resolve()),'time_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'compiled_production_sources_changed':False,'prior_product_artifacts_imported':False}
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
