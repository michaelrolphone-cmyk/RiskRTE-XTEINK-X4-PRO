#!/usr/bin/env python3
"""Freeze firmware and SD package output with both repository identities."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def record(runtime, output):
    runtime, output = Path(runtime).resolve(), Path(output).resolve()
    origin = json.loads((runtime / 'build-origin.json').read_text())
    if origin['platform']['dirty']:
        raise ValueError('Cannot publish artifacts from a dirty platform checkout')
    if output.exists():
        raise ValueError('Artifact destination must be new')
    firmware = runtime / '.pio/build/xteink-x4-pro/firmware.bin'
    if not firmware.is_file():
        raise ValueError('Build target firmware before recording artifacts')
    stage = runtime / 'dist/x4-independent-packages'
    if json.loads((stage / 'build-origin.json').read_text()) != origin:
        raise ValueError('Staged packages do not have matching combined-source custody')
    output.mkdir(parents=True)
    for name in ('firmware.bin', 'firmware.elf'):
        shutil.copy2(runtime / '.pio/build/xteink-x4-pro' / name, output / name)
    shutil.copytree(stage / 'sdcard', output / 'sdcard')
    shutil.copy2(stage / 'artifacts.json', output / 'packages.json')
    shutil.copy2(runtime / 'build-origin.json', output / 'build-origin.json')
    files = []
    for path in sorted(output.rglob('*')):
        if path.is_file():
            data = path.read_bytes()
            files.append({'path': path.relative_to(output).as_posix(), 'bytes': len(data),
                          'sha256': hashlib.sha256(data).hexdigest()})
    manifest = {'schema': 1, 'board': 'xteink-x4-pro', 'sources': origin,
                'firmware_kind': 'app-only', 'firmware_offset': '0x10000', 'files': files}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    print(json.dumps(record(args.runtime, args.output), indent=2))


if __name__ == '__main__':
    main()
