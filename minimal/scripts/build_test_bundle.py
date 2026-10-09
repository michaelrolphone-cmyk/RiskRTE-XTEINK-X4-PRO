#!/usr/bin/env python3
"""Compose an offline NEW-device X4 test image; never opens hardware."""
import argparse, hashlib, importlib.util, json, re, shutil, subprocess, sys, zipfile
from pathlib import Path
from generate_profile import IDS, PATHS, stage, selections
import native_time_cohort
import file_browser_admission
import update_artifacts
import telemetry_cohort
import idle_cohort
import contexts_cohort
import prepare_native_runtime as native_composition
ROOT=Path(__file__).resolve().parents[2]
APPS=('default','springboard','file_browser','ble_scanner','points_in_time','settings','calculator','stopwatch','countdown','timecard','battery','alarms','wifi_settings','ble_touchpad','ble_buttons','waterfall','ota_update','app_store')
CAPS={'contexts.service':0,'software.update.firmware':0,'software.update.apps':0,'runtime.realtime':0,'runtime.retained-wake':0,'runtime.realtime-control':0,'runtime.provider-promotion':0,'x4.power':17,'display.output':3,'input.touch.raw':4,'input.navigation':6,'board.battery':7,'rtc.clock':8,'storage.volume':9,'bluetooth.hci':16,'alarm.service':0,'file.open':0,'storage.installed-files':0,'bluetooth.sensors':0,'storage.app-data':1,'net.wifi':15,'bluetooth.hid':0,'radio.iq':0}
KV_NAMESPACES={'ota_update':(6,1),'app_store':(6,1),'points_in_time':(5,1),'stopwatch':(2,1),'countdown':(3,1),'alarms':(3,1),'wifi_settings':(6,1),'ble_buttons':(11,1)}
APPDATA_NAMESPACES={'timecard':1,'waterfall':3}
def app_grants(name, requirements, sleep=False, desk_clock=False, sparse_clock=False, native_time=False, idle_policy=False, contexts=False):
    if idle_policy and (not sleep or name not in (*APPS, *(("contexts",) if contexts else ()))):raise ValueError("Idle policy requires an explicit selected app and sleep graph")
    if desk_clock and not sleep:raise ValueError('Desk clock requires the explicit sleep graph')
    if sparse_clock and not desk_clock:raise ValueError('Sparse clock requires the explicit desk-clock profile')
    grants=[]
    for req in requirements:
        cap=req['capability']
        if cap=='contexts.service' and (not contexts or name not in contexts_cohort.PROFILE['context_capability_owners'] or type(req['api']) is not int or req['api']!=1):
            raise ValueError('Contexts authority requires the selected RF-only owner profile')
        if cap=='telemetry.broadcast':raise ValueError('Telemetry authority requires explicit cohort composition')
        if cap.startswith('software.update.') and (name not in ('ota_update','app_store') or cap!=('software.update.firmware' if name=='ota_update' else 'software.update.apps') or req['api']!=1):
            raise ValueError('Update authority requires the exact action-specific app')
        if cap=='runtime.realtime' and (not native_time or name in ('default','settings') or type(req['api']) is not int or req['api']!=1):
            raise ValueError('Readonly native time restricted to explicit native foreground apps')
        if cap=='runtime.provider-promotion' and (name!='default' or not sparse_clock or type(req['api']) is not int or req['api']!=1):
            raise ValueError('Provider promotion restricted to explicit sparse default Clock')
        if cap=='runtime.realtime-control' and (name not in ('default','settings') or not sparse_clock or type(req['api']) is not int or req['api']!=1):
            raise ValueError('Native time control restricted to explicit sparse Clock/Settings')
        if cap=='x4.power' and (not sleep or (name!='default' and not idle_policy) or type(req['api']) is not int or req['api']!=1):
            raise ValueError('Power authority requires explicit Clock sleep or app idle policy')
        if cap=='runtime.retained-wake' and (name!='default' or not desk_clock or req['api']!=1):
            raise ValueError('Retained-wake authority restricted to explicit desk Clock')
        if cap=='storage.app-data' and name not in APPDATA_NAMESPACES:
            raise ValueError('App-data authority requires an explicit deployment mapping')
        instances=(5,1) if native_time and sparse_clock and name=='default' and cap=='storage.key-value' else ((8,) if req['api']==2 else (1,)) if name=='waterfall' and cap=='storage.key-value' else KV_NAMESPACES.get(name,(1,)) if cap=='storage.key-value' else (APPDATA_NAMESPACES[name],) if cap=='storage.app-data' else (CAPS[cap],)
        for instance in instances:
            grant={'capability':cap,'api':req['api'],'instance_id':instance}
            if grant not in grants:grants.append(grant)
    if idle_policy:
        required={('x4.power',1),('display.output',1),('input.touch.raw',1),('storage.volume',1),
                  ('net.wifi',1),('bluetooth.hci',1),('alarm.service',2),('storage.key-value',1)}
        if not required.issubset({(r['capability'],r['api']) for r in requirements}):
            raise ValueError('Idle policy requires complete typed Light, alarm and preference authority')
    if sleep and name=='default' and not any(g['capability']=='x4.power' for g in grants):
        raise ValueError('Sleep graph requires explicit Clock power grant')
    if len({(r['capability'],r['api']) for r in requirements})>(16 if sparse_clock else 12) or len(grants)>(17 if contexts and name in ('default','waterfall') else 16):
        raise ValueError('App policy exceeds bounded Runtime capacity')
    if desk_clock and name=='default' and not any(g['capability']=='runtime.retained-wake' for g in grants):
        raise ValueError('Desk Clock requires explicit retained-wake authority')
    if sparse_clock and name=='default' and not all(any(g['capability']==cap for g in grants) for cap in ('runtime.realtime-control','runtime.provider-promotion')):
        raise ValueError('Sparse Clock requires explicit native-time and foreground-promotion authority')
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

