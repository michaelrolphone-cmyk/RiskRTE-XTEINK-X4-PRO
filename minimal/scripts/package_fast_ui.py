#!/usr/bin/env python3
"""Compose the qualified interactive UI cohort over exact delivered X4 .46."""
import argparse,json,shutil,sys
from pathlib import Path
from build_recovered_diagnostic import require,sha,encoded,inventory,digest_inventory,load
from package_resident_cohort import descriptor
import prepare_native_runtime as native
import resident_native
ROOT=Path(__file__).resolve().parents[2]
def pinned(path,expected):
 data=path.read_bytes();require(expected=={'bytes':len(data),'sha256':sha(data)},'Pinned input differs: '+str(path));return data
def build(a):
 require(not a.output.exists(),'Output already exists')
 source=native.clean_source(ROOT);spec=json.loads((ROOT/'minimal/apps/fast-ui-cohort.json').read_text());product=json.loads((ROOT/'minimal/product.json').read_text())
 require(product['version']=='0.1.47','Wrong product version')
 base=json.loads(pinned(a.baseline/'build-custody.json',spec['baseline']['receipt']))
 require(base['source_revision']==spec['baseline']['source'] and base['product']['version']=='0.1.46','Baseline identity differs')
 old_image=pinned(a.baseline/base['image']['name'],{k:spec['baseline']['image'][k] for k in ('bytes','sha256')})
 original=inventory(a.baseline/'store');require(digest_inventory(original)==base['store_files'],'Frozen store differs');files=dict(original)
 require(native.clean_source(a.watch)['commit']==spec['packaging_source'],'Packaging pin differs')
 sys.path.insert(0,str(a.watch/'scripts'))
 from check_runtime_store_admission import admit_cohort
 from compact_current_elf import compact
 from current_bootfs import build as pack
 from read_only_spiffs import read_image
 import verify_update_elf
 require(read_image(old_image[0x2f0000:0x800000],0x510000)==original,'Baseline SPIFFS bytes differ')
 candidate=json.loads((a.native/'candidate.json').read_text())
 native_proof=resident_native.validate(a.native,candidate,a.runtime,native_source_root=a.native_platform)
 require(native_proof['failure_evidence_proof']['enabled'],'Missing failure evidence')
 require(b'RISC_RUNTIME_VERSION:0.1.98\0' in (a.native/'firmware.bin').read_bytes(),'Wrong Runtime')
 require(native_proof['runtime_options_proof']['retained_wake']['payload_bytes']==512,'Retained budget differs')
 boot=json.loads(files['boot.json']);policies={p['manifest']:p for p in boot['app_capabilities']}
 require(set(policies)=={n+'.json' for n in spec['apps']} and len(policies)==21,'Incomplete application inventory')
 require(boot['resident_shell']==base['preserved_resident_policy'],'Resident policy differs')
 a.output.mkdir(parents=True);receipts={};memory={};compactions={}
 for name,e in spec['apps'].items():
  folder=Path(e['directory']);blob=(folder/(name+'.elf')).read_bytes();meta=(folder/(name+'.json')).read_bytes();record=(folder/e['receipt']).read_bytes()
  require(sha(blob)==e['elf_sha256'] and sha(meta)==e['manifest_sha256'] and sha(record)==e['receipt_sha256'],'Qualified target differs: '+name)
  m=json.loads(meta);prior=json.loads(original[name+'.json']);r=json.loads(record)
  require(m['version']==e['version'] and m['file_name']==name+'.elf' and m['requires']==prior['requires'],'Manifest authority differs: '+name)
  require(tuple(map(int,m['version'].split('.')))>tuple(map(int,prior['version'].split('.'))),'Version did not advance: '+name)
  if name!='gameboy':
   require({(g['capability'],g['api'],g['instance_id']) for g in e['grants']}=={(g['capability'],g['api'],g['instance_id']) for g in policies[name+'.json']['grants']},'Grant binding changed: '+name)
   flags=r.get('build_defines',r.get('defines',[]));require('-DPORTABLE_ALARM_TERMINAL_RETENTION' in flags,'Missing terminal alarm guard: '+name)
   hashes={**r.get('source_sha256',{}),**r.get('compiled_dependencies_sha256',{}),**r.get('desk_sources',{})}
   require(any(k.endswith('lib/PortableApps/src/adapter.c') and v==spec['fast_adapter_sha256'] for k,v in hashes.items()),'Missing selected fast adapter: '+name)
   require(not r.get('source_dirty',False) and not r.get('working_tree_dirty',False) and not r.get('system_dirty',False),'Dirty input: '+name)
   memory[name]=descriptor(blob,1 if name=='default' else 2)
  else:
   require(r['source']==e['source'] and not r['dirty'],'GameBoy source custody differs')
  receipts[name]=r
  path=a.output/'compacted'/(name+'.elf');path.parent.mkdir(exist_ok=True);path.write_bytes(blob)
  compactions[name]=compact(path,str(a.compiler),debug_path=a.output/'debug-originals'/(name+'.elf'))
  files[name+'.elf']=path.read_bytes();files[name+'.json']=meta
  if name!='gameboy':descriptor(files[name+'.elf'],1 if name=='default' else 2,check_renderer=False)
  if (folder/'licenses').is_dir():shutil.copytree(folder/'licenses',a.output/'licenses'/name,dirs_exist_ok=True)
  target=a.output/'build-records'/name;target.mkdir(parents=True);(target/e['receipt']).write_bytes(record)
 platform=load('interactive_platform',ROOT/'minimal/scripts/build_test_bundle.py')
 firmware=(a.native/'firmware.bin').read_bytes();cohort=platform.cohort_identity(product,candidate,firmware,source['commit']);files['cohort.json']=encoded(cohort)
 require(files['boot.json']==original['boot.json'],'Boot settings/grants/providers changed')
 allowed={'cohort.json'}|{n+s for n in spec['apps'] for s in ('.elf','.json')}
 changed={n for n in files if files[n]!=original.get(n)};require(changed<=allowed and set(files)==set(original),'Unexpected store changes')
 policy_header='runtime/drivers/NativeProviderPolicyValidationV1.h';include='#include "'+policy_header+'"\n'
 require(include in (a.runtime/'src/ports/esp32s3/NativeBankStore.cpp').read_text(),'Native admission dependency differs')
 original_header=verify_update_elf.cohort_admission_header
 verify_update_elf.cohort_admission_header=lambda runtime,elf:include+original_header(runtime,elf)
 try:admission=admit_cohort(a.runtime,(a.native/'firmware.elf').read_bytes(),files,files,app_policy_rows=17)
 finally:verify_update_elf.cohort_admission_header=original_header
 require(admission['cohort_validated'] and admission['elf_count']==44 and admission['hardware_calls']==admission['storage_calls']==0,'Full store admission failed')
 bootfs,capacity=pack(files);require(read_image(bootfs,0x510000)==files,'SPIFFS roundtrip differs')
 banks=load('interactive_banks',a.runtime/'scripts/paired_bank_images.py')
 parts=[(0,(a.native/'bootloader.bin').read_bytes()),(0x8000,(a.native/'partitions.bin').read_bytes()),(0x10000,firmware),(0x270000,(a.native/'appdata.bin').read_bytes()),(0x2f0000,bootfs),(0xff0000,banks.initial_otadata()),(0xff2000,banks.initial_bank_state(firmware,bootfs,True))]
 image=bytearray(b'\xff'*0x1000000);occupied=[]
 for offset,data in parts:
  require(offset+len(data)<=len(image) and all(offset+len(data)<=x or offset>=y for x,y in occupied),'Partition overlap/overflow')
  image[offset:offset+len(data)]=data;occupied.append((offset,offset+len(data)))
 for offset,data in parts:require(image[offset:offset+len(data)]==data,'Partition readback differs')
 for start,end in [(0,0x10000),(0x270000,0x2f0000),(0x800000,0xff2000),(0xff4000,0x1000000)]:require(image[start:end]==old_image[start:end],'Preserved region differs')
 require(read_image(bytes(image[0x2f0000:0x800000]),0x510000)==files,'Full image store readback differs')
 name='xteink-x4-pro-0.1.47-fast-interactive-first-install.bin';(a.output/name).write_bytes(image);(a.output/'bootfs.bin').write_bytes(bootfs)
 for path,data in files.items():p=a.output/'store'/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
 shutil.copytree(a.native,a.output/'native');shutil.copytree(a.baseline/'licenses',a.output/'licenses/baseline')
 record={'schema':'x4.fast-interactive-cohort','schema_version':1,'product':product,'source_revision':source['commit'],'source_tree':source['tree'],'baseline':spec['baseline'],'inputs':spec['apps'],'build_receipts':receipts,'app_allocated_section_bytes':memory,'compactions':compactions,'native_proof':native_proof,'cohort':cohort,'admission':admission,'store_generator':capacity,'store_files':digest_inventory(files),'changed_paths':sorted(changed),'preserved_boot_sha256':sha(files['boot.json']),'resident_policy':boot['resident_shell'],'clock_retained_payload':{'encoded_bytes':408,'native_capacity_bytes':512},'partitions':[{'offset':o,'bytes':len(b),'sha256':sha(b)} for o,b in parts],'image':{'name':name,'bytes':len(image),'sha256':sha(image)},'hardware_tested':False,'known_unresolved':spec['omissions']+base['known_unresolved']}
 (a.output/'build-custody.json').write_bytes(encoded(record));print(json.dumps(record['image']))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for k in ('baseline','runtime','native','native-platform','watch','compiler','output'):p.add_argument('--'+k,type=Path,required=True)
 build(p.parse_args())
