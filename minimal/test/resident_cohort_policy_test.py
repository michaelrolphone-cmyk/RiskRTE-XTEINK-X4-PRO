"""Exercise resident policy integration against the exact delivered application inventory."""
import copy
import json
import os
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import package_resident_cohort as product


@unittest.skipUnless(os.environ.get('X4_RESIDENT_POLICY_INPUTS'), 'requires qualified cohort inputs')
class CohortPolicyTest(unittest.TestCase):
    def setUp(self):
        inputs = json.loads(Path(os.environ['X4_RESIDENT_POLICY_INPUTS']).read_text())
        self.files = product.inventory(Path(inputs['baseline']) / 'store')
        system = json.loads(Path(inputs['system_receipt']).read_text())
        clients = json.loads(Path(inputs['client_receipt']).read_text())
        self.entries = {}
        for name, item in system['built'].items():
            self.files[name + '.json'] = Path(item['elf']).with_suffix('.json').read_bytes()
            self.entries[name] = {'grants': item['proposed_grants']}
        for name, item in clients['apps'].items():
            self.files[name + '.json'] = Path(item['elf']).with_suffix('.json').read_bytes()
            self.entries[name] = {'grants': item['required_grants']}
        self.binding = json.loads((product.ROOT / 'minimal/apps/points-catalog-bindings.json').read_text())

    def test_all_apps_and_existing_kv_authority_preserved(self):
        old = json.loads(self.files['boot.json'])
        result = product.apply_policy(self.files, self.entries, self.binding)
        self.assertEqual(len(result['resident_shell']['foreground']), 19)
        self.assertEqual(result['resident_shell']['legacy'], ['gameboy.elf'])
        before = next(d for d in old['drivers'] if d['manifest'] == 'alarm/manifest.json')
        after = next(d for d in result['drivers'] if d['manifest'] == 'alarm/manifest.json')
        self.assertTrue(all(row in after['key_value'] for row in before['key_value']))
        self.assertEqual(len(after['key_value']), 10)
        self.assertEqual(after['app_data'], self.binding['provider_storage']['app_data'])

    def test_conflicting_app_namespace_refused(self):
        boot = json.loads(self.files['boot.json'])
        boot['app_capabilities'][-1]['grants'].append({'capability':'storage.app-data','api':1,'instance_id':5})
        self.files['boot.json'] = product.encoded(boot)
        with self.assertRaisesRegex(ValueError, 'namespace already used'):
            product.apply_policy(self.files, self.entries, self.binding)

    def test_conflicting_provider_namespace_refused(self):
        boot = json.loads(self.files['boot.json'])
        boot['drivers'][0]['app_data'] = [{'name':'other.file','namespace':5,'access':'read'}]
        self.files['boot.json'] = product.encoded(boot)
        with self.assertRaisesRegex(ValueError, 'namespace already used'):
            product.apply_policy(self.files, self.entries, self.binding)

    def test_no_lost_application_or_duplicate_authority(self):
        entries = copy.deepcopy(self.entries); del entries['contexts']
        with self.assertRaisesRegex(ValueError, 'loses or substitutes'):
            product.apply_policy(self.files, entries, self.binding)
        entries = copy.deepcopy(self.entries)
        entries['default']['grants'].append(entries['default']['grants'][0])
        with self.assertRaises(ValueError): product.apply_policy(self.files, entries, self.binding)

    def test_existing_occurrence_access_cannot_be_narrowed(self):
        binding = copy.deepcopy(self.binding)
        next(r for r in binding['provider_storage']['key_value'] if r['key']=='points_utc_occ')['access'] = 'read'
        with self.assertRaisesRegex(ValueError, 'authority was removed or changed'):
            product.apply_policy(self.files, self.entries, binding)


if __name__ == '__main__': unittest.main()