def validate_native_composition(folder,candidate,runtime,platform_root=ROOT,native_source_root=None):
    """Require the exact platform boot hook, not just the shared Runtime version."""
    folder=Path(folder).resolve();platform_root=Path(platform_root)
    require=native_composition.require
    composition=candidate.get('x4_native_composition')
    require(isinstance(composition,dict),'X4 native composition required; generic Runtime is insufficient')
    assets=candidate.get('assets',{})
    required={'firmware.bin','firmware.elf','x4-native-composition.json','x4-native-proof.json','x4-runtime-options-proof.json'}
    require(isinstance(assets,dict) and required<=assets.keys(),'X4 native composition assets missing')
    blobs={}
    for name,digest in assets.items():
        native_composition.safe_relative(name)
        path=folder/name
        require(path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(folder),
                'Unsafe or missing native candidate asset: '+name)
        blob=path.read_bytes()
        require(digest=={'bytes':len(blob),'sha256':sha(blob)},'Native candidate asset hash mismatch: '+name)
        blobs[name]=blob
    record=json.loads(blobs['x4-native-composition.json'])
    require(record.get('schema')==native_composition.SCHEMA and record.get('schema_version')==1,
            'Invalid staged X4 composition schema')
    payload={key:value for key,value in record.items() if key!='composition_sha256'}
    require(sha(native_composition.encoded(payload))==record.get('composition_sha256'),
            'Staged X4 composition digest mismatch')
    lock=json.loads((platform_root/'minimal/sources.lock.json').read_text())['runtime']
    require(candidate.get('source_sha')==lock['commit'] and candidate.get('firmware_version')==lock['version'] and
            record['runtime']['commit']==lock['commit'] and record['runtime']['version']==lock['version'] and
            record['runtime']['repository']==lock['repository'],'Composed Runtime differs from the product lock')
    require(record.get('build_environment') in native_composition.ENVIRONMENTS and
            candidate.get('build_environment')==record['build_environment'] and
            candidate.get('target')==native_composition.ENVIRONMENTS[0] and
            candidate.get('layout')=='riscrte-paired-appdata-v2' and candidate.get('store_abi')==2,
            'Composed native environment/ABI mismatch')
    native_composition.verify_source_custody(runtime,record,native_source_root or platform_root)
    markers=('RTE_SOURCE='+lock['commit'],'RISC_RUNTIME_VERSION:'+lock['version'],
             'RISC_PAIRED_STORE_ABI:2','X4_NATIVE_COMPOSITION:'+record['composition_sha256'])
    for name in ('firmware.bin','firmware.elf'):
        require(all(marker.encode()+b'\0' in blobs[name] for marker in markers),
                'Compiled native composition/source marker mismatch: '+name)
        require(b'RISC_PAIRED_STORE_ABI:1\0' not in blobs[name],'Mixed compiled native ABI')
    proof=native_composition.startup_proof(blobs['firmware.elf'],record)
    require(json.loads(blobs['x4-native-proof.json'])==proof,'Staged X4 startup proof mismatch')
    options=native_composition.runtime_options_proof(blobs,record)
    require(json.loads(blobs['x4-runtime-options-proof.json'])==options,'Staged Runtime option proof mismatch')
    require(candidate.get('build_options')==record.get('build_options'),'Candidate Runtime options differ')
    expected={'build_options':record['build_options'],'runtime_options_proof':options,
              'composition_sha256':record['composition_sha256'],'runtime':record['runtime'],
              'platform':record['platform'],'platform_source_sha256':record['platform_source_sha256'],
              'startup_proof':proof}
    flash=native_composition.flash_profile.prove(blobs,record)
    if flash is not None:
        require('x4-boot-flash-proof.json' in blobs and json.loads(blobs['x4-boot-flash-proof.json'])==flash,
                'Staged DIO boot flash proof mismatch')
        expected['boot_flash_proof']=flash
    require(composition==expected,'Native candidate composition summary mismatch')
    return expected

def validate_settings_profile(manifest,blob,record,desk_clock=False,source=None):
    # The selected X4 power hook supports manual light sleep only. A generic
    # Light/Deep/Hybrid preference would promise modes this deployment ignores.
    if record.get('working_tree_dirty') is not False or record.get('version')!=manifest['version']:
        raise ValueError('Settings build receipt identity is unverified')
    if record.get('sha256')!=sha(blob) or record.get('size_bytes')!=len(blob):
        raise ValueError('Settings ELF differs from its build receipt')
    flags=record.get('build_defines')
    if not isinstance(flags,list) or not all(isinstance(flag,str) for flag in flags):
        raise ValueError('Settings feature selection is missing')
    if desk_clock:
        required=['-DPORTABLE_SLEEP_SETTINGS','-DPORTABLE_SETTINGS_X4_DESK_CLOCK','-DPORTABLE_DISPLAY_ROTATION=90','-DPORTABLE_RTC_WALL_TIME','-DPORTABLE_INPUT_NAVIGATION']
        if any(flag not in flags for flag in required) or any(flag.startswith('-DPORTABLE_SLEEP_SETTINGS=') or flag.startswith('-DPORTABLE_SETTINGS_X4_DESK_CLOCK=') for flag in flags):
            raise ValueError('Desk Settings requires the exact paper Light/Deep profile')
        expected={'settings_profile':'x4-desk-clock','sleep_modes':['light','deep'],'sleep_fallback':'light',
                  'desk_clock_faces':['Segments','Sans','Serif','Minimal','Railway','Deco'],
                  'preferences':{'instance':1,'sleep_key':'sleep_mode','face_key':'desk_clock_face'}}
        if any(record.get(key)!=value for key,value in expected.items()) or record.get('repository_commit')!=source or manifest['version']!='1.3.5':
            raise ValueError('Desk Settings profile or source differs from the locked deployment')
        return {'manual_light_sleep':True,'sleep_mode_selector':True,'deep_desk_clock':True,'hybrid':False,'default_mode':'light'}
    if any(flag.split('=')[0]=='-DPORTABLE_SLEEP_SETTINGS' for flag in flags):
        raise ValueError('X4 manual-light profile cannot expose deep/hybrid preferences')
    return {'manual_light_sleep':True,'sleep_mode_selector':False}
