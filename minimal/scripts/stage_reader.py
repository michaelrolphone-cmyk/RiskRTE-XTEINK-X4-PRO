#!/usr/bin/env python3
"""Stage a reviewable Reader cohort and measure the existing store's capacity.

This does not emit a flash image. Native admission and a fitting product layout
are required before producing one. No existing application is removed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def stage(a):
    if a.output.exists():
        raise ValueError('Choose a new staging directory')
    store = a.output / 'store'
    shutil.copytree(a.baseline_store, store)
    app = json.loads((a.reader_package / 'ebook_reader.json').read_text())
    if app['id'] != 'ebook-reader' or app['file_name'] != 'ebook_reader.elf':
        raise ValueError('Unexpected Reader package')
    for source, target in [(a.reader_package / 'ebook_reader.elf', store / 'ebook_reader.elf'),
                           (a.reader_package / 'ebook_reader.json', store / 'ebook_reader.json'),
                           (a.scene_provider / 'driver.elf', store / 'ui-scene/driver.elf'),
                           (a.scene_provider / 'manifest.json', store / 'ui-scene/manifest.json'),
                           (a.storage_provider / 'driver.elf', store / 'sd/driver.elf'),
                           (a.storage_provider / 'manifest.json', store / 'sd/manifest.json')]:
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    boot_path = store / 'boot.json'
    boot = json.loads(boot_path.read_text())
    if any(row['manifest'] == 'ebook_reader.json' for row in boot['app_capabilities']):
        raise ValueError('Reader already selected in baseline')
    boot['app_capabilities'].append({'manifest': 'ebook_reader.json', 'grants':
                                    [{**row, 'instance_id': 0} for row in app['requires']]})
    boot['resident_shell']['foreground'].append('ebook_reader.elf')
    boot_path.write_text(json.dumps(boot, indent=2) + '\n')
    # A copied cohort identity describes the old firmware; never label this new
    # unqualified store with it. The product assembler must generate a new one.
    (store / 'cohort.json').unlink(missing_ok=True)
    inventory = {str(p.relative_to(store)): {'bytes': p.stat().st_size,
                 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                 for p in sorted(store.rglob('*')) if p.is_file()}
    total = sum(row['bytes'] for row in inventory.values())
    report = {'schema': 1, 'store_payload_bytes': total, 'store_partition_bytes': a.store_bytes,
              'payload_fits_before_filesystem_overhead': total <= a.store_bytes,
              'app_policies': len(boot['app_capabilities']), 'provider_count': len(boot['drivers']),
              'native_runtime_required': '0.2.4 with Reader shared capabilities',
              'complete_flash_image': False, 'hardware_qualified': False,
              'native_admission': 'separate matching-candidate verification required',
              'inventory': inventory}
    (a.output / 'capacity.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'inventory'}, indent=2))
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline-store', 'reader-package', 'scene-provider', 'storage-provider', 'output'):
        p.add_argument('--' + name, type=lambda x: Path(x).resolve(), required=True)
    p.add_argument('--store-bytes', type=lambda x: int(x, 0), default=0x510000)
    stage(p.parse_args())
