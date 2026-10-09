"""Admission of the explicit UTC/API2 cohort; ordinary profiles remain separate."""
import hashlib
import re
import json
from pathlib import Path

RUNTIME = '30dcec5ce6ce33223f2b203a2399283e1f758567'
ALARMS = '637e13b0bce62ad49b756bec2468a6271d163fc7'
PERFORMANCE = json.loads((Path(__file__).resolve().parents[1]/'performance-sdk.json').read_text())
DIAGNOSTIC_APPS = {'default','springboard','settings'}
def expected_sdk(name):
    if name not in DIAGNOSTIC_APPS:return RUNTIME, dict(SDK)
    headers=dict(PERFORMANCE['headers'])
    if name=='default':headers.update(PERFORMANCE['clock_overrides'])
    if name in ('default','springboard'):headers.update(PERFORMANCE['crossfade_headers'])
    return PERFORMANCE['runtime_source'],headers

SDK = {
 'RiscRuntimeV1.h':'2f1b785d65729c0866691570bb920299a8c5187765f3eac204df3cb427ce9c84',
 'RiscRealtimeV1.h':'639781f728841cda6d40c3033877436b9e5d59519a4b15e11a9bc99b2365395d',
 'AlarmServiceV1.h':'c70c087550306c123255e1ace41aeb504809e16b0193e382ec8dfbd7cb5531f5',
 'AlarmServiceV2.h':'d7e750a8093e1b5e698483254edc38533b7242a3446d0bca5866c9dfd6ad07a1',
}
from telemetry_cohort import VERSIONS

ALARM_KEYS = [('alarm_utc_cfg',3,'read'),('timer_utc_cfg',3,'read'),
 ('alarm_utc_occ',4,'read-write'),('timer_utc_occ',4,'read-write'),
 ('alert_mode',1,'read'),('points_utc_cfg',5,'read'),
 ('points_utc_occ',4,'read-write'),('alert_dnd',1,'read'),('time_zone',1,'read')]

def validate_app(name, manifest, blob, receipt, source):
    if name not in VERSIONS or not re.fullmatch(r'[0-9a-f]{40}',source):
        raise ValueError('Unknown native app or unpinned source')
    runtime_source, compiled_sdk = expected_sdk(name)
    expected = {'schema':1,'app':name,'version':VERSIONS[name],
        'source_revision':source,'runtime_source_revision':runtime_source,
        'alarm_source_revision':ALARMS,'alarm_api':2,'time_policy':'native-realtime-iana',
        'elf_sha256':hashlib.sha256(blob).hexdigest(),'elf_bytes':len(blob),
        'requires':manifest.get('requires'),'sdk_sha256':compiled_sdk}
    if any(receipt.get(k)!=v for k,v in expected.items()):
        raise ValueError('Native app receipt/source/SDK/ELF mismatch: '+name)
    if not re.fullmatch(r'[0-9a-f]{40}',receipt.get('system_source_revision','')):
        raise ValueError('Unpinned shared app adapter: '+name)
    repositories={'michaelrolphone-cmyk/RiscRTE-System-Apps',
                  'michaelrolphone-cmyk/RiscRTE-Utilities',
                  'michaelrolphone-cmyk/RiscRTE-Productivity'}
    if receipt.get('source_repo') not in repositories:
        raise ValueError('Unrecognized native app source: '+name)
    if manifest.get('version')!=VERSIONS[name] or manifest.get('file_name')!=name+'.elf':
        raise ValueError('Native app package identity mismatch: '+name)
    requirements=manifest.get('requires',[])
    if any(type(r.get('api')) is not int for r in requirements):
        raise ValueError('Native capability version is not an integer: '+name)
    caps={(r['capability'],r['api']) for r in requirements}
    if len(caps)!=len(requirements) or ('alarm.service',2) not in caps or ('alarm.service',1) in caps:
        raise ValueError('Mixed or duplicate alarm ABI: '+name)
    if ('storage.key-value',1) not in caps:
        raise ValueError('Native preference authority missing: '+name)
    control=name in ('default','settings')
    time_cap='runtime.realtime-control' if control else 'runtime.realtime'
    if (time_cap,1) not in caps or any(c.startswith('runtime.realtime') and c!=time_cap for c,_ in caps):
        raise ValueError('Native time authority differs from app role: '+name)
    if not control and any(c=='rtc.clock' for c,_ in caps):
        raise ValueError('Foreground native app cannot retain raw RTC authority: '+name)
    return dict(receipt)

def validate_alarm(manifest, blob, record):
    if manifest.get('version')!='0.4.4' or manifest.get('provides')!=[{'capability':'alarm.service','api':2}]:
        raise ValueError('Native cohort requires the exact tagged alarm provider')
    expected={('storage.key-value.bound',1),('platform.clock',1),('platform.realtime',1)}
    if {(r['capability'],r['api']) for r in manifest.get('requires',[])}!=expected:
        raise ValueError('Native alarm dependency authority mismatch')
    if record.get('source_sha')!=ALARMS or record.get('runtime_sha')!=RUNTIME or record.get('working_tree_dirty') is not False:
        raise ValueError('Native alarm source custody mismatch')
    rows=[r for r in record.get('modules',[]) if r.get('profile')=='native-utc']
    if len(rows)!=1 or rows[0].get('sha256')!=hashlib.sha256(blob).hexdigest() or rows[0].get('size_bytes')!=len(blob):
        raise ValueError('Native alarm ELF receipt mismatch')
