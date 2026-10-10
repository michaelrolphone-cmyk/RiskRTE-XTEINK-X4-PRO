#!/usr/bin/env python3
"""Package the renderer repair and fast panel 0.1.15 over exact delivered X4 .59."""
import argparse,hashlib,io,json,os,sys
from pathlib import Path
from build_recovered_diagnostic import require,sha,encoded,digest_inventory
from package_resident_cohort import descriptor
from package_home_ui import clean,pinned,write_store
from build_test_bundle import validate_native_composition
ROOT=Path(__file__).resolve().parents[2]
BASE_SHA='97e73c2aa29ee08c42c10bb3588cf7bb7141ade1a6cba40c24bff5ebbdbec3d7'

def artifact(spec):
 return pinned(Path(spec['path']),spec)

def build(a):
 spec=json.loads(a.spec.read_text());source=clean(ROOT)
 require(spec['x4_source']==source['commit'],'Source identity changed')
 require(spec['scope']=='exact-.59-render-performance','Unexpected integration scope')
 require(len(spec['modules'])==21,'Incomplete renderer and panel replacement scope')
 panel=spec['modules'].get('x4pro-uc8279-fast',{})
 require(panel.get('store_elf')=='panel/driver.elf' and panel.get('store_manifest')=='panel/manifest.json','Missing selected fast panel')
 require(json.loads(artifact(panel['manifest']))['version']=='0.1.15','Wrong fast panel version')
 require(sha(Path(spec['sources']['panel']['path'],'minimal/drivers/x4pro_uc8279_fast/driver.c').read_bytes())=='9d51c7c40f5aea918ffd0911f81a960020b08d66bdd04d6a733741bf60d05f61','Wrong selected panel source')
 require(json.loads((ROOT/'minimal/product.json').read_text())['version']=='0.1.62','Wrong product version')
 for label,row in spec['sources'].items():
  actual=clean(Path(row['path']));require(actual['commit']==row['commit'] and actual['tree']==row['tree'],'Changed source: '+label)
 for name,digest in {**spec['compiled_inputs'],**spec['tool_inputs']}.items():
  require(sha(Path(name).read_bytes())==digest,'Compiled input changed: '+name)
 require(spec['compiled_inputs'],'No compiled source custody')
 runtime=Path(spec['runtime']);tools=Path(spec['tools']);native=Path(spec['native']);native_platform=Path(spec['native_platform'])
 require(not a.output.exists(),'Output exists');a.output.mkdir(parents=True)
 sys.path[:0]=[str(tools/'scripts'),str(runtime/'scripts'),str(ROOT/'minimal/recovery_tools')]
 from read_only_spiffs import read_image
 import recovery_store_admission as admission
 admission.ROOT=tools
 admit_cohort=admission.admit_cohort
 def unpack_image(raw,tool,size):
  return admission.unpack_image(raw,tool,size,expected_tool_sha256=spec['mkspiffs_sha256'])
 from compact_current_elf import compact
 from current_bootfs import build as pack
 from paired_bank_images import initial_bank_state,initial_otadata,parse_record
 from native_binary_exports import exports as bin_exports,reference_tables
 from elftools.elf.elffile import ELFFile
 import verify_update_elf as verify
 raw=artifact(spec['baseline']);require(len(raw)==0x1000000 and sha(raw)==BASE_SHA,'Not delivered .59')
 frozen=read_image(raw[0x2f0000:0x800000],0x510000);files=dict(frozen);old=json.loads(files['cohort.json'])
 require(len(files)==97 and old['version']=='0.1.59' and old['runtime_version']=='0.2.2','Wrong cohort')
 require(pack(frozen)[0]==raw[0x2f0000:0x800000] and unpack_image(raw[0x2f0000:0x800000],a.mkspiffs,0x510000)==frozen,'Baseline dual readback differs')
 candidate=json.loads(artifact(spec['native_candidate']));native_proof=validate_native_composition(native,candidate,runtime,ROOT,native_source_root=native_platform)
 require(candidate['firmware_version']=='0.2.2' and candidate['build_options']=={'app_policy_rows':18,'app_requirement_rows':17,'app_image_cache':True,'usb_phy':True,'retained_wake_bytes':512,'failure_evidence':True},'Native options changed')
 for name,row in spec['native_assets'].items():require(artifact(row)==(native/name).read_bytes(),'Native asset changed: '+name)
 firmware=(native/'firmware.bin').read_bytes();native_elf=(native/'firmware.elf').read_bytes()
 require(raw[0x10000:0x10000+len(firmware)]==firmware and sha(firmware)==old['firmware_sha256'],'Native changed')
 require(raw[0xff2000:0xff4000]==initial_bank_state(firmware,raw[0x2f0000:0x800000],True) and raw[0xff0000:0xff2000]==initial_otadata(),'Baseline pairing/OTA differs')
 names,export_proof=bin_exports(firmware,reference_tables(native_elf));require(names==verify.public_exports(ELFFile(io.BytesIO(native_elf))),'Native BIN/ELF exports differ')
 (a.output/'native-bin-exports.json').write_bytes(encoded(export_proof))
 compactions={};receipts={};expected={'cohort.json'}
 for ident,row in spec['modules'].items():
  elf_path,meta_path=row['store_elf'],row['store_manifest']
  prior=json.loads(frozen[meta_path])['version'];version=json.loads(artifact(row['manifest']))['version']
  require(row['qualified'] and row.get('scope_accepted'), 'Unqualified/out-of-scope module: '+ident)
  blob=artifact(row['elf']);meta=artifact(row['manifest']);receipt=json.loads(artifact(row['build_receipt']))
  # Match both common receipt formats; never trust a version label alone.
  recorded_hash=receipt.get('elf_sha256',receipt.get('sha256'))
  recorded_bytes=receipt.get('elf_bytes',receipt.get('bytes',receipt.get('size_bytes')))
  require(recorded_hash==sha(blob) and recorded_bytes==len(blob),'Target receipt mismatch: '+ident)
  for item in row['qualification_receipts']:artifact(item)
  previous=json.loads(files[meta_path]);current=json.loads(meta)
  require(previous.pop('version')==prior and current['id']==ident and current['version']==version and version!=prior,'Manifest identity/version changed: '+ident)
  require(previous=={k:v for k,v in current.items() if k!='version'},'Manifest authority or metadata changed: '+ident)
  dest=a.output/'compacted'/elf_path;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(blob)
  compactions[ident]=compact(dest,str(a.compiler),debug_path=a.output/'debug-originals'/elf_path)
  files[elf_path]=dest.read_bytes();files[meta_path]=meta;receipts[ident]=receipt;expected.update((elf_path,meta_path))
 cohort=dict(old,version='0.1.62',source_revision=source['commit']);files['cohort.json']=encoded(cohort)
 changed=sorted(n for n in files if files[n]!=frozen[n]);require(changed==sorted(expected) and set(files)==set(frozen),'Unexpected store mutation')
 require(files['boot.json']==frozen['boot.json'],'Boot graph/grants changed')
 for name,role in [('default.elf',1),*[(n,2) for n in json.loads(files['boot.json'])['resident_shell']['foreground']]]:descriptor(files[name],role,check_renderer=False)
 write_store(a.output/'store',files)
 old_admission=verify.admission_source;old_header=verify.cohort_admission_header;old_include=os.environ.get('CPLUS_INCLUDE_PATH');old_sanitize=os.environ.get('SANITIZE');old_asan=os.environ.get('ASAN_OPTIONS')
 os.environ['CPLUS_INCLUDE_PATH']=str(runtime/'src')+(os.pathsep+old_include if old_include else '')
 header='#define RISC_APP_REQUIREMENT_ROWS 17\n#define RISC_APP_POLICY_ROWS 18\n#include "'+str(runtime/'src/runtime/drivers/NativeProviderPolicyValidationV1.h')+'"\n'
 def with_policy(text):
  body,roles,driver=old_admission(text);return header+body,roles,driver
 verify.admission_source=with_policy
 try:
  strict=[]
  for name in sorted(n for n in files if n.endswith('.elf')):
   row=verify.verify(runtime,native_elf,files[name],provider='/' in name);row['path']=name;strict.append(row)
  verify.admission_source=old_admission;verify.cohort_admission_header=lambda rt,elf:header+old_header(rt,elf)
  admissions=[]
  for mode in ('0','1'):
   os.environ['SANITIZE']=mode;os.environ['ASAN_OPTIONS']='detect_leaks=0';row=admit_cohort(runtime,native_elf,files,files,app_policy_rows=18);row['sanitized']=mode=='1';row['scope']='self-admission for full flash, not preserving-update admission';admissions.append(row)
 finally:
  verify.admission_source=old_admission;verify.cohort_admission_header=old_header
  for key,value in [('CPLUS_INCLUDE_PATH',old_include),('SANITIZE',old_sanitize),('ASAN_OPTIONS',old_asan)]:
   if value is None:os.environ.pop(key,None)
   else:os.environ[key]=value
 require(len(strict)==47 and all(r['cohort_validated'] and r['elf_count']==47 and r['hardware_calls']==r['storage_calls']==0 for r in admissions),'Incomplete admission')
 require({p.relative_to(a.output/'store').as_posix():p.read_bytes() for p in (a.output/'store').rglob('*') if p.is_file()}==files,'Admission changed store')
 (a.output/'strict-elf-admission.json').write_bytes(encoded(strict))
 fs,capacity=pack(files);require(pack(files)[0]==fs and read_image(fs,0x510000)==files and unpack_image(fs,a.mkspiffs,0x510000)==files,'Final dual readback/determinism failed')
 bank=initial_bank_state(firmware,fs,True);pair=parse_record(bank[:96],True);require(pair[6]==hashlib.sha256(firmware).digest() and pair[7]==hashlib.sha256(fs).digest(),'Pair SHA/CRC mismatch')
 image=bytearray(raw);image[0x2f0000:0x800000]=fs;image[0xff2000:0xff4000]=bank;regions=[(0,0x2f0000),(0x800000,0xff2000),(0xff4000,0x1000000)]
 require(all(image[lo:hi]==raw[lo:hi] for lo,hi in regions) and read_image(bytes(image[0x2f0000:0x800000]),0x510000)==files,'Final frozen regions/readback differ')
 name='X4-0.1.62-Renderer-Performance-Panel-0.1.15-full-0x0.bin';(a.output/name).write_bytes(image);(a.output/'bootfs.bin').write_bytes(fs)
 report={'schema':'x4.renderer-performance-cohort','schema_version':1,'product_version':'0.1.62','source':source,'inputs':spec,'native_proof':native_proof,'native_unchanged':True,'native_firmware_sha256':sha(firmware),'native_elf_sha256':sha(native_elf),'native_export_count':len(names),'cohort':cohort,'image':{'name':name,'bytes':len(image),'sha256':sha(image),'flash_offset':0},'build_receipts':receipts,'compactions':compactions,'cohort_admission':admissions,'strict_admission':strict,'strict_elf_count':len(strict),'store_generator':capacity,'store_files':digest_inventory(files),'changed_store_paths':changed,'preserved_store_paths':sorted(set(files)-expected),'preserved_regions':[{'offset':lo,'bytes':hi-lo,'sha256':sha(raw[lo:hi])} for lo,hi in regions],'boot_graph_and_grants_unchanged':True,'paired_sha_crc_verified':True,'hardware_tested':False,'inclusions':spec['inclusions'],'excluded':spec['excluded'],'first_install_warning':'Full 16 MiB image at 0x0 overwrites internal settings, Bluetooth bonds and AppData. Back up first. Removable SD contents are not included.'}
 (a.output/'build-custody.json').write_bytes(encoded(report));print(json.dumps(report['image']))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('spec','compiler','mkspiffs','output'):p.add_argument('--'+n,type=Path,required=True)
 build(p.parse_args())
