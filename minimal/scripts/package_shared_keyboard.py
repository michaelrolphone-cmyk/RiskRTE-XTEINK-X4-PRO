#!/usr/bin/env python3
"""Full X4 .51: exact .50 inventory plus two clients, three UI providers, native .100."""
import argparse,hashlib,json,os,shutil,subprocess,sys,tempfile
from pathlib import Path
from build_recovered_diagnostic import require,sha,encoded,digest_inventory
from package_resident_cohort import descriptor
from package_home_ui import clean,pinned,write_store
import resident_native
ROOT=Path(__file__).resolve().parents[2]
TEXT={'capability':'ui.text-input','api':1}
GRANT={**TEXT,'instance_id':0}
PROVIDERS={'ui-scene':'presentation/scene-host','ui-profile':'presentation/portrait-monochrome','ui-text':'text-input-host'}

def build(a):
 spec=json.loads((ROOT/'minimal/apps/shared-keyboard-051.json').read_text());source=clean(ROOT)
 require(clean(a.runtime)['commit']==spec['runtime_source'],'Wrong Runtime')
 require(clean(a.system)['commit']==spec['system_source'],'Wrong System')
 require(clean(a.tools)['commit']==spec['packaging_source'],'Wrong packaging source')
 require(not a.output.exists(),'Output already exists');a.output.mkdir(parents=True)
 sys.path[:0]=[str(a.tools/'scripts'),str(a.runtime/'scripts'),str(a.system/'scripts')]
 from read_only_spiffs import read_image
 from check_runtime_store_admission import admit_cohort,unpack_image
 from compact_current_elf import compact
 from current_bootfs import build as pack
 from paired_bank_images import initial_bank_state,initial_otadata,parse_record
 import verify_update_elf as verify
 from scene_sdk import stage_sdk
 raw=pinned(a.baseline,spec['baseline']);require(len(raw)==0x1000000,'Wrong baseline image size')
 frozen=read_image(raw[0x2f0000:0x800000],0x510000);files=dict(frozen)
 require(len(files)==91 and json.loads(files['cohort.json'])['version']=='0.1.50','Wrong baseline cohort')
 require(pack(frozen)[0]==raw[0x2f0000:0x800000] and unpack_image(raw[0x2f0000:0x800000],a.mkspiffs,0x510000)==frozen,'Baseline C/Python readback differs')
 old_cohort=json.loads(files['cohort.json']);old_fw=raw[0x10000:0x10000+old_cohort['firmware_size']]
 require(sha(old_fw)==old_cohort['firmware_sha256'] and raw[0xff2000:0xff4000]==initial_bank_state(old_fw,raw[0x2f0000:0x800000],True),'Baseline pair differs')
 require(raw[0xff0000:0xff2000]==initial_otadata(),'Baseline OTA state differs')
 candidate_blob=pinned(a.native/'candidate.json',spec['native_candidate']);candidate=json.loads(candidate_blob)
 native_proof=resident_native.validate(a.native,candidate,a.runtime,native_source_root=a.native_platform)
 require(native_proof['composition_sha256']==spec['native_composition'],'Wrong native composition')
 firmware=(a.native/'firmware.bin').read_bytes();native_elf=(a.native/'firmware.elf').read_bytes()
 require(b'RISC_RUNTIME_VERSION:0.1.100\0' in firmware and b'transport=hardware-sdmmc' in firmware,'Required native SDMMC/version absent')
 for name,offset in [('bootloader.bin',0),('partitions.bin',0x8000),('appdata.bin',0x270000)]:
  data=(a.native/name).read_bytes();require(raw[offset:offset+len(data)]==data,'Frozen native asset differs: '+name)
 old_boot=json.loads(files['boot.json']);boot=json.loads(files['boot.json']);policies={p['manifest']:p for p in boot['app_capabilities']}
 records={};compactions={};strict=[]
 def compact_blob(path,label):
  blob=path.read_bytes();dest=a.output/'compacted'/label;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(blob)
  record=compact(dest,str(a.compiler),debug_path=a.output/'debug-originals'/label);compactions[label]=record;return dest.read_bytes()
 for name in ('points_in_time','ble_scanner'):
  entry=spec['clients'][name];folder=Path(entry['target']);owner=Path(entry['source_path'])
  require(clean(owner)['commit']==entry['source'],'Client source differs: '+name)
  blob=pinned(folder/(name+'.elf'),entry['elf']);meta=pinned(folder/(name+'.json'),entry['manifest']);record=json.loads(pinned(folder/'x4-native-app.json',entry['receipt']))
  require(record['source_revision']==entry['source'] and not record['source_dirty'] and not record['system_dirty'],'Dirty client input')
  require(record['system_source_revision']==spec['system_source'] and record['runtime_source_revision']==spec['runtime_source'],'Client SDK/System pin differs')
  require(record['elf_sha256']==sha(blob) and record['elf_bytes']==len(blob),'Client receipt ELF differs')
  previous=json.loads(frozen[name+'.json']);manifest=json.loads(meta);prior_version=previous.pop('version');previous_requires=previous.pop('requires')
  require(manifest['version']==entry['version'] and tuple(map(int,entry['version'].split('.')))>tuple(map(int,prior_version.split('.'))),'Client version did not advance')
  require(previous=={k:v for k,v in manifest.items() if k not in ('version','requires')},'Changed client identity/ABI')
  require(manifest['requires']==previous_requires+[TEXT] and record['required_grants']==policies[name+'.json']['grants']+[GRANT],'Dropped/changed client authority')
  require(set(entry['preserved_flags'])<=set(record['build_defines']) and set(record['build_defines'])-set(entry['preserved_flags'])<= {'-DPORTABLE_TEXT_INPUT_CLIENT','-DPORTABLE_APP_LAUNCH_GUARD'},'Changed installed feature flags')
  require('-DPORTABLE_TEXT_INPUT_CLIENT' in record['build_defines'],'Missing shared text client')
  dependency_roots={'Source':owner,'System':a.system,'CompiledSDK':folder.parent/'sdk/include'}
  for path,digest in record['compiled_dependencies_sha256'].items():
   label,relative=path.split('/',1);require(label in dependency_roots and '..' not in Path(relative).parts,'Unrecognized compiled dependency: '+path)
   require(sha((dependency_roots[label]/relative).read_bytes())==digest,'Client compiled dependency differs: '+path)
  require({p.name:sha(p.read_bytes()) for p in dependency_roots['CompiledSDK'].glob('*.h')}==record['sdk_sha256'],'Client staged SDK differs')
  require(record['quick_render_definitions']==0,'Client unexpectedly embeds Quick Actions renderer')
  descriptor(blob,2);files[name+'.elf']=compact_blob(folder/(name+'.elf'),name+'.elf');files[name+'.json']=meta
  descriptor(files[name+'.elf'],2,check_renderer=False);policies[name+'.json']['grants'].append(GRANT);records[name]=record
 for destination,folder in PROVIDERS.items():
  src=a.providers/folder;entry=spec['providers'][destination];record=json.loads(pinned(src/'build.json',entry['receipt']));meta=pinned(src/'manifest.json',entry['manifest']);blob=pinned(src/'driver.elf',entry['elf'])
  require(record['sha256']==sha(blob) and record['size_bytes']==len(blob),'Provider receipt ELF differs')
  require(json.loads(meta)==json.loads((a.system/entry['source_manifest']).read_text()),'Provider source manifest differs')
  for path,digest in record['source_sha256'].items():require(Path(path).is_relative_to(a.system) and sha(Path(path).read_bytes())==digest,'Provider source hash differs')
  with tempfile.TemporaryDirectory() as tmp:
   inc=stage_sdk(a.runtime,a.system,Path(tmp))
   if destination=='ui-text':
    shutil.copy(a.system/'sdk/app/RiscTextEntryV1.h',inc)
    for header in ('RiscUsbHidV1.h','RiscUsbControllerV1.h','RiscUsbProviderV1.h'):shutil.copy(a.system/'Services/text_input'/header,inc)
   require({p.name:sha(p.read_bytes()) for p in inc.glob('*.h')}==record['sdk_sha256'],'Provider staged SDK differs')
  require(record['exports']==['t5_driver_get'],'Provider exports changed')
  files[destination+'/driver.elf']=compact_blob(src/'driver.elf',destination+'/driver.elf');files[destination+'/manifest.json']=meta
  boot['drivers'].append({'manifest':destination+'/manifest.json'});records[destination]=record
 require(json.loads(files['ui-text/manifest.json'])['requires']==[{'capability':'ui.scene','api':1}],'Unqualified hardware keyboard dependency selected')
 require(records['ui-profile']['defines']==['-DSCENE_PROFILE_PAPER=1','-DSCENE_DISPLAY_ROTATION=270','-DSCENE_PROFILE_ID="scene-profile-portrait-monochrome"'],'Wrong X4 portrait profile')
 require(len(boot['drivers'])==26 and len(boot['app_capabilities'])==21,'Incomplete graph')
 # Prove the entire old policy survives; only exactly two text grants and three
 # provider declarations are added. Hardware, namespaces and routes are frozen.
 expected=json.loads(frozen['boot.json'])
 for row in expected['app_capabilities']:
  if row['manifest'] in ('points_in_time.json','ble_scanner.json'):row['grants'].append(GRANT)
 expected['drivers'] += [{'manifest':d+'/manifest.json'} for d in PROVIDERS]
 require(boot==expected,'Unexpected boot policy change');files['boot.json']=encoded(boot)
 product=json.loads((ROOT/'minimal/product.json').read_text());require(product['version']=='0.1.51','Wrong product version')
 from build_test_bundle import cohort_identity
 cohort=cohort_identity(product,candidate,firmware,source['commit']);files['cohort.json']=encoded(cohort)
 changed=sorted(n for n in files if files[n]!=frozen.get(n));allowed=sorted(['boot.json','cohort.json','points_in_time.elf','points_in_time.json','ble_scanner.elf','ble_scanner.json']+[d+'/'+n for d in PROVIDERS for n in ('driver.elf','manifest.json')])
 require(changed==allowed and set(frozen)<=set(files) and len(files)==97,'Unexpected inventory changes')
 require(files['default.elf']==frozen['default.elf'] and files['default.json']==frozen['default.json'],'Qualified Home changed')
 write_store(a.output/'store',files)
 old_admission=verify.admission_source
 old_cohort_header=verify.cohort_admission_header
 old_include_path=os.environ.get('CPLUS_INCLUDE_PATH')
 os.environ['CPLUS_INCLUDE_PATH']=str(a.runtime/'src')+(os.pathsep+old_include_path if old_include_path else '')
 def with_policy(text):
  body,roles,driver=old_admission(text)
  header=a.runtime/'src/runtime/drivers/NativeProviderPolicyValidationV1.h'
  return '#include "'+str(header)+'"\n'+body,roles,driver
 verify.admission_source=with_policy
 try:
  for name in sorted(n for n in files if n.endswith('.elf')):
   row=verify.verify(a.runtime,native_elf,files[name],provider='/' in name);row['path']=name;strict.append(row)
  # The cohort extractor nests admitElf in CohortElf; its dependencies must be
  # included outside that namespace, unlike the standalone strict checker.
  verify.admission_source=old_admission
  def cohort_with_policy(runtime,elf):
   return '#include "'+str(a.runtime/'src/runtime/drivers/NativeProviderPolicyValidationV1.h')+'"\n'+old_cohort_header(runtime,elf)
  verify.cohort_admission_header=cohort_with_policy
  admissions=[]
  for mode in ('0','1'):
   os.environ['SANITIZE']=mode;os.environ['ASAN_OPTIONS']='detect_leaks=0'
   admissions.append(admit_cohort(a.runtime,native_elf,files,files,app_policy_rows=17))
 finally:
  verify.admission_source=old_admission
  verify.cohort_admission_header=old_cohort_header
  if old_include_path is None:os.environ.pop('CPLUS_INCLUDE_PATH',None)
  else:os.environ['CPLUS_INCLUDE_PATH']=old_include_path
 require(len(strict)==47 and all(r['cohort_validated'] and r['elf_count']==47 and r['hardware_calls']==r['storage_calls']==0 for r in admissions),'Full cohort admission failed')
 (a.output/'strict-elf-admission.json').write_bytes(encoded(strict))
 fs,capacity=pack(files);require(pack(files)[0]==fs and read_image(fs,0x510000)==files,'Store nondeterministic/readback failed')
 require(unpack_image(fs,a.mkspiffs,0x510000)==files,'Independent C readback differs')
 bank=initial_bank_state(firmware,fs,True);pair=parse_record(bank[:96],True)
 require(pair[6]==hashlib.sha256(firmware).digest() and pair[7]==hashlib.sha256(fs).digest(),'Paired SHA/CRC mismatch')
 image=bytearray(raw);image[0x10000:0x270000]=b'\xff'*0x260000;image[0x10000:0x10000+len(firmware)]=firmware;image[0x2f0000:0x800000]=fs;image[0xff2000:0xff4000]=bank
 regions=[(0,0x10000),(0x270000,0x2f0000),(0x800000,0xff2000),(0xff4000,0x1000000)]
 require(len(image)==0x1000000 and all(image[a:b]==raw[a:b] for a,b in regions),'Unrelated flash regions changed')
 require(read_image(bytes(image[0x2f0000:0x800000]),0x510000)==files,'Final full-image readback failed')
 name='X4-0.1.51-Shared-Keyboard-full-0x0.bin';(a.output/name).write_bytes(image);(a.output/'bootfs.bin').write_bytes(fs)
 report={'schema':'x4.shared-keyboard-cohort','schema_version':1,'product':product,'source':source,'inputs':spec,'native_proof':native_proof,'native_candidate':candidate,'cohort':cohort,'image':{'name':name,'bytes':len(image),'sha256':sha(image),'flash_offset':0},'build_receipts':records,'compactions':compactions,'admission':admissions,'strict_elf_count':len(strict),'store_generator':capacity,'store_files':digest_inventory(files),'changed_store_paths':changed,'preserved_apps':[n for n in frozen if n.endswith('.elf') and '/' not in n and n not in ('points_in_time.elf','ble_scanner.elf')],'preserved_providers':[n for n in frozen if n.endswith('/driver.elf')],'preserved_regions':[{'offset':lo,'bytes':hi-lo,'sha256':sha(raw[lo:hi])} for lo,hi in regions],'paired_sha_crc_verified':True,'hardware_tested':False,'hardware_keyboard_supported':False,'text_entry':'one shared onscreen plaintext keyboard; converted Points custom-type names and BLE per-address sensor aliases','unmet_requirements':['Physical hardware-keyboard preference requires a real usb.hid.keyboard provider and host/controller closure; not present','Other editors (legacy Points, Timecard, Wi-Fi, filenames, LoRa/audio/RF labels) remain unmigrated','Physical boot, keyboard/raster/touch, SDMMC/sleep, USB restoration and memory headroom untested'],'first_install_warning':'Full 16 MiB image at 0x0 overwrites internal settings, Bluetooth bonds and AppData. Back up first. Removable SD contents are not included.'}
 (a.output/'build-custody.json').write_bytes(encoded(report));print(json.dumps(report['image']))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('baseline','runtime','native','native-platform','system','tools','providers','compiler','mkspiffs','output'):p.add_argument('--'+n,type=Path,required=True)
 build(p.parse_args())