DESK_REQUIREMENTS={('display.output',1),('input.touch.raw',1),('rtc.clock',2),('board.battery',1),
    ('storage.key-value',1),('input.navigation',1),('alarm.service',1),('x4.power',1),
    ('runtime.retained-wake',1),('storage.volume',1),('net.wifi',1),('bluetooth.hci',1)}
SPARSE_REQUIREMENTS=DESK_REQUIREMENTS|{('runtime.realtime-control',1),('runtime.provider-promotion',1)}
def validate_sparse_clock_profile(manifest,blob,record,source,local_source,headers,tagged_alarm=False,broadcast=False,contexts=False):
    """Unselected future-profile gate; does not enable demand activation."""
    required_headers={'RiscRuntimeV1.h','RiscRealtimeV1.h','RiscProviderPromotionV1.h',
                      'RiscRetainedWakeV1.h','RiscDisplayOutputPowerV1.h','RiscTouchPowerV1.h',
                      'RiscStorageVolumeV1.h','RiscTimedSleepV1.h','RiscDeepSleepV1.h','RiscLightSleepV1.h'}
    if not required_headers.issubset(headers) or any(not re.fullmatch(r'[0-9a-f]{64}',value) for value in headers.values()):
        raise ValueError('Sparse Clock requires exact canonical lifecycle/native SDK hashes')
    version=native_time_cohort.VERSIONS['default'] if tagged_alarm else '0.3.2'
    expected={'working_tree_dirty':False,'desk_clock':True,'version':version,
              'clock_policy':'native-realtime-iana','sparse_start':True,'provider_activation':'demand',
              'timer_preferences':'retained-only','foreground_promotion':True,'invocation_retention':True,
              'display_rotation':90,'launcher_app':'springboard.elf','navigation':True,'sleep_capability':'x4.power',
              'alarm_client':True,'quick_actions':True,'quick_radios':True,'grant_count':(15 if broadcast else 14)+int(contexts),
              'repository_commit':source,'sha256':sha(blob),'size_bytes':len(blob),
              'local_sleep_source_sha256':sha(local_source),'desk_sdk_headers':headers,
              'retained_wake_sdk_sha256':headers['RiscRetainedWakeV1.h']}
    if tagged_alarm:
        flags=set(record.get('build_defines',[]))
        required={'-DPORTABLE_DESK_LOCK_HOME','-DPORTABLE_PAPER_TRANSITIONS','-DPORTABLE_PAPER_CROSSFADE','-DPORTABLE_STAGE_LOGS'}
        if not required.issubset(flags) or not record.get('paper_motion',{}).get('enabled') or not record.get('paper_transition',{}).get('enabled'):
            raise ValueError('Selected Clock requires explicit Home lock, paper motion, crossfade and plain diagnostics')
        expected['home_points']={'clock_policy':'native-utc','storage_instance':5,'foreground_only':True,'records':['points_utc_cfg','points_utc_meta'],'projection':'Utilities PointsUtcSchedule','model':'Watch nova_points_state','tap_app':'points_in_time.elf'}
    if manifest.get('id')!='paper_clock' or manifest.get('version')!=version or any(record.get(k)!=v for k,v in expected.items()):
        raise ValueError('Sparse Clock source, feature or artifact identity mismatch')
    requirements=manifest.get('requires',[])
    expected_requirements=(SPARSE_REQUIREMENTS-{('alarm.service',1)}|{('alarm.service',2)}) if tagged_alarm else SPARSE_REQUIREMENTS
    if broadcast:
        if not tagged_alarm:raise ValueError('Telemetry Clock requires native API2 profile')
        expected_requirements=expected_requirements|{('telemetry.broadcast',1)}
    if contexts:expected_requirements=expected_requirements|{('contexts.service',1)}
    if len(requirements)!=(15 if broadcast else 14)+int(contexts) or {(r['capability'],r['api']) for r in requirements}!=expected_requirements:
        raise ValueError('Sparse Clock requires exactly its fourteen typed capabilities')
    return {'record_type':'0x44434c4b','record_schema':1,'grant_count':((16 if broadcast else 15) if tagged_alarm else 14)+int(contexts),
            'home_points_foreground_only':bool(tagged_alarm),
            'provider_activation':'demand','timer_preferences':'retained-only',
            'foreground_promotion':True,'invocation_retention':True,
            'rtc_policy':'native-utc-with-explicit-iana-conversion','hardware_qualified':False}
