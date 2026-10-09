#!/usr/bin/env python3
"""X4 seed callbacks retain exact proofs and reject edited source/link metadata."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import native_bundle_composition_test as fixtures
import provision_seed_extension as extension


@unittest.skipUnless(importlib.util.find_spec('elftools'), 'Requires pinned pyelftools')
class SeedExtensionTest(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.NativeBundleCompositionTest(methodName='runTest')
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.candidate = self.fixture.candidate
        self.candidate.update(stage_logs=False, performance_trace=False,
                              native_proof={'core_proof_remains_upstream': True})
        self.blobs = {path.name: path.read_bytes() for path in self.fixture.native.iterdir()}
        self.validator = extension.make_validator(self.fixture.runtime, self.fixture.platform)

    def validate(self):
        return self.validator(self.candidate, self.blobs)

    def replace(self, name, value):
        self.blobs[name] = fixtures.composition.encoded(value)
        self.candidate['assets'][name] = {'bytes': len(self.blobs[name]),
                                          'sha256': fixtures.composition.sha(self.blobs[name])}

    def test_exact_original_assets_metadata_and_no_core_override(self):
        before = copy.deepcopy(self.candidate)
        result = self.validate()
        self.assertEqual(set(result), {'id', 'assets', 'metadata', 'native_proof'})
        self.assertEqual(result['id'], 'x4.native-composition-v1')
        self.assertEqual(result['native_proof'], {})
        self.assertEqual(result['assets'], {name: self.blobs[name] for name in extension.ASSETS})
        self.assertEqual(result['metadata'], {name: self.candidate[name] for name in extension.METADATA})
        self.assertEqual(self.candidate, before)

    def test_tampered_asset_digest_and_rehashed_sidecar_refused(self):
        self.blobs['x4-native-proof.json'] = b'{}'
        self.assertRaisesRegex(ValueError, 'asset digest', self.validate)
        self.replace('x4-native-proof.json', {})
        self.assertRaisesRegex(ValueError, 'startup proof mismatch', self.validate)

    def test_missing_or_additional_sidecars_refused(self):
        del self.blobs['x4-native-proof.json']
        self.assertRaisesRegex(ValueError, 'assets missing', self.validate)
        self.blobs['x4-native-proof.json'] = (self.fixture.native / 'x4-native-proof.json').read_bytes()
        self.replace('other.json', {})
        self.assertRaisesRegex(ValueError, 'asset inventory', self.validate)

    def test_metadata_stage_types_and_proof_types_refused(self):
        for value in (True, 0, 1, None, 'false'):
            self.candidate['stage_logs'] = value
            self.assertRaisesRegex(ValueError, 'stage-log metadata', self.validate)
        self.candidate['stage_logs'] = False
        self.candidate['x4_native_composition']['startup_proof']['hardware_qualified'] = 0
        self.assertRaisesRegex(ValueError, 'summary differs', self.validate)

    def test_build_options_and_unexpected_stage_proof_refused(self):
        self.candidate['build_options'] = {'app_policy_rows': 17, 'app_image_cache': False}
        self.assertRaisesRegex(ValueError, 'options differ', self.validate)
        self.candidate['build_options'] = self.fixture.record['build_options']
        self.candidate['native_proof']['stage_logs'] = {'enabled': True}
        self.assertRaisesRegex(ValueError, 'stage-log proof mismatch', self.validate)

    def test_rehashed_source_binding_and_dirty_sources_refused(self):
        record = copy.deepcopy(self.fixture.record)
        record['platform_source_sha256']['minimal/native/X4EarlyBoot.cpp'] = 'a' * 64
        record.pop('composition_sha256')
        record['composition_sha256'] = fixtures.composition.sha(fixtures.composition.encoded(record))
        self.replace('x4-native-composition.json', record)
        self.assertRaisesRegex(ValueError, 'Committed X4 source differs', self.validate)
        self.replace('x4-native-composition.json', self.fixture.record)
        (self.fixture.runtime / 'src/main.cpp').write_text('changed')
        self.assertRaisesRegex(ValueError, 'Clean committed source', self.validate)

    def test_missing_strong_native_hook_refused_after_rehash(self):
        self.fixture.compile_elf(weak=True)
        raw = (self.fixture.native / 'firmware.elf').read_bytes()
        self.blobs['firmware.elf'] = raw
        self.candidate['assets']['firmware.elf'] = {'bytes': len(raw), 'sha256': fixtures.composition.sha(raw)}
        self.assertRaisesRegex(ValueError, 'Missing strong X4 native symbol', self.validate)

    def test_compiled_source_marker_refused_after_rehash(self):
        raw = self.blobs['firmware.bin'].replace(b'RTE_SOURCE=', b'BAD_SOURCE=')
        self.blobs['firmware.bin'] = raw
        self.candidate['assets']['firmware.bin'] = {'bytes': len(raw), 'sha256': fixtures.composition.sha(raw)}
        self.assertRaisesRegex(ValueError, 'marker mismatch', self.validate)


if __name__ == '__main__':
    unittest.main()
