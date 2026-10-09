#!/usr/bin/env python3
"""Freeze an X4 payload template from a completed candidate, offline only.

The credential-free template is NOT a native-installable owner profile. Use the
shared owner workflow separately to add Wi-Fi and validate private inputs.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from generate_profile import profile, selections as profile_selections

ROOT = Path(__file__).resolve().parents[2]
LAYOUT = 'riscrte-paired-appdata-v2'
PANELS = ('ssd1677', 'uc8279')


def shared_tool(runtime):
    lock = json.loads((ROOT / 'minimal/sources.lock.json').read_text())['runtime']
    runtime = runtime.absolute()
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=runtime, text=True).strip()
    if head != lock['commit'] or subprocess.run(
            ['git', 'diff', '--quiet', 'HEAD', '--', 'scripts'], cwd=runtime).returncode:
        raise ValueError('exact clean pinned Runtime scripts required')
    if subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', 'scripts'], cwd=runtime):
        raise ValueError('untracked Runtime scripts refused')
    sys.path.insert(0, str(runtime / 'scripts'))
    spec = importlib.util.spec_from_file_location('x4_shared_provision', runtime / 'scripts/provision_profile.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, lock


def create(runtime, bundle, native, panel, base_url, profile_url, output):
    p, lock = shared_tool(runtime)
    output = p.destination(output)
    for source_dir in (bundle, native, runtime):
        source_dir = p.safe_path(source_dir).resolve()
        p.require(source_dir != output and source_dir not in output.resolve().parents,
                  'output must be outside input directories')
    p.require(panel in PANELS, 'explicit panel required')
    p.source(base_url, True)
    p.source(profile_url)
    # A builder receipt is mandatory: never synthesize provenance from a loose store.
    custody_raw = p.read(bundle / 'build-custody.json')
    custody = p.decode(custody_raw)
    candidate_raw = p.read(native / 'candidate.json')
    candidate = p.decode(candidate_raw)
    p.require(custody.get('schema') == 1 and custody.get('panel') == panel and
              custody.get('runtime') == candidate and
              custody.get('shared_source_lock') == p.decode(p.read(ROOT / 'minimal/sources.lock.json')) and
              re.fullmatch('[0-9a-f]{40}', custody.get('x4_source', '')),
              'bundle/native source identity mismatch')
    p.require(candidate.get('schema') == 1 and candidate.get('source_sha') == lock['commit'] and
              candidate.get('firmware_version') == lock['version'] and
              type(candidate.get('store_abi')) is int and candidate['store_abi'] == 2 and
              candidate.get('layout') == LAYOUT and
              candidate.get('target') in ('esp32s3-16mb-appdata', 'esp32s3-16mb-appdata-iq') and
              candidate.get('flash_bytes') == 0x1000000, 'native ABI/layout/version mismatch')
    assets = candidate['assets']
    p.require({'firmware.bin', 'firmware.elf', 'bootloader.bin', 'partitions.bin',
               'appdata.bin', 'appdata-image.json'} <= assets.keys(), 'incomplete native candidate')
    native_blobs = {}
    for name, digest in assets.items():
        p.relative(name)
        # Match the shared release/candidate bound for the debug-bearing native
        # ELF. It is not an installed store file or a larger firmware slot.
        blob = p.read(native / name, 32 * 1024 * 1024 if name == 'firmware.elf'
                      else 16 * 1024 * 1024)
        p.require(digest == {'bytes': len(blob), 'sha256': p.sha(blob)}, 'native asset hash mismatch')
        native_blobs[name] = blob
    firmware = native_blobs['firmware.bin']
    for name in ('firmware.bin', 'firmware.elf'):
        blob = native_blobs[name]
        for marker in ('RTE_SOURCE=' + lock['commit'], 'RISC_RUNTIME_VERSION:' + lock['version'],
                       'RISC_PAIRED_STORE_ABI:2'):
            p.require(marker.encode() + b'\0' in blob, 'compiled native identity mismatch')
        p.require(b'RISC_PAIRED_STORE_ABI:1\0' not in blob, 'mixed compiled ABI')
    p.require(candidate['target'].encode() + b'\0' in firmware and len(firmware) <= 0x260000,
              'native target/slot mismatch')
    p.require(p.read(bundle / 'firmware.bin') == firmware, 'mixed firmware/store candidate')
    files = p.store_files(bundle / 'store')
    p.require(custody.get('store_files') == {
        name: {'bytes': len(data), 'sha256': p.sha(data)} for name, data in files.items()},
        'absent or stale store manifest')
    board = p.decode(files['board.json'])
    sleep = custody.get('sleep', False)
    panel_driver = custody.get('panel_driver', 'fallback')
    p.require(type(sleep) is bool and panel_driver in ('fallback', 'uc8279-fast'),
              'invalid selected X4 provider profile')
    expected = profile(panel, sleep, panel_driver)
    p.require(all(board.get(k) == v for k, v in expected.items() if k != 'devices') and
              all(board['devices'].count(device) == 1 for device in expected['devices']) and
              len({d['instance_id'] for d in board['devices']}) == len(board['devices']),
              'X4 panel/board identity mismatch')
    boot = p.decode(files['boot.json'])
    p.require(boot.get('board') == 'board.json' and boot.get('default_app') == 'default.elf',
              'X4 boot identity mismatch')
    # Require all X4 providers and every referenced manifest/ELF, not just the
    # three generic shared-tool bootstrap files. Graph/ELF admission remains a
    # prerequisite of the completed builder, not something this wrapper claims.
    for _, (instance, folder, _) in profile_selections(sleep, panel_driver).items():
        manifest_path = folder + '/manifest.json'
        p.require(sum(d.get('manifest') == manifest_path and d.get('instance_id') == instance
                      for d in boot['drivers']) == 1, 'missing X4 driver selection')
    for kind, selections in [('driver', boot['drivers']),
                             ('application', boot.get('app_capabilities', []))]:
        for selection in selections:
            name = p.relative(selection['manifest'])
            manifest = p.decode(files[name])
            p.require(manifest.get('type') == kind and isinstance(manifest.get('version'), str)
                      and re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', manifest['version']),
                      'store manifest identity/version')
            leaf = p.relative(manifest['file_name'])
            executable = p.relative((Path(name).parent / leaf).as_posix())
            p.require(executable in files, 'missing manifest executable')
    p.require(any(s['manifest'] == 'default.json' for s in boot.get('app_capabilities', [])),
              'missing default application manifest')
    record = p.pin_store(bundle / 'store', LAYOUT, base_url=base_url, target=candidate['target'])
    p.verify_inventory(record, files)
    payload = {'schema': 'riscrte.provisioning', 'schema_version': 2, 'base_url': base_url,
               'files': [{k: entry[k] for k in ('path', 'bytes', 'sha256')} for entry in record['files']]}
    data = p.encode(payload)
    # Reserve the largest legal Wi-Fi object, including worst-case JSON escaping.
    p.require(len(data) + len(',"wifi":{"ssid":"","password":""}') + 6 * (32 + 63) <= p.MAX_PROFILE,
              'profile exceeds 16 KiB with owner Wi-Fi reserve')
    p.require(profile_url not in {entry['url'] for entry in record['files']}, 'profile URL overlaps payload')
    output.mkdir()
    try:
        for name, blob in files.items():
            dest = output / 'files' / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            p.write(dest, blob)
        p.verify_inventory(record, p.store_files(output / 'files'))
        p.write(output / 'inventory.json', p.encode(record))
        p.write(output / 'payload-profile.json', data)
        receipt = {'schema': 'x4.provisioning-payload', 'schema_version': 1, 'panel': panel,
                   'runtime': candidate, 'x4_source': custody['x4_source'],
                   'build_custody_sha256': p.sha(custody_raw), 'candidate_sha256': p.sha(candidate_raw),
                   'profile_url': profile_url, 'payload_profile_sha256': p.sha(data),
                   'payload_profile_bytes': len(data), 'file_count': len(files),
                   'scope': 'Credential-free schema2 template; not an installable owner profile. No upload or hardware qualification.'}
        p.write(output / 'payload.json', p.encode(receipt))
        p.write(output / 'SHA256SUMS', ''.join(
            f'{p.sha(p.read(path))}  {path.relative_to(output).as_posix()}\n'
            for path in sorted(output.rglob('*')) if path.is_file()).encode())
        p.write(output / 'COMPLETE', b'x4.provisioning-payload.v1\n')
    except BaseException:
        shutil.rmtree(output)
        raise
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('runtime', 'bundle', 'native', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--panel', choices=PANELS, required=True)
    parser.add_argument('--base-url', required=True)
    parser.add_argument('--profile-url', required=True)
    args = parser.parse_args()
    try:
        create(**vars(args))
    except (ValueError, OSError, KeyError, TypeError, subprocess.SubprocessError):
        print('Provisioning payload refused; check pinned candidate, manifest, paths and URLs.', file=sys.stderr)
        return 1
    print('Credential-free X4 payload complete. No owner inputs, upload or device action.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
