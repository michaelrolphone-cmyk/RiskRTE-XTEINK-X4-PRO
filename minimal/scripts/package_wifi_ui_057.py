#!/usr/bin/env python3
"""Package the explicit stable .57 replacement set over source-bound delivered .55."""
import argparse,hashlib,io,json,os,sys
from pathlib import Path
from build_recovered_diagnostic import require,sha,encoded,digest_inventory
from package_resident_cohort import descriptor
from package_home_ui import clean,write_store
ROOT=Path(__file__).resolve().parents[2]
EXPECTED={
 'paper_clock':'0.3.29','springboard':'1.7.24','settings':'1.3.25','file_browser':'1.5.18',
 'wifi_settings':'1.1.21','ota_update':'1.2.11','app_store':'1.2.11','usb_sd_transfer':'0.1.7',
 'alarms':'0.2.17','battery':'1.1.15','calculator':'0.1.22','stopwatch':'0.1.22',
 'countdown':'0.1.21','ble_scanner':'0.2.21','ble_touchpad':'0.1.20','ble_buttons':'0.1.21',
 'waterfall':'0.2.20','points_in_time':'0.6.14','timecard':'0.2.12','contexts':'0.1.8',
 'text-input-host':'0.1.3','wifi':'0.2.1'}
BASE_SHA='015f7e3f697081b14c1cccbc08c38fa50f2052b3097e8e290a41f27dfc1ae10e'
NATIVE_OPTIONS={'app_policy_rows':18,'app_requirement_rows':17,'app_image_cache':True,
                'usb_phy':True,'retained_wake_bytes':512,'failure_evidence':True}

def artifact(row):
 p=Path(row['path']);require(p.is_file() and not p.is_symlink(),'Missing artifact: '+str(p))
 data=p.read_bytes();require(len(data)==row['bytes'] and sha(data)==row['sha256'],'Artifact changed: '+str(p));return data

