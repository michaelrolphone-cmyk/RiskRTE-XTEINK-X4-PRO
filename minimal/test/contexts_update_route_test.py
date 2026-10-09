import copy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import build_contexts_update_route as route


class ContextsUpdateRouteTest(unittest.TestCase):
    def setUp(self):
        self.firmware = b'F' * 64
        self.image = bytes(0x510000)
        self.before = {'product': route.PRODUCT, 'source_repo': route.REPOSITORY,
                       'version': '0.1.25', 'runtime_version': '0.1.74',
                       'source_revision': route.BRIDGE_SOURCE, 'layout': route.LAYOUT,
                       'store_abi': 2, 'firmware_size': 64, 'firmware_sha256': route.sha(self.firmware)}
        self.after = dict(self.before, version='0.1.26', runtime_version='0.1.75', source_revision='b' * 40)
        boot = {'app_capabilities': [{'manifest': 'default.json', 'grants': [
            {'capability': 'storage.key-value', 'api': 1, 'instance_id': 1}]}]}
        self.old = {'cohort.json': route.encoded(self.before), 'board.json': b'board',
                    'default.json': route.encoded({'id': 'paper_clock'}),
                    'default.elf': b'old app', 'boot.json': route.encoded(boot)}
        boot['app_capabilities'].append({'manifest': 'contexts.json', 'grants': [
            {'capability': 'storage.key-value', 'api': 1, 'instance_id': 1}]})
        self.new = {**self.old, 'cohort.json': route.encoded(self.after),
                    'boot.json': route.encoded(boot), 'default.elf': b'new app',
                    'contexts.json': route.encoded({'id': 'contexts'}), 'contexts.elf': b'contexts app'}
        ota = {**self.after, 'asset': 'full-pair.bin', 'url': 'https://example.invalid/full-pair.bin'}
        self.catalog = {'schema': 1, 'product': route.PRODUCT, 'source_repo': route.REPOSITORY,
                        'firmware': {'version': '0.1.26', 'ota': ota}, 'apps': []}

    def test_exact_migration_changes_only_target_boot(self):
        old, new = copy.deepcopy(self.old), copy.deepcopy(self.new)
        result = route.migration_variant(old, new)
        self.assertEqual(old, self.old)
        self.assertEqual(new, self.new)
        self.assertEqual([n for n in result if result[n] != new[n]], ['boot.json'])
        migration = json.loads(result['boot.json'])['cohort_migration']
        self.assertEqual(migration['from']['source_revision'], route.BRIDGE_SOURCE)
        self.assertEqual(migration['shared_key_value'], [{'application_id': 'contexts', 'api': 1, 'namespace': 1}])

    def test_source_and_layout_cannot_be_generalized(self):
        for field, value in [('source_revision', 'c' * 40), ('version', '0.1.24'),
                             ('runtime_version', '0.1.65'), ('product', 'other'),
                             ('source_repo', 'other/repo'), ('layout', 'other'), ('store_abi', 1)]:
            previous = dict(self.old, **{'cohort.json': route.encoded(dict(self.before, **{field: value}))})
            with self.subTest(field=field), self.assertRaises(ValueError):
                route.migration_variant(previous, self.new)

    def test_board_and_existing_migrations_refused(self):
        following = dict(self.new, **{'board.json': b'different'})
        with self.assertRaisesRegex(ValueError, 'board'):
            route.migration_variant(self.old, following)
        for which in ('old', 'new'):
            old, new = dict(self.old), dict(self.new)
            selected = old if which == 'old' else new
            boot = json.loads(selected['boot.json']);boot['cohort_migration'] = {}
            selected['boot.json'] = route.encoded(boot)
            with self.subTest(which=which), self.assertRaisesRegex(ValueError, 'existing migration'):
                route.migration_variant(old, new)

    def test_private_namespace_and_duplicate_owner_refused(self):
        for grants in [[], [{'capability': 'storage.key-value', 'api': 1, 'instance_id': 3}],
                       [{'capability': 'storage.app-data', 'api': 1, 'instance_id': 1}],
                       [{'capability': 'storage.key-value', 'api': 1, 'instance_id': 1},
                        {'capability': 'storage.key-value', 'api': 1, 'instance_id': 2}]]:
            following = dict(self.new);boot = json.loads(following['boot.json'])
            boot['app_capabilities'][-1]['grants'] = grants;following['boot.json'] = route.encoded(boot)
            with self.subTest(grants=grants), self.assertRaisesRegex(ValueError, 'private namespace'):
                route.migration_variant(self.old, following)
        following = dict(self.new);boot = json.loads(following['boot.json'])
        boot['app_capabilities'].append(boot['app_capabilities'][-1]);following['boot.json'] = route.encoded(boot)
        with self.assertRaisesRegex(ValueError, 'exact new Contexts'):
            route.migration_variant(self.old, following)

    def test_catalog_binds_source_and_bytes_without_legacy_offer(self):
        original = copy.deepcopy(self.catalog);payload = self.firmware + self.image
        result = route.catalog_route(original, self.before, self.after, 'c' * 64,
                                     self.firmware, self.image, payload, 'source-specific.bin')
        self.assertEqual(original, self.catalog)
        self.assertIsNone(result['firmware'])
        self.assertEqual(result['apps'], [])
        selected = result['firmware_routes'][0]
        self.assertEqual(selected['from']['active_store_sha256'], 'c' * 64)
        self.assertEqual(selected['from']['source_revision'], route.BRIDGE_SOURCE)
        self.assertEqual(selected['firmware']['ota']['sha256'], route.sha(payload))
        self.assertEqual(selected['firmware']['ota']['url'], 'https://example.invalid/source-specific.bin')

    def test_catalog_cannot_relabel_target_native_or_source(self):
        for field, value in [('source_revision', 'c' * 40), ('firmware_sha256', '0' * 64),
                             ('runtime_version', '0.1.74'), ('layout', 'other')]:
            catalog = copy.deepcopy(self.catalog);catalog['firmware']['ota'][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'target firmware'):
                route.catalog_route(catalog, self.before, self.after, 'c' * 64,
                                    self.firmware, self.image, self.firmware + self.image, 'pair.bin')
        with self.assertRaisesRegex(ValueError, 'payload/native'):
            route.catalog_route(self.catalog, self.before, self.after, 'c' * 64,
                                self.firmware + b'x', self.image, self.firmware + self.image, 'pair.bin')


if __name__ == '__main__':
    unittest.main()
