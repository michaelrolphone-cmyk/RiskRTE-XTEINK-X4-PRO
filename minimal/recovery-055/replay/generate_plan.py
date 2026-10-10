import hashlib,json,pathlib,subprocess,sys
P=pathlib.Path
BASE=P('/workspace/shared'); X=BASE/'x4-recovered-test-composition-055'
def sha(p):return hashlib.sha256(P(p).read_bytes()).hexdigest()
def git(p,*args):return subprocess.check_output(['git','-C',str(p),*args],text=True).strip()
paths={'x4':X,'runtime':BASE/'recovered-runtime-0106','system':P('/tmp/system-ui-state-decoupling-20261010'),'utilities':P('/tmp/utilities-ui-state-decoupling-20261010'),'reader':BASE/'usb-msc015-complete-source','drivers':P('/tmp/radio-iq-source-recovery'),'hid':P('/tmp/radio-hid-source-recovery'),'updates':BASE/'update-firmware-015-source-recovery','gameboy':BASE/'gameboy-original-ui-reconstruction','points':BASE/'points-home-0613/source/points','text':BASE/'system-text-home-reason-012','contexts':BASE/'contexts-service-git-recovery','tinyusb':BASE/'x4-usb-tinyusb-pinned','littlefs':BASE/'x4-baseline-recovery/littlefs-source','pointsutils':BASE/'points-home-0613/source/baseline-utilities'}
sources={n:{'path':str(p),'commit':git(p,'rev-parse','HEAD'),'tree':git(p,'rev-parse','HEAD^{tree}')} for n,p in paths.items()}
for n,p in paths.items():assert not git(p,'status','--porcelain','--untracked-files=no'),n
python='/workspace/scratch/c744abbbbd60/watch-build-tools/python/bin/python';core='/workspace/scratch/c744abbbbd60/watch-build-tools/platformio-core';cc=core+'/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc'
watch=BASE/'x4-home-packaging-tools-050'; source_files={}; snapshots={}
def files(root,filter=lambda p:True):
 root=P(root);r={}
 for p in sorted(root.rglob('*')):
  rel=p.relative_to(root)
  if any(x in ('.git','.pio','dist','__pycache__','node_modules') for x in rel.parts):continue
  if p.is_file() and not p.is_symlink() and p.suffix.lower() not in ('.elf','.bin','.o','.a','.so','.pyc') and filter(p):r[str(rel)]=sha(p)
 return r
def snapshot(n,p,filter=lambda p:True):snapshots[n]={'path':str(p),'files':files(p,filter)}
snapshot('watchsrc',watch)
snapshot('system047',BASE/'system-fast-ui-047')
snapshot('gamesdk',BASE/'gameboy-reconstruction-sdk')
snapshot('sleepsdk',BASE/'x4-baseline-recovery/053/evidence/sleep/target-sdk')
snapshot('pointssdk',BASE/'points-source-recovery/051/source-validation/targets/points/sdk')
snapshot('oldappsdk',BASE/'x4-fast-utilities-build-047/sdk/include')
snapshot('alarmsource',BASE/'alarm-service-048-source-recovery')
snapshot('contextsrecipe',BASE/'contexts-service-qualification')
snapshot('contextsapprecipe',BASE/'contexts-app-017-recovery')
for p in [BASE/'x4-baseline-recovery/make-esptool-fallback-config.py',BASE/'points-home-0613/proof/version-reservation.json',BASE/'gameboy-baseline-recovery/baseline-047-build-custody.json',BASE/'recovered-app-union-final/commands.json',BASE/'recovered-app-union-final/source-freeze.json']:
 source_files[str(p)]=sha(p)
