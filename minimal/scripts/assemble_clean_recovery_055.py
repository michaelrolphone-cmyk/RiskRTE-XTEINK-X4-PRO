#!/usr/bin/env python3
"""Assemble only a completed fresh-build ledger into the full X4 test image."""
import argparse,hashlib,importlib.util,io,json,os,sys
from pathlib import Path
from build_clean_recovery_055 import require,sha,encoded,inside,validate
ROOT=Path(__file__).resolve().parents[2]

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def assemble(ledger_path,output,mkspiffs,compiler):
    ledger_path=Path(ledger_path).resolve(strict=True);ledger=json.loads(ledger_path.read_text());root=Path(ledger['build_root']).resolve(strict=True);plan=ledger['plan'];out=Path(output).absolute()
    require(ledger_path==root/'build-ledger.json' and ledger.get('completed') is True,'Incomplete or displaced build ledger')
    require(sha(root/'plan.json')==ledger['plan_sha256'],'Frozen plan changed')
    require(not out.exists() and not out.is_symlink(),'Assembly output must be absent')
    validate(plan,root)
    def artifact(name):
        require(name in ledger['artifacts'],'Undeclared product artifact: '+name)
        p=inside(root,name);row=ledger['artifacts'][name]
        require(p.is_file() and not p.is_symlink() and p.stat().st_size==row['bytes'] and sha(p)==row['sha256'] and p.stat().st_mtime_ns==row['mtime_ns'],'Changed fresh build artifact: '+name)
        return p.read_bytes()
    runtime=Path(plan['sources']['runtime']['path']);watch=Path(plan['tools']['watch'])
    sys.path[:0]=[str(watch/'scripts'),str(runtime/'scripts'),str(ROOT/'minimal/recovery_tools')]
    from current_bootfs import build as pack
    from read_only_spiffs import read_image
    from compact_current_elf import compact
    from paired_bank_images import initial_bank_state,initial_otadata,parse_record
    from native_binary_exports import exports as bin_exports,reference_tables
    from elftools.elf.elffile import ELFFile
    import verify_update_elf as verify
    import recovery_store_admission as admission
    admission.ROOT=watch
    native={name:artifact(path) for name,path in plan['native'].items()}
    require(set(native)>={'firmware.bin','firmware.elf','bootloader.bin','partitions.bin','appdata.bin','candidate.json'},'Native output set incomplete')
    candidate=json.loads(native['candidate.json'])
    from build_test_bundle import validate_native_composition,cohort_identity
    stage=inside(root,plan['native']['candidate.json']).parent
    native_proof=validate_native_composition(stage,candidate,runtime,ROOT)
    require(candidate['firmware_version']=='0.1.106' and candidate['build_options']=={'app_policy_rows':18,'app_requirement_rows':17,'app_image_cache':True,'usb_phy':True,'retained_wake_bytes':512,'failure_evidence':True},'Wrong native build selection')
    names,export_proof=bin_exports(native['firmware.bin'],reference_tables(native['firmware.elf']))
    require(names==verify.public_exports(ELFFile(io.BytesIO(native['firmware.elf']))),'Native BIN/ELF exports differ')
    out.mkdir(parents=True);files={};compactions={}
    for module in plan['modules']:
        data=artifact(module['elf']);meta=artifact(module['manifest']);manifest=json.loads(meta)
        require(manifest['id']==module['id'],'Wrong module identity: '+module['store_elf'])
        require(manifest['version']==module['version'],'Wrong source-selected version: '+module['id'])
        dest=out/'compacted'/module['store_elf'];dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
        compactions[module['id']]=compact(dest,str(compiler),debug_path=out/'debug-originals'/module['store_elf'])
        files[module['store_elf']]=dest.read_bytes();files[module['store_manifest']]=meta
    require(len(files)==94,'Duplicate module path')
    for name in ('board.json','boot.json'):
        src=Path(plan['configuration'][name]);require(sha(src)==plan['source_files'][str(src)],'Configuration source changed');files[name]=src.read_bytes()
    product=json.loads((ROOT/'minimal/product.json').read_text());revision=plan['sources']['x4']['commit']
    require(product['version']=='0.1.55','Wrong source product version')
    files['cohort.json']=encoded(cohort_identity(product,candidate,native['firmware.bin'],revision))
    require(len(files)==97,'Unexpected store member set')
    boot=json.loads(files['boot.json']);require(len(boot['app_capabilities'])==21 and len(boot['drivers'])==26,'Wrong cohort graph cardinality')
    home=next(x for x in boot['app_capabilities'] if x['manifest']=='default.json')
    require(len(home['grants'])==18 and home['grants'][-1]=={'api':1,'capability':'storage.app-data','instance_id':62},'Crash Home authority differs')
    from package_resident_cohort import descriptor
    for name,role in [('default.elf',1),*[(n,2) for n in boot['resident_shell']['foreground']]]:descriptor(files[name],role,check_renderer=False)
    old_source=verify.admission_source;old_header=verify.cohort_admission_header
    prefix='#define RISC_APP_REQUIREMENT_ROWS 17\n#define RISC_APP_POLICY_ROWS 18\n#include "'+str(runtime/'src/runtime/drivers/NativeProviderPolicyValidationV1.h')+'"\n'
    def with_policy(text):
        body,roles,driver=old_source(text);return prefix+body,roles,driver
    saved={k:os.environ.get(k) for k in ('CPLUS_INCLUDE_PATH','SANITIZE','ASAN_OPTIONS')}
    os.environ['CPLUS_INCLUDE_PATH']=str(runtime/'src')
    try:
        verify.admission_source=with_policy
        strict=[]
        for name in sorted(n for n in files if n.endswith('.elf')):
            row=verify.verify(runtime,native['firmware.elf'],files[name],provider='/' in name);row['path']=name;strict.append(row)
        verify.admission_source=old_source;verify.cohort_admission_header=lambda rt,elf:prefix+old_header(rt,elf)
        admissions=[]
        for mode in ('0','1'):
            os.environ['SANITIZE']=mode;os.environ['ASAN_OPTIONS']='detect_leaks=0'
            row=admission.admit_cohort(runtime,native['firmware.elf'],files,files,app_policy_rows=18);row['sanitized']=mode=='1';row['scope']='self-admission of full-flash cohort; not old-to-new update admission';admissions.append(row)
    finally:
        verify.admission_source=old_source;verify.cohort_admission_header=old_header
        for key,value in saved.items():
            if value is None:os.environ.pop(key,None)
            else:os.environ[key]=value
    require(len(strict)==47 and all(x['cohort_validated'] and x['elf_count']==47 and x['hardware_calls']==x['storage_calls']==0 for x in admissions),'Admission incomplete')
    fs,capacity=pack(files)
    require(pack(files)[0]==fs and read_image(fs,0x510000)==files and admission.unpack_image(fs,mkspiffs,0x510000)==files,'Independent store readback/determinism failed')
    bank=initial_bank_state(native['firmware.bin'],fs,True);pair=parse_record(bank[:96],True)
    require(pair[6]==hashlib.sha256(native['firmware.bin']).digest() and pair[7]==hashlib.sha256(fs).digest(),'Pair SHA/CRC differs')
    parts=[(0,native['bootloader.bin'],'bootloader'),(0x8000,native['partitions.bin'],'partitions'),(0x10000,native['firmware.bin'],'native'),(0x270000,native['appdata.bin'],'appdata'),(0x2f0000,fs,'bootfs'),(0xff0000,initial_otadata(),'otadata'),(0xff2000,bank,'paired-bank')]
    image=bytearray(b'\xff'*0x1000000);ranges=[]
    for offset,data,label in parts:
        require(offset+len(data)<=len(image) and all(offset+len(data)<=lo or offset>=hi for lo,hi in ranges),'Partition overlap/overflow')
        image[offset:offset+len(data)]=data;ranges.append((offset,offset+len(data)))
    require(read_image(bytes(image[0x2f0000:0x800000]),0x510000)==files,'Full image store readback differs')
    for name,data in files.items():p=out/'store'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
    name='X4-0.1.55-Recovered-source-full-0x0.bin';(out/name).write_bytes(image);(out/'bootfs.bin').write_bytes(fs)
    result={'schema':'x4.clean-recovery-image','product_version':'0.1.55','source_revision':revision,'ledger_sha256':sha(ledger_path),'plan_sha256':ledger['plan_sha256'],'image':{'name':name,'bytes':len(image),'sha256':hashlib.sha256(image).hexdigest(),'flash_offset':0},'native_proof':native_proof,'native_exports':export_proof,'compactions':compactions,'strict_admission':strict,'cohort_admission':admissions,'store_generator':capacity,'store_files':{n:{'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()} for n,b in sorted(files.items())},'parts':[{'offset':o,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'kind':label} for o,b,label in parts],'old_product_inputs':False,'hardware_tested':False,'warning':'Full0x0 installation overwrites internal settings, bonds and AppData. Back up first. Removable SD contents are not included.'}
    (out/'build-custody.json').write_bytes(encoded(result));print(json.dumps(result['image']))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('ledger','output','mkspiffs','compiler'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();assemble(a.ledger,a.output,a.mkspiffs,a.compiler)
