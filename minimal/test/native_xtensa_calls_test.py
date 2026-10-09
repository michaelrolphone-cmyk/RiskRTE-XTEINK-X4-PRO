#!/usr/bin/env python3
"""Pinned-disassembler boundaries and the actual product 0.1.21 ELF regression."""
import io
import json
import os
from pathlib import Path
import unittest
from unittest.mock import patch

from native_composition_test import composition as c


class Section(dict):
    def __init__(self, address, data):
        super().__init__(sh_addr=address, sh_size=len(data), sh_type='SHT_PROGBITS')
        self.contents = data

    def data(self):
        return self.contents


class FixtureElf:
    def __init__(self, address, code, literal, target):
        self.stream = io.BytesIO(b'fixture')
        self.sections = [Section(address, code), Section(literal, target.to_bytes(4, 'little'))]

    def get_section(self, index):
        return self.sections[index]

    def iter_sections(self):
        return iter(self.sections)


class XtensaCallsTest(unittest.TestCase):
    def calls(self, address, code, literal, disassembly):
        elf = FixtureElf(address, code, literal, 0x42003894)
        symbol = {'st_value': address, 'st_size': len(code), 'st_shndx': 0}
        output = 'firmware.elf: file format elf32-xtensa-le\n' + disassembly
        with patch.object(c, 'xtensa_objdump', return_value='/pinned/objdump'), \
                patch.object(c.subprocess, 'check_output', return_value=output):
            return c.xtensa_calls(elf, {'caller': symbol}, 'caller')

    def test_actual_long_negative_literal_offset(self):
        # Product ELF 99e6411f: main_task+0x17. The former signed16 decoder
        # incorrectly used 0x420f85e4, 256 KiB above the actual literal.
        calls = self.calls(0x420de2e3, bytes.fromhex('81c068e00800'), 0x420b85e4,
                          '420de2e3: 68c081 l32r a8, 420b85e4\n'
                          '420de2e6: 0008e0 callx8 a8\n')
        self.assertEqual(calls, [{'instruction': 0x420de2e3, 'literal': 0x420b85e4, 'target': 0x42003894}])

    def test_embedded_call_opcode_is_not_an_instruction(self):
        # Actual __wrap_app_main L32R contains 0xe5 at byte offset one. The
        # old scan invented a CALL8 there, inside this three-byte instruction.
        calls = self.calls(0x42003937, bytes.fromhex('81e5f1e00800'), 0x420000cc,
                          '42003937: f1e581 l32r a8, 420000cc\n'
                          '4200393a: 0008e0 callx8 a8\n')
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0]['instruction'], 0x42003937)

    def test_real_unmapped_literal_is_still_rejected(self):
        with self.assertRaisesRegex(ValueError, 'outside a loaded section'):
            self.calls(0x420de2e3, bytes.fromhex('81c068e00800'), 0x420f85e4,
                       '420de2e3: 68c081 l32r a8, 420b85e4\n'
                       '420de2e6: 0008e0 callx8 a8\n')

    def test_disassembly_must_match_all_bytes_and_literal_semantics(self):
        prefix = '420de2e3: 68c081 l32r a8, 420b85e4\n'
        invalid = (prefix, prefix + '420de2e7: 0008e0 callx8 a8\n',
                   prefix + '420de2e6: 0009e0 callx8 a9\n',
                   prefix.replace('420b85e4', '420f85e4') + '420de2e6: 0008e0 callx8 a8\n')
        for output in invalid:
            with self.subTest(output=output), self.assertRaises(ValueError):
                self.calls(0x420de2e3, bytes.fromhex('81c068e00800'), 0x420b85e4, output)

    def test_unpinned_disassembler_is_rejected(self):
        with patch.object(c.subprocess, 'check_output', return_value='GNU objdump unreviewed\n'), \
                self.assertRaisesRegex(ValueError, 'pinned esp-2021r2-patch5'):
            c.verified_xtensa_objdump('/test/unreviewed-objdump')


@unittest.skipUnless(os.environ.get('X4_XTENSA_REGRESSION_WORKSPACE'), 'Set the frozen product 0.1.21 workspace')
class ActualStageElfRegressionTest(unittest.TestCase):
    def test_frozen_final_elf_has_exact_edges_and_rejects_unmapped_literal(self):
        from elftools.elf.elffile import ELFFile
        workspace = Path(os.environ['X4_XTENSA_REGRESSION_WORKSPACE'])
        record = c.verify_composition(workspace)
        folder = workspace / '.pio/build' / record['build_environment']
        blobs = {name: (folder / name).read_bytes() for name in ('firmware.elf', 'firmware.bin')}
        data = blobs['firmware.elf']
        self.assertEqual(c.sha(data), '99e6411f7a51b1881eb5e37f141333a3f8ed0f164a6b7f40efab8367e3a10746')
        proof = c.startup_proof(data, record)
        self.assertEqual(proof['target_call_edges']['main_task -> __wrap_app_main'],
                         {'instruction': 0x420de350, 'literal': 0x420b8600, 'target': 0x42003894})
        options = c.runtime_options_proof(blobs, record)
        self.assertEqual(len(options['app_image_cache']['target_call_edges']), 3)
        elf = ELFFile(io.BytesIO(data))
        symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
        for edge_name, edge in options['app_image_cache']['target_call_edges'].items():
            for section in elf.iter_sections():
                if section['sh_addr'] <= edge['instruction'] < section['sh_addr'] + section['sh_size']:
                    offset = section['sh_offset'] + edge['instruction'] - section['sh_addr']
                    break
            else:
                self.fail('Cache call instruction is absent')
            mutated = bytearray(data)
            mutated[offset:offset + 3] = bytes(3)
            with self.subTest(edge=edge_name), self.assertRaisesRegex(ValueError, 'Unproven app image cache call'):
                c.runtime_options_proof(dict(blobs, **{'firmware.elf': bytes(mutated)}), record)
        calls = c.xtensa_calls(elf, symbols, '__wrap_app_main')
        self.assertNotIn(0x42003938, {call['instruction'] for call in calls})
        # Mutate a real L32R at the actual instruction boundary. This remains
        # fatal; successful recovery must never discard unresolved real calls.
        for section in elf.iter_sections():
            if section['sh_addr'] <= 0x42003937 < section['sh_addr'] + section['sh_size']:
                offset = section['sh_offset'] + 0x42003937 - section['sh_addr']
                break
        else:
            self.fail('Regression L32R is absent')
        mutated = bytearray(data)
        mutated[offset + 1:offset + 3] = bytes(2)
        with self.assertRaisesRegex(ValueError, 'outside a loaded section'):
            c.startup_proof(bytes(mutated), record)


if __name__ == '__main__':
    unittest.main()
