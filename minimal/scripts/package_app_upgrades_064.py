#!/usr/bin/env python3
"""Compose the selected .64 apps/native over delivered .63; preserve panel .13."""
import argparse,hashlib,io,json,os,sys
from pathlib import Path
from build_recovered_diagnostic import require,sha,encoded,digest_inventory
from package_resident_cohort import descriptor
from package_home_ui import clean,pinned,write_store
from build_test_bundle import validate_native_composition,cohort_identity
ROOT=Path(__file__).resolve().parents[2]
BASE_SHA='6f70536738b71259b136eec4886e809069a88605eb48d81522bcef5a669e98a7'
APPS={'default','springboard','contexts','waterfall'}
PROVIDERS={'iq','contexts'}
DISTRIBUTION_NOTICES={
 'ui-scene/licenses/COMPONENT-SOURCES.json','ui-scene/licenses/SOURCES.json',
 'ui-scene/licenses/LICENSE-Orbitron.txt','ui-scene/licenses/LICENSE-Rajdhani.txt'}

def artifact(row):
 return pinned(Path(row['path']),row)

def build(a):
 spec=json.loads(a.spec.read_text());source=clean(ROOT)
 require(spec['x4_source']==source['commit'],'Changed X4 source')
 product=json.loads((ROOT/'minimal/product.json').read_text())
 require(product['version']=='0.1.64','Wrong product version')
 for label,row in spec['sources'].items():
  require(clean(Path(row['path']))=={'commit':row['commit'],'tree':row['tree']},'Changed source: '+label)
 for name,digest in spec['compiled_inputs'].items():
  require(sha(Path(name).read_bytes())==digest,'Compiled dependency changed: '+name)
 for row in spec['qualifications']:artifact(row)
 require(spec['compiled_inputs'] and spec['qualifications'],'Missing build/test custody')
 require(not a.output.exists(),'Output exists');a.output.mkdir(parents=True)
 runtime=Path(spec['runtime']);tools=Path(spec['tools']);native=Path(spec['native'])
 sys.path[:0]=[str(tools/'scripts'),str(runtime/'scripts'),str(ROOT/'minimal/recovery_tools')]
 from read_only_spiffs import read_image
 from current_bootfs import build as pack
 from paired_bank_images import initial_bank_state,initial_otadata,parse_record
 from native_binary_exports import exports as bin_exports,reference_tables
 from elftools.elf.elffile import ELFFile
 import verify_update_elf as verify
 import recovery_store_admission as admission
 admission.ROOT=tools
 def unpack(blob):return admission.unpack_image(blob,a.mkspiffs,0x510000,expected_tool_sha256=spec['mkspiffs_sha256'])
 raw=artifact(spec['baseline']);require(len(raw)==0x1000000 and sha(raw)==BASE_SHA,'Wrong delivered baseline')
 prior=json.loads(artifact(spec['baseline_custody']));require(prior['image']['sha256']==BASE_SHA,'Wrong prior custody')
 frozen=read_image(raw[0x2f0000:0x800000],0x510000)
 require(digest_inventory(frozen)==prior['store_files'],'Baseline inventory differs')
 require(pack(frozen)[0]==raw[0x2f0000:0x800000] and unpack(raw[0x2f0000:0x800000])==frozen,'Baseline dual readback differs')
 files={name:artifact(row) for name,row in spec['staged_store'].items()}
 # The Lists composer may replace only the scene host/profile and add its app,
 # civil clock provider and license. All other delivered bytes remain frozen.
 staged_allowed={'boot.json','launcher-catalog.json','ui-scene/driver.elf','ui-scene/manifest.json','ui-profile/driver.elf','ui-profile/manifest.json'}
 for name,blob in frozen.items():
  if name=='cohort.json':continue
  require(name in files and (name in staged_allowed or files[name]==blob),'Unexpected staged replacement: '+name)
 require(set(files)-set(frozen)<=set(spec['staged_additions']),'Unexpected added files')
 # The native cohort admits an exact executable/manifest inventory. Preserve
 # the composer's distribution notices alongside the image, outside SPIFFS.
 require(DISTRIBUTION_NOTICES<=set(files),'Missing distribution notices')
 notices={name:files.pop(name) for name in sorted(DISTRIBUTION_NOTICES)}
 write_store(a.output/'distribution-notices',notices)
 require(set(spec['apps'])==APPS and set(spec['providers'])==PROVIDERS,'Wrong replacement scope')
 receipts={}
 for directory,rows in ((False,spec['apps']),(True,spec['providers'])):
  for name,row in rows.items():
   blob=artifact(row['elf']);manifest=json.loads(artifact(row['manifest']));receipt=json.loads(artifact(row['receipt']))
   if 'receipt_key' in row:receipt=receipt[row['receipt_key']]
   require(sha(blob)==receipt.get('elf_sha256',receipt.get('sha256')),'ELF receipt mismatch: '+name)
   require(len(blob)==receipt.get('elf_bytes',receipt.get('size_bytes',receipt.get('bytes'))),'ELF size mismatch: '+name)
   require(manifest['version']==row['version'],'Version mismatch: '+name)
   elf_name=name+'/driver.elf' if directory else name+'.elf'
   meta_name=name+'/manifest.json' if directory else name+'.json'
   files[elf_name]=blob;files[meta_name]=encoded(manifest);receipts[elf_name]=receipt
 for name in ('panel/driver.elf','panel/manifest.json'):
  require(files[name]==frozen[name],'Panel must remain byte-identical .13')
 require(json.loads(files['panel/manifest.json'])['version']=='0.1.13','Wrong panel')
 # Distribution catalog fields are not part of the runtime's strict app
 # manifest schema. Keep their source receipt; project only the three known
 # catalog-only fields after checking the runtime/profile compatibility.
 lists_manifest=json.loads(files['lists.json'])
 lists_catalog={k:lists_manifest.pop(k) for k in ('category','min_firmware_version','runtime_profile')}
 require(lists_catalog['runtime_profile']=='portable-riscrte-v1' and tuple(map(int,lists_catalog['min_firmware_version'].split('.'))) <= (0,2,3),'Unsupported Lists native profile')
 files['lists.json']=encoded(lists_manifest)
 boot=json.loads(files['boot.json'])
 # Explicit, exact file grants. Shared records never imply arbitrary access to
 # another app namespace. Timer rules use the existing countdown namespace.
 grants=[{'capability':'storage.shared-data','api':1,'instance_id':3,'file':'context-fingerprints.cfp'},
         {'capability':'storage.shared-data','api':1,'instance_id':4,'file':'context-rules.ctx'},
         {'capability':'contexts.service','api':1,'instance_id':0},
         {'capability':'storage.key-value','api':1,'instance_id':3}]
 for name in ('default','contexts','waterfall'):
  policy=next(x for x in boot['app_capabilities'] if x['manifest']==name+'.json')
  meta=json.loads(files[name+'.json'])
  extra=grants+([{'capability':'telemetry.broadcast','api':1,'instance_id':0}] if name=='contexts' else [])
  for grant in extra:
   require({'capability':grant['capability'],'api':grant['api']} in meta['requires'],'Undeclared grant: '+name)
   if grant not in policy['grants']:policy['grants'].append(grant)
  require(len(policy['grants'])<=24 and len(meta['requires'])<=24,'App policy overflow')
 lists=next(x for x in boot['app_capabilities'] if x['manifest']=='lists.json')
 require({'capability':'storage.app-data','api':1,'instance_id':63} in lists['grants'],'Wrong Lists namespace')
 require(all(g.get('instance_id')!=63 for p in boot['app_capabilities'] if p!=lists for g in p['grants'] if g['capability']=='storage.app-data'),'Lists namespace collision')
 require(len(boot['drivers'])==27 and len(boot['app_capabilities'])==22,'Wrong cohort count')
 files['boot.json']=encoded(boot)
 candidate=json.loads(artifact(spec['native_candidate']))
 native_proof=validate_native_composition(native,candidate,runtime,ROOT,native_source_root=Path(spec['native_platform']))
 require(candidate['firmware_version']=='0.2.3' and candidate['build_options']=={'app_policy_rows':24,'app_requirement_rows':24,'app_image_cache':True,'usb_phy':True,'retained_wake_bytes':512,'failure_evidence':True},'Wrong native selection')
 firmware=(native/'firmware.bin').read_bytes();native_elf=(native/'firmware.elf').read_bytes()
 names,exports_proof=bin_exports(firmware,reference_tables(native_elf))
 require(names==verify.public_exports(ELFFile(io.BytesIO(native_elf))),'Native BIN/ELF exports differ')
 cohort=cohort_identity(product,candidate,firmware,source['commit']);files['cohort.json']=encoded(cohort)
 for name,role in [('default.elf',1),*[(n,2) for n in boot['resident_shell']['foreground']]]:descriptor(files[name],role,check_renderer=False)
 write_store(a.output/'store',files)
 old_admission=verify.admission_source;old_header=verify.cohort_admission_header
 environment={k:os.environ.get(k) for k in ('CPLUS_INCLUDE_PATH','SANITIZE','ASAN_OPTIONS')}
 os.environ['CPLUS_INCLUDE_PATH']=str(runtime/'src')+(os.pathsep+environment['CPLUS_INCLUDE_PATH'] if environment['CPLUS_INCLUDE_PATH'] else '')
 header='#define RISC_APP_REQUIREMENT_ROWS 24\n#define RISC_APP_POLICY_ROWS 24\n#include "'+str(runtime/'src/runtime/drivers/NativeProviderPolicyValidationV1.h')+'"\n'
 def with_policy(text):
  body,roles,driver=old_admission(text);return header+body,roles,driver
 try:
  verify.admission_source=with_policy;strict=[]
  for name in sorted(n for n in files if n.endswith('.elf')):
   result=verify.verify(runtime,native_elf,files[name],provider='/' in name);result['path']=name;strict.append(result)
  verify.admission_source=old_admission;verify.cohort_admission_header=lambda rt,elf:header+old_header(rt,elf)
  admissions=[]
  for mode in ('0','1'):
   os.environ['SANITIZE']=mode;os.environ['ASAN_OPTIONS']='detect_leaks=0'
   row=admission.admit_cohort(runtime,native_elf,files,files,app_policy_rows=24);row['sanitized']=mode=='1';admissions.append(row)
 finally:
  verify.admission_source=old_admission;verify.cohort_admission_header=old_header
  for key,value in environment.items():
   if value is None:os.environ.pop(key,None)
   else:os.environ[key]=value
 require(len(strict)==49 and all(r['cohort_validated'] and r['elf_count']==49 and r['hardware_calls']==r['storage_calls']==0 for r in admissions),'Incomplete native admission')
 fs,capacity=pack(files)
 require(pack(files)[0]==fs and read_image(fs,0x510000)==files and unpack(fs)==files,'Final dual readback/determinism failed')
 require({p.relative_to(a.output/'store').as_posix():p.read_bytes() for p in (a.output/'store').rglob('*') if p.is_file()}==files,'Validation changed store')
 bank=initial_bank_state(firmware,fs,True);pair=parse_record(bank[:96],True)
 require(pair[6]==hashlib.sha256(firmware).digest() and pair[7]==hashlib.sha256(fs).digest(),'Pair SHA/CRC mismatch')
 # Same board boot chain and partition layout; only native app0, bootfs0 and
 # initial pair selection change. Full image retains the empty AppData bank.
 for name,offset in [('bootloader.bin',0),('partitions.bin',0x8000)]:
  blob=(native/name).read_bytes();require(raw[offset:offset+len(blob)]==blob,'Boot chain changed: '+name)
 image=bytearray(raw);image[0x10000:0x270000]=firmware+b'\xff'*(0x260000-len(firmware))
 image[0x2f0000:0x800000]=fs;image[0xff0000:0xff2000]=initial_otadata();image[0xff2000:0xff4000]=bank
 regions=[(0,0x10000),(0x270000,0x2f0000),(0x800000,0xff0000),(0xff4000,0x1000000)]
 require(all(image[lo:hi]==raw[lo:hi] for lo,hi in regions),'Frozen flash region changed')
 name='X4-0.1.64-Render-Latency-Contexts-Lists-Panel-0.1.13-full-0x0.bin'
 (a.output/name).write_bytes(image);(a.output/'bootfs.bin').write_bytes(fs)
 report={'schema':'x4.app-upgrades-render-latency','schema_version':1,'product_version':'0.1.64','source':source,'inputs':spec,'native_proof':native_proof,'native_export_count':len(names),'native_exports':exports_proof,'cohort':cohort,'image':{'name':name,'bytes':len(image),'sha256':sha(image),'flash_offset':0},'build_receipts':receipts,'strict_admission':strict,'cohort_admission':admissions,'store_generator':capacity,'store_files':digest_inventory(files),'distribution_notices':digest_inventory(notices),'panel_unchanged':True,'lists_catalog_metadata':lists_catalog,'changed_store_paths':sorted(n for n in files if files[n]!=frozen.get(n)),'preserved_regions':[{'offset':lo,'bytes':hi-lo,'sha256':sha(raw[lo:hi])} for lo,hi in regions],'paired_sha_crc_verified':True,'hardware_tested':False,'first_install_warning':'Full 16 MiB image at 0x0 overwrites internal settings, Bluetooth bonds and AppData. Back up first. Removable SD contents are not included.'}
 (a.output/'build-custody.json').write_bytes(encoded(report));print(json.dumps(report['image']))

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('spec','mkspiffs','output'):p.add_argument('--'+name,type=Path,required=True)
 build(p.parse_args())
