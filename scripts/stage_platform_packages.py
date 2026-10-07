#!/usr/bin/env python3
"""Stage the complete X4 profile through the pinned shared package engine."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    lock = json.loads((ROOT / 'migration-source.json').read_text())
    origin = json.loads((runtime / 'build-origin.json').read_text())
    if origin['runtime'] != lock['upstream']:
        raise ValueError('Composed runtime provenance does not match source lock')
    profile = json.loads((ROOT / 'profiles/xteink-x4-pro.json').read_text())
    sys.path.insert(0, str(runtime / 'scripts'))
    spec = importlib.util.spec_from_file_location('shared_package_stage', runtime / 'scripts/stage_x4pro_packages.py')
    stage = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(stage)
    stage.ROOT = runtime
    output = args.output.resolve() if args.output else runtime / 'dist/x4-independent-packages'
    stage.stage(sources=profile['sources'], board=profile['board_id'], output=output)
    (output / 'build-origin.json').write_text(json.dumps(origin, indent=2) + '\n')


if __name__ == '__main__':
    main()
