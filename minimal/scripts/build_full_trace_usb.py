#!/usr/bin/env python3
"""Build the full boot-trace/SD-export test image from frozen .30 inputs."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
from build_recovered_diagnostic import require, sha, encoded, load, inventory, digest_inventory

ROOT = Path(__file__).resolve().parents[2]
BASELINE_FILE = 'xteink-x4-pro-0.1.30-sd-bootlog-first-install.bin'
BASELINE_SHA = 'f340843fae3774fd45c3366569ddbe9cca00df3cd2079537718b72b8465a0413'

def clean(root):
    require(not subprocess.check_output(['git','-C',str(root),'status','--porcelain'],text=True).strip(),
            'Clean committed source required: '+str(root))
    return subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()

def check_sources(root, hashes):
    for name, digest in hashes.items():
        path = Path(name)
        require(not path.is_absolute() and '..' not in path.parts, 'Unsafe source receipt path')
        require(sha((root/path).read_bytes()) == digest, 'Source differs: '+name)

def build(a):
    require(not a.output.exists(), 'Output exists')
    original = json.loads((a.baseline/'build-custody.json').read_text())
    raw = (a.baseline/BASELINE_FILE).read_bytes()
    require(len(raw)==0x1000000 and sha(raw)==BASELINE_SHA, 'Frozen .30 image differs')
    files = inventory(a.baseline/'store')
    require(digest_inventory(files)==original['store_files'] and len(files)==85, 'Frozen .30 store differs')
    frozen = dict(files)
    revision, system, reader = clean(ROOT), clean(a.system), clean(a.reader)
    springboard_system = clean(a.springboard_system)
    product = json.loads((ROOT/'minimal/product.json').read_text())
    require(product['version']=='0.1.36', 'Wrong product version')
    sys.path.insert(0,str(a.watch/'scripts'))
    from check_runtime_store_admission import admit_cohort
    from current_bootfs import build as pack_store
    from read_only_spiffs import read_image
    from compact_current_elf import compact
    require(read_image(raw[0x2f0000:0x800000],0x510000)==files, 'Baseline image/store differs')
    platform = load('usb_platform',a.native_product/'minimal/scripts/build_test_bundle.py')
    native = json.loads((a.native/'candidate.json').read_text())
    native_proof = platform.validate_native_composition(a.native,native,a.runtime,a.native_product)
    require(native['firmware_version']=='0.1.81' and native['stage_logs'] is True, 'Wrong native diagnostic selection')
    require(native['build_options']=={'app_image_cache':True,'app_policy_rows':17,'usb_phy':True},
            'USB/native options differ')
    a.output.mkdir(parents=True)
    inputs = {}
    def install_app(name, folder, manifest, blob, receipt):
        dest=a.output/'replacement-inputs'/name
        shutil.copytree(folder,dest)
        packed=dest/(name+'.elf')
        compaction=compact(packed,str(a.compiler),debug_path=a.output/'debug-originals'/(name+'.elf'))
        files[name+'.elf']=packed.read_bytes()
        files[name+'.json']=encoded(manifest)
        inputs[name]={'receipt':receipt,'compaction':compaction,'packaged_sha256':sha(files[name+'.elf'])}
    sb=json.loads((a.springboard/'springboard.json').read_text())
    sb_blob=(a.springboard/'springboard.elf').read_bytes()
    sb_receipt=json.loads((a.springboard/'x4-native-app.json').read_text())
    sb_build=json.loads((a.springboard/'springboard-build-record.json').read_text())
    require(sb['version']=='1.7.17' and sb['file_name']=='springboard.elf', 'Wrong Springboard')
    require(sb_receipt['source_revision']==springboard_system and sb_receipt['system_source_revision']==springboard_system and
            sb_receipt['working_tree_dirty'] is False, 'Springboard source differs')
    require(sha(sb_blob)==sb_receipt['elf_sha256'] and len(sb_blob)==sb_receipt['elf_bytes'], 'Springboard bytes differ')
    require(sb_receipt['version']==sb['version'] and sb_receipt['requires']==sb['requires'], 'Springboard receipt differs')
    catalog=json.loads((ROOT/'minimal/apps/catalog.json').read_text())
    require(sb_build['catalog']['count']==18 and sb_build['catalog']['apps']==catalog['apps'], 'Springboard catalog differs')
    require(sb_build['repository_commit']==springboard_system and sb_build['working_tree_dirty'] is False and
            sb_build['sha256']==sha(sb_blob) and sb_build['size_bytes']==len(sb_blob) and
            '-DPORTABLE_QUICK_USB_TRANSFER' in sb_build['build_defines'] and sb_build['quick_usb_transfer'] is True,
            'Springboard USB entry/build differs')
    check_sources(a.springboard_system,sb_build['source_sha256'])
    require(sb['requires']==json.loads(files['springboard.json'])['requires'], 'Springboard authority changed')
    install_app('springboard',a.springboard,sb,sb_blob,sb_receipt)
    manifest=json.loads((a.transfer/'usb_sd_transfer.json').read_text())
    blob=(a.transfer/'usb_sd_transfer.elf').read_bytes()
    receipt=json.loads((a.transfer/'usb_sd_transfer-build-record.json').read_text())
    contract=load('usb_transfer_contract',ROOT/'minimal/scripts/usb_transfer_app.py')
    grants=contract.validate(manifest,blob,receipt,system,(a.reader/'sdk/driver/RiscUsbDeviceMscV1.h').read_bytes())
    check_sources(a.system,receipt['source_sha256'])
    install_app('usb_sd_transfer',a.transfer,manifest,blob,receipt)
    for name,folder,source in [('sd',a.sd,a.native_product),('usb-msc',a.usb,a.reader)]:
        elf=(folder/'driver.elf').read_bytes(); manifest=(folder/'manifest.json').read_bytes()
        receipt=json.loads((folder/'target-proof.json').read_text())
        source_key,dirty_key=('source_commit','working_tree_dirty') if name=='sd' else ('reader_commit','source_dirty')
        require(receipt[source_key]==clean(source) and receipt[dirty_key] is False, 'Provider source differs: '+name)
        require(receipt['target_structure_passed'] and sha(elf)==receipt['elf_sha256'] and len(elf)==receipt['elf_bytes'],
                'Provider bytes differ: '+name)
        check_sources(source,receipt['source_sha256'])
        expected=source/('minimal/drivers/x4pro_sd/manifest.json' if name=='sd' else 'Drivers/usb_device_msc_esp32s3/manifest.json')
        require(manifest==expected.read_bytes(), 'Provider manifest differs: '+name)
        files[name+'/driver.elf'],files[name+'/manifest.json']=elf,manifest
        shutil.copytree(folder,a.output/'replacement-inputs'/name)
        inputs[name]=receipt
    boot=json.loads(files['boot.json'])
    require(len(boot['drivers'])==22 and len(boot['app_capabilities'])==19, 'Baseline graph differs')
    require(not any(x['manifest']=='usb-msc/manifest.json' for x in boot['drivers']), 'Duplicate USB provider')
    boot['drivers'].append({'manifest':'usb-msc/manifest.json'})
    boot['app_capabilities'].append({'manifest':'usb_sd_transfer.json','grants':grants})
    files['boot.json']=encoded(boot)
    firmware=(a.native/'firmware.bin').read_bytes()
    cohort=platform.cohort_identity(product,native,firmware,revision)
    files['cohort.json']=encoded(cohort)
    changed={name for name in files if files[name]!=frozen.get(name)}
    allowed={'springboard.elf','springboard.json','usb_sd_transfer.elf','usb_sd_transfer.json',
             'sd/driver.elf','sd/manifest.json','usb-msc/driver.elf','usb-msc/manifest.json','boot.json','cohort.json'}
    require(changed<=allowed and len(files)==89, 'Unexpected store change')
    admission=admit_cohort(a.runtime,(a.native/'firmware.elf').read_bytes(),files,files,app_policy_rows=17)
    filesystem,fs_proof=pack_store(files)
    require(read_image(filesystem,0x510000)==files, 'SPIFFS roundtrip differs')
    banks=load('usb_banks',a.runtime/'scripts/paired_bank_images.py')
    loader=(a.native/'bootloader.bin').read_bytes();table=(a.native/'partitions.bin').read_bytes()
    appdata=(a.native/'appdata.bin').read_bytes()
    require(sha(loader)=='1033730a6df733f53a7a347353c1c5450547f76e98e0746da633079310a563b9','DIO bootloader differs')
    require(len(table)==3072 and table==raw[0x8000:0x8c00], 'Partition table differs')
    require(len(appdata)==0x80000 and appdata==raw[0x270000:0x2f0000], 'Initial app-data differs')
    parts=[(0,loader),(0x8000,table),(0x10000,firmware),(0x270000,appdata),(0x2f0000,filesystem),
           (0xff0000,banks.initial_otadata()),(0xff2000,banks.initial_bank_state(firmware,filesystem,True))]
    image=bytearray(b'\xff'*0x1000000); occupied=[]
    for offset,data in parts:
        require(offset+len(data)<=len(image) and all(offset+len(data)<=x or offset>=y for x,y in occupied),'Partition overlap/overflow')
        image[offset:offset+len(data)]=data;occupied.append((offset,offset+len(data)))
    name='xteink-x4-pro-0.1.36-usb-sd-preparation-first-install.bin'
    (a.output/name).write_bytes(image);(a.output/'bootfs.bin').write_bytes(filesystem)
    for path,data in files.items():
        dest=a.output/'store'/path;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
    shutil.copytree(a.native,a.output/'native')
    shutil.copytree(a.baseline/'licenses',a.output/'licenses')
    shutil.copy(a.reader/'Drivers/usb_device_msc_esp32s3/LICENSE.TinyUSB',a.output/'licenses/LICENSE.TinyUSB')
    result={'schema':'x4.full-trace-usb-diagnostic','schema_version':1,'source_revision':revision,
            'product':product,'baseline_image_sha256':BASELINE_SHA,'frozen_source_records':original,
            'system_source':system,'springboard_system_source':springboard_system,'reader_source':reader,'cohort':cohort,'native_proof':native_proof,
            'inputs':inputs,'changed_store_paths':sorted(changed),'admission':admission,'store_generator':fs_proof,
            'store_files':digest_inventory(files),'partitions':[{'offset':o,'bytes':len(b),'sha256':sha(b)} for o,b in parts],
            'image':{'name':name,'bytes':len(image),'sha256':sha(image)},'hardware_tested':False}
    (a.output/'build-custody.json').write_bytes(encoded(result))
    print(json.dumps(result['image']))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('baseline','springboard','springboard-system','transfer','system','sd','reader','usb','native','native-product','runtime','watch','compiler','output'):
        p.add_argument('--'+name,type=Path,required=True)
    build(p.parse_args())