# JSON profile only: no historic ELF is consumed by the Points recipe.
for p in (BASE/'points-source-recovery/051/source-validation/targets/points/points_in_time').glob('*.json'):source_files[str(p)]=sha(p)
for p in [X/'minimal/recovery-055/board.json',X/'minimal/recovery-055/boot.json',X/'minimal/apps/catalog.json']:source_files[str(p)]=sha(p)
cmds=json.loads((BASE/'recovered-app-union-final/commands.json').read_text())
source_files.update(cmds['external_sources'])
steps=[];mods=[]
def step(id,argv,outputs=(),cwd=None):
 row={'id':id,'argv':argv,'outputs':list(outputs)}
 if cwd:row['cwd']=cwd
 steps.append(row)
def module(store,elf,manifest,producer,identity,version):mods.append({'id':identity,'version':version,'elf':elf,'manifest':manifest,'producer':producer,'store_elf':store,'store_manifest':store[:-4]+'.json' if '/' not in store else store.rsplit('/',1)[0]+'/manifest.json'})
prep=['{python}','{x4}/minimal/scripts/prepare_native_runtime.py']
step('native-prepare',prep+['prepare','--runtime','{runtime}','--runtime-commit',sources['runtime']['commit'],'--output','{build}/native-source','--environment','esp32s3-16mb-appdata-iq-stage','--app-policy-rows','18','--app-requirement-rows','17','--app-image-cache','--boot-flash-dio','--usb-phy','--retained-wake-bytes','512','--failure-evidence'],['native-source/platformio.ini'])
step('native-tool-config',['{python}',str(BASE/'x4-baseline-recovery/make-esptool-fallback-config.py'),'{build}/native-source/platformio.ini','{build}/native-platformio.ini'],['native-platformio.ini'])
nd='native-source/.pio/build/esp32s3-16mb-appdata-iq-stage/'
step('native-compile',['{python}','-m','platformio','run','--project-dir','{build}/native-source','--project-conf','{build}/native-platformio.ini','--environment','esp32s3-16mb-appdata-iq-stage','--jobs','1'],[nd+n for n in ('firmware.elf','firmware.bin','bootloader.bin','partitions.bin')])
step('appdata-compile-format',['{python}','{runtime}/scripts/app_data_image.py','--littlefs-source','{littlefs}','--output','{build}/appdata'],['appdata/appdata.bin','appdata/appdata-image.json'])
native={n:'native/'+n for n in ('firmware.bin','firmware.elf','bootloader.bin','partitions.bin','appdata.bin','candidate.json')}
step('native-stage',prep+['stage','--runtime','{runtime}','--workspace','{build}/native-source','--output','{build}/native','--appdata','{build}/appdata'],native.values())
step('canonical-sdk',['{python}','{x4}/minimal/scripts/prepare_sdk.py','--runtime','{runtime}','--reader','{reader}','--output','{build}/sdk'],['sdk/source-hashes.json'])
# 10 selected X4 hardware providers, plus an unselected fallback compiled by the same builder.
xmap={'board_power':'board','i2c':'i2c','uc8279_fast':'panel','frontlight':'light','power_buttons':'buttons','rtc':'rtc','sd':'sd','battery':'battery','gt911':'touch','power':'power'}
for folder,store in xmap.items():
 m=json.loads((X/'minimal/drivers'/('x4pro_'+folder)/'manifest.json').read_text());pre='providers/x4/'+m['id'];module(store+'/driver.elf',pre+'/driver.elf',pre+'/manifest.json','x4-providers',m['id'],m['version'])
step('x4-providers',['{python}','{x4}/minimal/scripts/build_drivers.py','--runtime','{runtime}','--reader','{reader}','--cc','{cc}','--output','{build}/providers/x4','--sleep','--panel-driver','uc8279-fast'])
# Shared source families: ids/manifests come from source, not an old store.
shared={'hid':paths['hid']/'Drivers/ble_hid/manifest.json','iq':paths['drivers']/'Drivers/s3_radio_iq_v1/manifest.json','ble':watch/'drivers/twatch_ble/manifest.json','wifi':watch/'drivers/twatch_wifi/manifest.json','sensors':paths['drivers']/'Drivers/ble_sensors/manifest.json','ble-telem':paths['drivers']/'Drivers/ble_telemetry/manifest.json','battery-telem':paths['drivers']/'Drivers/telemetry_battery/manifest.json','broadcast':paths['utilities']/'Services/telemetry_broadcast/manifest.json'}
for key,p in shared.items():
 m=json.loads(p.read_text());pre='providers/shared/'+key;module(key+'/driver.elf',pre+'/driver.elf',pre+'/manifest.json','shared-providers',m['id'],m['version'])
