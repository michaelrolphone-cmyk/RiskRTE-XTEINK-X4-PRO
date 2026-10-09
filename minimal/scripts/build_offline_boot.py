#!/usr/bin/env python3
"""Build the native half of X4 0.1.30; preserve the separately packaged 0.1.29 store."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

VERSION = '0.1.30'
RUNTIME = 'dc41f90795d7580abea335f082e1132936bffe50'
NATIVE_HASHES = {
    'X4BootRecord.h': 'caf55b171cc97800fa8c07474748e8607aec5d92f7a16daea10f2eb35d7a4e4d',
    'X4EarlyBoot.cpp': 'c8646b70e9e8a8805d7e03152418129628860f59a3b2f3de4662c81ad425dcc6',
}
ENVIRONMENT = '''
[env:x4-offline-boot]
extends = env:esp32s3-16mb-appdata-iq-stage
board_build.arduino.memory_type = dio_opi
board_build.flash_mode = dio
board_build.f_flash = 80000000L
board_build.f_cpu = 240000000L
board_build.flash_size = 16MB
board_upload.flash_size = 16MB
build_flags =
  ${env:esp32s3-16mb-appdata-iq-stage.build_flags}
  -DRISC_APP_POLICY_ROWS=17
  -DRISC_APP_IMAGE_CACHE=1
  -DRISC_NATIVE_DIAGNOSTIC_OBSERVER=1
  -Wl,--wrap=app_main
  -Wl,-u,risc_x4_native_composition_identity
'''

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    product = Path(__file__).resolve().parents[2]
    runtime, output = args.runtime.resolve(), args.output.resolve()
    if git(runtime, 'rev-parse', 'HEAD') != RUNTIME:
        raise SystemExit('Runtime checkout does not match the reviewed diagnostic-drain pin')
    output.mkdir(parents=True, exist_ok=True)
    source = product / 'minimal/native'
    destination = runtime / 'src/x4product'
    destination.mkdir(parents=True, exist_ok=True)
    for name, expected in NATIVE_HASHES.items():
        if digest(source / name) != expected:
            raise SystemExit('Native source checksum mismatch: ' + name)
        shutil.copy2(source / name, destination / name)
    product_sha = git(product, 'rev-parse', 'HEAD')
    identity = f'X4_PRODUCT={VERSION};X4_SOURCE={product_sha};RUNTIME={RUNTIME};OFFLINE_BOOTLOG=1'
    (destination / 'X4NativeBuildIdentity.h').write_text(
        '#pragma once\n#define X4_NATIVE_COMPOSITION_IDENTITY ' + json.dumps(identity) + '\n')
    ini = runtime / 'platformio.ini'
    if '[env:x4-offline-boot]' in ini.read_text():
        raise SystemExit('Use a fresh Runtime checkout, not an already composed directory')
    ini.write_text(ini.read_text() + ENVIRONMENT)
    subprocess.run(['pio', 'run', '-d', str(runtime), '-e', 'x4-offline-boot', '-j', '2'], check=True)
    built = runtime / '.pio/build/x4-offline-boot'
    for name in ('firmware.bin', 'firmware.elf', 'firmware.map', 'bootloader.bin', 'partitions.bin'):
        shutil.copy2(built / name, output / name)
    from elftools.elf.elffile import ELFFile
    required = ('__wrap_app_main', 'initVariant', 'risc_native_startup_error',
                'risc_native_diagnostic_observer', 'risc_native_diagnostic_drain',
                'risc_x4_native_composition_identity', 'risc_x4_boot_record')
    with (output / 'firmware.elf').open('rb') as f:
        elf = ELFFile(f)
        symbols = elf.get_section_by_name('.symtab')
        verified = {}
        for name in required:
            entries = symbols.get_symbol_by_name(name) or []
            entry = next((s for s in entries if s['st_shndx'] != 'SHN_UNDEF' and
                          s['st_info']['bind'] == 'STB_GLOBAL'), None)
            if entry is None:
                raise SystemExit('Required strong native symbol absent: ' + name)
            verified[name] = {'address': entry['st_value'], 'bytes': entry['st_size']}
        if verified['risc_x4_boot_record']['bytes'] != 264:
            raise SystemExit('Unexpected RTC record layout')
    fw = (output / 'firmware.bin').read_bytes()
    if fw[0] != 0xE9 or fw[2] != 2 or len(fw) > 0x230000:
        raise SystemExit('Invalid DIO application image or partition overflow')
    if identity.encode() not in fw or b'/appdata/x4-boot.log' not in fw:
        raise SystemExit('The built firmware does not contain the requested logger')
    # A source archive enables offline paired-bank packaging and independent audit.
    with tarfile.open(output / 'runtime-source.tar.gz', 'w:gz') as archive:
        for path in sorted(runtime.rglob('*')):
            rel = path.relative_to(runtime)
            if path.is_file() and not any(part in ('.git', '.pio', '__pycache__') for part in rel.parts):
                archive.add(path, arcname=str(Path('runtime') / rel), recursive=False)
    metadata = {
        'schema': 'x4.offline-boot-native', 'schema_version': 1,
        'product': 'xteink-x4-pro', 'version': VERSION,
        'product_source': product_sha, 'runtime_source': RUNTIME, 'runtime_version': '0.1.76',
        'baseline_product_version': '0.1.29',
        'baseline_full_sha256': 'b06d0679e2be7b0c8450aaef46fd1ad942415cf213586ee90f974fff18079205',
        'native_source_sha256': NATIVE_HASHES, 'strong_symbols': verified,
        'flash_mode': 'dio', 'flash_bytes': 16777216, 'psram': 'opi',
        'app_policy_rows': 17, 'app_image_cache': True, 'native_diagnostic_observer': True,
        'scope': 'New native firmware. Full product assembly must retain and re-admit the 0.1.29 app/provider store.',
        'hardware_tested': False,
        'artifacts': {x.name: {'bytes': x.stat().st_size, 'sha256': digest(x)}
                      for x in sorted(output.iterdir()) if x.is_file()}
    }
    (output / 'native-build.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(json.dumps(metadata, indent=2))

if __name__ == '__main__':
    main()
