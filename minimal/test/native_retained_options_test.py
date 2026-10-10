#!/usr/bin/env python3
"""Compiled payload marker, actual RTC layout and tampering regression."""
import io
import os
from pathlib import Path
import unittest
from elftools.elf.elffile import ELFFile
from native_composition_test import composition


@unittest.skipUnless(os.environ.get('X4_RETAINED_TARGET_ELF'), 'Set a qualified 512-byte Xtensa target')
class RetainedTarget(unittest.TestCase):
    def setUp(self):
        self.raw = Path(os.environ['X4_RETAINED_TARGET_ELF']).read_bytes()

    def proof(self, raw=None, options=None):
        elf = ELFFile(io.BytesIO(self.raw if raw is None else raw))
        symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
        return composition.retained_wake_proof(elf, symbols, {'retained_wake_bytes': 512} if options is None else options)

    def test_actual_compiled_rtc_budget(self):
        proof = self.proof()
        self.assertEqual(proof['payload_bytes'], 512)
        self.assertEqual(proof['rtc_object_bytes'], 1212)
        for bank in proof['memory'].values():
            self.assertLessEqual(bank['used_span_bytes'], bank['capacity_bytes'])
        self.assertGreater(proof['memory']['slow']['used_span_bytes'], 1212)

    def test_extended_native_cannot_be_relabelled_default(self):
        with self.assertRaisesRegex(ValueError, 'Unexpected extended'):
            self.proof(options={})

    def test_mutated_constant_refused(self):
        elf = ELFFile(io.BytesIO(self.raw))
        marker = elf.get_section_by_name('.symtab').get_symbol_by_name('risc_retained_wake_payload_max')[0]
        section = elf.get_section(marker['st_shndx'])
        offset = section['sh_offset'] + marker['st_value'] - section['sh_addr']
        raw = bytearray(self.raw)
        raw[offset:offset + 4] = (128).to_bytes(4, 'little')
        with self.assertRaisesRegex(ValueError, 'payload marker'):
            self.proof(raw)

    def test_rtc_object_size_and_bank_overflow_refused(self):
        elf = ELFFile(io.BytesIO(self.raw))
        table = elf.get_section_by_name('.symtab')
        symbols = {s.name: s for s in table.iter_symbols()}
        image = symbols['_ZN7RiscCpu18NativeRetainedWake12_GLOBAL__N_15imageE']
        image.entry['st_size'] = 828
        with self.assertRaisesRegex(ValueError, 'object size'):
            composition.retained_wake_proof(elf, symbols, {'retained_wake_bytes': 512})
        # Alter the actual ELF32 section header size, preserving its object.
        raw = bytearray(self.raw)
        offset = elf['e_shoff'] + image['st_shndx'] * elf['e_shentsize'] + 20
        raw[offset:offset + 4] = (8193).to_bytes(4, 'little')
        with self.assertRaisesRegex(ValueError, 'memory exceeds'):
            self.proof(raw)


if __name__ == '__main__':
    unittest.main()