step('shared-providers',['{python}','{x4}/minimal/scripts/build_clean_shared_providers_055.py','--watch','{watchsrc}','--drivers','{drivers}','--hid','{hid}','--system','{system}','--utilities','{utilities}','--cc','{cc}','--output','{build}/providers/shared'])
# Remaining source-only service recipes.
step('msc-provider',['{python}','{reader}/scripts/build_usb_device_msc.py','--tinyusb','{tinyusb}','--framework',core+'/packages/framework-arduinoespressif32','--cc','{cc}','--output','{build}/providers/msc'])
step('alarm-provider',['{python}','{alarmsource}/recovery/build_native_utc.py','--cc','{cc}','--output','{build}/providers/alarm'])
step('contexts-provider',['{python}','{contextsrecipe}/build_rf_only.py','--source','{contexts}','--cc','{cc}','--output','{build}/providers/contexts'])
step('update-providers',['{python}','{updates}/scripts/build_portable_updates.py','--services-only','--product','x4','--source-routes','--output-dir','{build}/providers/updates'])
step('scene-providers',['{python}','{system}/scripts/build_scene_services.py','--runtime','{runtime}','--output','{build}/providers/scene'])
step('text-provider',['{python}','{text}/scripts/build_text_home_service.py','--system','{system}','--runtime','{runtime}','--output','{build}/providers/text'])
# Read only historical JSON identity when build recipe generates its manifest dynamically.
old=BASE/'x4-baseline-recovery/054/composition-output/store'
for store,sub,producer,v in [('usb-msc','msc','msc-provider','0.1.5'),('alarm','alarm','alarm-provider','0.4.8'),('contexts','contexts','contexts-provider','0.1.2'),('upd-fw','updates/software-update-firmware','update-providers','0.1.5'),('upd-app','updates/software-update-apps','update-providers','0.1.4'),('ui-scene','scene/scene-host','scene-providers','0.1.4'),('ui-profile','scene/portrait-monochrome','scene-providers','0.1.1'),('ui-text','text/text-input-host','text-provider','0.1.2')]:
 m=json.loads((old/store/'manifest.json').read_text());pre='providers/'+sub;module(store+'/driver.elf',pre+'/driver.elf',pre+'/manifest.json',producer,m['id'],v)
# Frozen owner-generated 17-app commands, with new output paths only.
appversions={'default':'0.3.28','springboard':'1.7.23','settings':'1.3.24','file_browser':'1.5.17','wifi_settings':'1.1.20','ota_update':'1.2.10','app_store':'1.2.10','usb_sd_transfer':'0.1.6','alarms':'0.2.16','battery':'1.1.14','calculator':'0.1.21','stopwatch':'0.1.21','countdown':'0.1.20','ble_scanner':'0.2.20','ble_touchpad':'0.1.19','ble_buttons':'0.1.20','waterfall':'0.2.19'}
for group,argv in cmds['commands'].items():
 argv=[a.replace('/tmp/recovered-17app-final-source-plan','{build}/apps') for a in argv];argv[0]='{python}'
 # Bind the same display/settled ABI headers generated from recovered canonical source.
 # Owner-qualified historical SDK is retained as pure header input for exact app flags.
 step('apps-'+group,argv)
