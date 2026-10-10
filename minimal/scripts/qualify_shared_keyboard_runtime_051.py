#!/usr/bin/env python3
"""Replay selected clients and UI services over production Runtime/Graph host hooks.

Use the native-owned fixture with the final product's portrait profile and the
per-app target receipt. Peripheral providers remain explicit synthetic fixtures.
"""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runtime', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a, extra = p.parse_known_args()
a.output.mkdir(parents=True, exist_ok=True)
source = a.runtime / 'test/run_shared_keyboard_resident.py'
original = source.read_text()
script = original
replacements = {
    'ROOT = Path(__file__).resolve().parents[1]': f'ROOT = Path({str(a.runtime.resolve())!r})',
    "points_receipt['built']['points_in_time']['build_defines']": "points_receipt['build_defines']",
    "'-DSCENE_DISPLAY_ROTATION=90'": "'-DSCENE_DISPLAY_ROTATION=270'",
    "'runtime_version':'0.1.99'": "'runtime_version':'0.1.100'",
    "inventory(ROOT, [Path(__file__).resolve(), *ROOT.glob('test/shared_keyboard*')])":
        "inventory(ROOT, [ROOT/'test/run_shared_keyboard_resident.py', *ROOT.glob('test/shared_keyboard*')])",
}
for old, new in replacements.items():
    if script.count(old) != 1:
        raise ValueError('Native fixture changed: ' + old)
    script = script.replace(old, new)
runner = a.output / 'run_selected_fixture.py'
runner.write_text(script)
record = {
    'source': str(source),
    'source_sha256': hashlib.sha256(original.encode()).hexdigest(),
    'adapted_sha256': hashlib.sha256(script.encode()).hexdigest(),
    'replacements': replacements,
    'limits': ['Synthetic peripheral graph; real Runtime, Graph and controller/service sources.',
               'Controller entrypoint replay is separate from the app_main fixtures.',
               'No Xtensa instructions or physical device execution.'],
}
(a.output / 'fixture-adaptation.json').write_text(json.dumps(record, indent=2) + '\n')
subprocess.run([sys.executable, str(runner), '--output', str(a.output), *extra], check=True)
