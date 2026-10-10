"""Product admission rejects a real unselected QIO candidate before packaging."""
import json
import os
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import resident_native


class ResidentNativeTest(unittest.TestCase):
    def test_selected_recipe_preserves_accepted_boot(self):
        spec = resident_native.selection()
        self.assertEqual(spec['boot_flash_selection'], 'dio-opi-80mhz')
        self.assertEqual(spec['preserved_flash']['memory_type'], 'dio_opi')
        self.assertEqual(spec['preserved_flash']['preserved_sdk_settings']['CONFIG_SPIRAM_MODE_OCT'], '1')

    @unittest.skipUnless(os.environ.get('X4_UNSELECTED_QIO_NATIVE'), 'requires actual QIO candidate')
    def test_rejects_real_default_qio_candidate(self):
        folder = Path(os.environ['X4_UNSELECTED_QIO_NATIVE'])
        candidate = json.loads((folder / 'candidate.json').read_text())
        with self.assertRaisesRegex(ValueError, 'accepted DIO'):
            resident_native.validate(folder, candidate, folder)

    @unittest.skipUnless(os.environ.get('X4_SELECTED_DIO_NATIVE'), 'requires actual selected DIO candidate')
    def test_selected_real_candidate_and_flash_tampering(self):
        import copy
        folder = Path(os.environ['X4_SELECTED_DIO_NATIVE'])
        runtime = Path(os.environ['X4_SELECTED_RUNTIME'])
        candidate = json.loads((folder / 'candidate.json').read_text())
        platform = Path(os.environ['X4_SELECTED_PLATFORM'])
        resident_native.validate(folder, candidate, runtime, native_source_root=platform)
        for key, value in [('memory_type', 'qio_opi'), ('frequency_hz', 40000000)]:
            bad = copy.deepcopy(candidate)
            bad['x4_native_composition']['boot_flash_proof'][key] = value
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'flash/PSRAM'):
                resident_native.validate(folder, bad, runtime, native_source_root=platform)


if __name__ == '__main__':
    unittest.main()
