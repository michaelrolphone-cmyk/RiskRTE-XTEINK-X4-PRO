"""Reject drift in the selected final native failure-evidence proof."""
import copy
import io
import json
import os
from pathlib import Path
import sys
import unittest
from elftools.elf.elffile import ELFFile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import prepare_native_runtime as native


class FailureOptions(unittest.TestCase):
    def test_explicit_boolean_opt_in(self):
        for value in (False, 0, 1, None, 'true'):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'explicit true'):
                native.validate_build_options({'app_policy_rows': 17, 'app_image_cache': True, 'failure_evidence': value})


@unittest.skipUnless(os.environ.get('X4_SELECTED_DIO_NATIVE'), 'requires final .45 native candidate')
class LinkedFailureProof(unittest.TestCase):
    def setUp(self):
        root = Path(os.environ['X4_SELECTED_DIO_NATIVE'])
        self.record = json.loads((root / 'x4-native-composition.json').read_text())
        self.blobs = {p.name: p.read_bytes() for p in root.iterdir() if p.is_file()}

    def test_strong_marker_is_required(self):
        proof = native.runtime_options_proof(self.blobs, self.record)
        self.assertEqual(proof['failure_evidence'], {'enabled': True, 'abi': 1, 'marker': 'risc_native_failure_evidence_abi'})
        raw = bytearray(self.blobs['firmware.elf'])
        elf = ELFFile(io.BytesIO(raw))
        symbol = elf.get_section_by_name('.symtab').get_symbol_by_name('risc_native_failure_evidence_abi')[0]
        section = elf.get_section(symbol['st_shndx'])
        offset = section['sh_offset'] + symbol['st_value'] - section['sh_addr']
        raw[offset:offset+4] = b'\0' * 4
        with self.assertRaisesRegex(ValueError, 'strong failure-evidence'):
            native.runtime_options_proof(dict(self.blobs, **{'firmware.elf': bytes(raw)}), self.record)

    def test_audit_requires_actual_stack_report(self):
        blobs = dict(self.blobs)
        del blobs['NativeFailureEvidence.cpp.su']
        with self.assertRaisesRegex(ValueError, 'stack-usage'):
            native.failure_evidence_proof(blobs, self.record, Path(os.environ['X4_SELECTED_RUNTIME']))


if __name__ == '__main__':
    unittest.main()