def build(a):
 spec=json.loads(a.spec.read_text());require(spec['schema']=='x4.stable-wifi-ui-057','Wrong input schema')
 source=clean(ROOT);require(spec['x4_source']==source['commit'],'Source/spec identity differs')
 require(json.loads((ROOT/'minimal/product.json').read_text())['version']=='0.1.57','Wrong product version')
 require(not a.output.exists(),'Output must be absent')
 for name,row in spec['sources'].items():
  actual=clean(Path(row['path']));require(actual['commit']==row['commit'] and actual['tree']==row['tree'],'Changed source: '+name)
 for path,digest in spec['tool_inputs'].items():require(sha(Path(path).read_bytes())==digest,'Changed tool input: '+path)
 require(spec['tool_inputs'],'Tool input hashes missing')
 runtime=Path(spec['sources']['runtime']['path']);watch=Path(spec['tools']['watch']);native=Path(spec['native_directory'])
 sys.path[:0]=[str(watch/'scripts'),str(runtime/'scripts'),str(ROOT/'minimal/recovery_tools')]
 from read_only_spiffs import read_image
 from current_bootfs import build as pack
 from compact_current_elf import compact
 from paired_bank_images import initial_bank_state,initial_otadata,parse_record
 from native_binary_exports import exports as bin_exports,reference_tables
 from elftools.elf.elffile import ELFFile
 from build_test_bundle import validate_native_composition,cohort_identity
 import verify_update_elf as verify
 import recovery_store_admission as admission
 admission.ROOT=watch
 raw=artifact(spec['baseline']);require(len(raw)==0x1000000 and sha(raw)==BASE_SHA,'Not delivered .55')
 prior=json.loads(artifact(spec['baseline_custody']));require(prior['image']['sha256']==BASE_SHA and prior['strict_admission'] and len(prior['strict_admission'])==47,'Baseline proof differs')
 frozen=read_image(raw[0x2f0000:0x800000],0x510000);files=dict(frozen);old=json.loads(files['cohort.json'])
 require(len(files)==97 and old['version']=='0.1.55' and old['runtime_version']=='0.1.106','Wrong baseline cohort')
 require(pack(frozen)[0]==raw[0x2f0000:0x800000] and admission.unpack_image(raw[0x2f0000:0x800000],a.mkspiffs,0x510000)==frozen,'Baseline independent readback differs')
 require(digest_inventory(frozen)==prior['store_files'],'Baseline custody file inventory differs')
 require(json.loads(files['panel/manifest.json'])['version']=='0.1.13','Baseline fast panel differs')
 require(set(spec['native'])=={'firmware.bin','firmware.elf','bootloader.bin','partitions.bin','appdata.bin','candidate.json'},'Native asset set differs')
 assets={name:artifact(row) for name,row in spec['native'].items()};candidate=json.loads(assets['candidate.json'])
 for name,row in spec['native'].items():require(Path(row['path']).resolve()==(native/name).resolve(),'Native asset displaced')
 native_platform=Path(spec['sources']['native_platform']['path'])
 native_proof=validate_native_composition(native,candidate,runtime,ROOT,native_source_root=native_platform)
 for name,digest in native_proof['platform_source_sha256'].items():
  require(sha((ROOT/name).read_bytes())==digest,'Native platform source changed after composition: '+name)
 require(candidate['firmware_version']=='0.2.2' and candidate['build_options']==NATIVE_OPTIONS,'Wrong native selection')
 require(candidate['build_environment']=='esp32s3-16mb-appdata-iq-stage','Default stage logs not selected')
 names,export_proof=bin_exports(assets['firmware.bin'],reference_tables(assets['firmware.elf']))
 require(names==verify.public_exports(ELFFile(io.BytesIO(assets['firmware.elf']))),'Native BIN/ELF exports differ')
 # Board, partitions, startup layout and initial appdata remain the delivered selection.
 require(assets['partitions.bin']==raw[0x8000:0x8000+len(assets['partitions.bin'])],'Partition layout changed')
 require(len(assets['appdata.bin'])==0x80000 and assets['appdata.bin']==raw[0x270000:0x2f0000],'Initial AppData changed')
 require(set(spec['modules'])==set(EXPECTED),'Unexpected replacement set')
 a.output.mkdir(parents=True);compactions={};receipts={};expected={'cohort.json','boot.json'}
 for ident,row in spec['modules'].items():
  require(row['qualified'] is True,'Unqualified target: '+ident)
  blob=artifact(row['elf']);meta=artifact(row['manifest']);receipt=json.loads(artifact(row['build_receipt']))
  require(receipt.get('elf_sha256',receipt.get('sha256'))==sha(blob),'Target receipt hash differs: '+ident)
  require(receipt.get('elf_bytes',receipt.get('size_bytes',receipt.get('bytes')))==len(blob),'Target receipt size differs: '+ident)
  for proof in row['qualification_receipts']:artifact(proof)
  require(row['qualification_receipts'],'Missing qualification receipt: '+ident)
  manifest=json.loads(meta);before=json.loads(files[row['store_manifest']]);require(before['id']==manifest['id']==ident and manifest['version']==EXPECTED[ident] and before['version']!=manifest['version'],'Wrong module identity/version')
  old_without={k:v for k,v in before.items() if k!='version'};new_without={k:v for k,v in manifest.items() if k!='version'}
  if ident=='wifi_settings':
   req={'capability':'ui.text-input','api':1};require(req not in old_without['requires'],'Unexpected old text requirement');old_without['requires'].append(req)
   old_without['requires']=sorted(old_without['requires'],key=lambda x:(x['capability'],x['api']));new_without['requires']=sorted(new_without['requires'],key=lambda x:(x['capability'],x['api']))
  if ident=='wifi':
   require(old_without['status']=='software-verified-native-station-prerequisite' and new_without['status']=='software-verification-in-progress-async-native-prerequisite','Unexpected provider qualification label')
   old_without['status']=new_without['status']
  require(old_without==new_without,'Manifest authority or metadata changed: '+ident)
  dest=a.output/'compacted'/row['store_elf'];dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(blob)
  compactions[ident]=compact(dest,str(a.compiler),debug_path=a.output/'debug-originals'/row['store_elf'])
  files[row['store_elf']]=dest.read_bytes();files[row['store_manifest']]=meta;receipts[ident]=receipt;expected.update((row['store_elf'],row['store_manifest']))
 files['boot.json']=artifact(spec['boot']);boot=json.loads(files['boot.json']);old_boot=json.loads(frozen['boot.json'])
 old_wifi=next(x for x in old_boot['app_capabilities'] if x['manifest']=='wifi_settings.json')
 old_wifi['grants'].append({'api':1,'capability':'ui.text-input','instance_id':0})
 require(boot==old_boot,'Unexpected graph/grant changes')
 require(len(boot['app_capabilities'])==21 and len(boot['drivers'])==26,'Module roster changed')
 require(files['board.json']==artifact(spec['board'])==frozen['board.json'],'Board wiring changed')
 product=json.loads((ROOT/'minimal/product.json').read_text());files['cohort.json']=encoded(cohort_identity(product,candidate,assets['firmware.bin'],source['commit']))
 changed=sorted(n for n in files if files[n]!=frozen[n]);require(set(files)==set(frozen) and set(changed)<=expected,'Unexpected store mutations')
 require({'cohort.json','boot.json'}<=set(changed),'Expected product graph changes absent')
 require(all(files[n]==frozen[n] for n in set(files)-expected),'Unchanged module bytes differ')
 for name,role in [('default.elf',1),*[(n,2) for n in boot['resident_shell']['foreground']]]:descriptor(files[name],role,check_renderer=False)
 old_source=verify.admission_source;old_header=verify.cohort_admission_header
 prefix='#define RISC_APP_REQUIREMENT_ROWS 17\n#define RISC_APP_POLICY_ROWS 18\n#include "'+str(runtime/'src/runtime/drivers/NativeProviderPolicyValidationV1.h')+'"\n'
 saved={k:os.environ.get(k) for k in ('CPLUS_INCLUDE_PATH','SANITIZE','ASAN_OPTIONS')};os.environ['CPLUS_INCLUDE_PATH']=str(runtime/'src')
 def with_policy(text):
  body,roles,driver=old_source(text);return prefix+body,roles,driver
 try:
  verify.admission_source=with_policy;strict=[]
  for name in sorted(n for n in files if n.endswith('.elf')):
   row=verify.verify(runtime,assets['firmware.elf'],files[name],provider='/' in name);row['path']=name;strict.append(row)
  verify.admission_source=old_source;verify.cohort_admission_header=lambda rt,elf:prefix+old_header(rt,elf);admissions=[]
  for mode in ('0','1'):
   os.environ['SANITIZE']=mode;os.environ['ASAN_OPTIONS']='detect_leaks=0';row=admission.admit_cohort(runtime,assets['firmware.elf'],files,files,app_policy_rows=18);row['sanitized']=mode=='1';row['scope']='self-admission for full flash, not data-preserving update admission';admissions.append(row)
 finally:
  verify.admission_source=old_source;verify.cohort_admission_header=old_header
  for key,value in saved.items():
   if value is None:os.environ.pop(key,None)
   else:os.environ[key]=value
 require(len(strict)==47 and all(x['cohort_validated'] and x['elf_count']==47 and x['hardware_calls']==x['storage_calls']==0 for x in admissions),'Admission incomplete')
 fs,capacity=pack(files);require(pack(files)[0]==fs and read_image(fs,0x510000)==files and admission.unpack_image(fs,a.mkspiffs,0x510000)==files,'Final independent store readback differs')
 bank=initial_bank_state(assets['firmware.bin'],fs,True);pair=parse_record(bank[:96],True)
 require(pair[6]==hashlib.sha256(assets['firmware.bin']).digest() and pair[7]==hashlib.sha256(fs).digest(),'Paired bank checksum differs')
 parts=[(0,assets['bootloader.bin'],'bootloader'),(0x8000,assets['partitions.bin'],'partitions'),(0x10000,assets['firmware.bin'],'native'),(0x270000,assets['appdata.bin'],'appdata'),(0x2f0000,fs,'bootfs'),(0xff0000,initial_otadata(),'otadata'),(0xff2000,bank,'paired-bank')]
 image=bytearray(b'\xff'*0x1000000);ranges=[]
 for offset,data,label in parts:
  require(offset+len(data)<=len(image) and all(offset+len(data)<=lo or offset>=hi for lo,hi in ranges),'Partition overlap');image[offset:offset+len(data)]=data;ranges.append((offset,offset+len(data)))
 require(read_image(bytes(image[0x2f0000:0x800000]),0x510000)==files,'Full image readback differs')
 frozen_regions=[(0x800000,0xff0000),(0xff4000,0x1000000)]
 require(all(image[lo:hi]==raw[lo:hi] for lo,hi in frozen_regions),'Inactive/frozen flash regions differ')
 write_store(a.output/'store',files);name='X4-0.1.57-Wifi-UI-full-0x0.bin';(a.output/name).write_bytes(image);(a.output/'bootfs.bin').write_bytes(fs)
 result={'schema':'x4.stable-wifi-ui-cohort','product_version':'0.1.57','source':source,'spec_sha256':sha(a.spec.read_bytes()),'inputs':spec,'native_proof':native_proof,'native_exports':export_proof,'build_receipts':receipts,'compactions':compactions,'strict_admission':strict,'cohort_admission':admissions,'store_generator':capacity,'store_files':digest_inventory(files),'changed_store_paths':changed,'preserved_store_paths':sorted(set(files)-set(changed)),'parts':[{'offset':o,'bytes':len(b),'sha256':sha(b),'kind':label} for o,b,label in parts],'image':{'name':name,'bytes':len(image),'sha256':sha(image),'flash_offset':0},'hardware_tested':False,'warning':'Full0x0 installation overwrites internal settings, bonds and AppData. Back up first. Removable SD contents are not included.'}
 (a.output/'build-custody.json').write_bytes(encoded(result));print(json.dumps(result['image']))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('spec','output','compiler','mkspiffs'):p.add_argument('--'+n,type=Path,required=True)
 build(p.parse_args())
