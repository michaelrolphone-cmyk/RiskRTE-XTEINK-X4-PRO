#!/usr/bin/env python3
"""Actual target Clock/client and alarm provider compositions; no product install."""
import argparse,hashlib,json,os,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
BASE='a5150a8042b7dce2468906ecc7ef7fbf5b302451'
PINS={'system':'81f884b8a053cf917054fb1433c7850714cd0c48','runtime':'30dcec5ce6ce33223f2b203a2399283e1f758567','utilities':'637e13b0bce62ad49b756bec2468a6271d163fc7'}
def run(cmd,**kwargs):return subprocess.run(list(map(str,cmd)),check=True,**kwargs)
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('system','runtime','utilities','reader','xtensa_cc','output'):
  p.add_argument('--'+name.replace('_','-'),type=Path,required=True)
 a=p.parse_args()
 for name,ref in PINS.items():
  assert subprocess.check_output(['git','-C',str(getattr(a,name)),'rev-parse','HEAD'],text=True).strip()==ref,name
  assert not subprocess.check_output(['git','-C',str(getattr(a,name)),'status','--porcelain','--untracked-files=no'],text=True).strip(),name
 a.output.mkdir(parents=True,exist_ok=True)
 compiler=subprocess.check_output([a.xtensa_cc,'--version'],text=True).splitlines()[0];assert '8.4.0' in compiler and '2021r2-patch5' in compiler
 receipt={'base':BASE,'inputs':PINS,'compiler':compiler,'source_sha256':sha(ROOT/'minimal/apps/portable_sleep.c'),'compatibility':[],'targets':[],'hardware':False}
 with tempfile.TemporaryDirectory(prefix='x4-tagged-sleep-') as temporary:
  stage=Path(temporary);include=stage/'include';shutil.copytree(a.system/'lib/PortableApps/include',include);shutil.copytree(a.system/'lib/PortableApps/time',stage/'time')
  for folder in (a.runtime/'sdk/app',a.runtime/'sdk/driver'):
   for header in folder.glob('*.h'):shutil.copyfile(header,include/header.name)
  for name in ('RiscDisplayOutputV1','RiscDisplayOutputPowerV1','RiscTouchV1','RiscTouchPowerV1','RiscStorageVolumeV1'):shutil.copyfile(a.reader/'sdk/driver'/(name+'.h'),include/(name+'.h'))
  tagged=stage/'tagged/include';shutil.copytree(include,tagged);shutil.copytree(stage/'time',tagged.parent/'time')
  for header in (a.utilities/'lib/Alarm/include').glob('*.h'):shutil.copyfile(header,tagged/header.name)
  before=stage/'base/portable_sleep.c';before.parent.mkdir();before.write_bytes(subprocess.check_output(['git','-C',ROOT,'show',BASE+':minimal/apps/portable_sleep.c']))
  current=ROOT/'minimal/apps/portable_sleep.c'
  inc=['-iquote',ROOT/'minimal/apps','-I'+str(include),'-I'+str(a.system/'lib/NativeApps/include')]
  tagged_inc=['-iquote',ROOT/'minimal/apps','-I'+str(tagged),'-I'+str(a.system/'lib/NativeApps/include')]
  mapfile=stage/'app.map';mapfile.write_text('{ global: app_main; app_module_init; app_module_fini; local: *; };\n')
  provider_map=stage/'provider.map';provider_map.write_text('{ global: t5_driver_get; local: *; };\n')
  catalog=stage/'catalog.c';catalog.write_text('#include "PortableApps.h"\nconst t5_app_manifest_t portable_catalog[1]={{.compatible=false}};\nconst unsigned portable_catalog_count=0;\n')
  common=['-std=c11','-Os','-Wall','-Wextra','-Werror']
  target=['-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden','-ffreestanding','-fno-builtin']
  link=['-nostdlib','-nostartfiles','-shared','-Wl,--no-relax','-Wl,--hash-style=sysv']
  app_flags=['-DPORTABLE_ALARM_CLIENT','-DPORTABLE_APP_OWNS_TOUCH_CHROME','-DPORTABLE_RTC_WALL_TIME','-DPORTABLE_DISPLAY_ROTATION=90','-DPORTABLE_INPUT_NAVIGATION','-DPORTABLE_APP_SLEEP_LOCAL','-DPORTABLE_CROWN_SLEEP_LOCAL','-DPORTABLE_SLEEP_MANUAL_ONLY']
  validator=a.output/'validate-elf'
  run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-I'+str(a.system/'test/native_apps/stubs'),'-I'+str(a.system/'lib/elf_loader/include'),a.system/'lib/elf_loader/src/esp_elf_validate.c',a.system/'test/native_apps/validate_test.c','-o',validator])
  for profile in ('light','desk','sparse'):
   for quick in (False,True):
    key=profile+('-quick' if quick else '')
    defines=[*app_flags]+(['-DPORTABLE_DESK_CLOCK'] if profile!='light' else [])+(['-DPORTABLE_DESK_CLOCK_SPARSE_START'] if profile=='sparse' else [])+(['-DPORTABLE_QUICK_ACTIONS','-DPORTABLE_QUICK_RADIOS'] if quick else [])
    sources=[a.system/'Apps/paper_clock.c',a.system/'lib/PortableApps/src/adapter.c',catalog]
    if profile!='light':sources.append(a.system/'lib/PortableApps/src/desk_clock_faces.c')
    if quick:sources.extend(a.system/'lib/PortableApps/src'/part for part in ('quick_actions.c','quick_render.c','quick_session.c','quick_radios.c'))
    if profile=='sparse':sources.extend(a.system/'lib/PortableApps/src'/part for part in ('PortableRealtimeClient.c','PortableTimeZone.c','PortableTimeZoneCatalog.c','PortableTimeZonePreference.c'))
    for cc in (os.environ.get('CC','cc'),a.xtensa_cc):
     for label,source in (('base',before),('off',current)):
      for kind,flags in (('i',['-E','-P']),('o',['-c'])):run([cc,*common,*defines,*inc,*flags,source,'-o',stage/(label+'.'+kind)])
     for kind in ('i','o'):assert (stage/('base.'+kind)).read_bytes()==(stage/('off.'+kind)).read_bytes(),(key,str(cc),kind)
     receipt['compatibility'].append({'profile':key,'compiler':str(cc),'object_sha256':sha(stage/'off.o'),'preprocessed_identical':True})
    outputs={}
    for label,source,extra in (('base',before,[]),('off',current,[]),('v2',current,['-DALARM_SERVICE_TAGGED_V2'])):
     elf=a.output/(key+'-'+label+'.elf')
     run([a.xtensa_cc,*common,*target,*link,'-Wl,--version-script='+str(mapfile),*defines,*extra,*(tagged_inc if label=='v2' else inc),*sources,source,'-lgcc','-o',elf]);run([validator,elf]);outputs[label]=elf
    assert outputs['base'].read_bytes()==outputs['off'].read_bytes(),key
    symbols=subprocess.check_output([str(a.xtensa_cc).removesuffix('gcc')+'nm','-D',str(outputs['v2'])],text=True)
    imports={line.split()[-1] for line in symbols.splitlines() if ' U ' in ' '+line}
    exports={line.split()[-1] for line in symbols.splitlines() if len(line.split())>=3 and line.split()[-2] in ('T','D','B','R')}
    assert imports<={'risc_runtime_get_api','memcpy','memset','memcmp','strcmp','strlen','snprintf','strcpy','malloc','free'},imports
    assert exports=={'app_main','app_module_init','app_module_fini'},exports
    receipt['targets'].append({'profile':key,'flag_off_elf_identical':True,'flag_off_sha256':sha(outputs['off']),'v2_sha256':sha(outputs['v2']),'v2_size':outputs['v2'].stat().st_size,'imports':sorted(imports)})
    print(key+': host/target preprocessed and object exact; full flag-off ELF exact; API2 actual Clock ELF validated',flush=True)
  elf=a.output/'alarm-service-v2.elf'
  run([a.xtensa_cc,*common,*target,*link,'-Wl,--version-script='+str(provider_map),'-DALARM_SERVICE_TAGGED_V2','-DALARM_NATIVE_UTC','-DALARM_VISUAL_ONLY','-DALARM_DND_CONTROL','-DPOINTS_IN_TIME_SERVICE',*tagged_inc,a.utilities/'Services/alarm_service/service.c',a.system/'lib/PortableApps/src/PortableTimeZone.c',a.system/'lib/PortableApps/src/PortableTimeZoneCatalog.c','-lgcc','-o',elf]);run([validator,elf])
  receipt['provider']={'sha256':sha(elf),'size':elf.stat().st_size,'api':2,'profile':'native-utc-visual-points'}
 (a.output/'evidence.json').write_text(json.dumps(receipt,indent=2)+'\n')
 print('Qualified provider + actual X4 Clock target compositions PASS')
if __name__=='__main__':main()
