#!/usr/bin/env python3
"""Real Runtime/Graph admission with host provider bodies at the X4 mapping."""
import argparse,json,os,subprocess,sys,shutil
from pathlib import Path
r=Path(__file__).resolve().parents[2];sys.path.insert(0,str(r/'minimal/scripts'))
from generate_profile import profile
from build_test_bundle import app_grants
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--runtime',type=Path,required=True);a=p.parse_args();rt=a.runtime.resolve()
base=r/'build/idle-grant-runtime';base.mkdir(parents=True,exist_ok=True)
rows=[('board-ready','board.power.ready',1,1),('i2c','i2c.bus',1,2),('frontlight','display.frontlight',1,5),('buttons','input.navigation',1,6),('battery','board.battery',1,7),('rtc','rtc.clock',2,8),('power','x4.power',1,17),('display','display.output',1,3),('touch','input.touch.raw',1,4),('sd','storage.volume',1,9),('wifi','net.wifi',1,15),('bt','bluetooth.hci',1,16),('alarm','alarm.service',2,0)]
for san in (False,True):
 out=base/('sanitized' if san else 'normal');out.mkdir(exist_ok=True)
 extra=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'] if san else []
 board=profile('uc8279',True,'uc8279-fast')
 for id,model in ((15,'esp32s3-wifi'),(16,'esp32s3-ble')):board['devices'].append({'instance_id':id,'chip':{'vendor':'espressif','model':model,'revision':'unspecified'},'compatible':'espressif,'+model,'config_type':'radio.integrated','config_version':1,'config':{'unit':0,'features':1}})
 (out/'board.json').write_text(json.dumps(board));devices={d['instance_id']:d for d in board['devices']}
 drivers=[];requirements=[]
 flags=['-std=c11','-g','-Wall','-Wextra','-Werror','-shared','-fPIC','-fvisibility=hidden',*extra,'-I'+str(rt/'sdk/app'),'-I'+str(rt/'sdk/driver'),'-I'+str(rt/'sdk/hardware')]
 for name,cap,api,id in rows:
  manifest={'type':'driver','id':name,'version':'1.0.0','driver_abi':2,'architecture':'xtensa-esp32s3','file_name':name+'.elf','requires':[{'capability':'hardware.device','api':1}] if id else [],'provides':[{'capability':cap,'api':api}]}
  if id:
   d=devices[id];manifest['requires'] += [{'capability':c,'api':next(a for _,n,a,_ in rows if n==c)} for c in d.get('bindings',{})]
   manifest['hardware_compatibility']=[{'compatible':d['compatible'],'revisions':['unspecified'],'config_type':d['config_type'],'config_version':d['config_version']}]
  (out/(name+'.json')).write_text(json.dumps(manifest));drivers.append({'manifest':name+'.json',**({'instance_id':id} if id else {})});
  if cap in ('x4.power','display.output','input.touch.raw','storage.volume','net.wifi','bluetooth.hci','alarm.service'):requirements.append({'capability':cap,'api':api})
  subprocess.run(['cc',*flags,'-DTEST_ID="'+name+'"','-DTEST_CAP="'+cap+'"','-DTEST_API='+str(api),'-DTEST_INSTANCE='+str(id),str(r/'minimal/test/idle_grant_provider.c'),'-o',str(out/(name+'.elf'))],check=True)
 requirements.append({'capability':'storage.key-value','api':1})
 app={'type':'application','id':'calculator','version':'1.0.0','architecture':'xtensa-esp32s3','entry':'app_main','file_name':'default.elf','requires':requirements}
 (out/'app.json').write_text(json.dumps(app));grants=app_grants('calculator',requirements,sleep=True,idle_policy=True)
 boot={'board':'board.json','default_app':'default.elf','provider_activation':'demand','drivers':drivers,'app_capabilities':[{'manifest':'app.json','grants':grants}]}
 (out/'boot.json').write_text(json.dumps(boot))
 subprocess.run(['cc',*flags,str(r/'minimal/test/idle_grant_app.c'),'-o',str(out/'default.elf')],check=True)
 sources=['src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp','src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp']
 exe=out/'test'
 subprocess.run(['c++','-std=c++17','-g','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-Wno-misleading-indentation','-rdynamic',*extra,*(['-no-pie'] if san else []),*['-I'+str(rt/n) for n in ('src','sdk/app','sdk/driver','sdk/hardware','lib/ArduinoJson/src','test/drivers/stubs')],*[str(rt/n) for n in sources],str(r/'minimal/test/idle_grant_runtime.cpp'),'-ldl','-o',str(exe)],check=True)
 env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0');subprocess.run([exe,out],env=env,check=True)
 for mode in ('wrong-power','wrong-display','duplicate','missing-power'):
  bad=json.loads(json.dumps(boot));g=bad['app_capabilities'][0]['grants']
  if mode=='wrong-power':next(x for x in g if x['capability']=='x4.power')['instance_id']=18
  elif mode=='wrong-display':next(x for x in g if x['capability']=='display.output')['instance_id']=4
  elif mode=='duplicate':g.append(g[0])
  else:g[:]=[x for x in g if x['capability']!='x4.power']
  (out/'boot.json').write_text(json.dumps(bad));subprocess.run([exe,out,'reject'],env=env,check=True)
 (out/'boot.json').write_text(json.dumps(boot))