def validate_desk_clock_profile(manifest,blob,record,source,local_source,headers):
    expected={'working_tree_dirty':False,'desk_clock':True,'version':'0.3.0','clock_policy':'rtc-wall-time',
              'display_rotation':90,'launcher_app':'springboard.elf','navigation':True,'sleep_capability':'x4.power',
              'alarm_client':True,'quick_actions':True,'quick_radios':True,'grant_count':12,'repository_commit':source,
              'sha256':sha(blob),'size_bytes':len(blob),'local_sleep_source_sha256':sha(local_source)}
    if manifest.get('id')!='paper_clock' or manifest.get('version')!='0.3.0' or any(record.get(k)!=v for k,v in expected.items()):
        raise ValueError('Desk Clock build identity or feature selection differs from the locked deployment')
    requirements=manifest.get('requires',[])
    if len(requirements)!=12 or {(r['capability'],r['api']) for r in requirements}!=DESK_REQUIREMENTS:
        raise ValueError('Desk Clock requires exactly the twelve bounded typed capabilities')
    if record.get('desk_sdk_headers')!=headers or record.get('retained_wake_sdk_sha256')!=headers['RiscRetainedWakeV1.h']:
        raise ValueError('Desk Clock SDK differs from the selected driver/native SDK')
    return {'record_type':'0x44434c4b','record_schema':1,'grant_count':12,'wake_inputs':['timer','GPIO3'],
            'hardware_qualified':False,'timer_boot':'full-admitted-driver-graph','rtc_policy':'wall-time'}
def validate_provider_source(product,root):
    hashes=product.get('local_source_hashes')
    if not isinstance(hashes,dict) or not hashes:raise ValueError('Missing provider source custody')
    for relative,digest in hashes.items():
        source=Path(relative)
        if source.is_absolute() or '..' in source.parts or not source.parts or source.parts[0]!='minimal':
            raise ValueError('Invalid provider source path')
        if not (root/source).is_file() or sha((root/source).read_bytes())!=digest:
            raise ValueError('Provider source differs from the selected product')
def load_module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def configure_boot_logging(boot, enabled):
    """Explicit diagnostic-cohort policy; no new app grant or eager graph."""
    if not enabled:return
    if boot.get('provider_activation') not in ('demand','demand-retained'):
        raise ValueError('Cold boot logging requires explicit demand provider policy')
    selected=[d for d in boot['drivers'] if d.get('instance_id')==9 and d.get('manifest')=='sd/manifest.json']
    if len(selected)!=1 or 'boot_start' in selected[0]:
        raise ValueError('Cold boot logging requires one unambiguous selected SD driver')
    selected[0]['boot_start']='cold'


