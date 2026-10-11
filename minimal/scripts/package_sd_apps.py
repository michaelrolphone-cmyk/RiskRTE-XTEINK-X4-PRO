#!/usr/bin/env python3
"""Compose X4 core firmware and a separately admitted removable-app bundle."""
import argparse, hashlib, io, json, os, shutil, sys, zipfile
from pathlib import Path
from build_recovered_diagnostic import require, sha, encoded, digest_inventory
from package_home_ui import clean, write_store
from package_resident_cohort import descriptor
from build_test_bundle import validate_native_composition, cohort_identity

ROOT=Path(__file__).resolve().parents[2]
CORE={'default','springboard','settings','wifi_settings','file_browser','usb_sd_transfer',
      'ota_update','app_store','alarms','battery'}
BASE_SHA='c49211b4d3aa05940ad28b44ad98533ab94a43da3d74126f4d2eb70b4ceb28c8'

def build(a):
    require(not a.output.exists(),'Choose a new output directory')
    source=clean(ROOT); runtime=a.runtime.resolve(); native=a.native.resolve(); components=a.components.resolve()
    require(json.loads((ROOT/'minimal/product.json').read_text())['version']=='0.1.73','Wrong product')
    sys.path[:0]=[str(a.tools.resolve()/'scripts'),str(runtime/'scripts'),str(ROOT/'minimal/recovery_tools')]
    from read_only_spiffs import read_image
    from current_bootfs import build as pack
    from paired_bank_images import initial_bank_state, initial_otadata, parse_record
    from native_binary_exports import exports as bin_exports, reference_tables
    from elftools.elf.elffile import ELFFile
    import verify_update_elf as verify
    import recovery_store_admission as admission
    admission.ROOT=a.tools.resolve()
    raw=a.baseline.read_bytes();require(len(raw)==0x1000000 and sha(raw)==BASE_SHA,'Wrong baseline')
    files=read_image(raw[0x2f0000:0x800000],0x510000);frozen=dict(files)
    candidate=json.loads((native/'candidate.json').read_text())
    require(candidate['firmware_version']=='0.2.5','Removable loader Runtime required')
    native_proof=validate_native_composition(native,candidate,runtime,ROOT,native_source_root=a.native_platform.resolve())
    a.output.mkdir(parents=True)
    receipts={}
    for name in ('default','springboard'):
        p=components/'apps'/name
        files[name+'.elf']=(p/(name+'.elf')).read_bytes();files[name+'.json']=(p/(name+'.json')).read_bytes()
        receipts[name]=json.loads((p/('build-evidence.json' if name=='default' else 'springboard-build-record.json')).read_text())
    for name,p in [('sd',components/'storage'),('ui-scene',components/'scene/scene-host')]:
        files[name+'/driver.elf']=(p/'driver.elf').read_bytes();files[name+'/manifest.json']=(p/'manifest.json').read_bytes()
        receipts[name]=json.loads((p/'build.json').read_text())
    # The panel is inherited exactly, not rebuilt from a similarly named branch.
    require(files['panel/driver.elf']==frozen['panel/driver.elf'] and
            json.loads(files['panel/manifest.json'])['version']=='0.1.12','Panel changed')
    require(files['contexts.elf']==frozen['contexts.elf'] and json.loads(files['contexts.json'])['version']=='0.4.1','Contexts changed')
    for name in ('model_viewer','hollow_trail'):
        for suffix in ('.elf','.json'):files[name+suffix]=(components/'canvas'/(name+suffix)).read_bytes()
        receipts[name]=json.loads((components/'canvas'/(name+'-build.json')).read_text())
    reader=components/'reader/ebook-reader'
    for suffix in ('.elf','.json'):files['ebook_reader'+suffix]=(reader/('ebook_reader'+suffix)).read_bytes()
    receipts['ebook_reader']=json.loads((reader/'build.json').read_text())
    boot=json.loads(files['boot.json'])
    boot['app_capabilities']+=json.loads((components/'canvas/policies.json').read_text())
    reader_manifest=json.loads(files['ebook_reader.json'])
    boot['app_capabilities'].append({'manifest':'ebook_reader.json','grants':[
        dict(r,instance_id=9 if r['capability']=='storage.volume' else 0) for r in reader_manifest['requires']]})
    boot['resident_shell']['foreground'].append('ebook_reader.elf')
    boot['resident_shell']['legacy']+=['model_viewer.elf','hollow_trail.elf']
    require(len(boot['app_capabilities'])==25 and len(boot['drivers'])==27,'Wrong cohort')
    external={}; sd={}
    for row in boot['app_capabilities']:
        manifest=json.loads(files[row['manifest']]);name=manifest['file_name'];stem=Path(name).stem
        if stem in CORE:continue
        path='Apps/'+manifest['id']+'/'+name
        require(len(files[name])<=2*1024*1024,'External ELF exceeds bounded snapshot size')
        row['image_source']={'volume_instance':9,'path':path}
        external[name]=files.pop(name);sd[path]=external[name]
        sd[str(Path(path).with_name('manifest.json'))]=files[row['manifest']]
    for p in (reader/'sd').rglob('*'):
        if p.is_file():sd[p.relative_to(reader/'sd').as_posix()]=p.read_bytes()
    require(len(external)==15 and not any(p.startswith('System/State/') for p in sd),'Unexpected SD scope')
    files['boot.json']=encoded(boot)
    firmware=(native/'firmware.bin').read_bytes();native_elf=(native/'firmware.elf').read_bytes()
    exports,export_proof=bin_exports(firmware,reference_tables(native_elf))
    require(exports==verify.public_exports(ELFFile(io.BytesIO(native_elf))),'Native exports differ')
    product=json.loads((ROOT/'minimal/product.json').read_text())
    files['cohort.json']=encoded(cohort_identity(product,candidate,firmware,source['commit']))
    images={**{n:b for n,b in files.items() if n.endswith('.elf')},**external}
    for name,role in [('default.elf',1),*[(n,2) for n in boot['resident_shell']['foreground']]]:
        descriptor(images[name],role,check_renderer=False)
    write_store(a.output/'store',files);write_store(a.output/'sd',sd)
    header='#define RISC_APP_REQUIREMENT_ROWS 24\n#define RISC_APP_POLICY_ROWS 24\n#include "'+str(runtime/'src/runtime/drivers/NativeProviderPolicyValidationV1.h')+'"\n'
    old_admit=verify.admission_source;old_header=verify.cohort_admission_header
    old_env={k:os.environ.get(k) for k in ('CPLUS_INCLUDE_PATH','SANITIZE','ASAN_OPTIONS')}
    os.environ['CPLUS_INCLUDE_PATH']=str(runtime/'src')
    def with_policy(text):
        body,roles,driver=old_admit(text);return header+body,roles,driver
    try:
        verify.admission_source=with_policy;strict=[]
        for name,blob in sorted(images.items()):
            row=verify.verify(runtime,native_elf,blob,provider='/' in name)
            row.update(path=name,source='sd' if name in external else 'internal');strict.append(row)
        verify.admission_source=old_admit
        verify.cohort_admission_header=lambda rt,elf:header+old_header(rt,elf)
        cohorts=[]
        for sanitize in ('0','1'):
            os.environ['SANITIZE']=sanitize;os.environ['ASAN_OPTIONS']='detect_leaks=0'
            row=admission.admit_cohort(runtime,native_elf,files,files,app_policy_rows=24)
            row['sanitized']=sanitize=='1';cohorts.append(row)
    finally:
        verify.admission_source=old_admit;verify.cohort_admission_header=old_header
        for k,v in old_env.items():
            if v is None:os.environ.pop(k,None)
            else:os.environ[k]=v
    require(len(strict)==52 and all(r['elf_count']==37 for r in cohorts),'Incomplete app/provider admission')
    fs,capacity=pack(files)
    require(read_image(fs,0x510000)==files,'Internal store readback differs')
    require(admission.unpack_image(fs,a.mkspiffs,0x510000,expected_tool_sha256=sha(a.mkspiffs.read_bytes()))==files,'Independent SPIFFS readback differs')
    require(pack(files)[0]==fs,'Store generation is not deterministic')
    bank=initial_bank_state(firmware,fs,True);pair=parse_record(bank[:96],True)
    require(pair[6]==hashlib.sha256(firmware).digest() and pair[7]==hashlib.sha256(fs).digest(),'Pair SHA/CRC mismatch')
    image=bytearray(raw)
    for name,offset in [('bootloader.bin',0),('partitions.bin',0x8000)]:
        blob=(native/name).read_bytes();image[offset:offset+len(blob)]=blob
    image[0x10000:0x270000]=firmware+b'\xff'*(0x260000-len(firmware))
    image[0x2f0000:0x800000]=fs;image[0xff0000:0xff2000]=initial_otadata();image[0xff2000:0xff4000]=bank
    require(image[0x270000:0x2f0000]==raw[0x270000:0x2f0000],'Unexpected initial AppData change')
    bin_name='X4-0.1.73-SD-Apps-Panel-0.1.12-full-0x0.bin'
    (a.output/bin_name).write_bytes(image);(a.output/'bootfs.bin').write_bytes(fs)
    sd['X4-SD-Apps-0.1.73.json']=encoded({'version':'0.1.73','apps':sorted(external),'files':digest_inventory(sd)})
    sd['X4-SD-Apps-README.txt']=(
        'X4 0.1.73 SD applications\n\nCopy Apps/ and fonts/ into the root of the existing SD card.\n'
        'Merge folders; do not format the card or replace your books, ROMs, models or System/State.\n'
        'Use with the matching X4 0.1.73 firmware. Home, launcher, settings, file browser,\n'
        'USB transfer, updates, alarms, battery and all drivers remain internal.\n'
        'The device boots without SD; the 15 optional apps require their files on the card.\n'
        'Panel: original x4pro-uc8279-fast 0.1.12. Apps request capabilities, not panel versions.\n'
        'First transition: new app identities/grants and launcher entries still use firmware policy.\n').encode()
    zip_name='X4-0.1.73-SD-Apps.zip'
    with zipfile.ZipFile(a.output/zip_name,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,blob in sorted(sd.items()):
            info=zipfile.ZipInfo(name,(2026,10,10,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;z.writestr(info,blob)
    with zipfile.ZipFile(a.output/zip_name) as z:require({n:z.read(n) for n in z.namelist()}==sd,'SD archive readback differs')
    report={'schema':'x4.sd-apps','version':'0.1.73','source':source,'runtime_source':clean(runtime),
        'native_proof':native_proof,'native_exports':export_proof,'build_receipts':receipts,
        'strict_admission':strict,'cohort_admission':cohorts,'store_generator':capacity,
        'store_files':digest_inventory(files),'sd_files':digest_inventory(sd),'core_apps':sorted(CORE),
        'external_apps':sorted(external),'panel_elf_unchanged':True,'paired_sha_crc_verified':True,
        'image':{'name':bin_name,'bytes':len(image),'sha256':sha(image),'flash_offset':0},
        'sd_bundle':{'name':zip_name,'bytes':(a.output/zip_name).stat().st_size,'sha256':sha((a.output/zip_name).read_bytes())},
        'hardware_tested':False,'full_flash_overwrites_internal_data':True}
    (a.output/'build-custody.json').write_bytes(encoded(report));print(json.dumps({k:report[k] for k in ('image','sd_bundle','store_generator')}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('runtime','native','native-platform','components','tools','baseline','mkspiffs','output'):
        p.add_argument('--'+n,type=Path,required=True)
    build(p.parse_args())
