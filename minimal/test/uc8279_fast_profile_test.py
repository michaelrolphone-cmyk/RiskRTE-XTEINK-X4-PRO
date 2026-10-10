#!/usr/bin/env python3
"""Fast transport is opt-in; fallback profile/provider identity stays exact."""
import importlib.util
import json
from pathlib import Path
import tempfile

root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('profile',root/'minimal/scripts/generate_profile.py')
profile=importlib.util.module_from_spec(spec);spec.loader.exec_module(profile)
for chip in ['ssd1677','uc8279']:
    legacy=profile.profile(chip)
    assert legacy['buses'][1]['frequency_hz']==1000000
    assert profile.selections()['panel'][2]=='panel'
for sleep in [False,True]:
    with tempfile.TemporaryDirectory() as tmp:
        profile.stage('uc8279',tmp,sleep,'uc8279-fast')
        folder=Path(tmp);board=json.loads((folder/'board.json').read_text());manifest=json.loads((folder/'panel/manifest.json').read_text())
        assert board['buses'][1]['frequency_hz']==20000000
        assert manifest['id']=='x4pro-uc8279-fast' and manifest['version']=='0.1.18'
        assert {'capability':'spi.bus','api':1} in manifest['requires']
        assert [x['compatible'] for x in manifest['hardware_compatibility']]==['ultrachip,uc8279']
        assert len(json.loads((folder/'boot.json').read_text())['drivers'])==(10 if sleep else 9)
try:profile.profile('ssd1677',False,'uc8279-fast')
except ValueError:pass
else:raise AssertionError('SSD cannot select the UC-only fast driver')
print('UC8279 fast profile: explicit selection, 20MHz typed bus, unchanged fallback, SSD rejection PASS')
