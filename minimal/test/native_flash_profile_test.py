#!/usr/bin/env python3
"""Exercise selected DIO source custody, SDK inputs and frozen real target proof."""
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('flash_profile', ROOT / 'minimal/native/flash_profile.py')
flash = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flash)


class FlashTest(unittest.TestCase):
    def test_default_is_unchanged_and_unknown_selection_rejected(self):
        self.assertFalse(flash.selected({}))
        self.assertIsNone(flash.prove({}, {}))
        for value in (None, True, 'qio', 'dio', ''):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'Invalid recorded'):
                flash.selected({'boot_flash_experiment': value})

    def test_config_does_not_silently_replace_existing_override(self):
        original = b'[env:test]\nboard_build.flash_mode = qio\nextra_scripts = pre:scripts/reproducible_build.py\n'
        with self.assertRaisesRegex(ValueError, 'already overrides'):
            flash.compose_config(original, 'test', {'boot_flash_experiment': flash.DIO})

    def test_build_checks_actual_board_and_sdk_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            config = root / 'tools/sdk/esp32s3/dio_opi/include/sdkconfig.h'
            config.parent.mkdir(parents=True)
            config.write_text(''.join('#define ' + k + ' ' + v + '\n' for k, v in flash.PRESERVED_SETTINGS.items()))
            board = {'build.arduino.memory_type': 'dio_opi', 'build.flash_mode': 'dio',
                     'build.f_flash': '80000000L', 'build.f_cpu': '240000000L',
                     'build.flash_size': '16MB', 'upload.flash_size': '16MB'}
            class Platform:
                def get_package_dir(self, name): return root
            class Env:
                def BoardConfig(self): return board
                def PioPlatform(self): return Platform()
            pins = {str(config.relative_to(root)): flash.hashlib.sha256(config.read_bytes()).hexdigest()}
            record = {'boot_flash_experiment': flash.DIO}
            with mock.patch.object(flash, 'DIO_SDK_SHA256', pins):
                flash.verify_build(Env(), record)
                for key in board:
                    previous = board[key]; board[key] = 'wrong'
                    with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'board selection differs'):
                        flash.verify_build(Env(), record)
                    board[key] = previous
                config.write_text(config.read_text() + '// changed\n')
                with self.assertRaisesRegex(ValueError, 'SDK source differs'):
                    flash.verify_build(Env(), record)
                pins[str(config.relative_to(root))] = flash.hashlib.sha256(config.read_bytes()).hexdigest()
                config.write_text(config.read_text().replace('#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1',
                                                            '#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 0'))
                pins[str(config.relative_to(root))] = flash.hashlib.sha256(config.read_bytes()).hexdigest()
                with self.assertRaisesRegex(ValueError, 'preserved SDK setting differs'):
                    flash.verify_build(Env(), record)

    @unittest.skipUnless(os.environ.get('X4_FROZEN_DIO_NATIVE'), 'Requires exact frozen .29 DIO artifacts')
    def test_real_dio_elf_and_image_modes_and_mutations(self):
        from elftools.elf.elffile import ELFFile
        root = Path(os.environ['X4_FROZEN_DIO_NATIVE'])
        blobs = {name: (root / name).read_bytes() for name in ('firmware.bin', 'firmware.elf', 'bootloader.bin')}
        record = json.loads((root / 'x4-native-composition.json').read_text())
        proof = flash.prove(blobs, record)
        self.assertEqual(proof['linked_flash_mode']['read_mode'], 3)
        for name in ('firmware.bin', 'bootloader.bin'):
            for offset in (2, 3):
                changed = bytearray(blobs[name]); changed[offset] ^= 1
                with self.subTest(name=name, offset=offset), self.assertRaises(ValueError):
                    flash.prove(dict(blobs, **{name: bytes(changed)}), record)
        elf = ELFFile(io.BytesIO(blobs['firmware.elf']))
        symbol = next(s for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name == 'default_chip')
        section = elf.get_section(symbol['st_shndx'])
        offset = section['sh_offset'] + symbol['st_value'] - section['sh_addr'] + 16
        changed = bytearray(blobs['firmware.elf']); changed[offset:offset+4] = (0).to_bytes(4, 'little')
        with self.assertRaisesRegex(ValueError, 'linked SPI flash read mode mismatch'):
            flash.prove(dict(blobs, **{'firmware.elf': bytes(changed)}), record)


if __name__ == '__main__':
    unittest.main()