for name,v in appversions.items():
 if name in ('ota_update','app_store'):group='updates';pre='apps/updates/'+name
 elif name in ('alarms','battery','calculator','stopwatch','countdown','ble_scanner','ble_touchpad','ble_buttons','waterfall'):group='utilities';pre='apps/utilities/'+name
 else:group=name;pre='apps/'+name
 m=json.loads((old/(name+'.json')).read_text());module(name+'.elf',pre+'/'+name+'.elf',pre+'/'+name+'.json','apps-'+group,m['id'],v)
step('points-app',['{python}','{points}/scripts/build_x4_home_points.py','--common-system','--baseline-target',str(BASE/'points-source-recovery/051/source-validation/targets/points/points_in_time'),'--baseline-sdk','{pointssdk}','--system','{system}','--runtime','{runtime}','--utilities','{pointsutils}','--output','{build}/apps/points','--reservation',str(BASE/'points-home-0613/proof/version-reservation.json')])
step('timecard-app',['{python}','{points}/scripts/build_x4_recovered_timecard.py','--baseline-custody',str(BASE/'gameboy-baseline-recovery/baseline-047-build-custody.json'),'--display-sdk','{pointssdk}/include','--system','{system}','--runtime','{runtime}','--utilities','{pointsutils}','--output','{build}/apps/timecard'])
step('gameboy-app',['{python}','{gameboy}/capability/build.py','--runtime','{runtime}','--system','{system047}','--sdk','{gamesdk}','--cc','{cc}','--output','{build}/apps/gameboy'])
step('contexts-app',['{python}','{contextsapprecipe}/build_contexts_resident.py','--resident-shell-client','--source','{contexts}','--system-apps','{system}','--system-revision',sources['system']['commit'],'--runtime','{runtime}','--display-sdk','{oldappsdk}','--output','{build}/apps/contexts'])
for name,sub,producer,v in [('points_in_time','points/points_in_time','points-app','0.6.13'),('timecard','timecard/timecard','timecard-app','0.2.11'),('gameboy','gameboy','gameboy-app','1.3.24'),('contexts','contexts/contexts','contexts-app','0.1.7')]:
 m=json.loads((old/(name+'.json')).read_text());pre='apps/'+sub;module(name+'.elf',pre+'/'+name+'.elf',pre+'/'+name+'.json',producer,m['id'],v)
for s in steps:
 for m in mods:
  if m['producer']==s['id']:s['outputs'] += [m['elf'],m['manifest']]
assert len(mods)==47 and len({m['store_elf'] for m in mods})==47
plan={'schema':'x4.clean-recovery-build','schema_version':1,'product_version':'0.1.55','source_recovery_complete':True,'sources':sources,'snapshots':snapshots,'source_files':source_files,'tools':{'python':python,'cc':cc,'watch':str(watch)},'environment':{'NATIVE_APP_CC':'{cc}','PLATFORMIO_CORE_DIR':core,'PATH':str(P(python).parent)+':'+str(P(cc).parent)+':/usr/local/bin:/usr/bin:/bin'},'configuration':{n:str(X/'minimal/recovery-055'/n) for n in ('board.json','boot.json')},'steps':steps,'modules':mods,'native':native,'existing_product_inputs':[],'compiler_provenance':{'compiler_version':subprocess.check_output([cc,'--version'],text=True).splitlines()[0],'compiler_sha256':sha(cc),'platform':'espressif32@6.13.0','arduino':'3.20017.241212+sha.dcc1105b','esptool':'official PyPI4.11.0 explicit package configuration'},'scope':'Source-recovered/reconstructed full clean build. Historical JSON used only for identity/profile/source-hash investigation; no saved product ELF, BIN or object is input.'}
output=BASE/'x4-clean-build-055-plan/executable-plan.json';output.write_text(json.dumps(plan,sort_keys=True,indent=2)+'\n');print(output,len(mods),'modules',len(steps),'steps',len(json.dumps(plan)),'bytes')
sys.path.insert(0,str(X/'minimal/scripts'));import build_clean_recovery_055 as b;b.validate(plan,P('/workspace/shared/x4-clean-product-build-055').resolve());print('validated')
