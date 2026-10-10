#!/usr/bin/env python3
"""Compose only Home over frozen X4 .49, using exact native-BIN admission."""
import argparse,hashlib,inspect,io,json,os,shutil,struct,subprocess,sys,tempfile
from pathlib import Path
from build_recovered_diagnostic import require,sha,encoded,digest_inventory,load
from package_resident_cohort import descriptor
from native_binary_exports import qualified_exports
ROOT=Path(__file__).resolve().parents[2]

def clean(path):
    require(not subprocess.check_output(['git','-C',str(path),'status','--porcelain'],text=True).strip(),'Dirty source: '+str(path))
    return {key:subprocess.check_output(['git','-C',str(path),'rev-parse',arg],text=True).strip() for key,arg in [('commit','HEAD'),('tree','HEAD^{tree}')]}

def pinned(path,spec):
    raw=path.read_bytes();require(sha(raw)==spec['sha256'] and len(raw)==spec['bytes'],'Pinned artifact differs: '+str(path));return raw

def write_store(path,files):
    for name,blob in files.items():
        p=path/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(blob)

def cohort_admit(a,firmware,exports,reference_elf,files,output,sanitize):
    import check_runtime_store_admission as admission
    import verify_update_elf as verify
    # Shared harness normally takes an ELF. Here its policy marker is read from
    # the actual BIN; the header receives only the independently witnessed BIN
    # resolver names. The reference ELF is a parser fixture, never .99 identity.
    original_header,original_exports=verify.cohort_admission_header,verify.public_exports
    policy='runtime/drivers/NativeProviderPolicyValidationV1.h'
    source=(a.runtime/'src/ports/esp32s3/NativeBankStore.cpp').read_text()
    require('#include "'+policy+'"' in source,'Missing production policy dependency')
    def header(runtime,candidate):
        require(candidate==firmware,'Wrong native contract input')
        verify.public_exports=lambda unused:exports
        try:return '#include "'+policy+'"\n'+original_header(runtime,reference_elf)
        finally:verify.public_exports=original_exports
    verify.cohort_admission_header=header
    saved=os.environ.get('SANITIZE');os.environ['SANITIZE']='1' if sanitize else '0'
    try:
        # The original .99 ELF marker lookup cannot run. Select the frozen
        # .49 native feature contract explicitly, without pretending that a
        # reference ELF is the candidate. No Runtime/Graph code changes.
        require(b'platform.usb.phy.resource\0' in firmware and b'platform.diagnostic-source\0' in firmware,
                'Frozen native feature strings missing')
        compile_source=inspect.getsource(admission.compile_harness)
        start=compile_source.index('    if native_elf is not None:\n        from elftools.elf.elffile import ELFFile')
        end=compile_source.index("    if 'bool (*coldBoot)()'",start)
        selection="    command += ['-DSTORE_ADMISSION_USB_PHY', '-DSTORE_ADMISSION_DIAGNOSTIC_SOURCE']\n"
        compile_source=compile_source[:start]+selection+compile_source[end:]
        (output/'native-bin-compile-harness.py').write_text(compile_source)
        namespace=dict(vars(admission));exec(compile(compile_source,'native-bin-compile-harness.py','exec'),namespace)
        harness=namespace['compile_harness'](a.runtime,output/'admit',True,firmware,17)
        result=subprocess.run([str(harness),str(output.parent/'store'),str(output.parent/'store')],capture_output=True,text=True,check=False,timeout=180,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
    finally:
        verify.cohort_admission_header=original_header;verify.public_exports=original_exports
        if saved is None:os.environ.pop('SANITIZE',None)
        else:os.environ['SANITIZE']=saved
    (output/'result.stdout').write_text(result.stdout);(output/'result.stderr').write_text(result.stderr)
    result.check_returncode()
    value=json.loads(result.stdout)
    require(value['prepared'] and value['cohort_validated'] and value['elf_count']==44 and value['hardware_calls']==value['storage_calls']==0,'Complete cohort admission failed')
    value.update(native_firmware_sha256=sha(firmware),native_elf_check='not run; original .99 ELF unavailable',native_exports='exact BIN pointer-table readback',production_admission_source_sha256=sha(source.encode()),policy_header_sha256=sha((a.runtime/'src'/policy).read_bytes()),app_policy_rows=17,sanitize=sanitize,leak_sanitizer='disabled because sandbox tracing is incompatible',target_instructions_executed=False,native_feature_contract='Archived .49 USB PHY and diagnostic-source selection, with capability strings in exact BIN; original ELF marker lookup unrun',harness_adapter_sha256=sha(compile_source.encode()))
    return value

def build(a):
    spec=json.loads((ROOT/'minimal/apps/home-ui-050.json').read_text());source=clean(ROOT)
    require(clean(a.system)['commit']==spec['system_commit'],'System commit differs')
    require(clean(a.runtime)['commit']==spec['sdk_commit'],'Runtime SDK commit differs')
    require(clean(a.tools)['commit']==spec['packaging_commit'],'Packaging commit differs')
    require(not a.output.exists(),'Output already exists');a.output.mkdir(parents=True)
    sys.path[:0]=[str(a.tools/'scripts'),str(a.runtime/'scripts')]
    from read_only_spiffs import read_image
    from current_bootfs import build as pack
    from compact_current_elf import compact
    from check_runtime_store_admission import unpack_image
    from paired_bank_images import initial_bank_state,initial_otadata,parse_record
    original_image=pinned(a.baseline,spec['baseline']);require(len(original_image)==0x1000000,'Wrong full image size')
    original=read_image(original_image[0x2f0000:0x800000],0x510000);files=dict(original)
    require(len(files)==91,'Wrong file inventory');old=json.loads(files['cohort.json'])
    require(old['version']=='0.1.49' and old['runtime_version']=='0.1.99','Wrong baseline cohort')
    firmware=original_image[0x10000:0x10000+old['firmware_size']]
    require(sha(firmware)==old['firmware_sha256']==spec['native']['sha256'],'Native firmware differs')
    require(original_image[0xff2000:0xff4000]==initial_bank_state(firmware,original_image[0x2f0000:0x800000],True),'Baseline pair binding differs')
    require(original_image[0xff0000:0xff2000]==initial_otadata(),'OTA metadata differs')
    require(pack(original)[0]==original_image[0x2f0000:0x800000],'Baseline store is not reproducible')
    require(unpack_image(original_image[0x2f0000:0x800000],a.mkspiffs,0x510000)==original,'C baseline reader differs')
    exports,export_proof=qualified_exports(a.runtime,firmware,a.reference_runtime,a.reference_native/'firmware.elf',a.reference_native/'firmware.bin')
    (a.output/'native-binary-exports.json').write_bytes(encoded(export_proof))
    home=pinned(a.home/'default.elf',spec['home_elf']);meta=pinned(a.home/'default.json',spec['home_manifest'])
    receipt_blob=pinned(a.home/'build-evidence.json',spec['home_receipt']);receipt=json.loads(receipt_blob)
    require(receipt['repository_commit']==spec['system_commit'] and not receipt['working_tree_dirty'] and receipt['sha256']==sha(home),'Home receipt differs')
    require(receipt['resident_shell']['host_power_policy'] and receipt['resident_shell']['legacy_handoff'],'Home host feature missing')
    require(receipt['resident_shell']['runtime_sdk']==str(a.runtime/'sdk/app'),'Home was not built against repaired .99 SDK')
    for name,digest in receipt['resident_shell']['sdk_sha256'].items():require(sha((a.runtime/'sdk/app'/name).read_bytes())==digest,'SDK hash differs: '+name)
    source_hashes={**receipt['desk_sources'],**receipt['resident_shell']['source_sha256'],**receipt['idle_policy']['system_source_sha256'],**receipt['idle_policy']['build_source_sha256']}
    for name,digest in source_hashes.items():require(sha((a.system/name).read_bytes())==digest,'Compiled System source differs: '+name)
    require(sha((ROOT/'minimal/apps/portable_idle_sleep.c').read_bytes())==receipt['idle_policy']['helper_sha256'],'Product helper differs')
    manifest=json.loads(meta);prior=json.loads(files['default.json'])
    require(prior.pop('version')=='0.3.21' and manifest['version']=='0.3.22' and prior=={k:v for k,v in manifest.items() if k!='version'},'Home manifest authority differs')
    require(descriptor(home,1)>0,'Home descriptor missing')
    dest=a.output/'compacted/default.elf';dest.parent.mkdir();dest.write_bytes(home)
    compaction=compact(dest,str(a.compiler),debug_path=a.output/'debug-originals/default.elf')
    files['default.elf']=dest.read_bytes();files['default.json']=meta
    cohort=dict(old,version='0.1.50',source_revision=source['commit']);files['cohort.json']=encoded(cohort)
    changed=sorted(n for n in files if files[n]!=original[n])
    require(changed==['cohort.json','default.elf','default.json'] and set(files)==set(original),'Unexpected store mutation')
    apps=sorted(n for n in files if n.endswith('.elf') and '/' not in n);providers=sorted(n for n in files if n.endswith('/driver.elf'))
    require(len(apps)==21 and len(providers)==23,'Incomplete apps/providers')
    boot=json.loads(files['boot.json']);foreground=boot['resident_shell']['foreground']
    require(len(foreground)==19 and boot['resident_shell']['host']=='default.elf' and boot['resident_shell']['legacy']==['gameboy.elf'],'Resident policy differs')
    descriptor(files['default.elf'],1,check_renderer=False)
    for name in foreground:descriptor(files[name],2,check_renderer=False)
    write_store(a.output/'store',files)
    admissions=[]
    for sanitize in (False,True):
        folder=a.output/('admission-sanitized' if sanitize else 'admission-normal');folder.mkdir()
        admissions.append(cohort_admit(a,firmware,exports,(a.reference_native/'firmware.elf').read_bytes(),files,folder,sanitize))
    require({p.relative_to(a.output/'store').as_posix():p.read_bytes() for p in (a.output/'store').rglob('*') if p.is_file()}==files,'Admission changed store')
    fs,fs_proof=pack(files);require(pack(files)[0]==fs,'Nondeterministic store')
    require(unpack_image(fs,a.mkspiffs,0x510000)==files,'C candidate readback differs')
    bank=initial_bank_state(firmware,fs,True);pair=parse_record(bank[:96],True)
    require(pair[6]==hashlib.sha256(firmware).digest() and pair[7]==hashlib.sha256(fs).digest(),'Candidate pair SHA/CRC differs')
    image=bytearray(original_image);image[0x2f0000:0x800000]=fs;image[0xff2000:0xff4000]=bank
    regions=[(0,0x2f0000),(0x800000,0xff2000),(0xff4000,0x1000000)]
    require(all(image[lo:hi]==original_image[lo:hi] for lo,hi in regions),'Frozen image region changed')
    require(len(image)==0x1000000 and read_image(bytes(image[0x2f0000:0x800000]),0x510000)==files,'Full image readback differs')
    name='X4-0.1.50-Home-SDMMC-full-0x0.bin';(a.output/name).write_bytes(image);(a.output/'bootfs.bin').write_bytes(fs)
    (a.output/'home-build-evidence.json').write_bytes(receipt_blob)
    report={'schema':'x4.frozen-native-home-composition','schema_version':1,'product_version':'0.1.50','source':source,'sources':spec,'baseline_cohort':old,'cohort':cohort,'image':{'name':name,'bytes':len(image),'sha256':sha(image),'flash_offset':0},'native':dict(spec['native'],rebuilt=False,preserved_byte_identically=True,original_elf_unavailable=True,binary_exports_verified=len(exports)),'home':{'version':'0.3.22','compiled_elf_sha256':sha(home),'packaged_elf_sha256':sha(files['default.elf']),'compaction':compaction,'compiled_system_source_files_verified':len(source_hashes)},'changed_store_paths':changed,'preserved_nonhome_apps':[n for n in apps if n!='default.elf'],'preserved_providers':providers,'store_files':digest_inventory(files),'store_generator':fs_proof,'preserved_regions':[{'offset':lo,'bytes':hi-lo,'sha256':sha(original_image[lo:hi])} for lo,hi in regions],'boot_grants_unchanged':True,'pair_sha_crc_verified':True,'independent_python_and_c_readback_files':len(files),'admission':admissions,'sleeping_overlay':{'owner':'Home host; installed foreground policy checkpoints already exist','resident_routes':foreground,'excluded_legacy':['gameboy.elf'],'idle_inhibited_entire_session':['usb_sd_transfer.elf'],'deferred_conditions':['active capture in Contexts/Waterfall','busy Wi-Fi/update transactions','touch, modal UI, uncompleted display or uncertain custody'],'client_rebuild_needed':False,'hardware_verified':False},'hardware_tested':False,'native_elf_check':'Not run: exact .99 ELF was not delivered; exact BIN resolver tables replace the symbol-table witness.','pending':['final independent immutable artifact review','physical boot, e-ink timing, SDMMC sleep/resume and power testing'],'excluded':['shared user-volume backend','shared text-input keyboard','declarative Alarms prototype'],'first_install_warning':'Full 0x0 image overwrites internal settings, bonds and AppData. Back up internal data first; removable SD contents are not included.'}
    (a.output/'build-custody.json').write_bytes(encoded(report));print(json.dumps(report['image']))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('baseline','home','system','runtime','tools','reference-runtime','reference-native','compiler','mkspiffs','output'):p.add_argument('--'+name,type=Path,required=True)
    build(p.parse_args())
