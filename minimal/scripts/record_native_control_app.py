#!/usr/bin/env python3
"""Record a built native Clock/Settings cohort receipt from verified build inputs."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
import native_time_cohort as cohort

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--app',required=True,choices=('default','settings'))
p.add_argument('--input',type=Path,required=True)
p.add_argument('--system',type=Path,required=True)
a=p.parse_args()
source=subprocess.check_output(['git','-C',str(a.system),'rev-parse','HEAD'],text=True).strip()
assert not subprocess.check_output(['git','-C',str(a.system),'status','--porcelain']).strip(),'Source is dirty'
record=json.loads((a.input/('build-evidence.json' if a.app=='default' else 'settings-build-record.json')).read_text())
manifest=json.loads((a.input/(a.app+'.json')).read_text())
blob=(a.input/(a.app+'.elf')).read_bytes()
digest=hashlib.sha256(blob).hexdigest()
assert record['repository_commit']==source and record['working_tree_dirty'] is False
assert record['sha256']==digest and record['size_bytes']==len(blob)
assert record['version']==manifest['version']
runtime_source,expected_headers=cohort.expected_sdk(a.app)
if record.get('paper_transition',{}).get('enabled'):
    # Crossfade copies the canonical prefixes into the final include tree;
    # tagged alarm headers are installed there after diagnostic SDK staging.
    include=a.input/'paper-sdk/include'
    assert include.resolve()==Path(record['paper_transition']['compiled_include_directory']).resolve()
else:
    include=a.input/('performance-sdk/include' if record.get('performance_trace') else 'desk-sdk/include' if a.app=='default' else 'native-time-sdk/include')
headers={name:hashlib.sha256((include/name).read_bytes()).hexdigest() for name in expected_headers}
assert headers==expected_headers,'Compiled SDK headers differ from the cohort'
tagged=json.loads((a.input/'tagged-alarm-sdk.json').read_text())
assert tagged['commit']==cohort.ALARMS and tagged['api']==2
receipt={'schema':1,'app':a.app,'version':manifest['version'],
 'source_repo':'michaelrolphone-cmyk/RiscRTE-System-Apps','source_revision':source,
 'system_source_revision':source,'runtime_source_revision':runtime_source,
 'alarm_source_revision':cohort.ALARMS,'alarm_api':2,'time_policy':'native-realtime-iana',
 'elf_sha256':digest,'elf_bytes':len(blob),'requires':manifest['requires'],'sdk_sha256':headers,
 'build_record_sha256':hashlib.sha256((a.input/('build-evidence.json' if a.app=='default' else 'settings-build-record.json')).read_bytes()).hexdigest()}
cohort.validate_app(a.app,manifest,blob,receipt,source)
(a.input/'x4-native-app.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(a.app+': canonical native control receipt recorded')
