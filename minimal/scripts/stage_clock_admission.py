#!/usr/bin/env python3
"""Derive native Clock admission from its exact compiled inputs and build receipt."""
import argparse,hashlib,json
from pathlib import Path
from native_time_cohort import ALARMS,expected_sdk
from idle_cohort import sdk_headers,validate_app

def stage(folder,drivers,runtime,helper):
 folder=Path(folder);record=json.loads((folder/'build-evidence.json').read_text());manifest=json.loads((folder/'default.json').read_text());blob=(folder/'default.elf').read_bytes()
 if record.get('working_tree_dirty') is not False or record.get('sha256')!=hashlib.sha256(blob).hexdigest() or record.get('size_bytes')!=len(blob):raise ValueError('Clock source/ELF mismatch')
 if record.get('clock_policy')!='native-realtime-iana' or not record.get('sparse_start') or record.get('version')!=manifest['version']:raise ValueError('Not the selected native sparse Clock')
 runtime_source,expected=expected_sdk('default');expected.update(sdk_headers(drivers,runtime))
 if record.get('performance_trace',{}).get('commit')!=runtime_source or record.get('tagged_alarm_sdk',{}).get('commit')!=ALARMS:raise ValueError('Unexpected Clock Runtime/alarm provenance')
 includes=Path(record['idle_policy']['compiled_include_directory'])
 actual={name:hashlib.sha256((includes/name).read_bytes()).hexdigest() for name in expected}
 if actual!=expected:raise ValueError('Clock compiled SDK differs from cohort')
 receipt={'schema':1,'app':'default','version':manifest['version'],'source_repo':'michaelrolphone-cmyk/RiscRTE-System-Apps',
 'source_revision':record['repository_commit'],'system_source_revision':record['repository_commit'],
 'runtime_source_revision':runtime_source,'alarm_source_revision':ALARMS,'alarm_api':2,'time_policy':'native-realtime-iana',
 'elf_sha256':record['sha256'],'elf_bytes':len(blob),'requires':manifest['requires'],'sdk_sha256':actual,
 'build_defines':record['build_defines'],'ble_broadcast':record['ble_broadcast'],'idle_policy':record['idle_policy']}
 validate_app('default',manifest,receipt,Path(helper).read_bytes(),sdk_headers(drivers,runtime))
 (folder/'x4-native-app.json').write_text(json.dumps(receipt,indent=2)+'\n')
 return receipt
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for key in ('folder','drivers','runtime','helper'):p.add_argument('--'+key,type=Path,required=True)
 a=p.parse_args();stage(a.folder,a.drivers,a.runtime,a.helper)
