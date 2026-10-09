#!/usr/bin/env python3
"""Compose the complete, explicitly pinned resident X4 first-install image."""
import argparse
import io
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from build_recovered_diagnostic import require, sha, encoded, inventory, digest_inventory, load
import resident_native

ROOT = Path(__file__).resolve().parents[2]


def descriptor(blob, role):
    from elftools.elf.elffile import ELFFile
    elf = ELFFile(io.BytesIO(blob))
    symbols = [s for s in elf.get_section_by_name('.symtab').iter_symbols()
               if s.name == 'risc_resident_app_descriptor_v1']
    require(len(symbols) == 1, 'Missing or ambiguous resident descriptor')
    s = symbols[0]
    require(s['st_size'] == 16 and isinstance(s['st_shndx'], int), 'Invalid resident descriptor layout')
    section = elf.get_section(s['st_shndx'])
    start = s['st_value'] - section['sh_addr']
    require(section['sh_flags'] & 2 and section['sh_type'] != 'SHT_NOBITS' and
            0 <= start <= section['sh_size'] - 16, 'Resident descriptor is not loaded data')
    require(struct.unpack('<4I', section.data()[start:start+16]) == (1, 16, role, 0),
            'Resident descriptor role/version differs')
    definitions = [s.name for s in elf.get_section_by_name('.symtab').iter_symbols()
                   if s['st_shndx'] != 'SHN_UNDEF']
    require(definitions.count('pqa_render') == (1 if role == 1 else 0), 'Shared renderer ownership differs')
    if role == 2:
        require(not any(n.startswith(('pqa_render', 'pqa_sheet', 'pqa_font')) for n in definitions),
                'Foreground contains Quick Actions renderer/assets')
    return sum(s['sh_size'] for s in elf.iter_sections() if s['sh_flags'] & 2)


def apply_policy(files, entries, binding):
    boot = json.loads(files['boot.json'])
    policies = {p['manifest']: p for p in boot['app_capabilities']}
    require(len(policies) == 21, 'Delivered application inventory differs')
    require({n + '.json' for n in entries} == set(policies) - {'gameboy.json'},
            'Resident selection loses or substitutes an installed application')
    require(not any(g['capability'] == 'storage.app-data' and g['instance_id'] == 5
                    for p in policies.values() for g in p['grants']), 'Points AppData namespace already used')
    require(not any(row['namespace'] == 5 for p in boot['drivers'] for row in p.get('app_data', [])),
            'Provider AppData namespace already used')
    for name, entry in entries.items():
        manifest = json.loads(files[name + '.json'])
        grants = entry['grants']
        expected = {(r['capability'], r['api']) for r in manifest['requires']}
        require({(g['capability'], g['api']) for g in grants} == expected and len(grants) <= 17,
                'Manifest/grant mismatch: ' + name)
        require(len({(g['capability'], g['api'], g['instance_id']) for g in grants}) == len(grants),
                'Duplicate app binding: ' + name)
        policies[name + '.json']['grants'] = grants
    alarm = [d for d in boot['drivers'] if d['manifest'] == 'alarm/manifest.json']
    require(len(alarm) == 1 and alarm[0].get('instance_id', 0) == 0, 'Alarm provider identity changed')
    desired = binding['provider_storage']
    require(all(row in desired['key_value'] for row in alarm[0]['key_value']),
            'Existing alarm KV authority was removed or changed')
    alarm[0]['key_value'] = desired['key_value']
    alarm[0]['app_data'] = desired['app_data']
    boot['resident_shell'] = {'api': 1, 'host': 'default.elf',
                              'foreground': sorted(n + '.elf' for n in entries if n != 'default'),
                              'legacy': ['gameboy.elf']}
    require(len(boot['resident_shell']['foreground']) == 19 and len(entries) == 20,
            'Incomplete resident application conversion')
    files['boot.json'] = encoded(boot)
    return boot


