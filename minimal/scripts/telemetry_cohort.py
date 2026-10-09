"""Explicit X4 BLE telemetry composition; never infer grants from linked code."""
import copy,hashlib,json
from pathlib import Path
DRIVERS='4088b6892c2e2654b0342f04a7d191068e4a8e2e'
VERSIONS={'default': '0.3.12', 'springboard': '1.7.9', 'settings': '1.3.17', 'file_browser': '1.5.12', 'wifi_settings': '1.1.14', 'battery': '1.1.9', 'calculator': '0.1.16', 'stopwatch': '0.1.16', 'countdown': '0.1.15', 'timecard': '0.2.7', 'points_in_time': '0.6.7', 'ble_scanner': '0.2.12', 'ble_touchpad': '0.1.11', 'ble_buttons': '0.1.11', 'waterfall': '0.2.7', 'alarms': '0.2.11', 'ota_update': '1.2.4', 'app_store': '1.2.4'}
PROVIDERS={'telemetry-battery':('battery-telem',[('board.battery',1)],'sensor.telemetry'),
 'ble-telemetry':('ble-telem',[('bluetooth.hci',1),('platform.clock',1),('sensor.telemetry',1)],'bluetooth.telemetry'),
 'telemetry-broadcast':('broadcast',[('platform.clock',1),('bluetooth.telemetry',1)],'telemetry.broadcast')}
EXCLUDED={'ble_scanner','ble_touchpad','ble_buttons','waterfall'}
CAPABILITY={'capability':'telemetry.broadcast','api':1}
GRANT={**CAPABILITY,'instance_id':0}
def validate_app(name,manifest,receipt):
 if name not in VERSIONS or manifest.get('version')!=VERSIONS[name]:raise ValueError('Wrong telemetry app/version: '+name)
 requirements=manifest.get('requires',[])
 if any(type(r.get('api')) is not int for r in requirements) or len({(r['capability'],r['api']) for r in requirements})!=len(requirements):raise ValueError('Invalid or duplicate telemetry app declaration')
 if requirements.count(CAPABILITY)!=1 or len(requirements)>16:raise ValueError('Missing/duplicate/unbounded telemetry declaration')
 flags=set(receipt.get('build_defines',[]))
 if not {'-DPORTABLE_BLE_BROADCAST','-DPORTABLE_BLE_BROADCAST_DEFAULT_OFF'}<=flags:raise ValueError('Missing explicit telemetry client/default profile')
 if ('-DPORTABLE_BLE_FOREGROUND' in flags)!=(name in EXCLUDED):raise ValueError('Competing BLE/IQ foreground selection differs')
 if not any(r=={'capability':'storage.key-value','api':1} for r in requirements):raise ValueError('Missing shared preference authority')
 if not any(r=={'capability':'alarm.service','api':2} for r in requirements):raise ValueError('Native alarm API2 was lost')
 if name=='default' and (len(requirements)!=15 or receipt.get('ble_broadcast',{}).get('timer_only') is not False):raise ValueError('Clock telemetry must be foreground-only with15 declarations')
def validate_provider(identity,manifest,blob,record,source):
 if identity not in PROVIDERS:raise ValueError('Unknown telemetry provider')
 folder,requires,provides=PROVIDERS[identity]
 if manifest.get('id')!=identity or manifest.get('version')!='0.1.0' or manifest.get('driver_abi')!=2:raise ValueError('Telemetry provider identity/ABI mismatch')
 if manifest.get('requires')!=[{'capability':c,'api':v} for c,v in requires] or manifest.get('provides')!=[{'capability':provides,'api':1}]:raise ValueError('Telemetry provider authority differs')
 if record.get('source_revision')!=source or record.get('source_dirty') is not False or record.get('sha256')!=hashlib.sha256(blob).hexdigest() or record.get('size_bytes')!=len(blob):raise ValueError('Telemetry source/ELF custody mismatch')
 return folder

def extend_boot(boot,manifests):
 result=copy.deepcopy(boot)
 if result.get('provider_activation') not in ('demand','demand-retained'):raise ValueError('Telemetry cohort retains sparse demand boot')
 rows=result['app_capabilities']
 if set(manifests)!=set(VERSIONS) or {r['manifest'].removesuffix('.json') for r in rows}!=set(VERSIONS):raise ValueError('Incomplete telemetry foreground cohort')
 for row in rows:
  name=row['manifest'].removesuffix('.json');m=manifests[name]
  if m.get('version')!=VERSIONS[name] or m.get('requires',[]).count(CAPABILITY)!=1:raise ValueError('App was not rebuilt for telemetry: '+name)
  if any(g.get('capability')=='telemetry.broadcast' for g in row['grants']):raise ValueError('Telemetry grant already selected')
  row['grants'].append(dict(GRANT))
  if len({(g['capability'],g['api'],g['instance_id']) for g in row['grants']})!=len(row['grants']):raise ValueError('Duplicate telemetry cohort grant')
  if any(type(g.get('api')) is not int or type(g.get('instance_id')) is not int for g in row['grants']):raise ValueError('Invalid telemetry cohort grant')
  if len(row['grants'])>16 or len(m['requires'])>16:raise ValueError('Runtime16-grant limit exceeded')
  pairs={(g['capability'],g['api']) for g in row['grants']}
  if pairs!={(r['capability'],r['api']) for r in m['requires']}:raise ValueError('App declarations/grants differ: '+name)
  if not any(g=={'capability':'storage.key-value','api':1,'instance_id':1} for g in row['grants']):raise ValueError('Shared namespace1 absent')
 for folder,_,_ in PROVIDERS.values():
  path=folder+'/manifest.json'
  if any(r['manifest']==path for r in result['drivers']):raise ValueError('Telemetry provider already selected')
  result['drivers'].append({'manifest':path})
 if len(result['drivers'])>24:raise ValueError('Runtime24-provider limit exceeded')
 return result

def install_providers(store,inputs,source_pins):
 store=Path(store)
 for identity in PROVIDERS:
  source=Path(inputs[identity]);manifest=json.loads((source/'manifest.json').read_text());blob=(source/'driver.elf').read_bytes();record=json.loads((source/'build-record.json').read_text())
  folder=validate_provider(identity,manifest,blob,record,source_pins[identity]);dest=store/folder
  if dest.exists():raise ValueError('Refuse to replace an existing provider')
  dest.mkdir();(dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');(dest/'driver.elf').write_bytes(blob)