def build(a):
    bootlog=bool(getattr(a,'boot_log',False))
    desk=getattr(a,'desk_clock',False)
    sparse=getattr(a,'sparse_clock',False)
    broadcast=getattr(a,'ble_telemetry',False)
    idle=getattr(a,'idle_policy',False)
    retain=getattr(a,'retain_promoted_providers',False)
    contexts=getattr(a,'contexts_rf_only',False)
    if contexts and not (sparse and broadcast and idle and retain):raise ValueError('Contexts requires the complete native retained foreground profile')
    selected_apps=APPS+(('contexts',) if contexts else ())
    if (idle or retain) and not (sparse and broadcast and getattr(a,'sleep',False)):
        raise ValueError('Idle/retained-demand profiles require complete explicit native telemetry sleep cohort')
    if broadcast and not sparse:raise ValueError('BLE telemetry requires the explicit native sparse cohort')
    if sparse and not desk:raise ValueError('Sparse cohort requires the explicit desk-clock graph')
    if sparse and not getattr(a,'app_compiler',None):raise ValueError('Native cohort requires the explicit pinned ELF compaction toolchain')
    if subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=ROOT,text=True).strip():raise ValueError('Clean product source required for bundle custody')
    if desk and not getattr(a,'sleep',False):raise ValueError('Desk Clock requires --sleep')
    inputs=json.loads(a.inputs.read_text());out=a.output.resolve()
    app_sources=json.loads((ROOT/'minimal/apps/sources.json').read_text())
    if bool(app_sources.get('features',{}).get('ble_telemetry'))!=broadcast:
        raise ValueError('Selected source cohort requires matching explicit --ble-telemetry profile')
    for feature,selected in [('idle_policy',idle),('retained_provider_demand',retain),('contexts_rf_only',contexts),('boot_logging',bootlog)]:
        if bool(app_sources.get('features',{}).get(feature))!=selected:raise ValueError('Source cohort and explicit profile differ: '+feature)
    idle_headers=idle_cohort.sdk_headers(a.drivers,a.runtime) if idle else None
    native_candidate=json.loads((a.native/'candidate.json').read_text())
    validate_native_composition(a.native,native_candidate,a.runtime,native_source_root=getattr(a,'native_source',None))
    expected_options={'app_policy_rows':17 if contexts else 16,'app_image_cache':True}
    if native_candidate.get('build_options')!=expected_options:raise ValueError('Native Runtime options differ from selected product')
    if out.exists():raise ValueError('Output already exists; no stale-image reuse')
    out.mkdir(parents=True);store=out/'store';stage(a.panel,store,getattr(a,"sleep",False),getattr(a,"panel_driver","fallback"))
    boot=json.loads((store/'boot.json').read_text());board=json.loads((store/'board.json').read_text())
    if sparse:boot['provider_activation']='demand-retained' if retain else 'demand'
    configure_boot_logging(boot,bootlog)
    products=json.loads((a.drivers/'products.json').read_text());products={p['id']:p for p in products}
    driver_origin=json.loads((a.drivers/'build-origin.json').read_text())
    custody={'shared_source_lock':json.loads((ROOT/'minimal/sources.lock.json').read_text()),'schema':1,'panel':a.panel,'panel_driver':getattr(a,'panel_driver','fallback'),'runtime':native_candidate,'x4_source':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'apps':{},'drivers':products,'verification':'Compiled and packaged test candidate. No device run. Extended validation and CI may still be pending.'}
    if sparse:
        custody['native_apps']={}
        custody['elf_compaction']={}
        compact_tool=load_module('x4_compact_elf',a.watch/'scripts/compact_current_elf.py')
    lock=custody['shared_source_lock']
    if custody['runtime']['source_sha']!=lock['runtime']['commit'] or custody['runtime']['firmware_version']!=lock['runtime']['version']:raise ValueError('Native candidate differs from locked Runtime source')
    if custody['runtime'].get('stage_logs') is not True or custody['runtime'].get('performance_trace') is not False or custody['runtime'].get('native_proof',{}).get('stage_logs',{}).get('enabled') is not True:raise ValueError('Diagnostic cohort requires automatic plain stage logs with recorder disabled')
    if driver_origin['source_fixture']:raise ValueError('Source-fixture drivers cannot be packaged')
    for name in ('runtime','shared'):
        source=driver_origin['sources'][name]
        if source['commit']!=lock[name]['commit'] or source['dirty']:raise ValueError('Driver dependency source custody mismatch: '+name)
    custody['driver_build_origin']=driver_origin
    for name,(_,folder,_) in selections(getattr(a,"sleep",False),getattr(a,"panel_driver","fallback")).items():
        path=store/folder;m=json.loads((path/'manifest.json').read_text());src=a.drivers/m['id'];blob=(src/'driver.elf').read_bytes()
        if sha(blob)!=products[m['id']]['sha256'] or json.loads((src/'manifest.json').read_text())!=m:raise ValueError('Driver identity mismatch: '+name)
        validate_provider_source(products[m['id']],ROOT)
        (path/'driver.elf').write_bytes(blob)
    for folder,key in [('ble','ble_provider'),('alarm','alarm_service'),('sensors','sensor_provider'),('wifi','wifi_provider'),('hid','hid_provider'),('iq','iq_provider')]+([('contexts','contexts_provider')] if contexts else []):
        src=Path(inputs[key]);dest=store/folder;dest.mkdir();m=json.loads((src/'manifest.json').read_text());blob=(src/'driver.elf').read_bytes()
        if sparse and key=='alarm_service':native_time_cohort.validate_alarm(m,blob,json.loads((src/'build-evidence.json').read_text()))
        if key=='contexts_provider':contexts_cohort.validate_provider(m,blob,json.loads((src/'build-evidence.json').read_text()))
        (dest/'manifest.json').write_bytes(encoded(m));(dest/'driver.elf').write_bytes(blob)
        custody[key]={'id':m['id'],'version':m['version'],'sha256':sha(blob)}
        evidence=out/'build-records'/'providers'/m['id'];evidence.mkdir(parents=True,exist_ok=True)
        notices=out/'licenses'/'providers'/m['id'];notices.mkdir(parents=True,exist_ok=True)
        for record in src.glob('*.json'):
            if record.name!='manifest.json':shutil.copyfile(record,evidence/record.name)
        if (src/'licenses').is_dir():shutil.copytree(src/'licenses',notices,dirs_exist_ok=True)
        for notice in src.iterdir():
            if notice.is_file() and ('LICENSE' in notice.name or 'NOTICE' in notice.name or notice.name.endswith('SOURCE.json')):shutil.copyfile(notice,notices/notice.name)
    board['devices'].append({'instance_id':16,'chip':{'vendor':'espressif','model':'esp32s3-ble','revision':'unspecified'},'compatible':'espressif,esp32s3-ble','config_type':'radio.integrated','config_version':1,'config':{'unit':0,'features':1}})
    boot['drivers'].append({'manifest':'ble/manifest.json','instance_id':16})
    boot['drivers'].append({'manifest':'sensors/manifest.json'})
    board['devices'].append({'instance_id':15,'chip':{'vendor':'espressif','model':'esp32s3-wifi','revision':'unspecified'},'compatible':'espressif,esp32s3-wifi','config_type':'radio.integrated','config_version':1,'config':{'unit':0,'features':1}})
    boot['drivers'].append({'manifest':'wifi/manifest.json','instance_id':15})
    boot['drivers'].append({'manifest':'iq/manifest.json'})
    if contexts:boot['drivers'].append({'manifest':'contexts/manifest.json'})
    boot['drivers'].append({'manifest':'hid/manifest.json','key_value':[{'key':k,'namespace':10,'access':'read-write'} for k in ('hid_ours','hid_peer','hid_ccc','hid_identity')]})
    keys=[('alarm_cfg',3,'read'),('timer_cfg',3,'read'),('alarm_occ',4,'read-write'),('timer_occ',4,'read-write'),('alert_mode',1,'read'),('points_cfg',5,'read'),('points_occ',4,'read-write'),('alert_dnd',1,'read')]
    if sparse:keys=native_time_cohort.ALARM_KEYS
    boot['drivers'].append({'manifest':'alarm/manifest.json','key_value':[{'key':k,'namespace':n,'access':v} for k,n,v in keys]})
    if not sparse:raise ValueError('X4 update applications require the complete native-time cohort')
    custody['update_providers'],update_selections=update_artifacts.stage_providers(store,out,inputs,app_sources['update_system_apps'],app_sources.get('update_firmware_routes_source'))
    boot['drivers'].extend(update_selections)
    policies=[];manifests={};licenses=out/'licenses';licenses.mkdir(exist_ok=True)
    for name in selected_apps:
        src=Path(inputs['apps'][name]);blob=(src/(name+'.elf')).read_bytes();m=json.loads((src/(name+'.json')).read_text())
        if m['file_name']!=name+'.elf' or m['type']!='application':raise ValueError('App identity mismatch: '+name)
        if name=='contexts':
            custody['native_apps'][name]=contexts_cohort.validate_app(m,blob,json.loads((src/'build-evidence.json').read_text()),(ROOT/'minimal/apps/portable_idle_sleep.c').read_bytes(),idle_headers)
        elif sparse:
            custody['native_apps'][name]=native_time_cohort.validate_app(name,m,blob,json.loads((src/'x4-native-app.json').read_text()),app_sources['native_cohort'][name],idle_headers)
        if idle and name!='contexts':idle_cohort.validate_app(name,m,custody['native_apps'][name],(ROOT/'minimal/apps/portable_idle_sleep.c').read_bytes(),idle_headers)
        if broadcast and name!='contexts':telemetry_cohort.validate_app(name,m,custody['native_apps'][name],contexts=contexts)
        if contexts and name in contexts_cohort.PROFILE['context_capability_owners']:contexts_cohort.validate_owner(name,m,custody['native_apps'][name])
        if name in ('ota_update','app_store'):update_artifacts.validate_app(name,m,blob,json.loads((src/'build-record.json').read_text()),app_sources['update_system_apps'])
        if name=='settings' and not sparse:custody['settings_power_ui']=validate_settings_profile(m,blob,json.loads((src/'settings-build-record.json').read_text()),desk,app_sources['settings_system_apps'])
        if name=='settings' and sparse:custody['settings_power_ui']={'manual_light_sleep':False,'sleep_mode_selector':False,'deep_desk_clock':True,'hybrid':False,'home_key_mode':'locked-deep-desk-clock','native_time_editing':True}
        if name=='default' and desk:
            headers={name:sha(((a.runtime/'sdk/app' if name=='RiscRetainedWakeV1.h' else a.runtime/'sdk/driver' if name in ('RiscTimedSleepV1.h','RiscLightSleepV1.h','RiscDeepSleepV1.h') else a.drivers/'sdk')/name).read_bytes()) for name in ('RiscDisplayOutputV1.h','RiscDisplayOutputPowerV1.h','RiscTouchV1.h','RiscTouchPowerV1.h','RiscStorageVolumeV1.h','RiscRetainedWakeV1.h','RiscTimedSleepV1.h','RiscLightSleepV1.h','RiscDeepSleepV1.h')}
            if sparse:
                for header in ('RiscRuntimeV1.h','RiscRealtimeV1.h','RiscProviderPromotionV1.h'):
                    headers[header]=sha((a.runtime/'sdk/app'/header).read_bytes())
                headers['RiscDisplayOutputSnapshotV1.h']=sha((a.drivers/'sdk/RiscDisplayOutputSnapshotV1.h').read_bytes())
                compatibility=native_time_cohort.IDLE_SDK.get('runtime_header_compatibility')
                if compatibility:
                    if headers['RiscRuntimeV1.h']!=compatibility['runtime_sha256'] or lock['runtime']['commit']!=compatibility['runtime_source']:
                        raise ValueError('Runtime app API differs from the reviewed append-only stream suffix')
                    headers['RiscRuntimeV1.h']=compatibility['compiled_sha256']
                if retain:
                    compatibility=native_time_cohort.IDLE_SDK['promotion_header_compatibility']
                    if headers['RiscProviderPromotionV1.h']!=compatibility['runtime_sha256']:raise ValueError('Promotion Runtime header differs from the reviewed documentation-only successor')
                    headers['RiscProviderPromotionV1.h']=compatibility['compiled_sha256']
            validator=validate_sparse_clock_profile if sparse else validate_desk_clock_profile
            custody['desk_clock']=validator(m,blob,json.loads((src/'build-evidence.json').read_text()),app_sources['desk_clock_system_apps'],(ROOT/'minimal/apps/portable_sleep.c').read_bytes(),headers,**({'tagged_alarm':True,'broadcast':broadcast,'contexts':contexts} if sparse else {}))
            for notice in ('LICENSE-NotoSans.txt','LICENSE-NotoSerif.txt','SOURCES.json'):
                if not (src/'licenses/desk_clock'/notice).is_file():raise ValueError('Desk Clock font custody is missing: '+notice)
        # The shared adapter exposes battery telemetry only with explicit grants.
        if not any(r['capability']=='board.battery' for r in m['requires']):m['requires'].append({'capability':'board.battery','api':1})
        requirements=[]
        for req in m['requires']:
            normalized={'capability':req['capability'],'api':req['api']}
            if normalized not in requirements:requirements.append(normalized)
        m['requires']=requirements
        grant_requirements=[r for r in m['requires'] if not broadcast or r['capability']!='telemetry.broadcast']
        grants=app_grants(name,grant_requirements,getattr(a,'sleep',False),desk,sparse,sparse,idle,contexts)
        manifests[name]=m
        if name=='file_browser' and sparse:
            custody['file_browser_storage']=file_browser_admission.validate(
                m,blob,json.loads((src/'file_browser-build-record.json').read_text()),
                custody['native_apps'][name],grants,app_sources['native_cohort'][name])
        policies.append({'manifest':name+'.json','grants':grants});(store/(name+'.elf')).write_bytes(blob);(store/(name+'.json')).write_bytes(encoded(m))
        if sparse:
            custody['elf_compaction'][name]=compact_tool.compact(store/(name+'.elf'),str(a.app_compiler),debug_path=out/'debug-originals'/(name+'.elf'))
            blob=(store/(name+'.elf')).read_bytes()
        custody['apps'][name]={'id':m['id'],'version':m['version'],'sha256':sha(blob),'manifest_sha256':sha(encoded(m))}
        records=out/'build-records'/name;records.mkdir(parents=True)
        for record in src.glob('*.json'):
            if record.name!=name+'.json':shutil.copyfile(record,records/record.name)
        if (src/'licenses').is_dir():shutil.copytree(src/'licenses',licenses/name)
        for notice in src.glob('LICENSE*'):
            (licenses/name).mkdir(exist_ok=True);shutil.copyfile(notice,licenses/name/notice.name)
    custody['sleep']=getattr(a,'sleep',False)
    custody['idle_policy']=idle
    custody['contexts_rf_only']=dict(contexts_cohort.PROFILE) if contexts else False
    custody['provider_activation']=boot.get('provider_activation','eager')
    custody['boot_logging']={'enabled':bootlog,'sd_path':'/x4-boot.log','boot_start':'cold' if bootlog else None,'initial_max_snapshots':9,'initial_max_text_bytes':13815,'deep_wake_activation':False}
    boot['app_capabilities']=policies
    if broadcast:
        pins=json.loads((ROOT/'minimal/apps/telemetry-sources.json').read_text())
        sources={identity:pins['utilities'] if identity=='telemetry-broadcast' else pins['drivers'] for identity in telemetry_cohort.PROVIDERS}
        telemetry_cohort.install_providers(store,inputs['telemetry_providers'],sources)
        boot=telemetry_cohort.extend_boot(boot,manifests,contexts=contexts)
        custody['ble_telemetry']={'enabled':True,'default':'off','sources':sources,'timer_only_acquisition':False}
        for identity in telemetry_cohort.PROVIDERS:
            src=Path(inputs['telemetry_providers'][identity]);evidence=out/'build-records/providers'/identity;evidence.mkdir(parents=True)
            for record in src.glob('*.json'):shutil.copyfile(record,evidence/record.name)
            if not (src/'licenses').is_dir():raise ValueError('Missing telemetry provider license custody')
            shutil.copytree(src/'licenses',licenses/'providers'/identity,dirs_exist_ok=True)
    # Preserve the full source/debug provider as a sidecar. The shared compactor
    # proves loaded segments, symbols and relocation semantics remain identical.
    # This recovers SPIFFS page headroom without removing an application.
    if sparse:
        hid_path=store/'hid/driver.elf'
        custody['provider_elf_compaction']={'hid/driver.elf':compact_tool.compact(hid_path,str(a.app_compiler),debug_path=out/'debug-originals/hid/driver.elf')}
        custody['hid_provider']['packaged_sha256']=sha(hid_path.read_bytes())
    (store/'boot.json').write_bytes(encoded(boot));(store/'board.json').write_bytes(encoded(board))
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
        custody['store_admission']=admit_cohort(a.runtime,(a.native/'firmware.elf').read_bytes(),files,files,app_policy_rows=expected_options['app_policy_rows'])
    native=load_module('x4_native_candidate',a.runtime/'scripts/paired_bank_images.py')
    loader=(a.native/'bootloader.bin').read_bytes();table=(a.native/'partitions.bin').read_bytes();data=(a.native/'appdata.bin').read_bytes()
    flash=custody['runtime'].get('x4_native_composition',{}).get('boot_flash_proof')
    expected_loader=native_composition.flash_profile.DIO_BOOTLOADER_SHA256 if flash else native.BOOTLOADER_SHA256
    if sha(loader)!=expected_loader or len(data)!=0x80000:raise ValueError('Native first-install inputs differ')
    image=out/'bootfs.bin'
    if getattr(a,'static_spiffs',False):
        shared_packer=load_module('x4_shared_bootfs',a.watch/'scripts/current_bootfs.py')
        filesystem,custody['store_generator']=shared_packer.build(files)
        image.write_bytes(filesystem)
    else:
        subprocess.run([str(a.mkspiffs),'-c',str(store),'-p','256','-b','4096','-s',str(0x510000),str(image)],check=True)
        custody['store_generator']={'producer':'legacy mkspiffs','partition_bytes':0x510000}

    filesystem=image.read_bytes()
    if len(filesystem)!=0x510000:raise ValueError('SPIFFS geometry mismatch')
    if not a.skip_extended_checks:
        from read_only_spiffs import read_image
        if read_image(filesystem,0x510000)!=files:raise ValueError('Packed SPIFFS differs from admitted source files')
        custody['store_image_verified']=True
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
    (out/'README.txt').write_text('X4 MINIMAL TEST BUILD - '+a.panel+'\n\nNEW 16 MiB paired/app-data layout only. Flashing the full BIN at0x0 overwrites firmware, partition table, NVS and app-data. Do not use as a data-preserving update. No device was flashed or physically tested. Select the panel variant explicitly; wrong-controller detection fails closed.\n\nBoot: clock; top-edge swipe down opens QuickActions, other clock swipes open Springboard; File Browser, Bluetooth Scanner with sensor details, Points in Time, Settings, Calculator, Stopwatch, Countdown, Timecard, Battery, Alarms, Wi-Fi, Bluetooth Touchpad, Bluetooth Buttons and RF Spectrogram. Serial Monitor is not included; implementation remains pending. RTC uses unconverted wall time. Alarms are visual-only; no audio/haptic hardware is simulated. File opening requires a declared installed handler; no arbitrary SD ELF execution. Center Home returns to Clock. The top-right short press returns to Clock from an app and requests manual light sleep from Clock; GPIO3 wakes it. Deep/hybrid/idle/touch wake is not implemented; unsupported sleep mode preferences are hidden in Settings. QuickActions radio toggles remain unselected; use Wi-Fi Settings and Bluetooth Scanner Enable BT before HID pairing. OTA/App Store are omitted because their service still selects the Watch feed. Deep-sleep desk clock is pending.\n\nExtra checks/CI wait were skipped for this requested accelerated test artifact. See build-custody.json for exact hashes and verification limits.\n')
    if desk:
        readme=out/'README.txt'
        text=readme.read_text().replace('requests manual light sleep from Clock; GPIO3 wakes it. Deep/hybrid/idle/touch wake is not implemented; unsupported sleep mode preferences are hidden in Settings.', 'requests the selected Light or Deep Desk Clock mode from Clock; Light is the default and GPIO3 wakes either mode. Deep mode preserves a six-face clock image and uses timer wakes for minute updates. Hybrid, automatic idle entry and touch wake are unavailable. GPIO/other/reset wakes return to normal UI.').replace('QuickActions radio toggles remain unselected;', 'Clock QuickActions includes radio policy controls; other apps keep their existing selection;').replace('Deep-sleep desk clock is pending.', 'Deep entry checks radio shutdown, alarms and panel/touch/SD/board power custody. Ordinary refusal returns to the Clock with radios off. Timer wakes currently start the full admitted driver graph. Native timer-arm latency, rail behavior and battery current remain unqualified.')
        readme.write_text(text)
    if sparse:
        readme=out/'README.txt';text=readme.read_text().replace('RTC uses unconverted wall time.', 'Native UTC is projected through the selected Reader/IANA timezone. Settings explicitly saves time with verified RTC/native updates.').replace('Timer wakes currently start the full admitted driver graph.', 'Valid retained minute wakes activate only the Clock/alarm dependency closure; normal interaction explicitly promotes the admitted foreground graph.').replace('other apps keep their existing selection;', 'other apps expose their selected controls;')
        text+='\nNative UTC alarm/countdown/Points records and Stopwatch state use isolated keys. Existing civil Timecard history is not reinterpreted. Reader timezone/language/flip and six clock faces are selected in Settings. Automatic idle sleep is still unavailable. This is a development test image, not hardware qualification.\n'
        readme.write_text(text)
    if not a.skip_extended_checks:
        readme=out/'README.txt'
        readme.write_text(readme.read_text().replace('Extra checks/CI wait were skipped for this requested accelerated test artifact.', 'The complete packaged store passed the production Runtime policy and ELF-admission preflight. Hardware operation remains unverified.'))
    if idle:
        release_notes=ROOT/'minimal/docs'/('BUILD_'+cohort['version'].replace('.','_')+'.md')
        if not release_notes.is_file():
            release_notes=ROOT/'minimal/docs'/('BUILD_0_1_21.md' if contexts else 'BUILD_0_1_20.md')
        (out/'README.txt').write_text(release_notes.read_text()+'\nExact app, provider, native-partition and image hashes are in build-custody.json. See updates/update-artifacts.json for local paired payload custody.\n')
    if not a.skip_extended_checks:
        custody['update_artifacts']=update_artifacts.create(out,cohort,fw,filesystem,files,full)
        (out/'build-custody.json').write_bytes(encoded(custody))
        readme=out/'README.txt';readme.write_text(readme.read_text().replace('OTA/App Store are omitted because their service still selects the Watch feed.', 'OTA Update and App Store use explicit X4 native UTC/API2 providers. No X4 release feed is configured, so they truthfully report no available update. Local updates/ contains source-bound candidate artifacts only; no release or tag was created.'))
    notes=ROOT/'minimal/docs'/('REPAIR_'+cohort['version'].replace('.','')+'.md')
    if notes.is_file():
        readme=out/'README.txt';readme.write_text(readme.read_text()+'\n'+notes.read_text())
    if sparse:
        readme=out/'README.txt';text=readme.read_text()
        text=text.replace('requests the selected Light or Deep Desk Clock mode from Clock; Light is the default and GPIO3 wakes either mode.', 'locks the landscape Deep Desk Clock after key release; a later GPIO3 press wakes to Home.')
        readme.write_text(text)
    archive=out.with_suffix('.zip')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(out.rglob('*')):
            if p.is_file():z.write(p,p.relative_to(out))
    print(json.dumps({'bin':str(out/filename),'bin_sha256':sha(full),'archive':str(archive),'archive_sha256':sha(archive.read_bytes())}))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['inputs','drivers','native','runtime','watch','mkspiffs','output']:p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--native-source',type=Path,help='Exact clean X4 source checkout recorded by the native composition; default is this product checkout')
    p.add_argument('--static-spiffs',action='store_true',help='Use the shared qualified static producer for dense immutable stores')
    p.add_argument('--app-compiler',type=Path,help='Pinned application compiler whose objcopy preserves verified loader semantics')
    p.add_argument('--boot-log',action='store_true',help='Explicit diagnostic cohort: cold-boot SD activation and persistent boot export; deep wakes stay demand-only')
    p.add_argument('--sparse-clock',action='store_true',help='Explicit complete native-time/API2 cohort with demand startup')
    p.add_argument('--contexts-rf-only',action='store_true',help='Explicit Contexts UI, RF model owner and sparse Clock rendezvous')
    p.add_argument('--idle-policy',action='store_true',help='Explicit reversible Light and low-battery policy across the selected cohort')
    p.add_argument('--retain-promoted-providers',action='store_true',help='Explicit sparse lazy first-use provider retention after foreground promotion')
    p.add_argument('--ble-telemetry',action='store_true',help='Explicit foreground-only X4 BLE broadcast cohort');p.add_argument('--desk-clock',action='store_true',help='Explicit Clock retained wake and six-face Light/Deep Settings profile');p.add_argument('--sleep',action='store_true',help='Explicit GPIO3 power graph and Clock-only sleep authority');p.add_argument('--panel',choices=['ssd1677','uc8279'],required=True);p.add_argument('--panel-driver',choices=['fallback','uc8279-fast'],default='fallback');p.add_argument('--skip-extended-checks',action='store_true');build(p.parse_args())
