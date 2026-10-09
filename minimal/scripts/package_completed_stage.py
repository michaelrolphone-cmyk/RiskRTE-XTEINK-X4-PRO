#!/usr/bin/env python3
"""Package the completed .38 app stage on the unchanged qualified .37 native."""
import argparse,json,shutil,subprocess,sys
from pathlib import Path
from build_recovered_diagnostic import require,sha,encoded,inventory,digest_inventory,load
ROOT=Path(__file__).resolve().parents[2]
def build(a):
    require(not a.output.exists(),'Output exists')
    require(not subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain'],text=True).strip(),'Clean source required')
    revision=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
    product=json.loads((ROOT/'minimal/product.json').read_text());require(product['version']=='0.1.38','Wrong product')
    stage=json.loads((a.stage/'integration.json').read_text()); files=inventory(a.stage/'store')
    require(stage['admission']['cohort_validated'] and stage['admission']['elf_count']==44 and len(files)==91,'Stage not admitted')
    require(digest_inventory(files)==stage['store_files'],'Stage files changed')
    base=json.loads((a.baseline/'build-custody.json').read_text())
    raw=(a.baseline/base['image']['name']).read_bytes()
    require(sha(raw)=='23375b19bcd9f5d6a58104beed53ffd20fc758242591b0357a0171795815b292' and len(raw)==0x1000000,'Frozen .37 image differs')
    sys.path.insert(0,str(a.watch/'scripts'))
    from current_bootfs import build as pack
    from read_only_spiffs import read_image
    from compact_current_elf import compact
    from check_runtime_store_admission import admit_cohort
    native=a.baseline/'native';candidate=json.loads((native/'candidate.json').read_text())
    platform=load('completed_platform',a.native_product/'minimal/scripts/build_test_bundle.py')
    native_proof=platform.validate_native_composition(native,candidate,a.runtime,a.native_product)
    require(candidate['firmware_version']=='0.1.81' and candidate['build_options']=={'app_image_cache':True,'app_policy_rows':17,'usb_phy':True},'Native selection changed')
    a.output.mkdir(parents=True)
    compactions={}
    for name in ('springboard','ble_touchpad','ble_buttons','gameboy'):
        temp=a.output/'compacted'/(name+'.elf');temp.parent.mkdir(parents=True,exist_ok=True);temp.write_bytes(files[name+'.elf'])
        compactions[name]=compact(temp,str(a.compiler),debug_path=a.output/'debug-originals'/(name+'.elf'))
        files[name+'.elf']=temp.read_bytes()
    firmware=(native/'firmware.bin').read_bytes()
    cohort=platform.cohort_identity(product,candidate,firmware,revision);files['cohort.json']=encoded(cohort)
    admission=admit_cohort(a.runtime,(native/'firmware.elf').read_bytes(),files,files,app_policy_rows=17)
    bootfs,capacity=pack(files);require(read_image(bootfs,0x510000)==files,'SPIFFS roundtrip differs')
    banks=load('completed_banks',a.runtime/'scripts/paired_bank_images.py')
    loader=(native/'bootloader.bin').read_bytes();table=(native/'partitions.bin').read_bytes();appdata=(native/'appdata.bin').read_bytes()
    require(loader==raw[:len(loader)] and table==raw[0x8000:0x8000+len(table)] and firmware==raw[0x10000:0x10000+len(firmware)] and appdata==raw[0x270000:0x2f0000],'Baseline native/startup changed')
    parts=[(0,loader),(0x8000,table),(0x10000,firmware),(0x270000,appdata),(0x2f0000,bootfs),(0xff0000,banks.initial_otadata()),(0xff2000,banks.initial_bank_state(firmware,bootfs,True))]
    image=bytearray(b'\xff'*0x1000000);occupied=[]
    for offset,data in parts:
        require(offset+len(data)<=len(image) and all(offset+len(data)<=x or offset>=y for x,y in occupied),'Partition overlap')
        image[offset:offset+len(data)]=data;occupied.append((offset,offset+len(data)))
    name='xteink-x4-pro-0.1.38-gameboy-touch-grid-first-install.bin'
    (a.output/name).write_bytes(image);(a.output/'bootfs.bin').write_bytes(bootfs)
    for path,data in files.items():
        p=a.output/'store'/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
    shutil.copytree(native,a.output/'native');shutil.copytree(a.baseline/'licenses',a.output/'licenses')
    shutil.copytree(a.stage/'inputs',a.output/'inputs')
    record={'schema':'x4.completed-app-cohort','schema_version':1,'product':product,'source_revision':revision,
        'baseline':base['image'],'stage':stage,'compactions':compactions,'cohort':cohort,'native_proof':native_proof,
        'admission':admission,'store_generator':capacity,'store_files':digest_inventory(files),
        'partitions':[{'offset':o,'bytes':len(b),'sha256':sha(b)} for o,b in parts],
        'image':{'name':name,'bytes':len(image),'sha256':sha(image)},'hardware_tested':False,
        'known_unresolved':['Windows MSC disk access hang','USB mode-exit serial restoration and unplug behavior','Resident shell and requested desk/Points changes unfinished','New BLE reconnect/forget report under investigation']}
    (a.output/'build-custody.json').write_bytes(encoded(record));print(json.dumps(record['image']))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('stage','baseline','runtime','native-product','watch','compiler','output'):p.add_argument('--'+k,type=Path,required=True)
    build(p.parse_args())
