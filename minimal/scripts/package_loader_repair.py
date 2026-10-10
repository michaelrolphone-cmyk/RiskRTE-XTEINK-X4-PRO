#!/usr/bin/env python3
"""Build the native resident-descriptor repair over the exact delivered .45 store."""
import argparse
import json
from pathlib import Path
import shutil
import sys
from build_recovered_diagnostic import require, sha, encoded, inventory, digest_inventory, load
from package_resident_cohort import descriptor
import prepare_native_runtime as native
import resident_native

ROOT = Path(__file__).resolve().parents[2]


def pinned_blob(path, expected):
    blob = path.read_bytes()
    require(expected == {'bytes': len(blob), 'sha256': sha(blob)}, 'Pinned input differs: ' + str(path))
    return blob


def build(a):
    require(not a.output.exists(), 'Output already exists')
    source = native.clean_source(ROOT)
    spec = json.loads((ROOT / 'minimal/apps/loader-repair-cohort.json').read_text())
    product = json.loads((ROOT / 'minimal/product.json').read_text())
    require(product['version'] == '0.1.46', 'Wrong product version')
    base_raw = pinned_blob(a.baseline / 'build-custody.json', spec['baseline']['receipt'])
    base = json.loads(base_raw)
    original_image = pinned_blob(a.baseline / base['image']['name'], spec['baseline']['image'])
    require(base['source_revision'] == spec['baseline']['source'] and base['product']['version'] == '0.1.45',
            'Frozen baseline identity differs')
    files = inventory(a.baseline / 'store')
    require(digest_inventory(files) == base['store_files'], 'Delivered .45 store changed')
    original = dict(files)
    require(native.clean_source(a.watch)['commit'] == spec['packaging_source'], 'Packaging source differs')
    sys.path.insert(0, str(a.watch / 'scripts'))
    from check_runtime_store_admission import admit_cohort
    import verify_update_elf
    from current_bootfs import build as pack
    from read_only_spiffs import read_image
    require(read_image(original_image[0x2f0000:0x800000], 0x510000) == original,
            'Delivered .45 full image does not contain the pinned store')
    candidate = json.loads((a.native / 'candidate.json').read_text())
    native_proof = resident_native.validate(a.native, candidate, a.runtime, native_source_root=a.native_platform)
    require(native_proof['failure_evidence_proof']['enabled'], 'Native failure evidence is not enabled')
    retained = base['clock_retained_payload']['encoded_bytes']
    capacity = native_proof['runtime_options_proof']['retained_wake']['payload_bytes']
    require(retained == 408 and capacity == 512, 'Retained Clock budget differs')
    descriptor(files['default.elf'], 1, check_renderer=False)
    boot = json.loads(files['boot.json'])
    require(boot['resident_shell'] == base['preserved_resident_policy'] and
            len(boot['resident_shell']['foreground']) == 19 and
            boot['resident_shell']['host'] == 'default.elf' and boot['resident_shell']['legacy'] == ['gameboy.elf'],
            'Resident cohort policy differs')
    require(json.loads(files['gameboy.json'])['version'] == '1.3.22', 'Original GameBoy version differs')
    for name in boot['resident_shell']['foreground']:
        descriptor(files[name], 2, check_renderer=False)
    a.output.mkdir(parents=True)
    platform = load('failure_platform', ROOT / 'minimal/scripts/build_test_bundle.py')
    firmware = (a.native / 'firmware.bin').read_bytes()
    cohort = platform.cohort_identity(product, candidate, firmware, source['commit'])
    files['cohort.json'] = encoded(cohort)
    changed = {n for n in files if files[n] != original.get(n)}
    require(changed == {'cohort.json'} and set(files) == set(original),
            'Unexpected store change')
    require(files['boot.json'] == original['boot.json'], 'Boot grants, namespaces or provider policy changed')
    # Frozen packaging tooling extracts the complete production admission body.
    # Runtime .91 added this inline policy-validation dependency; preserve that
    # include too, without changing the body or selecting a provider policy.
    policy_header = 'runtime/drivers/NativeProviderPolicyValidationV1.h'
    policy_include = '#include "' + policy_header + '"\n'
    require(policy_include in (a.runtime / 'src/ports/esp32s3/NativeBankStore.cpp').read_text(),
            'Selected native admission no longer declares the expected policy dependency')
    original_header = verify_update_elf.cohort_admission_header
    def header_with_production_dependency(runtime, elf):
        return policy_include + original_header(runtime, elf)
    verify_update_elf.cohort_admission_header = header_with_production_dependency
    try:
        admission = admit_cohort(a.runtime, (a.native / 'firmware.elf').read_bytes(), files, files, app_policy_rows=17)
    finally:
        verify_update_elf.cohort_admission_header = original_header
    admission['preserved_production_dependency'] = {
        'path': 'src/' + policy_header, 'sha256': sha((a.runtime / 'src' / policy_header).read_bytes()),
        'admission_function_changed': False}

    require(admission['cohort_validated'] and admission['elf_count'] == 44, 'Complete store/native admission failed')
    bootfs, capacity_record = pack(files)
    require(read_image(bootfs, 0x510000) == files, 'Final SPIFFS round-trip differs')
    banks = load('failure_banks', a.runtime / 'scripts/paired_bank_images.py')
    parts = [(0, (a.native / 'bootloader.bin').read_bytes()), (0x8000, (a.native / 'partitions.bin').read_bytes()),
             (0x10000, firmware), (0x270000, (a.native / 'appdata.bin').read_bytes()), (0x2f0000, bootfs),
             (0xff0000, banks.initial_otadata()), (0xff2000, banks.initial_bank_state(firmware, bootfs, True))]
    image = bytearray(b'\xff' * 0x1000000)
    occupied = []
    for offset, data in parts:
        require(offset + len(data) <= len(image) and all(offset + len(data) <= x or offset >= y for x, y in occupied),
                'Partition overlap or overflow')
        image[offset:offset+len(data)] = data
        occupied.append((offset, offset+len(data)))
    for offset, data in parts:
        require(image[offset:offset+len(data)] == data, 'Partition readback differs')
    for start, end in [(0, 0x10000), (0x270000, 0x2f0000), (0x800000, 0xff2000), (0xff4000, 0x1000000)]:
        require(image[start:end] == original_image[start:end], 'Preserved full-image region differs')
    require(read_image(bytes(image[0x2f0000:0x800000]), 0x510000) == files,
            'Final full-image store readback differs')
    name = 'xteink-x4-pro-0.1.46-resident-loader-repair-first-install.bin'
    (a.output / name).write_bytes(image)
    (a.output / 'bootfs.bin').write_bytes(bootfs)
    for path, data in files.items():
        out = a.output / 'store' / path
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)
    shutil.copytree(a.native, a.output / 'native')
    shutil.copytree(a.baseline / 'licenses', a.output / 'licenses/baseline')
    record = {'schema': 'x4.resident-loader-repair-cohort', 'schema_version': 1, 'product': product,
              'source_revision': source['commit'], 'source_tree': source['tree'], 'baseline': spec['baseline'],
              'preserved_home': base['home'], 'native_proof': native_proof,
              'preserved_resident_policy': boot['resident_shell'], 'preserved_boot_sha256': sha(files['boot.json']),
              'preserved_file_count': len(files) - len(changed),
              'clock_retained_payload': {'encoded_bytes': retained, 'native_capacity_bytes': capacity},
              'cohort': cohort, 'admission': admission, 'store_generator': capacity_record,
              'store_files': digest_inventory(files), 'changed_paths': sorted(changed),
              'partitions': [{'offset': o, 'bytes': len(b), 'sha256': sha(b)} for o, b in parts],
              'full_image_readback_verified': True, 'image': {'name': name, 'bytes': len(image), 'sha256': sha(image)},
              'hardware_tested': False, 'publication': 'held-local-only',
              'known_unresolved': base['known_unresolved']}
    (a.output / 'build-custody.json').write_bytes(encoded(record))
    print(json.dumps(record['image']))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('baseline', 'runtime', 'native', 'native-platform', 'watch', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    build(parser.parse_args())
