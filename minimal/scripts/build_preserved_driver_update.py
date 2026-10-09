#!/usr/bin/env python3
"""Assemble X4 0.1.48 from the exact delivered 0.1.47 first-install image.

Preserves Runtime 0.1.98 and every application. Replaces only SD/display ELF
packages and cohort metadata. This is a driver-fix test image, NOT an enablement
of hardware SDMMC: the preserved native firmware does not include that addition.
Uses tools exported by x4-preserved-cohort-tools.yml. Performs no device I/O.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import zipfile

BASE_SHA = 'd783471dee5b7cb9126e5e42d5e68776d1a6c0d752d16e0a756e2de809916be6'
DRIVERS_SHA = 'af9916ca62cf3a6784d211b538e457f0cf879ae927a3050f40270eafb84da796'
DRIVERS_REV = '0cb708175b9217892d9e09106e84a5e82477f4c0'
WATCH_REV = 'db5f6c5ba1a71ed3fcc22ff29847397a050b7ee4'
TOOLS_RUNTIME_REV = '1113ad2af1974b261addd09cfabc311a95b088ce'
NATIVE_REV = 'da3fa1a3ab177bc96af61298849a40ea9ca314f3'
IMAGE_BYTES = 0x1000000
FS_BEGIN, FS_END = 0x2f0000, 0x800000
BANK_BEGIN, BANK_END = 0xff2000, 0xff4000
OUTPUT_NAME = 'xteink-x4-pro-0.1.48-from-0.1.47-driver-fixes-full-0x0.bin'
PACKAGES = (
    ('panel', 'x4pro-uc8279-fast', '0.1.7', '0.1.8',
     '1cc2df856d5d2818525fe2b103038a152e90fc81117ec65ce44632cd24e692a7'),
    ('sd', 'x4pro-sd', '0.2.11', '0.2.12',
     '6478e849156c7966f452d2c1e42ecdcdf8eba0e36bd43507a770a09ba74108fa'),
)


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def encode(data):
    return (json.dumps(data, indent=2, sort_keys=True) + '\n').encode()


def inventory(files):
    return {name: {'bytes': len(blob), 'sha256': sha(blob)}
            for name, blob in sorted(files.items())}


def elf_symbols(blob):
    """Read the ELF32 dynamic symbol table without executing target code."""
    require(len(blob) >= 52 and blob[:7] == b'\x7fELF\x01\x01\x01', 'Not LE ELF32')
    h = struct.unpack_from('<16sHHIIIIIHHHHHH', blob)
    require(h[1:4] == (3, 94, 1) and h[8] == 52 and h[11] == 40,
            'Expected Xtensa ELF32 shared object')
    require(h[6] + h[12] * 40 <= len(blob), 'Section header bounds')
    sections = [struct.unpack_from('<10I', blob, h[6] + i * 40) for i in range(h[12])]
    dyn = [s for s in sections if s[1] == 11]
    require(len(dyn) == 1, 'Expected one dynamic symbol table')
    s = dyn[0]
    require(s[9] == 16 and s[5] % 16 == 0 and s[6] < len(sections), 'Symbol geometry')
    strings = sections[s[6]]
    require(strings[4] + strings[5] <= len(blob) and s[4] + s[5] <= len(blob),
            'Symbol table bounds')
    names = blob[strings[4]:strings[4] + strings[5]]
    imports, exports = [], []
    for at in range(s[4], s[4] + s[5], 16):
        name, value, size, info, other, ndx = struct.unpack_from('<IIIBBH', blob, at)
        require(name < len(names), 'Symbol name bounds')
        if not name:
            continue
        end = names.find(b'\0', name)
        require(end >= 0, 'Unterminated symbol')
        text = names[name:end].decode('ascii')
        (imports if ndx == 0 else exports).append(text)
    require('t5_driver_get' in exports, 'Missing provider entrypoint')
    return {'imports': sorted(imports), 'exports': sorted(exports)}


def c_readback(tool, image, expected):
    """Independently mount and extract with the pinned C SPIFFS implementation."""
    with tempfile.TemporaryDirectory(prefix='x4-spiffs-verify-') as tmp:
        root = Path(tmp)
        source = root / 'bootfs.bin'
        source.write_bytes(image)
        unpacked = root / 'files'
        unpacked.mkdir()
        result = subprocess.run([str(tool), '-u', str(unpacked), '-p', '256',
                                 '-b', '4096', '-s', str(FS_END-FS_BEGIN), str(source)],
                                text=True, capture_output=True, timeout=60)
        require(result.returncode == 0, 'C SPIFFS extraction failed: ' + result.stderr)
        recovered = {p.relative_to(unpacked).as_posix(): p.read_bytes()
                     for p in unpacked.rglob('*') if p.is_file()}
        require(recovered == expected, 'C SPIFFS file readback differs')
        return {'files_verified': len(recovered), 'tool_sha256': sha(tool.read_bytes()),
                'exit_code': result.returncode}


def build(args):
    require(__debug__, 'Run verification without Python -O')
    require(not args.output.exists(), 'Output directory already exists')
    require(re.fullmatch(r'[0-9a-f]{40}', args.source_revision), 'Packaging commit required')
    tools = args.tools.resolve()
    require((tools/'watch-source.txt').read_text().strip() == WATCH_REV, 'Watch tool pin differs')
    require((tools/'runtime-tools-source.txt').read_text().strip() == TOOLS_RUNTIME_REV,
            'Bank metadata tool pin differs')
    receipts = json.loads((tools/'source-sha256.json').read_text())
    for name in ('watch/scripts/current_bootfs.py', 'watch/scripts/read_only_spiffs.py',
                 'watch/vendor/esp-idf-spiffs/spiffsgen.py', 'runtime/scripts/paired_bank_images.py'):
        require(sha((tools/name).read_bytes()) == receipts[name], 'Tool source differs: ' + name)
    sys.path[:0] = [str(tools/'watch/scripts'), str(tools/'runtime/scripts')]
    from current_bootfs import build as pack
    from read_only_spiffs import read_image
    from paired_bank_images import initial_bank_state, initial_otadata, parse_record

    original = args.baseline.read_bytes()
    require(len(original) == IMAGE_BYTES and sha(original) == BASE_SHA, 'Wrong 0.1.47 image')
    original_fs = original[FS_BEGIN:FS_END]
    files = read_image(original_fs, FS_END-FS_BEGIN)
    require(len(files) == 91, 'Baseline file inventory differs')
    frozen = dict(files)
    baseline_cohort = json.loads(files['cohort.json'])
    require(baseline_cohort['version'] == '0.1.47' and
            baseline_cohort['runtime_version'] == '0.1.98' and
            baseline_cohort['store_abi'] == 2, 'Baseline cohort differs')
    require(json.loads(files['gameboy.json'])['version'] == '1.3.23', 'GameBoy version differs')
    firmware = original[0x10000:0x10000 + baseline_cohort['firmware_size']]
    require(sha(firmware) == baseline_cohort['firmware_sha256'] and
            ('RTE_SOURCE=' + NATIVE_REV).encode() in firmware, 'Native image identity differs')
    require(original[BANK_BEGIN:BANK_END] == initial_bank_state(firmware, original_fs, True),
            'Baseline bank metadata differs')
    require(original[0xff0000:0xff2000] == initial_otadata(), 'Baseline OTA metadata differs')
    same_fs, baseline_fs_proof = pack(files)
    require(same_fs == original_fs, 'Tooling does not reproduce baseline filesystem exactly')
    c_tool = tools/'mkspiffs/mkspiffs_espressif32_arduino'
    c_tool.chmod(c_tool.stat().st_mode | 0o111)
    baseline_c = c_readback(c_tool, original_fs, files)

    archive = args.drivers.read_bytes()
    require(sha(archive) == DRIVERS_SHA, 'Wrong compiled-driver artifact')
    with zipfile.ZipFile(args.drivers) as package_zip:
        require(package_zip.read('platform-source-sha.txt').decode().strip() == DRIVERS_REV,
                'Driver source revision differs')
        products = {x['id']: x for x in json.loads(package_zip.read('products.json'))}
        replacements = []
        for folder, package, before, after, digest in PACKAGES:
            blob = package_zip.read(package+'/driver.elf')
            manifest_blob = package_zip.read(package+'/manifest.json')
            old = json.loads(files[folder+'/manifest.json'])
            new = json.loads(manifest_blob)
            require(old['version'] == before and new['version'] == after, 'Driver version differs')
            require({k:v for k,v in old.items() if k != 'version'} ==
                    {k:v for k,v in new.items() if k != 'version'}, 'Provider contract changed')
            require(sha(blob) == digest == products[package]['sha256'] and
                    len(blob) == products[package]['bytes'], 'Compiled provider differs')
            symbols = elf_symbols(blob)
            require(symbols == elf_symbols(files[folder+'/driver.elf']), 'Provider imports/exports changed')
            require(symbols['imports'] == products[package]['imports'], 'Provider receipt imports differ')
            files[folder+'/driver.elf'] = blob
            files[folder+'/manifest.json'] = manifest_blob
            replacements.append({'path': folder, 'from_version': before, 'to_version': after,
                                 'symbols': symbols, 'target_build': products[package]})

    cohort = dict(baseline_cohort, version='0.1.48', source_revision=args.source_revision)
    files['cohort.json'] = encode(cohort)
    changed = sorted(name for name in files if files[name] != frozen[name])
    require(changed == ['cohort.json', 'panel/driver.elf', 'panel/manifest.json',
                        'sd/driver.elf', 'sd/manifest.json'], 'Unexpected payload changes')
    app_names = sorted(name for name in files if name.endswith('.elf') and '/' not in name)
    require(len(app_names) == 21 and all(files[n] == frozen[n] for n in app_names), 'Application changed')
    require(files['boot.json'] == frozen['boot.json'], 'Boot graph changed')
    filesystem, fs_proof = pack(files)
    require(read_image(filesystem, FS_END-FS_BEGIN) == files, 'Filesystem round trip differs')
    new_c = c_readback(c_tool, filesystem, files)
    bank = initial_bank_state(firmware, filesystem, True)
    parsed = parse_record(bank[:96], True)
    require(parsed[6] == hashlib.sha256(firmware).digest() and
            parsed[7] == hashlib.sha256(filesystem).digest(), 'Bank pair digest differs')
    image = bytearray(original)
    image[FS_BEGIN:FS_END] = filesystem
    image[BANK_BEGIN:BANK_END] = bank
    preserved_regions = [(0, FS_BEGIN), (FS_END, BANK_BEGIN), (BANK_END, IMAGE_BYTES)]
    require(len(image) == IMAGE_BYTES and all(image[a:b] == original[a:b] for a,b in preserved_regions),
            'Unexpected flash-region changes')
    require(read_image(bytes(image[FS_BEGIN:FS_END]), FS_END-FS_BEGIN) == files, 'Merged store differs')
    report = {
        'schema': 'x4.preserved-driver-update', 'schema_version': 1,
        'source_revision': args.source_revision, 'builder_sha256': sha(Path(__file__).read_bytes()),
        'baseline': {'file': args.baseline.name, 'sha256': BASE_SHA, 'cohort': baseline_cohort},
        'cohort': cohort, 'native_source_marker': NATIVE_REV, 'native_rebuilt': False,
        'native_firmware_byte_identical': True, 'hardware_sdmmc_enabled': False,
        'rom_loading_speedup_claimed': False, 'hardware_tested': False,
        'driver_source_revision': DRIVERS_REV, 'driver_artifact_sha256': DRIVERS_SHA,
        'driver_ci_run': 38000904287, 'replacements': replacements,
        'changed_store_paths': changed, 'preserved_application_elves': app_names,
        'preserved_store_file_count': len(files)-len(changed),
        'baseline_store': baseline_fs_proof, 'store_generator': fs_proof,
        'baseline_c_readback': baseline_c, 'updated_c_readback': new_c,
        'preserved_flash_regions': [{'offset':a, 'bytes':b-a, 'sha256':sha(original[a:b])}
                                    for a,b in preserved_regions],
        'bank_state': {'offset':BANK_BEGIN, 'bytes':len(bank), 'sha256':sha(bank),
                       'crc_and_pair_digest_verified': True},
        'store_files': inventory(files),
        'image': {'name':OUTPUT_NAME, 'flash_offset':0, 'bytes':len(image), 'sha256':sha(image)},
        'flash_warning': 'Full first-install image overwrites internal flash settings/app-data; back up first.',
        'remaining': 'Integrate the native SDMMC addition into complete Runtime 0.1.98 source; no native downgrade.'
    }
    args.output.mkdir(parents=True)
    (args.output/OUTPUT_NAME).write_bytes(image)
    (args.output/'bootfs.bin').write_bytes(filesystem)
    (args.output/'bank_state.bin').write_bytes(bank)
    (args.output/'build-custody.json').write_bytes(encode(report))
    (args.output/'SHA256SUMS.txt').write_text(sha(image)+'  '+OUTPUT_NAME+'\n')
    print(json.dumps(report['image'], indent=2))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'drivers', 'tools', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--source-revision', required=True)
    args = parser.parse_args()
    try:
        build(args)
    except (ValueError, OSError, KeyError, AssertionError, subprocess.SubprocessError) as error:
        parser.exit(1, 'Build verification failed: '+str(error)+'\n')


if __name__ == '__main__':
    main()
