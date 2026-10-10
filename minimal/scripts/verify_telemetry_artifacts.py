#!/usr/bin/env python3
"""Verify actual rebuilt Clock/utility declarations, source receipts and provider bytes."""
import argparse,hashlib,json
from pathlib import Path
import build_test_bundle as base
import telemetry_cohort as telemetry
ROOT=Path(__file__).resolve().parents[2];pins=json.loads((ROOT/'minimal/apps/telemetry-sources.json').read_text())
p=argparse.ArgumentParser();p.add_argument('--clock',type=Path,required=True);p.add_argument('--utilities',type=Path,required=True);p.add_argument('--providers',type=Path,required=True);a=p.parse_args();rows={}
for name,folder in [('default',a.clock),*[(n,a.utilities/n) for n in ('battery','calculator','stopwatch','ble_scanner','ble_touchpad','ble_buttons','waterfall')]]:
 manifest=json.loads((folder/(name+'.json')).read_text());record=json.loads((folder/('build-evidence.json' if name=='default' else 'x4-native-app.json')).read_text());blob=(folder/(name+'.elf')).read_bytes();telemetry.validate_app(name,manifest,record)
 assert (record['sha256'] if name=='default' else record['elf_sha256'])==hashlib.sha256(blob).hexdigest()
 if name=='default':assert record['working_tree_dirty'] is False and record['repository_commit']==pins['system_apps']
 else:assert record['source_dirty'] is False and record['system_dirty'] is False and record['source_revision']==pins['utilities'] and record['system_source_revision']==pins['system_apps']
 prior=[r for r in manifest['requires'] if r!=telemetry.CAPABILITY];grants=base.app_grants(name,prior,True,True,True,True)+[telemetry.GRANT];assert len(grants)<=16
 assert {(r['capability'],r['api']) for r in manifest['requires']}=={(r['capability'],r['api']) for r in grants}
 if name=='default':assert len(manifest['requires'])==15 and len(grants)==16
 rows[name]={'version':manifest['version'],'declarations':len(manifest['requires']),'policy_grants':len(grants),'sha256':hashlib.sha256(blob).hexdigest()}
for name in telemetry.PROVIDERS:
 folder=a.providers/name;telemetry.validate_provider(name,json.loads((folder/'manifest.json').read_text()),(folder/'driver.elf').read_bytes(),json.loads((folder/'build-record.json').read_text()),pins['utilities' if name=='telemetry-broadcast' else 'drivers'])
print(json.dumps(rows,indent=2));print('Eight actual app declarations/grants and three provider custody records PASS')
