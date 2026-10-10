#!/usr/bin/env python3
"""Offline custody tests using the real pinned shared profile implementation."""
import hashlib
import configparser
import json
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import create_provisioning_profile as wrapper
from generate_profile import stage

RUNTIME = Path(os.environ['RISCRTE_RUNTIME_ROOT']).absolute()


def digest(data):
    return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def write_json(path, obj):
    path.write_text(json.dumps(obj))


class ProvisioningProfileTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        # This is a wrapper-contract fixture, not a qualification of the current
        # product cohort. CI deliberately supplies its public, locked Runtime.
        # Keep the real shared_tool identity/clean-source checks active against
        # that exact checkout instead of borrowing a newer product source pin.
        product_root = wrapper.ROOT
        lock = json.loads((product_root / 'minimal/sources.lock.json').read_text())
        runtime_config = configparser.ConfigParser()
        runtime_config.read(RUNTIME / 'platformio.ini')
        lock['runtime']['commit'] = subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], cwd=RUNTIME, text=True).strip()
        lock['runtime']['version'] = runtime_config['riscrte']['version']
        wrapper.ROOT = self.root / 'product-fixture'
        self.addCleanup(setattr, wrapper, 'ROOT', product_root)
        (wrapper.ROOT / 'minimal').mkdir(parents=True)
        self.fixture_lock = wrapper.ROOT / 'minimal/sources.lock.json'
        write_json(self.fixture_lock, lock)
        self.bundle = self.root / 'bundle'
        self.bundle.mkdir()
        self.native = self.root / 'native'
        self.native.mkdir()
        self.output = self.root / 'output'
        self.store = self.bundle / 'store'
        stage('ssd1677', self.store)
        (self.store / 'default.elf').write_bytes(b'fixture application')
        for manifest in self.store.rglob('manifest.json'):
            (manifest.parent / 'driver.elf').write_bytes(b'fixture driver')
        write_json(self.store / 'default.json', {'type': 'application', 'version': '1.0.0',
                                               'file_name': 'default.elf'})
        self.add_policy()
        lock = json.loads((wrapper.ROOT / 'minimal/sources.lock.json').read_text())
        markers = ('RTE_SOURCE=' + lock['runtime']['commit'] + '\0RISC_RUNTIME_VERSION:' +
                   lock['runtime']['version'] + '\0RISC_PAIRED_STORE_ABI:2\0esp32s3-16mb-appdata\0').encode()
        assets = {}
        for name in ('firmware.bin', 'firmware.elf', 'bootloader.bin', 'partitions.bin',
                     'appdata.bin', 'appdata-image.json'):
            blob = markers if name.startswith('firmware.') else b'fixture native input'
            (self.native / name).write_bytes(blob)
            assets[name] = digest(blob)
        (self.bundle / 'firmware.bin').write_bytes(markers)
        self.candidate = {'schema': 1, 'target': 'esp32s3-16mb-appdata',
                          'source_sha': lock['runtime']['commit'], 'firmware_version': lock['runtime']['version'],
                          'layout': wrapper.LAYOUT, 'store_abi': 2, 'flash_bytes': 0x1000000, 'assets': assets}
        self.custody = {'schema': 1, 'panel': 'ssd1677', 'runtime': self.candidate,
                        'shared_source_lock': lock, 'x4_source': 'a' * 40}
        self.refresh()

    def add_policy(self):
        boot = json.loads((self.store / 'boot.json').read_text())
        boot['app_capabilities'] = [{'manifest': 'default.json', 'grants': []}]
        write_json(self.store / 'boot.json', boot)

    def refresh(self):
        self.custody['store_files'] = {p.relative_to(self.store).as_posix(): digest(p.read_bytes())
                                     for p in self.store.rglob('*') if p.is_file()}
        write_json(self.bundle / 'build-custody.json', self.custody)
        write_json(self.native / 'candidate.json', self.candidate)

    def run_create(self, **changes):
        args = dict(runtime=RUNTIME, bundle=self.bundle, native=self.native, panel=self.custody['panel'],
                    base_url='https://test.invalid/payload/', profile_url='https://test.invalid/profile.json',
                    output=self.output)
        args.update(changes)
        return wrapper.create(**args)

    def refused(self, **changes):
        with self.assertRaises((ValueError, OSError, KeyError)):
            self.run_create(**changes)
        self.assertFalse(self.output.exists())

    def test_both_panels_exact_bytes_schema2_and_no_secrets(self):
        for panel in wrapper.PANELS:
            with self.subTest(panel=panel):
                stage(panel, self.store)
                self.add_policy()
                self.custody['panel'] = panel
                self.refresh()
                out = self.root / panel
                receipt = self.run_create(output=out)
                payload = json.loads((out / 'payload-profile.json').read_text())
                self.assertEqual(payload['schema_version'], 2)
                self.assertNotIn('wifi', payload)
                self.assertLessEqual(receipt['payload_profile_bytes'], 16384)
                self.assertEqual(receipt['payload_profile_sha256'], digest((out / 'payload-profile.json').read_bytes())['sha256'])
                self.assertTrue((out / 'COMPLETE').is_file())
                self.assertFalse(list(out.rglob('nvs*')))
                for entry in payload['files']:
                    self.assertEqual(digest((out / 'files' / entry['path']).read_bytes()),
                                     {k: entry[k] for k in ('bytes', 'sha256')})

    def test_absent_manifest(self):
        (self.bundle / 'build-custody.json').unlink()
        self.refused()

    def test_mismatched_runtime_checkout_is_refused(self):
        lock = json.loads(self.fixture_lock.read_text())
        lock['runtime']['commit'] = 'b' * 40
        write_json(self.fixture_lock, lock)
        with self.assertRaisesRegex(ValueError, 'exact clean pinned Runtime scripts required'):
            self.run_create()
        self.assertFalse(self.output.exists())

    def test_sleep_and_fast_panel_profiles_preserve_exact_selection(self):
        for panel, provider in (('ssd1677', 'fallback'), ('uc8279', 'fallback'),
                                ('uc8279', 'uc8279-fast')):
            with self.subTest(panel=panel, provider=provider):
                stage(panel, self.store, True, provider)
                for manifest in self.store.rglob('manifest.json'):
                    (manifest.parent / 'driver.elf').write_bytes(b'fixture driver')
                self.add_policy()
                self.custody.update(panel=panel, panel_driver=provider, sleep=True)
                self.refresh()
                out = self.root / (panel + '-' + provider)
                self.run_create(output=out)
                self.assertEqual((out / 'files/board.json').read_bytes(),
                                 (self.store / 'board.json').read_bytes())
                self.assertEqual((out / 'files/power/manifest.json').read_bytes(),
                                 (self.store / 'power/manifest.json').read_bytes())

    def test_profile_custody_cannot_relabel_fast_bus_or_power_owner(self):
        stage('uc8279', self.store, True, 'uc8279-fast')
        for manifest in self.store.rglob('manifest.json'):
            (manifest.parent / 'driver.elf').write_bytes(b'fixture driver')
        self.add_policy()
        self.custody.update(panel='uc8279', panel_driver='uc8279-fast', sleep=True)
        for key, value in (('panel_driver', 'fallback'), ('panel_driver', 'unknown'),
                           ('sleep', False), ('sleep', 1)):
            original = self.custody[key]
            self.custody[key] = value
            self.refresh()
            self.refused()
            self.custody[key] = original

    def test_selected_power_provider_is_required(self):
        stage('uc8279', self.store, True, 'uc8279-fast')
        for manifest in self.store.rglob('manifest.json'):
            (manifest.parent / 'driver.elf').write_bytes(b'fixture driver')
        self.add_policy()
        boot = json.loads((self.store / 'boot.json').read_text())
        boot['drivers'] = [d for d in boot['drivers'] if d.get('instance_id') != 17]
        write_json(self.store / 'boot.json', boot)
        self.custody.update(panel='uc8279', panel_driver='uc8279-fast', sleep=True)
        self.refresh()
        self.refused()

    def test_missing_application_manifest_even_with_fresh_receipt(self):
        (self.store / 'default.json').unlink()
        self.refresh()
        self.refused()

    def test_missing_driver_executable(self):
        (self.store / 'panel/driver.elf').unlink()
        self.refresh()
        self.refused()

    def test_stale_manifest(self):
        (self.store / 'default.elf').write_bytes(b'changed')
        self.refused()

    def test_added_file_is_stale(self):
        (self.store / 'extra').write_bytes(b'x')
        self.refused()

    def test_native_hash_mismatch(self):
        (self.native / 'firmware.bin').write_bytes(b'changed')
        self.refused()

    def test_debug_native_elf_uses_shared_32mib_bound_only(self):
        path = self.native / 'firmware.elf'
        marker = path.read_bytes()
        raw = marker + b'\0' * (17 * 1024 * 1024 - len(marker))
        path.write_bytes(raw)
        self.candidate['assets']['firmware.elf'] = digest(raw)
        self.refresh()
        self.run_create(output=self.root / 'large-debug-elf')
        # The debug allowance does not enlarge the app slot or profile/files.
        firmware = self.native / 'firmware.bin'
        original_firmware = firmware.read_bytes()
        firmware.write_bytes(raw)
        self.candidate['assets']['firmware.bin'] = digest(raw)
        (self.bundle / 'firmware.bin').write_bytes(raw)
        self.refresh()
        self.refused()
        firmware.write_bytes(original_firmware)
        self.candidate['assets']['firmware.bin'] = digest(original_firmware)
        (self.bundle / 'firmware.bin').write_bytes(original_firmware)
        self.refresh()
        with path.open('wb') as stream:
            stream.write(marker)
            stream.truncate(32 * 1024 * 1024 + 1)
        with self.assertRaisesRegex(ValueError, 'input file bounds'):
            self.run_create()
        self.assertFalse(self.output.exists())

    def test_mixed_firmware_store(self):
        (self.bundle / 'firmware.bin').write_bytes(b'other candidate')
        self.refused()

    def test_mixed_runtime_receipt(self):
        other = dict(self.candidate, source_sha='b' * 40)
        write_json(self.native / 'candidate.json', other)
        self.refused()

    def test_abi_layout_version(self):
        for key, value in [('store_abi', 1), ('layout', 'riscrte-paired-16m-v1'),
                           ('firmware_version', '0.1.39'), ('store_abi', True)]:
            with self.subTest(key=key, value=value):
                old = self.candidate[key]
                self.candidate[key] = value
                self.refresh()
                self.refused()
                self.candidate[key] = old

    def test_wrong_panel(self):
        self.refused(panel='uc8279')

    def test_unsafe_store_paths(self):
        for name in ('bad name', 'x' * 31, '.provision-sha256'):
            with self.subTest(name=name):
                path = self.store / name
                path.write_bytes(b'x')
                self.refresh()
                self.refused()
                path.unlink()

    def test_symlink(self):
        (self.store / 'link').symlink_to(self.store / 'default.elf')
        self.refresh()
        self.refused()

    def test_native_traversal(self):
        self.candidate['assets']['../escape'] = digest(b'x')
        self.refresh()
        self.refused()

    def test_url_validation(self):
        for url in ('http://test.invalid/a/', 'https://user:pass@test.invalid/a/',
                    'https://test.invalid/a/../b/', 'https://test.invalid/a/?token=secret'):
            self.refused(base_url=url)
        self.refused(profile_url='http://test.invalid/profile.json')
        self.refused(profile_url='https://test.invalid/payload/boot.json')

    def test_file_count_and_profile_bounds(self):
        for i in range(129):
            (self.store / str(i)).write_bytes(b'x')
        self.refresh()
        self.refused()

    def test_16k_limit_even_below_file_count(self):
        count = len(self.custody['store_files'])
        for i in range(128 - count):
            (self.store / (f'{i:03}' + 'a' * 27)).write_bytes(b'x')
        self.refresh()
        self.refused()

    def test_output_cannot_modify_input_tree(self):
        nested = self.bundle / 'new-output'
        with self.assertRaises(ValueError):
            self.run_create(output=nested)
        self.assertFalse(nested.exists())

    def test_existing_output_preserved(self):
        self.output.mkdir()
        sentinel = self.output / 'keep'
        sentinel.write_bytes(b'keep')
        with self.assertRaises(ValueError):
            self.run_create()
        self.assertEqual(sentinel.read_bytes(), b'keep')


if __name__ == '__main__':
    unittest.main()
