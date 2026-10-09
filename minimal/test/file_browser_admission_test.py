"""Reject compiled Files selectors which manifest-only admission cannot catch."""
import copy
import hashlib
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import file_browser_admission as files
from build_test_bundle import app_grants


class FilesAdmission(unittest.TestCase):
    def setUp(self):
        self.blob = b'host fixture ELF identity'
        self.source = 'a' * 40
        self.manifest = {'version': '1.5.8', 'requires': [
            {'capability': capability, 'api': 1} for capability in
            ('storage.volume', 'file.open', 'display.output', 'storage.key-value')]}
        self.grants = app_grants('file_browser', self.manifest['requires'], True, True, True, True)
        digest = hashlib.sha256(self.blob).hexdigest()
        self.record = {'repository_commit': self.source, 'working_tree_dirty': False,
                       'version': '1.5.8', 'sha256': digest, 'size_bytes': len(self.blob),
                       'requested_capabilities': copy.deepcopy(self.manifest['requires']),
                       'storage_selection': copy.deepcopy(files.SELECTION),
                       'required_grants': copy.deepcopy(self.grants),
                       'defines': [name if value is None else name + '=' + value
                                   for name, value in files.SELECTORS.items()]}
        self.receipt = {'source_revision': self.source, 'elf_sha256': digest,
                        'elf_bytes': len(self.blob), 'requires': copy.deepcopy(self.manifest['requires']),
                        'storage_selection': copy.deepcopy(files.SELECTION)}

    def check(self):
        return files.validate(self.manifest, self.blob, self.record, self.receipt,
                              self.grants, self.source)

    def test_real_deployment_grants(self):
        self.assertEqual(self.check()['grants'], files.GRANTS)

    def test_old_installed_files_instance_nine_is_rejected(self):
        self.record['defines'][0] = '-DPORTABLE_FILE_BROWSER_CAPABILITY="storage.installed-files"'
        with self.assertRaisesRegex(ValueError, 'compiled selector'):
            self.check()

    def test_wrong_zero_or_duplicate_compiled_instance(self):
        original = list(self.record['defines'])
        for flags in [original[:1] + ['-DPORTABLE_FILE_BROWSER_INSTANCE=0u'] + original[2:],
                      original + ['-DPORTABLE_FILE_BROWSER_INSTANCE=9u'],
                      original + ['-DPORTABLE_FILE_BROWSER_SECONDARY_INSTANCE=10u']]:
            self.record['defines'] = flags
            with self.subTest(flags=flags), self.assertRaises(ValueError):
                self.check()

    def test_each_authority_surface_is_required(self):
        for surface in ('manifest', 'record', 'receipt', 'grants'):
            self.setUp()
            if surface == 'manifest':
                self.manifest['requires'][0]['capability'] = 'storage.installed-files'
                self.record['requested_capabilities'] = copy.deepcopy(self.manifest['requires'])
                self.receipt['requires'] = copy.deepcopy(self.manifest['requires'])
            elif surface == 'grants':
                self.grants[0]['instance_id'] = 0
            elif surface == 'record':
                self.record['required_grants'][0]['instance_id'] = 0
            else:
                self.receipt['storage_selection']['primary']['instance_id'] = 0
            with self.subTest(surface=surface), self.assertRaises(ValueError):
                self.check()

    def test_no_implicit_handler_or_secondary_volume(self):
        for change in ('handler', 'secondary'):
            self.setUp()
            if change == 'handler':
                self.record['defines'].remove('-DPORTABLE_FILE_BROWSER_HANDLERS')
            else:
                self.grants.append({'capability': 'storage.volume', 'api': 1, 'instance_id': 10})
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.check()

    def test_exact_artifact_custody(self):
        for key, value in [('repository_commit', 'b'*40), ('working_tree_dirty', True),
                           ('sha256', 'f'*64), ('size_bytes', 0), ('version', '1.5.7')]:
            self.setUp()
            self.record[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check()

    def test_non_selected_installed_file_policy_stays_zero(self):
        requirements = [{'capability': 'storage.installed-files', 'api': 1}]
        self.assertEqual(app_grants('file_browser', requirements),
                         [dict(requirements[0], instance_id=0)])


if __name__ == '__main__':
    unittest.main()
