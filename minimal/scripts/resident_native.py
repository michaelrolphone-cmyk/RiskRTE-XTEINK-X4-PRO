"""Explicit accepted boot profile for the resident X4 cohort."""
import argparse
import hashlib
import json
from pathlib import Path
import prepare_native_runtime as native

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / 'minimal/apps/resident-native.json'


def selection():
    return json.loads(SPEC.read_text())


def validate(folder, candidate, runtime, *, platform_root=ROOT, native_source_root=None):
    from build_test_bundle import validate_native_composition
    spec = selection()
    # Reject the unselected generic QIO default before considering source parity.
    flash = candidate.get('x4_native_composition', {}).get('boot_flash_proof')
    native.require(isinstance(flash, dict) and flash.get('selection') == spec['boot_flash_selection'],
                   'Resident cohort requires the accepted DIO boot profile')
    native.require(candidate.get('source_sha') == spec['runtime_source'] and
                   candidate.get('firmware_version') == spec['runtime_version'] and
                   candidate.get('build_options') == spec['build_options'] and
                   candidate.get('build_environment') == spec['environment'],
                   'Resident native selection differs')
    for name, expected in spec['preserved_assets'].items():
        data = (Path(folder) / name).read_bytes()
        native.require({'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()} == expected,
                       'Accepted boot/partition/app-data asset changed: ' + name)
    for key, value in spec['preserved_flash'].items():
        native.require(flash.get(key) == value, 'Accepted flash/PSRAM setting changed: ' + key)
    source = candidate['x4_native_composition']['platform_source_sha256']
    native.require(all(source.get(k) == v for k, v in spec['preserved_startup_source_sha256'].items()),
                   'Accepted startup source changed')
    return validate_native_composition(folder, candidate, runtime, platform_root, native_source_root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    spec = selection()
    result = native.compose(args.runtime, args.output,
                            runtime_commit=spec['runtime_source'], environment=spec['environment'],
                            app_policy_rows=17, app_image_cache=True, usb_phy=True,
                            retained_wake_bytes=512, boot_flash_dio=True)
    print('Prepared accepted DIO resident native: ' + result['composition_sha256'])


if __name__ == '__main__':
    main()
