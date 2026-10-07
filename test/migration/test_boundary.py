#!/usr/bin/env python3
"""Check X4 migration ownership, source custody and shared dependency resolution."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
PACKAGES = ('battery', 'buttons', 'frontlight', 'gt911', 'i2c', 'panel', 'rtc', 'sd')
RUNTIME = None
SOURCE_FIXTURE = False
BUILT = False


def blob(path):
    data = path.read_bytes()
    return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()


class MigrationBoundary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lock = json.loads((ROOT/'migration-source.json').read_text())
        cls.additional = json.loads((ROOT/'migration-additional-files.json').read_text())
        cls.records = cls.lock['files'] + cls.additional

    def test_safe_unique_inventory(self):
        paths = [item['path'] for item in self.records]
        self.assertEqual(len(paths), len(set(paths)))
        for item in self.records:
            path = PurePosixPath(item['path'])
            self.assertFalse(path.is_absolute())
            self.assertNotIn('..', path.parts)
            self.assertEqual(str(path), item['path'])
            self.assertRegex(item['blob'], r'^[0-9a-f]{40}$')
            self.assertIn(item['mode'], ('100644', '100755'))
            self.assertTrue((ROOT/path).is_file(), str(path))
            self.assertFalse((ROOT/path).is_symlink(), str(path))

    def test_byte_identical_provider_and_board_migration(self):
        # Newly authored build/provisioning wrappers deliberately differ from their origin.
        for item in self.records:
            if item['path'].startswith(('Drivers/', 'lib/', 'src/')):
                self.assertEqual(blob(ROOT/item['path']), item['blob'], item['path'])
                self.assertEqual(blob(RUNTIME/item['path']), item['blob'], item['path'])

    def test_additional_fixture_custody(self):
        for item in self.additional:
            self.assertEqual(blob(ROOT/item['path']), item['blob'], item['path'])
            self.assertEqual(blob(RUNTIME/item['path']), item['blob'], item['path'])

    def test_shared_runtime_not_vendored(self):
        for path in ('sdk', 'Apps', 'Services', 'lib/elf_loader', 'Drivers/storage_fatfs',
                     'Drivers/platform_clock_v1', 'Drivers/common', 'src/runtime',
                     'src/native', 'src/activities', 'src/main.cpp', 'src/DeskClockSleep.cpp'):
            self.assertFalse((ROOT/path).exists(), path)
        self.assertEqual({p.name for p in (ROOT/'Drivers').iterdir() if p.is_dir()},
                         {'x4pro_'+name for name in PACKAGES} | {'x4pro_board'})

    def test_shared_inputs_resolve(self):
        for path in ('sdk/driver/RiscProviderV2.h', 'sdk/driver/RiscRtcCalendarV2.h',
                     'Drivers/platform_clock_v1/driver.c', 'Drivers/storage_fatfs/sd_protocol.h',
                     'Drivers/storage_fatfs/volume.c', 'Drivers/storage_fatfs/fatfs/ff.c',
                     'Drivers/storage_fatfs/fatfs/ffunicode.c', 'Drivers/common/pcf8563_rtc_ops.h',
                     'scripts/normalize_xtensa_relocations.py', 'scripts/validate_xtensa_relative_targets.py',
                     'scripts/generate_privileged_imports_v1.py', 'scripts/pack_rte_zip.py',
                     'lib/elf_loader/src/esp_privileged_manifest_imports.c'):
            self.assertTrue((RUNTIME/path).is_file(), path)
        for item in self.records:
            source = RUNTIME/item['path']
            if source.suffix not in ('.c', '.cpp', '.h'):
                continue
            for include in re.findall(r'^\s*#include\s+"(\.\./[^"\n]+)"', source.read_text(), re.M):
                resolved = (source.parent/include).resolve()
                self.assertTrue(resolved.is_relative_to(RUNTIME), str(resolved))
                self.assertTrue(resolved.is_file(), f'{source}: {include}')

    def test_package_identity_and_optional_build_custody(self):
        for name in (*PACKAGES, 'platform_clock_v1'):
            shared = name == 'platform_clock_v1'
            path = Path('Drivers')/(name if shared else 'x4pro_'+name)/'manifest.json'
            manifest = json.loads(((RUNTIME if shared else ROOT)/path).read_text())
            self.assertEqual(manifest['id'], 'platform-clock-v1' if shared else 'x4pro-'+name)
            self.assertRegex(manifest['version'], r'^\d+\.\d+\.\d+$')
            self.assertEqual(json.loads((RUNTIME/path).read_text()), manifest)
            built = RUNTIME/'dist/experimental'/manifest['id']
            if BUILT or (built/'driver.elf').exists() or (built/'manifest.json').exists():
                observed = json.loads((built/'manifest.json').read_text())
                for key, value in manifest.items():
                    self.assertEqual(observed.get(key), value, manifest['id']+': '+key)
                data = (built/'driver.elf').read_bytes()
                self.assertEqual(observed['size_bytes'], len(data))
                self.assertEqual(observed['sha256'], hashlib.sha256(data).hexdigest())

    def test_origin_or_explicit_source_fixture(self):
        origin = RUNTIME/'build-origin.json'
        if SOURCE_FIXTURE:
            # Source fixture is useful host coverage but cannot certify a composed build.
            for item in self.records:
                self.assertEqual(blob(RUNTIME/item['path']), item['blob'], item['path'])
            return
        self.assertTrue(origin.is_file(), 'run prepare_runtime.py first')
        value = json.loads(origin.read_text())
        self.assertEqual(value['schema'], 1)
        self.assertEqual(value['runtime'], self.lock['upstream'])
        self.assertEqual(value['platform']['repository'], 'michaelrolphone-cmyk/RiskRTE-XTEINK-X4-PRO')
        self.assertRegex(value['platform']['commit'], r'^[0-9a-f]{40}$')
        self.assertIsInstance(value['platform']['dirty'], bool)
        self.assertTrue({item['path'] for item in self.records} <= set(value['overlay_files']))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', required=True, type=Path)
    parser.add_argument('--source-fixture', action='store_true')
    parser.add_argument('--built', action='store_true')
    args, unittest_args = parser.parse_known_args()
    RUNTIME = args.runtime.resolve()
    SOURCE_FIXTURE = args.source_fixture
    BUILT = args.built
    unittest.main(argv=[sys.argv[0], *unittest_args], verbosity=2)
