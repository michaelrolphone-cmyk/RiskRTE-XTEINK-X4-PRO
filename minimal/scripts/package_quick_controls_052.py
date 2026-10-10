#!/usr/bin/env python3
"""Compose only qualified Home and two tone providers over immutable X4 .51."""
import argparse,hashlib,io,json,os,subprocess,sys
from pathlib import Path
from build_recovered_diagnostic import require,sha,encoded,digest_inventory
from package_resident_cohort import descriptor
from package_home_ui import clean,pinned,write_store
import resident_native
ROOT=Path(__file__).resolve().parents[2]

def build(a):
 spec=json.loads((ROOT/'minimal/apps/quick-controls-052.json').read_text()); source=clean(ROOT)
 for path,key in [(a.runtime,'runtime_source'),(a.system,'system_source'),(a.provider_source,'provider_source'),(a.tools,'packaging_source')]:require(clean(path)['commit']==spec[key],'Wrong source: '+key)
 require(not a.output.exists(),'Output exists');a.output.mkdir(parents=True)
 sys.path[:0]=[str(a.tools/'scripts'),str(a.runtime/'scripts')]
 from read_only_spiffs import read_image
 from check_runtime_store_admission import admit_cohort,unpack_image
 from compact_current_elf import compact
 from current_bootfs import build as pack
 from paired_bank_images import initial_bank_state,initial_otadata,parse_record
 from native_binary_exports import exports as bin_exports,reference_tables
 from elftools.elf.elffile import ELFFile
 import verify_update_elf as verify
 raw=pinned(a.baseline,spec['baseline']);require(len(raw)==0x1000000,'Baseline size differs')
 frozen=read_image(raw[0x2f0000:0x800000],0x510000);files=dict(frozen);old=json.loads(files['cohort.json'])
 require(len(files)==97 and old['version']=='0.1.51' and old['runtime_version']=='0.1.100','Wrong cohort')
 require(pack(frozen)[0]==raw[0x2f0000:0x800000] and unpack_image(raw[0x2f0000:0x800000],a.mkspiffs,0x510000)==frozen,'Baseline readback differs')
 candidate=json.loads(pinned(a.native/'candidate.json',spec['native_candidate']));native_proof=resident_native.validate(a.native,candidate,a.runtime,native_source_root=a.native_platform)
 firmware=(a.native/'firmware.bin').read_bytes();native_elf=(a.native/'firmware.elf').read_bytes()
 require(raw[0x10000:0x10000+len(firmware)]==firmware and sha(firmware)==old['firmware_sha256'],'Native changed')
 require(raw[0xff2000:0xff4000]==initial_bank_state(firmware,raw[0x2f0000:0x800000],True) and raw[0xff0000:0xff2000]==initial_otadata(),'Baseline pairing/OTA differs')
 names,export_proof=bin_exports(firmware,reference_tables(native_elf));require(names==verify.public_exports(ELFFile(io.BytesIO(native_elf))) and len(names)==39,'Native BIN/ELF exports differ')
 (a.output/'native-bin-exports.json').write_bytes(encoded(export_proof))
 compactions={};receipts={}
 def compacted(blob,label):
  p=a.output/'compacted'/label;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(blob)
  compactions[label]=compact(p,str(a.compiler),debug_path=a.output/'debug-originals'/label);return p.read_bytes()
 home=pinned(a.home/'default.elf',spec['home']['elf']);meta=pinned(a.home/'default.json',spec['home']['manifest']);receipt=json.loads(pinned(a.home/'build-evidence.json',spec['home']['receipt']))
 require(receipt['repository_commit']==spec['system_source'] and not receipt['working_tree_dirty'] and receipt['sha256']==sha(home),'Home source/target mismatch')
 require(receipt['resident_shell']['host_power_policy'] and receipt['resident_shell']['legacy_handoff'] and receipt['resident_shell']['frontlight_tone']['optional'] and not receipt['resident_shell']['frontlight_tone']['grants_added'],'Home authority/features changed')
 for n,h in receipt['resident_shell']['sdk_sha256'].items():require(sha((a.runtime/'sdk/app'/n).read_bytes())==h,'Home SDK incompatible with native: '+n)
 source_hashes={**receipt['desk_sources'],**receipt['resident_shell']['source_sha256'],**receipt['idle_policy']['system_source_sha256'],**receipt['idle_policy']['build_source_sha256'],**receipt['resident_shell']['quick_reference']['source_sha256']}
 for n,h in source_hashes.items():require(sha((a.system/n).read_bytes())==h,'Home source mismatch: '+n)
 require(sha((ROOT/'minimal/apps/portable_idle_sleep.c').read_bytes())==receipt['idle_policy']['helper_sha256'],'Idle helper changed')
 tone_header=a.home/'paper-sdk/include/RiscDisplayOutputFrontlightV1.h'
 require(sha(tone_header.read_bytes())==receipt['resident_shell']['frontlight_tone']['sdk_sha256'] and tone_header.read_bytes()==(a.provider_source/'minimal/interfaces/RiscDisplayOutputFrontlightV1.h').read_bytes(),'App/provider tone SDK mismatch')
 previous=json.loads(files['default.json']);new=json.loads(meta);require(previous.pop('version')=='0.3.22' and new['version']=='0.3.25' and previous=={k:v for k,v in new.items() if k!='version'},'Home identity/authority changed')
 old_flags=set(spec['home']['preserved_flags']);flags={v for v in receipt['build_defines'] if not v.startswith('-I')};require(flags==old_flags|{'-DPORTABLE_FRONTLIGHT_TONE'},'Home feature flags differ')
 descriptor(home,1);files['default.elf']=compacted(home,'default.elf');files['default.json']=meta;receipts['home']=receipt
 baseline=json.loads(pinned(a.providers/'baseline-custody.json',spec['provider_baseline']));require(sha(files['panel/driver.elf'])==baseline['deployed_elf']['sha256'],'Wrong fast display baseline')
 seal=json.loads(pinned(a.providers/'final-source-custody.json',spec['provider_final_seal']))
 require(seal['source_commit']==spec['provider_source'] and seal['source_clean'] and not seal['source_uncommitted'],'Provider final custody differs')
 require(sha((a.providers/'qualification.json').read_bytes())==seal['qualification_sha256'],'Provider qualification changed')
 for name,digest in seal['qualified_source_hashes_verified_unchanged'].items():require(sha((a.provider_source/name).read_bytes())==digest,'Qualified provider source changed: '+name)
 require((a.providers/'deployed-baseline-target/driver.elf').read_bytes()==files['panel/driver.elf'],'Exact shipped fast .9 was not reproduced')
 for label,ident,folder,version,prior in [('light','x4pro-frontlight','x4pro_frontlight','0.1.6','0.1.5'),('panel','x4pro-uc8279-fast','x4pro_uc8279_fast','0.1.10','0.1.9')]:
  src=a.providers/'targets'/ident;entry=spec['providers'][label];blob=pinned(src/'driver.elf',entry['elf']);meta=pinned(src/'manifest.json',entry['manifest']);record=json.loads(pinned(src/'build.json',entry['receipt']))
  require(record['sha256']==sha(blob) and record['bytes']==len(blob) and record['exports']==['t5_driver_get'],'Provider target mismatch')
  for path,digest in record['source_hashes'].items():require(sha(Path(path).read_bytes())==digest,'Provider compiled dependency changed: '+path)
  require(json.loads(meta)==json.loads((a.provider_source/'minimal/drivers'/folder/'manifest.json').read_text()),'Provider source manifest differs')
  old_manifest=json.loads(files[label+'/manifest.json']);new_manifest=json.loads(meta)
  require(old_manifest.pop('version')==prior and new_manifest['version']==version and old_manifest=={k:v for k,v in new_manifest.items() if k!='version'},'Provider identity/authority changed')
  files[label+'/driver.elf']=compacted(blob,label+'/driver.elf');files[label+'/manifest.json']=meta;receipts[label]=record
 require(json.loads((ROOT/'minimal/product.json').read_text())['version']=='0.1.52','Wrong product version')
 cohort=dict(old,version='0.1.52',source_revision=source['commit']);files['cohort.json']=encoded(cohort)
 changed=sorted(n for n in files if files[n]!=frozen[n]);require(changed==sorted(['default.elf','default.json','cohort.json','light/driver.elf','light/manifest.json','panel/driver.elf','panel/manifest.json']) and set(files)==set(frozen),'Unexpected store mutation')
 require(files['boot.json']==frozen['boot.json'],'Boot graph/grants changed')
 for name,role in [('default.elf',1),*[(n,2) for n in json.loads(files['boot.json'])['resident_shell']['foreground']]]:descriptor(files[name],role,check_renderer=False)
 write_store(a.output/'store',files)
 old_admission=verify.admission_source;old_header=verify.cohort_admission_header;old_include=os.environ.get('CPLUS_INCLUDE_PATH');os.environ['CPLUS_INCLUDE_PATH']=str(a.runtime/'src')+(os.pathsep+old_include if old_include else '')
 header='#include "'+str(a.runtime/'src/runtime/drivers/NativeProviderPolicyValidationV1.h')+'"\n'
 def with_policy(text):
  body,roles,driver=old_admission(text);return header+body,roles,driver
 verify.admission_source=with_policy
 try:
  strict=[]
  for name in sorted(n for n in files if n.endswith('.elf')):
   row=verify.verify(a.runtime,native_elf,files[name],provider='/' in name);row['path']=name;strict.append(row)
  verify.admission_source=old_admission;verify.cohort_admission_header=lambda runtime,elf:header+old_header(runtime,elf)
  admissions=[]
  for mode in ('0','1'):
   os.environ['SANITIZE']=mode;os.environ['ASAN_OPTIONS']='detect_leaks=0';row=admit_cohort(a.runtime,native_elf,files,files,app_policy_rows=17);row['sanitized']=mode=='1';admissions.append(row)
 finally:
  verify.admission_source=old_admission;verify.cohort_admission_header=old_header
  if old_include is None:os.environ.pop('CPLUS_INCLUDE_PATH',None)
  else:os.environ['CPLUS_INCLUDE_PATH']=old_include
 require(len(strict)==47 and all(r['cohort_validated'] and r['elf_count']==47 and r['hardware_calls']==r['storage_calls']==0 for r in admissions),'Incomplete admission')
 (a.output/'strict-elf-admission.json').write_bytes(encoded(strict))
 fs,capacity=pack(files);require(pack(files)[0]==fs and read_image(fs,0x510000)==files and unpack_image(fs,a.mkspiffs,0x510000)==files,'Final dual readback/determinism failed')
 bank=initial_bank_state(firmware,fs,True);pair=parse_record(bank[:96],True);require(pair[6]==hashlib.sha256(firmware).digest() and pair[7]==hashlib.sha256(fs).digest(),'Pair SHA/CRC mismatch')
 image=bytearray(raw);image[0x2f0000:0x800000]=fs;image[0xff2000:0xff4000]=bank;regions=[(0,0x2f0000),(0x800000,0xff2000),(0xff4000,0x1000000)]
 require(all(image[lo:hi]==raw[lo:hi] for lo,hi in regions) and read_image(bytes(image[0x2f0000:0x800000]),0x510000)==files,'Final frozen regions/readback differ')
 name='X4-0.1.52-Quick-Controls-full-0x0.bin';(a.output/name).write_bytes(image);(a.output/'bootfs.bin').write_bytes(fs)
 report={'schema':'x4.quick-controls-cohort','schema_version':1,'product_version':'0.1.52','source':source,'inputs':spec,'native_proof':native_proof,'native_unchanged':True,'native_firmware_sha256':sha(firmware),'native_elf_sha256':sha(native_elf),'native_export_count':len(names),'cohort':cohort,'image':{'name':name,'bytes':len(image),'sha256':sha(image),'flash_offset':0},'build_receipts':receipts,'home_compiled_source_files_verified':len(source_hashes),'compactions':compactions,'admission':admissions,'strict_elf_count':len(strict),'store_generator':capacity,'store_files':digest_inventory(files),'changed_store_paths':changed,'preserved_apps':[n for n in frozen if n.endswith('.elf') and '/' not in n and n!='default.elf'],'preserved_providers':[n for n in frozen if n.endswith('/driver.elf') and n.split('/')[0] not in ('light','panel')],'preserved_regions':[{'offset':lo,'bytes':hi-lo,'sha256':sha(raw[lo:hi])} for lo,hi in regions],'boot_graph_and_grants_unchanged':True,'paired_sha_crc_verified':True,'hardware_tested':False,'tone':'optional dimensionless cool-to-warm ratio; 50% neutral preserves former output; no calibrated brightness/Kelvin/current claims','inclusions':['matching USB Transfer grid tile','single Home-owned tone slider','terminal clean-refresh refusal fenced before diagnostics'],'inherited_limits':['Shared keyboard remains onscreen only; physical keyboard provider absent','Windows MSC mounting not established as fixed; preserved native serial-restoration candidate','Other editors remain unmigrated','No physical boot, tone/brightness, e-ink/touch, USB, SDMMC, sleep or live memory qualification'],'first_install_warning':'Full 16 MiB image at 0x0 overwrites internal settings, Bluetooth bonds and AppData. Back up first. Removable SD contents are not included.'}
 (a.output/'build-custody.json').write_bytes(encoded(report));print(json.dumps(report['image']))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('baseline','home','system','provider-source','providers','runtime','native','native-platform','tools','compiler','mkspiffs','output'):p.add_argument('--'+n,type=Path,required=True)
 build(p.parse_args())
