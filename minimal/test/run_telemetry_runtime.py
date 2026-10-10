#!/usr/bin/env python3
"""Actual Runtime/Graph plus production telemetry chain; radio and gauge are simulated."""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--runtime',type=Path,required=True);p.add_argument('--system-apps',type=Path,required=True);p.add_argument('--utilities',type=Path,required=True);p.add_argument('--drivers',type=Path,required=True);a=p.parse_args();rt=a.runtime.resolve();system=a.system_apps.resolve();utilities=a.utilities.resolve();drivers=a.drivers.resolve();out=ROOT/'build/telemetry-runtime';out.mkdir(parents=True,exist_ok=True);fixture=ROOT/'minimal/test/telemetry'
evidence={'hardware_verified':False,'runtime_source':subprocess.check_output(['git','-C',rt,'rev-parse','HEAD'],text=True).strip(),'drivers_source':subprocess.check_output(['git','-C',drivers,'rev-parse','HEAD'],text=True).strip(),'system_source':subprocess.check_output(['git','-C',system,'rev-parse','HEAD'],text=True).strip(),'utilities_source':subprocess.check_output(['git','-C',utilities,'rev-parse','HEAD'],text=True).strip(),'runs':[]}
for sanitized in [False,True]:
 folder=out/('sanitized' if sanitized else 'normal');folder.mkdir(exist_ok=True)
 sdk=folder/'include';sdk.mkdir(exist_ok=True)
 for source in [system/'lib/PortableApps/include',drivers/'sdk/driver',rt/'sdk/driver',rt/'sdk/hardware',rt/'sdk/app']:
  for header in source.glob('*.h'):shutil.copyfile(header,sdk/header.name)
 includes=['-I'+str(sdk)]
 san=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'] if sanitized else []
 flags=['cc','-std=c11','-g','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',*san,*includes,'-fPIC','-fvisibility=hidden','-shared']
 manifests=[]
 for name,cap,kind in [('gauge','board.battery',0),('hci','bluetooth.hci',1)]:
  subprocess.run([*flags,'-DKIND='+str(kind),'-DID='+json.dumps(name),'-DCAPABILITY='+json.dumps(cap),fixture/'provider.c','-o',folder/(name+'.elf')],check=True)
  manifest={'type':'driver','id':name,'version':'1.0.0','driver_abi':2,'architecture':'xtensa-esp32s3','file_name':name+'.elf','requires':[],'provides':[{'capability':cap,'api':1}]}
  (folder/(name+'.json')).write_text(json.dumps(manifest));manifests.append(name+'.json')
 for name,source in [('battery-telem',drivers/'Drivers/telemetry_battery'),('ble-telem',drivers/'Drivers/ble_telemetry'),('broadcast',utilities/'Services/telemetry_broadcast')]:
  subprocess.run([*flags,source/('service.c' if name=='broadcast' else 'driver.c'),'-o',folder/(name+'.elf')],check=True)
  manifest=json.loads((source/'manifest.json').read_text());manifest['file_name']=name+'.elf';(folder/(name+'.json')).write_text(json.dumps(manifest));manifests.append(name+'.json')
 subprocess.run([*flags,'-DPORTABLE_BLE_BROADCAST_DEFAULT_OFF',fixture/'app.c','-o',folder/'default.elf'],check=True);shutil.copyfile(folder/'default.elf',folder/'child.elf')
 boot={'board':'board.json','default_app':'default.elf','provider_activation':'demand','drivers':[{'manifest':name} for name in manifests],'app_capabilities':[]}
 for name in ['default','child']:
  capabilities=['telemetry.broadcast','storage.key-value']+(['runtime.provider-promotion'] if name=='default' else [])
  manifest={'type':'application','id':name,'version':'1.0.0','architecture':'xtensa-esp32s3','file_name':name+'.elf','entry':'app_main','requires':[{'capability':c,'api':1} for c in capabilities]};(folder/(name+'.json')).write_text(json.dumps(manifest))
  boot['app_capabilities'].append({'manifest':name+'.json','grants':[{'capability':c,'api':1,'instance_id':1 if c=='storage.key-value' else 0} for c in capabilities]})
 (folder/'boot.json').write_text(json.dumps(boot));(folder/'board.json').write_text(json.dumps({'schema':'riscrte.board-hardware','schema_version':1,'board_id':'test','revision':'unspecified','buses':[],'devices':[]}))
 src=['src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp','src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp','src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp']
 subprocess.run(['c++','-std=c++17','-g','-O0','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-Wno-misleading-indentation','-rdynamic',*san,*( ['-no-pie'] if sanitized else []),*includes,'-I'+str(rt/'src'),'-I'+str(rt/'lib/ArduinoJson/src'),'-I'+str(rt/'test/drivers/stubs'),*[rt/f for f in src],fixture/'runtime.cpp','-ldl','-o',folder/'run'],check=True)
 for mode in ['timer','handoff','airplane','retained']:
  result=subprocess.run([folder/'run',folder,mode],capture_output=True,text=True,timeout=30,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'));(folder/(mode+'.log')).write_text(result.stdout+result.stderr)
  if result.returncode:raise RuntimeError(result.stdout+result.stderr)
  evidence['runs'].append({'sanitized':sanitized,'mode':mode,'result':result.stdout.strip()});print(str(sanitized)+' '+result.stdout.strip(),flush=True)
evidence['runtime_files']={str(f):hashlib.sha256((rt/f).read_bytes()).hexdigest() for f in src}
(out/'evidence.json').write_text(json.dumps(evidence,indent=2)+'\n')
