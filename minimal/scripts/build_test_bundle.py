#!/usr/bin/env python3
"""Compose an offline NEW-device X4 test image; never opens hardware."""
import argparse, hashlib, importlib.util, json, re, shutil, subprocess, sys, zipfile
from pathlib import Path
from generate_profile import IDS, PATHS, stage, selections
ROOT=Path(__file__).resolve().parents[2]
APPS=('default','springboard','file_browser','ble_scanner','points_in_time','settings','calculator','stopwatch','countdown','timecard','battery','alarms','wifi_settings','ble_touchpad','ble_buttons','waterfall')
CAPS={'x4.power':17,'display.output':3,'input.touch.raw':4,'input.navigation':6,'board.battery':7,'rtc.clock':8,'storage.volume':9,'bluetooth.hci':16,'alarm.service':0,'file.open':0,'storage.installed-files':0,'bluetooth.sensors':0,'storage.app-data':1,'net.wifi':15,'bluetooth.hid':0,'radio.iq':0}
KV_NAMESPACES={'points_in_time':(5,1),'stopwatch':(2,1),'countdown':(3,1),'alarms':(3,1),'wifi_settings':(6,1),'ble_buttons':(11,1)}
APPDATA_NAMESPACES={'timecard':1,'waterfall':3}
def app_grants(name, requirements, sleep=False):
    grants=[]
    for req in requirements:
        cap=req['capability']
        if cap=='x4.power' and (name!='default' or not sleep):
            raise ValueError('Power authority restricted to explicit sleep Clock')
        if cap=='storage.app-data' and name not in APPDATA_NAMESPACES:
            raise ValueError('App-data authority requires an explicit deployment mapping')
        instances=((8,) if req['api']==2 else (1,)) if name=='waterfall' and cap=='storage.key-value' else KV_NAMESPACES.get(name,(1,)) if cap=='storage.key-value' else (APPDATA_NAMESPACES[name],) if cap=='storage.app-data' else (CAPS[cap],)
        for instance in instances:
            grant={'capability':cap,'api':req['api'],'instance_id':instance}
            if grant not in grants:grants.append(grant)
    if sleep and name=='default' and not any(g['capability']=='x4.power' for g in grants):
        raise ValueError('Sleep graph requires explicit Clock power grant')
    return grants
def sha(b):return hashlib.sha256(b).hexdigest()
def encoded(d):return (json.dumps(d,sort_keys=True,indent=2)+'\n').encode()
def cohort_identity(product,native,firmware,revision):
    if set(product)!={'schema','product','version','source_repo'} or product['schema']!=1:
        raise ValueError('Invalid X4 product identity')
    if not re.fullmatch(r'[a-z][a-z0-9-]*',product['product']) or not re.fullmatch(r'\d+\.\d+\.\d+',product['version']):
        raise ValueError('Invalid product/version')
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+',product['source_repo']) or '..' in product['source_repo'] or not re.fullmatch(r'[0-9a-f]{40}',revision):
        raise ValueError('Invalid product source custody')
    if native['layout']!='riscrte-paired-appdata-v2' or native['store_abi']!=2 or not re.fullmatch(r'\d+\.\d+\.\d+',native['firmware_version']):
        raise ValueError('Invalid native cohort layout/version')
    asset=native['assets']['firmware.bin']
    if not 32<=len(firmware)<=0x260000 or asset['bytes']!=len(firmware) or asset['sha256']!=sha(firmware):
        raise ValueError('Native firmware differs from candidate receipt')
    return {'schema':'riscrte.cohort','schema_version':1,'product':product['product'],'version':product['version'],
            'source_repo':product['source_repo'],'source_revision':revision,'runtime_version':native['firmware_version'],
            'layout':native['layout'],'store_abi':2,'firmware_size':len(firmware),'firmware_sha256':sha(firmware)}
