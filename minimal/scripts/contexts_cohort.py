"""Explicit RF-only Contexts composition over the ordinary shared service."""
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROFILE = json.loads((ROOT/'minimal/contexts-profile.json').read_text())
CAPABILITY = {'capability':'contexts.service','api':1}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def require(ok, detail):
    if not ok: raise ValueError(detail)

def validate_app(manifest, blob, receipt, helper, headers):
    profile = PROFILE
    require(all(type(r.get('api')) is int for r in manifest.get('requires',[])),
            'Contexts capability versions must be integers')
    expected = {'schema':1, 'profile':'x4-native-paper-contexts',
                'version':profile['app_version'], 'source_revision':profile['app_source'],
                'source_dirty':False, 'system_revision':profile['app_system'], 'system_dirty':False,
                'runtime_sdk_revision':profile['runtime_sdk'], 'alarm_sdk_revision':profile['alarm_sdk'],
                'elf_sha256':sha(blob), 'elf_bytes':len(blob), 'required_grants':profile['app_grants'],
                'idle_helper_sha256':sha(helper),
                'automatic_idle':'Light only, preserved drafts, no capture restart'}
    require(all(receipt.get(k)==v for k,v in expected.items()), 'Contexts app source/SDK/ELF custody mismatch')
    require(manifest == {'type':'application','id':'contexts','version':profile['app_version'],
        'architecture':'xtensa-esp32s3','file_name':'contexts.elf','entry':'app_main',
        'requires':[{'capability':g['capability'],'api':g['api']} for g in profile['app_grants']]},
        'Contexts app declaration differs from selected profile')
    required = {'-DPORTABLE_CONTEXTS_PAPER','-DPORTABLE_CONTEXTS_CLIENT','-DPORTABLE_CONTEXTS_EDITOR',
        '-DPORTABLE_NATIVE_CUSTODY_FENCE','-DPORTABLE_NATIVE_TIME_TOOLBAR','-DALARM_SERVICE_TAGGED_V2',
        '-DPORTABLE_QUICK_ACTIONS','-DPORTABLE_QUICK_RADIOS','-DPORTABLE_APP_LAUNCH_GUARD',
        '-DPORTABLE_X4_IDLE_POLICY','-DPORTABLE_LOW_BATTERY','-DPORTABLE_APP_SLEEP_LOCAL',
        '-DPORTABLE_INPUT_NAVIGATION','-DPORTABLE_APP_TOUCH_SCROLL','-DPORTABLE_TOUCH_SCROLL',
        '-DPORTABLE_DISPLAY_ROTATION=90','-DPORTABLE_HOME_APP="default.elf"'}
    require(required <= set(receipt.get('build_defines',[])), 'Incomplete Contexts paper lifecycle profile')
    deps = receipt.get('compiled_dependencies_sha256',{})
    for name,digest in {**profile['sdk_sha256'], **headers}.items():
        require(deps.get('CompiledSDK/'+name)==digest, 'Contexts compiled SDK differs: '+name)
    require(receipt.get('exports')==['app_main','app_module_fini','app_module_init'], 'Contexts app exports differ')
    require(set(receipt.get('imports',[])) <= {'risc_runtime_get_api','memcpy','memset','memcmp','strcmp',
        'strlen','snprintf','malloc','calloc','free','strcpy','strncmp','memchr'}, 'Unexpected Contexts import')
    return dict(receipt)

def validate_provider(manifest, blob, record):
    expected_deps = [{'capability':'platform.clock','api':1},{'capability':'radio.iq','api':1}]
    require(all(type(r.get('api')) is int for r in manifest.get('requires',[])+manifest.get('provides',[])),
            'Contexts provider capability versions must be integers')
    require(manifest.get('id')=='contexts-service' and manifest.get('version')==PROFILE['service_version']
        and manifest.get('driver_abi')==2 and manifest.get('architecture')=='xtensa-esp32s3'
        and manifest.get('file_name')=='driver.elf' and manifest.get('requires')==expected_deps
        and manifest.get('provides')==[CAPABILITY], 'Contexts RF-only provider identity or dependencies differ')
    require(record.get('profile')=='rf-only' and record.get('source_mask')==2
        and record.get('requires')==expected_deps and record.get('version')==PROFILE['service_version']
        and record.get('elf_sha256')==sha(blob) and record.get('size_bytes')==len(blob),
        'Contexts provider artifact/profile custody mismatch')
    for name,pin in PROFILE['service_sources'].items():
        require(re.fullmatch('[a-f0-9]{40}',pin) is not None and
                record.get('sources',{}).get(name)=={'commit':pin,'dirty':False},
                'Contexts provider source differs: '+name)
    require(record.get('models',{}).get('audio') is False and record.get('models',{}).get('radio') is True,
            'Contexts source availability differs')
    require(record.get('exports')==['t5_driver_get'] and
        set(record.get('imports',[])) <= {'memchr','memcmp','memcpy','memset','strcmp','strlen'},
        'Contexts provider imports/exports differ')
    return dict(record)

def validate_owner(name, manifest, receipt):
    require(name in PROFILE['context_capability_owners'] and manifest['requires'].count(CAPABILITY)==1,
            'Contexts authority missing or assigned to an unselected owner')
    require('-DPORTABLE_CONTEXTS_CLIENT' in receipt.get('build_defines',[]), 'Contexts owner was not compiled')
    require(len(manifest['requires'])<=16, 'Contexts owner exceeds the manifest bound')
    if name=='waterfall':
        require(receipt.get('contexts_rf_only')=={'enabled':True,'source_mask':2,
            'owner':'waterfall.elf','policy_rows':17}, 'RF model owner is not the selected profile')
        require('-DPORTABLE_BLE_FOREGROUND' in receipt['build_defines'], 'RF owner lacks radio arbitration')