def build(a):
    require(not a.output.exists(), 'Output already exists')
    require(not subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True).strip(),
            'Clean committed product source required')
    spec = json.loads((ROOT / 'minimal/apps/resident-cohort.json').read_text())
    revision = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    product = json.loads((ROOT / 'minimal/product.json').read_text())
    require(product['version'] == '0.1.44', 'Wrong product version')
    base = json.loads((a.baseline / 'build-custody.json').read_text())
    require(base['source_revision'] == spec['baseline_source'] and
            sha((a.baseline / 'build-custody.json').read_bytes()) == spec['baseline_receipt_sha256'],
            'Accepted baseline differs')
    files = inventory(a.baseline / 'store')
    require(digest_inventory(files) == base['store_files'], 'Accepted store changed')
    original = dict(files)
    candidate = json.loads((a.native / 'candidate.json').read_text())
    native_proof = resident_native.validate(a.native, candidate, a.runtime, native_source_root=a.native_platform)
    roots = {'system': a.system_target, 'utilities': a.utilities_target,
             'productivity': a.productivity_target, 'contexts': a.contexts_target}
    a.output.mkdir(parents=True)
    sources = {}; memory = {}; receipts = {}
    for name, entry in spec['apps'].items():
        folder = roots[entry['owner']] / entry['directory']
        blob = (folder / (name + '.elf')).read_bytes()
        meta = (folder / (name + '.json')).read_bytes()
        record = (folder / entry['receipt']).read_bytes()
        require(sha(blob) == entry['elf_sha256'] and sha(meta) == entry['manifest_sha256'] and
                sha(record) == entry['receipt_sha256'], 'Qualified input changed: ' + name)
        manifest = json.loads(meta)
        require(manifest['version'] == entry['version'] and manifest['file_name'] == name + '.elf',
                'Application identity differs: ' + name)
        prior = json.loads(original[name + '.json'])
        require(tuple(map(int, manifest['version'].split('.'))) > tuple(map(int, prior['version'].split('.'))),
                'Changed resident application must have a new version: ' + name)
        memory[name] = descriptor(blob, 1 if name == 'default' else 2)
        receipts[name] = json.loads(record)
        flags = receipts[name].get('build_defines', receipts[name].get('defines', []))
        require('-DPORTABLE_ALARM_TERMINAL_RETENTION' in flags,
                'Resident catalog client lacks the terminal alarm guard: ' + name)
        resident = receipts[name]['resident_shell']
        for header, digest in resident['sdk_sha256'].items():
            require(sha((a.runtime / 'sdk/app' / header).read_bytes()) == digest,
                    'Native/app shell SDK drift: ' + name + '/' + header)
        files[name + '.elf'] = blob; files[name + '.json'] = meta
        sources[name] = entry
        if (folder / 'licenses').is_dir():
            shutil.copytree(folder / 'licenses', a.output / 'licenses' / name, dirs_exist_ok=True)
    gb = json.loads((a.gameboy / 'build.json').read_text())
    game = (a.gameboy / 'gameboy.elf').read_bytes()
    require(sha((a.gameboy / 'build.json').read_bytes()) == spec['gameboy']['receipt_sha256'] and
            gb['source'] == spec['gameboy']['source'] and not gb['dirty'] and
            gb['version'] == spec['gameboy']['version'] and sha(game) == spec['gameboy']['elf_sha256'],
            'GameBoy qualified loading/contract source differs')
    gbmeta = (a.gameboy / 'gameboy.json').read_bytes()
    require(sha(gbmeta) == spec['gameboy']['manifest_sha256'] and
            json.loads(gbmeta)['requires'] == json.loads(original['gameboy.json'])['requires'],
            'GameBoy capability authority changed')
    files['gameboy.elf'] = game; files['gameboy.json'] = gbmeta
    service = (a.points_service / 'native-utc/driver.elf').read_bytes()
    smeta = (a.points_service / 'native-utc/manifest.json').read_bytes()
    sr = (a.points_service / 'build-evidence.json').read_bytes()
    require(sha(service) == spec['points_service']['elf_sha256'] and
            sha(sr) == spec['points_service']['receipt_sha256'] and
            sha(smeta) == spec['points_service']['manifest_sha256'], 'Qualified Points service differs')
    for name, digest in json.loads(sr)['pins']['runtime_inputs'].items():
        require(sha((a.runtime / name).read_bytes()) == digest, 'Points/native SDK drift: ' + name)
    prior_dependencies = json.loads(original['alarm/manifest.json'])['requires']
    require(json.loads(smeta)['requires'] == prior_dependencies + [
                {'capability': 'storage.app-data.bound', 'api': 1}],
            'Unexpected Points native dependency change')
    files['alarm/driver.elf'] = service; files['alarm/manifest.json'] = smeta
    binding = json.loads((ROOT / 'minimal/apps/points-catalog-bindings.json').read_text())
    boot = apply_policy(files, spec['apps'], binding)
    sys.path.insert(0, str(a.watch / 'scripts'))
    from compact_current_elf import compact
    from check_runtime_store_admission import admit_cohort
    from current_bootfs import build as pack
    from read_only_spiffs import read_image
    compactions = {}
    for name in [*spec['apps'], 'gameboy']:
        p = a.output / 'compacted' / (name + '.elf'); p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(files[name + '.elf'])
        compactions[name] = compact(p, str(a.compiler), debug_path=a.output / 'debug-originals' / (name + '.elf'))
        files[name + '.elf'] = p.read_bytes()
        if name != 'gameboy': descriptor(files[name + '.elf'], 1 if name == 'default' else 2)
    platform = load('resident_platform', ROOT / 'minimal/scripts/build_test_bundle.py')
    firmware = (a.native / 'firmware.bin').read_bytes()
    cohort = platform.cohort_identity(product, candidate, firmware, revision)
    files['cohort.json'] = encoded(cohort)
    admission = admit_cohort(a.runtime, (a.native / 'firmware.elf').read_bytes(), files, files, app_policy_rows=17)
    require(admission['cohort_validated'] and admission['elf_count'] == 44, 'Full resident cohort not admitted')
    bootfs, capacity = pack(files)
    require(read_image(bootfs, 0x510000) == files, 'Final SPIFFS round-trip differs')
    allowed = {'boot.json', 'cohort.json', 'alarm/driver.elf', 'alarm/manifest.json'} | {
        n + suffix for n in [*spec['apps'], 'gameboy'] for suffix in ('.elf', '.json')}
    changed = {n for n in files if files[n] != original.get(n)}
    require(changed <= allowed and set(files) == set(original), 'Unexpected store change')
    banks = load('resident_banks', a.runtime / 'scripts/paired_bank_images.py')
    parts = [(0, (a.native / 'bootloader.bin').read_bytes()), (0x8000, (a.native / 'partitions.bin').read_bytes()),
             (0x10000, firmware), (0x270000, (a.native / 'appdata.bin').read_bytes()), (0x2f0000, bootfs),
             (0xff0000, banks.initial_otadata()), (0xff2000, banks.initial_bank_state(firmware, bootfs, True))]
    image = bytearray(b'\xff' * 0x1000000); occupied = []
    for offset, data in parts:
        require(offset + len(data) <= len(image) and all(offset + len(data) <= x or offset >= y for x, y in occupied),
                'Partition overlap or overflow')
        image[offset:offset+len(data)] = data; occupied.append((offset, offset+len(data)))
    for offset, data in parts: require(image[offset:offset+len(data)] == data, 'Partition readback differs')
    name = 'xteink-x4-pro-0.1.44-resident-shell-first-install.bin'
    (a.output / name).write_bytes(image); (a.output / 'bootfs.bin').write_bytes(bootfs)
    for path, data in files.items():
        p = a.output / 'store' / path; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(data)
    shutil.copytree(a.native, a.output / 'native')
    shutil.copytree(a.baseline / 'licenses', a.output / 'licenses/baseline')
    if (a.gameboy / 'licenses').is_dir(): shutil.copytree(a.gameboy / 'licenses', a.output / 'licenses/gameboy')
    record = {'schema': 'x4.resident-cohort', 'schema_version': 1, 'product': product, 'source_revision': revision,
              'baseline': base['image'], 'inputs': sources, 'build_receipts': receipts, 'gameboy': gb,
              'points_service': json.loads(sr), 'resident_policy': boot['resident_shell'],
              'app_allocated_section_bytes': memory, 'compactions': compactions, 'cohort': cohort,
              'native_proof': native_proof, 'admission': admission, 'store_generator': capacity,
              'store_files': digest_inventory(files), 'changed_paths': sorted(changed),
              'partitions': [{'offset': o, 'bytes': len(b), 'sha256': sha(b)} for o, b in parts],
              'image': {'name': name, 'bytes': len(image), 'sha256': sha(image)}, 'hardware_tested': False,
              'known_unresolved': ['Windows MSC disk access hang', 'USB exit/serial restoration target remains unverified',
                                   'Manual low-power policy control is queued', 'Physical resident-shell/wake validation pending']}
    (a.output / 'build-custody.json').write_bytes(encoded(record))
    print(json.dumps(record['image']))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('baseline', 'runtime', 'native', 'native-platform', 'system-target', 'utilities-target',
                'productivity-target', 'contexts-target', 'points-service', 'gameboy', 'watch', 'compiler', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    build(parser.parse_args())