def validate_settings_profile(manifest,blob,record):
    # The selected X4 power hook supports manual light sleep only. A generic
    # Light/Deep/Hybrid preference would promise modes this deployment ignores.
    if record.get('working_tree_dirty') is not False or record.get('version')!=manifest['version']:
        raise ValueError('Settings build receipt identity is unverified')
    if record.get('sha256')!=sha(blob) or record.get('size_bytes')!=len(blob):
        raise ValueError('Settings ELF differs from its build receipt')
    flags=record.get('build_defines')
    if not isinstance(flags,list) or not all(isinstance(flag,str) for flag in flags):
        raise ValueError('Settings feature selection is missing')
    if any(flag.split('=')[0]=='-DPORTABLE_SLEEP_SETTINGS' for flag in flags):
        raise ValueError('X4 manual-light profile cannot expose deep/hybrid preferences')
    return {'manual_light_sleep':True,'sleep_mode_selector':False}
def load_module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def build(a):
    inputs=json.loads(a.inputs.read_text());out=a.output.resolve()
    if out.exists():raise ValueError('Output already exists; no stale-image reuse')
    out.mkdir(parents=True);store=out/'store';stage(a.panel,store,getattr(a,"sleep",False))
    boot=json.loads((store/'boot.json').read_text());board=json.loads((store/'board.json').read_text())
    products=json.loads((a.drivers/'products.json').read_text());products={p['id']:p for p in products}
    driver_origin=json.loads((a.drivers/'build-origin.json').read_text())
    custody={'shared_source_lock':json.loads((ROOT/'minimal/sources.lock.json').read_text()),'schema':1,'panel':a.panel,'runtime':json.loads((a.native/'candidate.json').read_text()),'x4_source':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'apps':{},'drivers':products,'verification':'Compiled and packaged test candidate. No device run. Extended validation and CI may still be pending.'}
    lock=custody['shared_source_lock']
    if custody['runtime']['source_sha']!=lock['runtime']['commit'] or custody['runtime']['firmware_version']!=lock['runtime']['version']:raise ValueError('Native candidate differs from locked Runtime source')
    if driver_origin['source_fixture']:raise ValueError('Source-fixture drivers cannot be packaged')
    for name in ('runtime','shared'):
        source=driver_origin['sources'][name]
        if source['commit']!=lock[name]['commit'] or source['dirty']:raise ValueError('Driver dependency source custody mismatch: '+name)
    custody['driver_build_origin']=driver_origin
    for name,(_,folder,_) in selections(getattr(a,"sleep",False)).items():
        path=store/folder;m=json.loads((path/'manifest.json').read_text());src=a.drivers/m['id'];blob=(src/'driver.elf').read_bytes()
        if sha(blob)!=products[m['id']]['sha256'] or json.loads((src/'manifest.json').read_text())!=m:raise ValueError('Driver identity mismatch: '+name)
        (path/'driver.elf').write_bytes(blob)
    for folder,key in [('ble','ble_provider'),('alarm','alarm_service'),('sensors','sensor_provider'),('wifi','wifi_provider'),('hid','hid_provider'),('iq','iq_provider')]:
        src=Path(inputs[key]);dest=store/folder;dest.mkdir();m=json.loads((src/'manifest.json').read_text());blob=(src/'driver.elf').read_bytes()
        (dest/'manifest.json').write_bytes(encoded(m));(dest/'driver.elf').write_bytes(blob)
        custody[key]={'id':m['id'],'version':m['version'],'sha256':sha(blob)}
        evidence=out/'build-records'/'providers'/m['id'];evidence.mkdir(parents=True,exist_ok=True)
        notices=out/'licenses'/'providers'/m['id'];notices.mkdir(parents=True,exist_ok=True)
        for record in src.glob('*.json'):
            if record.name!='manifest.json':shutil.copyfile(record,evidence/record.name)
        for notice in src.iterdir():
            if notice.is_file() and ('LICENSE' in notice.name or 'NOTICE' in notice.name or notice.name.endswith('SOURCE.json')):shutil.copyfile(notice,notices/notice.name)
    board['devices'].append({'instance_id':16,'chip':{'vendor':'espressif','model':'esp32s3-ble','revision':'unspecified'},'compatible':'espressif,esp32s3-ble','config_type':'radio.integrated','config_version':1,'config':{'unit':0,'features':1}})
    boot['drivers'].append({'manifest':'ble/manifest.json','instance_id':16})
    boot['drivers'].append({'manifest':'sensors/manifest.json'})
    board['devices'].append({'instance_id':15,'chip':{'vendor':'espressif','model':'esp32s3-wifi','revision':'unspecified'},'compatible':'espressif,esp32s3-wifi','config_type':'radio.integrated','config_version':1,'config':{'unit':0,'features':1}})
    boot['drivers'].append({'manifest':'wifi/manifest.json','instance_id':15})
    boot['drivers'].append({'manifest':'iq/manifest.json'})
    boot['drivers'].append({'manifest':'hid/manifest.json','key_value':[{'key':k,'namespace':10,'access':'read-write'} for k in ('hid_ours','hid_peer','hid_ccc','hid_identity')]})
    keys=[('alarm_cfg',3,'read'),('timer_cfg',3,'read'),('alarm_occ',4,'read-write'),('timer_occ',4,'read-write'),('alert_mode',1,'read'),('points_cfg',5,'read'),('points_occ',4,'read-write'),('alert_dnd',1,'read')]
    boot['drivers'].append({'manifest':'alarm/manifest.json','key_value':[{'key':k,'namespace':n,'access':v} for k,n,v in keys]})
    policies=[];licenses=out/'licenses';licenses.mkdir(exist_ok=True)
    for name in APPS:
        src=Path(inputs['apps'][name]);blob=(src/(name+'.elf')).read_bytes();m=json.loads((src/(name+'.json')).read_text())
        if m['file_name']!=name+'.elf' or m['type']!='application':raise ValueError('App identity mismatch: '+name)
        if name=='settings':custody['settings_power_ui']=validate_settings_profile(m,blob,json.loads((src/'settings-build-record.json').read_text()))
        # The shared adapter exposes battery telemetry only with explicit grants.
        if not any(r['capability']=='board.battery' for r in m['requires']):m['requires'].append({'capability':'board.battery','api':1})
        requirements=[]
        for req in m['requires']:
            normalized={'capability':req['capability'],'api':req['api']}
            if normalized not in requirements:requirements.append(normalized)
        m['requires']=requirements
        grants=app_grants(name,m['requires'],getattr(a,'sleep',False))
        policies.append({'manifest':name+'.json','grants':grants});(store/(name+'.elf')).write_bytes(blob);(store/(name+'.json')).write_bytes(encoded(m))
        custody['apps'][name]={'id':m['id'],'version':m['version'],'sha256':sha(blob),'manifest_sha256':sha(encoded(m))}
        records=out/'build-records'/name;records.mkdir(parents=True)
        for record in src.glob('*.json'):
            if record.name!=name+'.json':shutil.copyfile(record,records/record.name)
        if (src/'licenses').is_dir():shutil.copytree(src/'licenses',licenses/name)
        for notice in src.glob('LICENSE*'):
            (licenses/name).mkdir(exist_ok=True);shutil.copyfile(notice,licenses/name/notice.name)
    custody['sleep']=getattr(a,'sleep',False)
    boot['app_capabilities']=policies;(store/'boot.json').write_bytes(encoded(boot));(store/'board.json').write_bytes(encoded(board))
    # IQ authority requires the exact native reservation proof, not just a table name.
    iq_proof=custody['runtime'].get('native_proof',{}).get('radio_iq',{})
    if custody['runtime'].get('target')!='esp32s3-16mb-appdata-iq' or iq_proof.get('bank_bytes')!=65536 or iq_proof.get('elf_sha256')!=sha((a.native/'firmware.elf').read_bytes()):raise ValueError('RF cohort requires a proven native IQ reservation')
    fw=(a.native/'firmware.bin').read_bytes()
    cohort=cohort_identity(json.loads((ROOT/'minimal/product.json').read_text()),custody['runtime'],fw,custody['x4_source'])
    (store/'cohort.json').write_bytes(encoded(cohort));custody['cohort']=cohort
    files={p.relative_to(store).as_posix():p.read_bytes() for p in store.rglob('*') if p.is_file()}
    if any(len('/'+n)>=32 for n in files):raise ValueError('SPIFFS object name exceeds 31 bytes')
    sys.path.insert(0,str(a.watch/'scripts'))
    if not a.skip_extended_checks:
        from check_runtime_store_admission import admit_cohort
        custody['store_admission']=admit_cohort(a.runtime,(a.native/'firmware.elf').read_bytes(),files,files)
    native=load_module('x4_native_candidate',a.runtime/'scripts/paired_bank_images.py')
    loader=(a.native/'bootloader.bin').read_bytes();table=(a.native/'partitions.bin').read_bytes();data=(a.native/'appdata.bin').read_bytes()
    if sha(loader)!=native.BOOTLOADER_SHA256 or len(data)!=0x80000:raise ValueError('Native first-install inputs differ')
    image=out/'bootfs.bin';subprocess.run([str(a.mkspiffs),'-c',str(store),'-p','256','-b','4096','-s',str(0x510000),str(image)],check=True)
    filesystem=image.read_bytes()
    if len(filesystem)!=0x510000:raise ValueError('SPIFFS geometry mismatch')
    state=native.initial_bank_state(fw,filesystem,True);ota=native.initial_otadata()
    full=bytearray(b'\xff'*0x1000000)
    parts=[(0,loader),(0x8000,table),(0x10000,fw),(0x270000,data),(0x2f0000,filesystem),(0xff0000,ota),(0xff2000,state)]
    for at,blob in parts:
        if at+len(blob)>len(full):raise ValueError('Flash image overflow')
        full[at:at+len(blob)]=blob
    filename=f'x4-minimal-{a.panel}-test-full.bin';(out/filename).write_bytes(full)
    (out/'firmware.bin').write_bytes(fw);(out/'otadata.bin').write_bytes(ota);(out/'bank_state.bin').write_bytes(state)
    custody['image']={'name':filename,'bytes':len(full),'sha256':sha(full)};custody['store_files']={n:{'bytes':len(b),'sha256':sha(b)} for n,b in sorted(files.items())};custody['extended_checks_skipped']=a.skip_extended_checks
    custody['native_partitions']=[{'offset':at,'bytes':len(blob),'sha256':sha(blob)} for at,blob in parts]
    (out/'build-custody.json').write_bytes(encoded(custody))
    (out/'README.txt').write_text('X4 MINIMAL TEST BUILD - '+a.panel+'\n\nNEW 16 MiB paired/app-data layout only. Flashing the full BIN at0x0 overwrites firmware, partition table, NVS and app-data. Do not use as a data-preserving update. No device was flashed or physically tested. Select the panel variant explicitly; wrong-controller detection fails closed.\n\nBoot: clock; top-edge swipe down opens QuickActions, other clock swipes open Springboard; File Browser, Bluetooth Scanner with sensor details, Points in Time, Settings, Calculator, Stopwatch, Countdown, Timecard, Battery, Alarms, Wi-Fi, Bluetooth Touchpad, Bluetooth Buttons and RF Spectrogram. Serial Monitor deferred. RTC uses unconverted wall time. Alarms are visual-only; no audio/haptic hardware is simulated. File opening requires a declared installed handler; no arbitrary SD ELF execution. Center Home returns to Clock. The top-right short press returns to Clock from an app and requests manual light sleep from Clock; GPIO3 wakes it. Deep/hybrid/idle/touch wake is not implemented; unsupported sleep mode preferences are hidden in Settings. QuickActions radio toggles remain unselected; use Wi-Fi Settings and Bluetooth Scanner Enable BT before HID pairing. OTA/App Store are omitted because their service still selects the Watch feed. Deep-sleep desk clock is pending.\n\nExtra checks/CI wait were skipped for this requested accelerated test artifact. See build-custody.json for exact hashes and verification limits.\n')
    if not a.skip_extended_checks:
        readme=out/'README.txt'
        readme.write_text(readme.read_text().replace('Extra checks/CI wait were skipped for this requested accelerated test artifact.', 'The complete packaged store passed the production Runtime policy and ELF-admission preflight. Hardware operation remains unverified.'))
    archive=out.with_suffix('.zip')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(out.rglob('*')):
            if p.is_file():z.write(p,p.relative_to(out))
    print(json.dumps({'bin':str(out/filename),'bin_sha256':sha(full),'archive':str(archive),'archive_sha256':sha(archive.read_bytes())}))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['inputs','drivers','native','runtime','watch','mkspiffs','output']:p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--sleep',action='store_true',help='Explicit GPIO3 power graph and Clock-only sleep authority');p.add_argument('--panel',choices=['ssd1677','uc8279'],required=True);p.add_argument('--skip-extended-checks',action='store_true');build(p.parse_args())
