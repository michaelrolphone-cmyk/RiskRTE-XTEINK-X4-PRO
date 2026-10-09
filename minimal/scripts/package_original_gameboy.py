#!/usr/bin/env python3
"""Compose the original GameBoy UI test on the exact delivered .40 native/store."""
import argparse,json,shutil,subprocess,sys
from pathlib import Path
from build_recovered_diagnostic import require,sha,encoded,inventory,digest_inventory,load
ROOT=Path(__file__).resolve().parents[2]
def build(a):
 require(not a.output.exists(),'Output already exists')
 require(not subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain'],text=True).strip(),'Product source is dirty')
 revision=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
 product=json.loads((ROOT/'minimal/product.json').read_text());require(product['version']=='0.1.42','Wrong product version')
 base=json.loads((a.baseline/'build-custody.json').read_text());raw=(a.baseline/base['image']['name']).read_bytes()
 require(sha(raw)=='5d71f28397f9bf1e3d293b6bcf255b5c67e78ac655a2f0790bd5a5c108ae73ce' and len(raw)==0x1000000,'Frozen .40 image differs')
 app=json.loads((a.gameboy/'build.json').read_text());elf=(a.gameboy/'gameboy.elf').read_bytes()
 require(not app['dirty'] and sha(elf)==app['elf']['sha256'] and app['version']=='1.3.19','Unqualified source or changed app artifact')
 files=inventory(a.baseline/'store');require(digest_inventory(files)==base['store_files'],'Baseline store changed')
 files['gameboy.elf']=elf;files['gameboy.json']=(a.gameboy/'gameboy.json').read_bytes()
 boot=json.loads(files['boot.json']);rows=[r for r in boot['app_capabilities'] if r['manifest']=='gameboy.json'];require(len(rows)==1,'GameBoy policy absent or ambiguous')
 rows[0]['grants'] += [{'capability':n,'api':1,'instance_id':i} for n,i in [('board.battery',7),('runtime.realtime',0),('storage.key-value',1)]]
 require(len(rows[0]['grants'])==8,'Unexpected GameBoy policy');files['boot.json']=encoded(boot)
 native=a.baseline/'native';firmware=(native/'firmware.bin').read_bytes();cohort=json.loads(files['cohort.json']);cohort['version']=product['version'];cohort['source_revision']=revision;files['cohort.json']=encoded(cohort)
 sys.path.insert(0,str(a.watch/'scripts'))
 from current_bootfs import build as pack
 from read_only_spiffs import read_image
 from compact_current_elf import compact
 from check_runtime_store_admission import admit_cohort
 a.output.mkdir(parents=True)
 compacted=a.output/'gameboy.elf';compacted.write_bytes(elf)
 compact_record=compact(compacted,str(a.compiler),debug_path=a.output/'debug-original-gameboy.elf');files['gameboy.elf']=compacted.read_bytes()
 admission=admit_cohort(a.runtime,(native/'firmware.elf').read_bytes(),files,files,app_policy_rows=17)
 require(admission['cohort_validated'] and admission['elf_count']==44,'Full cohort admission failed')
 bootfs,capacity=pack(files);require(read_image(bootfs,0x510000)==files,'SPIFFS byte readback differs')
 banks=load('original_gb_banks',a.runtime/'scripts/paired_bank_images.py')
 parts=[(0,(native/'bootloader.bin').read_bytes()),(0x8000,(native/'partitions.bin').read_bytes()),(0x10000,firmware),(0x270000,(native/'appdata.bin').read_bytes()),(0x2f0000,bootfs),(0xff0000,banks.initial_otadata()),(0xff2000,banks.initial_bank_state(firmware,bootfs,True))]
 image=bytearray(b'\xff'*0x1000000);occupied=[]
 for offset,data in parts:
  require(offset+len(data)<=len(image) and all(offset+len(data)<=x or offset>=y for x,y in occupied),'Partition overlap')
  image[offset:offset+len(data)]=data;occupied.append((offset,offset+len(data)))
 for offset,data in parts:
  require(image[offset:offset+len(data)]==data,'Image readback mismatch')
 for offset,data in parts[:4]:require(raw[offset:offset+len(data)]==data,'Native/startup/app-data initial state changed')
 name='xteink-x4-pro-0.1.42-gameboy-startup-fix-first-install.bin';(a.output/name).write_bytes(image);(a.output/'bootfs.bin').write_bytes(bootfs)
 for path,data in files.items():
  p=a.output/'store'/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
 shutil.copytree(native,a.output/'native');shutil.copytree(a.baseline/'licenses',a.output/'licenses');shutil.copytree(a.gameboy,a.output/'gameboy-build')
 changed=sorted(p for p in files if files[p]!=(a.baseline/'store'/p).read_bytes())
 require(changed==['boot.json','cohort.json','gameboy.elf','gameboy.json'],'Unexpected store delta')
 record={'schema':'x4.original-gameboy-ui-test','schema_version':1,'product':product,'source_revision':revision,'baseline':base['image'],'gameboy':app,'compaction':compact_record,'cohort':cohort,'native_proof':base['native_proof'],'admission':admission,'store_generator':capacity,'store_files':digest_inventory(files),'partitions':[{'offset':o,'bytes':len(b),'sha256':sha(b)} for o,b in parts],'image':{'name':name,'bytes':len(image),'sha256':sha(image)},'changed_from_040':changed,'hardware_tested':False,'pending_checks':['Hardware original UI/touch and gameplay validation'],'known_unresolved':base['known_unresolved']}
 (a.output/'build-custody.json').write_bytes(encoded(record));print(json.dumps(record['image']))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for k in ('baseline','gameboy','runtime','watch','compiler','output'):p.add_argument('--'+k,type=Path,required=True)
 build(p.parse_args())
