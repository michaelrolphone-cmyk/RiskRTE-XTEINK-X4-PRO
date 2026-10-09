#!/usr/bin/env python3
"""Opt-in real Xtensa seed-callback regressions; no hardware or image install.

Set X4_SEED_RUNTIME_API, X4_SEED_RUNTIME_SOURCE, X4_SEED_PLATFORM_SOURCE and
X4_SEED_CANDIDATE to explicit local directories. The candidate must already
carry the canonical Runtime app-policy proof. X4_SEED_REFERENCE optionally
selects a verified generic ABI2 seed for the frozen-seed revalidation test.
Use the pinned Python environment and X4_XTENSA_OBJDUMP from the native build.
"""
import copy
import importlib
import io
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import provision_seed_extension as extension

INPUTS = ('X4_SEED_RUNTIME_API', 'X4_SEED_RUNTIME_SOURCE',
          'X4_SEED_PLATFORM_SOURCE', 'X4_SEED_CANDIDATE')


@unittest.skipUnless(all(os.environ.get(name) for name in INPUTS), 'Requires explicit real native/source inputs')
class TargetSeedExtensionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, str(Path(os.environ['X4_SEED_RUNTIME_API']) / 'scripts'))
        cls.seed = importlib.import_module('provision_seed')
        cls.device = importlib.import_module('provision_device')
        cls.iq = importlib.import_module('radio_iq_proof')
        cls.validator = staticmethod(extension.make_validator(os.environ['X4_SEED_RUNTIME_SOURCE'],
                                                               os.environ['X4_SEED_PLATFORM_SOURCE']))

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='x4-seed-target-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.folder = self.root / 'candidate'
        shutil.copytree(os.environ['X4_SEED_CANDIDATE'], self.folder)
        self.record = json.loads((self.folder / 'candidate.json').read_bytes())

    def save(self):
        (self.folder / 'candidate.json').write_bytes(extension.composition.encoded(self.record))

    def replace(self, name, raw):
        (self.folder / name).write_bytes(raw)
        self.record['assets'][name] = {'bytes': len(raw), 'sha256': self.seed.sha(raw)}
        self.save()

    def validate(self):
        return self.seed.candidate(self.folder, self.record['source_sha'], self.validator)

    def mutate_elf(self, symbol, data):
        from elftools.elf.elffile import ELFFile
        raw = bytearray((self.folder / 'firmware.elf').read_bytes())
        elf = ELFFile(io.BytesIO(raw))
        symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
        selected = symbols[symbol]
        section = elf.get_section(selected['st_shndx'])
        at = section['sh_offset'] + selected['st_value'] - section['sh_addr']
        raw[at:at + len(data)] = data
        self.replace('firmware.elf', bytes(raw))

    def test_real_candidate_preserves_original_sidecars_and_core_proofs(self):
        record, blobs = self.validate()
        receipt = record['_seed_extension']
        self.assertEqual(receipt['id'], extension.IDENTITY)
        self.assertEqual(set(receipt['native_proof']), {'stage_logs'})
        self.assertEqual(set(receipt['assets']), set(extension.ASSETS))
        self.assertEqual(set(receipt['metadata']), set(extension.METADATA))
        self.assertEqual(record['native_proof'], self.record['native_proof'])
        for name in extension.ASSETS:
            self.assertEqual(blobs[name], (self.folder / name).read_bytes())
        self.assertRaisesRegex(ValueError, 'explicit candidate extension',
                               self.seed.candidate, self.folder, self.record['source_sha'])

    def test_tampered_stage_metadata_refused(self):
        self.record['stage_logs'] = 1
        self.save()
        self.assertRaisesRegex(ValueError, 'stage-log metadata', self.validate)

    def test_rehashed_options_sidecar_refused(self):
        value = json.loads((self.folder / 'x4-runtime-options-proof.json').read_bytes())
        value['hardware_qualified'] = True
        self.replace('x4-runtime-options-proof.json', extension.composition.encoded(value))
        self.assertRaisesRegex(ValueError, 'option proof mismatch', self.validate)

    def test_rehashed_source_binding_refused(self):
        value = json.loads((self.folder / 'x4-native-composition.json').read_bytes())
        value['runtime']['tree'] = 'a' * 40
        del value['composition_sha256']
        value['composition_sha256'] = self.seed.sha(extension.composition.encoded(value))
        self.replace('x4-native-composition.json', extension.composition.encoded(value))
        self.assertRaisesRegex(ValueError, 'Runtime tree differs', self.validate)

    def test_native_main_task_bypass_refused_with_fresh_iq_proof(self):
        from elftools.elf.elffile import ELFFile
        raw = bytearray((self.folder / 'firmware.elf').read_bytes())
        elf = ELFFile(io.BytesIO(raw))
        symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
        edge = self.record['x4_native_composition']['startup_proof']['target_call_edges'][
            'main_task -> __wrap_app_main']
        address = edge['literal']
        section = next(s for s in elf.iter_sections()
                       if s['sh_addr'] <= address < s['sh_addr'] + s['sh_size'] and s['sh_flags'] & 2)
        at = section['sh_offset'] + address - section['sh_addr']
        raw[at:at + 4] = struct.pack('<I', symbols['app_main']['st_value'])
        self.replace('firmware.elf', bytes(raw))
        iq = self.iq.prove(bytes(raw))
        self.record['native_proof']['radio_iq'] = json.loads(json.dumps(iq))
        self.replace('radio-iq-proof.json', extension.composition.encoded(iq))
        self.assertRaisesRegex(ValueError, 'Unproven X4 startup call: main_task', self.validate)

    def test_core_rollback_tls_iq_and_policy_cannot_be_overridden(self):
        self.record['native_proof']['bundle_sha256'] = 'a' * 64
        self.save()
        self.assertRaisesRegex(ValueError, 'extension native proof mismatch', self.validate)
        self.record = json.loads((Path(os.environ['X4_SEED_CANDIDATE']) / 'candidate.json').read_bytes())
        self.record['native_proof']['app_policy']['rows'] = 16
        self.save()
        self.assertRaisesRegex(ValueError, 'policy row mismatch', self.validate)
        self.record['native_proof'].pop('app_policy')
        self.save()
        self.assertRaisesRegex(ValueError, 'app policy proof missing', self.validate)
        self.record = json.loads((Path(os.environ['X4_SEED_CANDIDATE']) / 'candidate.json').read_bytes())
        self.replace('radio-iq-proof.json', b'{}')
        self.assertRaisesRegex(ValueError, 'radio IQ linked reservation proof', self.validate)
        self.mutate_elf('verifyRollbackLater', b'\x00')
        self.assertRaisesRegex(ValueError, 'rollback hook does not return true', self.validate)

    @unittest.skipUnless(os.environ.get('X4_SEED_REFERENCE'), 'Requires a genuine generic ABI2 seed')
    def test_real_frozen_seed_revalidates_explicit_callback_and_receipt(self):
        reference = Path(os.environ['X4_SEED_REFERENCE'])
        reference_work = self.root / 'reference-verify'; reference_work.mkdir()
        original, _ = self.device.verify_seed(reference, reference_work)
        self.assertEqual(original['store_abi'], 2)
        record, blobs = self.validate()
        # Retain a previously verified genuine generic seed store. This tests
        # frozen-byte verification, not compose()'s newer source-HEAD guard.
        payloads = {**blobs, 'candidate.json': (self.folder / 'candidate.json').read_bytes()}
        for name in (*self.seed.SEED_FILES, 'bootfs0.bin'):
            payloads[name] = (reference / name).read_bytes()
        payloads['otadata.bin'] = self.seed.initial_otadata()
        payloads['bank_state.bin'] = self.seed.initial_bank_state(blobs['firmware.bin'], payloads['bootfs0.bin'], True)
        manifest = copy.deepcopy(original)
        for key in ('source_sha', 'firmware_version', 'target', 'layout', 'store_abi'):
            manifest[key] = record[key]
        manifest['extension'] = record['_seed_extension']
        manifest['assets'] = {name: {'bytes': len(raw), 'sha256': self.seed.sha(raw)}
                              for name, raw in payloads.items()}
        output = self.root / 'seed'; output.mkdir()
        for name, raw in payloads.items():
            (output / name).write_bytes(raw)

        def save_seed():
            (output / 'seed.json').write_bytes(extension.composition.encoded(manifest))
            (output / 'SHA256SUMS').write_text(''.join(
                f'{self.seed.sha(p.read_bytes())}  {p.name}\n' for p in sorted(output.iterdir()) if p.name != 'SHA256SUMS'))

        save_seed()
        work = self.root / 'verify'; work.mkdir()
        self.assertRaisesRegex(ValueError, 'explicit candidate extension', self.device.verify_seed, output, work)
        verified, frozen = self.device.verify_seed(output, work, self.validator)
        self.assertEqual(verified, manifest)
        for name in extension.ASSETS:
            self.assertEqual(frozen[name], blobs[name])
        manifest['extension']['metadata']['stage_logs'] = False
        save_seed()
        other = self.root / 'verify-tampered'; other.mkdir()
        self.assertRaisesRegex(ValueError, 'extension revalidation mismatch',
                               self.device.verify_seed, output, other, self.validator)


if __name__ == '__main__':
    unittest.main()
