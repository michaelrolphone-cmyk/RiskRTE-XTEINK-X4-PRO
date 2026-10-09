#!/usr/bin/env python3
"""Compose a bounded diagnostic increment over an immutable delivered store.

The frozen store carries the exact previously delivered Clock/Settings binaries
whose source worktrees were lost. Replacements are individually source-bound;
the complete result must pass real Runtime/native-ELF admission before packing.
This produces a destructive first-install image, never an update transaction.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BASELINE_SHA = 'b06d0679e2be7b0c8450aaef46fd1ad942415cf213586ee90f974fff18079205'
BASELINE_FILE = 'xteink-x4-pro-0.1.29-dio-battery-boot-first-install.bin'
APPS = {'springboard': '1.7.16', 'ble_touchpad': '0.1.13', 'ble_buttons': '0.1.13'}

def require(ok, message):
    if not ok:
        raise ValueError(message)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def encoded(value):
    return (json.dumps(value, sort_keys=True, indent=2) + '\n').encode()

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def inventory(folder):
    result = {}
    for path in Path(folder).rglob('*'):
        require(not path.is_symlink(), 'Symlink in input inventory')
        if path.is_file():
            result[path.relative_to(folder).as_posix()] = path.read_bytes()
    return result

def digest_inventory(files):
    return {name: {'bytes': len(data), 'sha256': sha(data)} for name, data in sorted(files.items())}

def build(args):
    baseline, output = args.baseline.resolve(), args.output.resolve()
    require(not output.exists(), 'Output already exists')
    raw = (baseline / BASELINE_FILE).read_bytes()
    require(len(raw) == 0x1000000 and sha(raw) == BASELINE_SHA, 'Delivered .29 image differs')
    original = json.loads((baseline / 'diagnostic-custody.json').read_text())
    files = inventory(baseline / 'store')
    require(digest_inventory(files) == original['store_files'], 'Frozen .29 file inventory differs')
    require(len(files) == 85, 'Frozen store is incomplete')
    sys.path.insert(0, str(args.watch / 'scripts'))
    from check_runtime_store_admission import admit_cohort
    from current_bootfs import build as pack_store
    from read_only_spiffs import read_image
    from compact_current_elf import compact
    require(read_image(raw[0x2f0000:0x800000], 0x510000) == files, 'Frozen image/store bytes differ')
    recipe = json.loads(args.replacements.read_text())
    require(set(recipe['apps']) == set(APPS), 'Unexpected replacement app scope')
    product = json.loads((ROOT / 'minimal/product.json').read_text())
    require(product['version'] == '0.1.30', 'Wrong diagnostic product version')
    revision = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    require(not subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True).strip(),
            'Committed clean product source required')
    native = json.loads((args.native / 'candidate.json').read_text())
    platform = load('diagnostic_platform_builder', args.native_product / 'minimal/scripts/build_test_bundle.py')
    native_proof = platform.validate_native_composition(args.native, native, args.runtime, args.native_product)
    require(native['build_options'] == {'app_image_cache': True, 'app_policy_rows': 17}, 'Native options differ')
    require(native['firmware_version'] == '0.1.78', 'Wrong diagnostic Runtime')
    output.mkdir(parents=True)
    changed = {}
    for name, version in APPS.items():
        entry = recipe['apps'][name]
        folder = Path(entry['path'])
        manifest_blob = (folder / (name + '.json')).read_bytes()
        blob = (folder / (name + '.elf')).read_bytes()
        receipt = json.loads((folder / 'x4-native-app.json').read_text())
        manifest = json.loads(manifest_blob)
        require(manifest['version'] == version and manifest['file_name'] == name + '.elf', 'App identity differs: ' + name)
        require(sha(blob) == entry['elf_sha256'] == receipt['elf_sha256'] and len(blob) == receipt['elf_bytes'], 'App bytes differ: ' + name)
        require(sha(manifest_blob) == entry['manifest_sha256'] and receipt['requires'] == manifest['requires'], 'App manifest differs: ' + name)
        require(receipt['source_revision'] == entry['source_revision'] and receipt['system_source_revision'] == entry['system_source_revision'], 'App source differs: ' + name)
        require(manifest['requires'] == json.loads(files[name + '.json'])['requires'], 'Unexpected app authority change: ' + name)
        dest = output / 'replacement-inputs' / name
        shutil.copytree(folder, dest)
        packed = dest / (name + '.elf')
        compaction = compact(packed, str(args.compiler), debug_path=output / 'debug-originals' / (name + '.elf'))
        files[name + '.elf'] = packed.read_bytes()
        files[name + '.json'] = manifest_blob
        changed[name] = {'receipt': receipt, 'compaction': compaction, 'packaged_sha256': sha(files[name + '.elf'])}
    sd = recipe['sd']
    sd_path = Path(sd['path'])
    sd_blob = (sd_path / 'driver.elf').read_bytes()
    sd_manifest = (sd_path / 'manifest.json').read_bytes()
    sd_proof = json.loads((sd_path / 'target-proof.json').read_text())
    require(sha(sd_blob) == sd['elf_sha256'] == sd_proof['elf_sha256'] and len(sd_blob) == sd_proof['elf_bytes'], 'SD target differs')
    require(sd_proof['source_commit'] == sd['source_revision'] and not sd_proof['working_tree_dirty'] and sd_proof['target_structure_passed'], 'SD target custody differs')
    require(sha(sd_manifest) == sd['manifest_sha256'], 'SD manifest differs')
    require(sd_manifest == (args.native_product / 'minimal/drivers/x4pro_sd/manifest.json').read_bytes(), 'Selected SD source manifest differs')
    for name, digest in sd_proof['source_sha256'].items():
        require(sha((args.native_product / name).read_bytes()) == digest, 'Selected SD source differs: ' + name)
    require(json.loads(sd_manifest)['version'] == '0.2.7', 'Wrong diagnostic SD version')
    files['sd/driver.elf'], files['sd/manifest.json'] = sd_blob, sd_manifest
    boot = json.loads(files['boot.json'])
    platform.configure_boot_logging(boot, True)
    files['boot.json'] = encoded(boot)
    firmware = (args.native / 'firmware.bin').read_bytes()
    cohort = platform.cohort_identity(product, native, firmware, revision)
    files['cohort.json'] = encoded(cohort)
    changed_paths = {name for name in files if files[name] != (baseline / 'store' / name).read_bytes()}
    allowed = {'boot.json', 'cohort.json', 'sd/driver.elf', 'sd/manifest.json'} | {name + suffix for name in APPS for suffix in ('.elf', '.json')}
    require(changed_paths <= allowed and len(files) == 85, 'Unexpected frozen-store modification')
    elf = (args.native / 'firmware.elf').read_bytes()
    admission = admit_cohort(args.runtime, elf, files, files, app_policy_rows=17)
    filesystem, filesystem_proof = pack_store(files)
    require(read_image(filesystem, 0x510000) == files, 'Packed store differs')
    banks = load('diagnostic_banks', args.runtime / 'scripts/paired_bank_images.py')
    loader = (args.native / 'bootloader.bin').read_bytes()
    table = (args.native / 'partitions.bin').read_bytes()
    appdata = (args.native / 'appdata.bin').read_bytes()
    require(sha(loader) == '1033730a6df733f53a7a347353c1c5450547f76e98e0746da633079310a563b9', 'DIO bootloader differs')
    require(table == raw[0x8000:0x8000+len(table)] and len(appdata) == 0x80000, 'Initial layout differs')
    parts = [(0, loader), (0x8000, table), (0x10000, firmware), (0x270000, appdata),
             (0x2f0000, filesystem), (0xff0000, banks.initial_otadata()),
             (0xff2000, banks.initial_bank_state(firmware, filesystem, True))]
    image = bytearray(b'\xff' * 0x1000000)
    occupied = []
    for offset, blob in parts:
        require(offset + len(blob) <= len(image), 'Partition overflow')
        require(all(offset + len(blob) <= a or offset >= b for a, b in occupied), 'Partition overlap')
        image[offset:offset+len(blob)] = blob
        occupied.append((offset, offset+len(blob)))
    require(read_image(bytes(image[0x2f0000:0x800000]), 0x510000) == files, 'Final image store differs')
    filename = 'xteink-x4-pro-0.1.30-sd-bootlog-first-install.bin'
    (output / filename).write_bytes(image)
    (output / 'bootfs.bin').write_bytes(filesystem)
    for name, blob in files.items():
        path = output / 'store' / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(blob)
    shutil.copytree(args.native, output / 'native')
    shutil.copytree(baseline / 'build-records', output / 'frozen-build-records')
    shutil.copytree(baseline / 'licenses', output / 'licenses')
    shutil.copytree(sd_path, output / 'replacement-inputs' / 'sd')
    record = {'schema': 'x4.recovered-diagnostic', 'schema_version': 1, 'source_revision': revision,
              'product': product, 'baseline_image_sha256': BASELINE_SHA, 'frozen_source_records': original,
              'cohort': cohort, 'native_proof': native_proof, 'replacements': changed, 'sd': sd_proof,
              'changed_store_paths': sorted(changed_paths), 'admission': admission,
              'store_generator': filesystem_proof, 'store_files': digest_inventory(files),
              'partitions': [{'offset': a, 'bytes': len(b), 'sha256': sha(b)} for a,b in parts],
              'image': {'name': filename, 'bytes': len(image), 'sha256': sha(image)}, 'hardware_tested': False}
    (output / 'build-custody.json').write_bytes(encoded(record))
    (output / 'README.txt').write_text('X4 Minimal0.1.30 UC8279 diagnostic test image\n\n'
        'Flash the full16MiB BIN at0x0. This first-install image overwrites NVS and app-data; it is not a preserving update.\n'
        'Includes automatic cold-boot SD logging, recovered-touch HID handling and finger-tracked horizontal Springboard pages. '
        'Preserves all nineteen .29 apps, including its Home idle desk-clock and landscape Points binaries.\n'
        'SD log: /x4-boot.log (File Browser /sd/x4-boot.log); prior rotated log /x4-boot.previous.log. '
        'SD starts after Runtime admission on cold boots, before the default app. Early software checkpoints are retained in bounded NVS and exported when SD mounts. '
        'Deep timer wakes do not start SD for logging. ROM, power loss before software starts, and failed NVS writes cannot be reconstructed from these logs.\n'
        'Battery-only boot remains under investigation. DIO is retained as the existing experiment, not a proven repair. '
        'No hardware test was performed. GameBoy and USB SD-card mass-storage mode are not included.\n')
    print(json.dumps(record['image']))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'replacements', 'native', 'native-product', 'runtime', 'watch', 'compiler', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    build(parser.parse_args())
