#!/usr/bin/env python3
"""Compose the exact bridge-to-Contexts paired route; no feed/device operation."""
import argparse
import copy
import json
from pathlib import Path
import sys

from build_test_bundle import encoded, sha, validate_native_composition

ROOT = Path(__file__).resolve().parents[2]
BRIDGE_SOURCE = 'def7a7f8b0a1e4f3756f430273708056c76fb9e1'
PRODUCT = 'xteink-x4-pro'
REPOSITORY = 'michaelrolphone-cmyk/RiskRTE-XTEINK-X4-PRO'
LAYOUT = 'riscrte-paired-appdata-v2'


def require(ok, reason):
    if not ok:
        raise ValueError(reason)


def files_at(folder):
    folder = Path(folder)
    result = {}
    for path in sorted(folder.rglob('*')):
        require(not path.is_symlink(), 'store symlink refused')
        if path.is_file():
            result[path.relative_to(folder).as_posix()] = path.read_bytes()
    return result


def identity(files):
    return json.loads(files['cohort.json'])


def migration_variant(previous, following):
    before, after = identity(previous), identity(following)
    require(all(c.get('product') == PRODUCT and c.get('source_repo') == REPOSITORY and
                c.get('layout') == LAYOUT and c.get('store_abi') == 2 for c in (before, after)),
            'route product/layout/ABI mismatch')
    require(before.get('version') == '0.1.25' and before.get('source_revision') == BRIDGE_SOURCE and
            before.get('runtime_version') == '0.1.74' and after.get('version') == '0.1.26' and
            after.get('runtime_version') == '0.1.75', 'route source/version mismatch')
    require(previous['board.json'] == following['board.json'], 'route board changed')
    old_boot, boot = json.loads(previous['boot.json']), json.loads(following['boot.json'])
    require('cohort_migration' not in old_boot and 'cohort_migration' not in boot,
            'existing migration cannot be silently replaced')
    old_ids = {json.loads(previous[p['manifest']])['id'] for p in old_boot['app_capabilities']}
    require('contexts' not in old_ids, 'route already has Contexts')
    owners = [p for p in boot['app_capabilities'] if json.loads(following[p['manifest']])['id'] == 'contexts']
    require(len(owners) == 1, 'exact new Contexts owner required')
    persistent = [g for g in owners[0]['grants'] if g['capability'] in ('storage.key-value', 'storage.app-data')]
    require(persistent == [{'capability': 'storage.key-value', 'api': 1, 'instance_id': 1}],
            'Contexts private namespace expansion refused')
    boot['cohort_migration'] = {
        'schema': 1,
        'from': {k: before[k] for k in ('product', 'version', 'source_revision')},
        'to': {k: after[k] for k in ('product', 'version')},
        'shared_key_value': [{'application_id': 'contexts', 'api': 1, 'namespace': 1}],
    }
    result = dict(following)
    result['boot.json'] = encoded(boot)
    require(all(result[n] == raw for n, raw in following.items() if n != 'boot.json'),
            'route changed another target file')
    return result


def catalog_route(original, before, after, store_hash, firmware, store_image, payload, asset):
    require(len(firmware) == after['firmware_size'] and sha(firmware) == after['firmware_sha256'] and
            len(store_image) == 0x510000 and payload == firmware + store_image,
            'route payload/native bytes mismatch')
    row = copy.deepcopy(original['firmware'])
    require(original.get('schema') == 1 and original.get('product') == PRODUCT and
            original.get('source_repo') == REPOSITORY and row and row.get('version') == '0.1.26' and
            all(row['ota'].get(k) == after[k] for k in
                ('product', 'version', 'source_repo', 'source_revision', 'runtime_version',
                 'layout', 'store_abi', 'firmware_size', 'firmware_sha256')),
            'wrong target firmware catalog')
    ota = row['ota']
    ota.update(asset=asset, url=ota['url'].rsplit('/', 1)[0] + '/' + asset,
               size=len(payload), sha256=sha(payload), firmware_size=len(firmware),
               firmware_sha256=sha(firmware), store_size=len(store_image), store_sha256=sha(store_image))
    selector = {k: before[k] for k in ('product', 'version', 'source_repo', 'source_revision',
                                      'runtime_version', 'layout', 'store_abi')}
    selector['active_store_sha256'] = store_hash
    return {'schema': 1, 'product': PRODUCT, 'source_repo': REPOSITORY,
            'firmware': None, 'firmware_routes': [{'from': selector, 'firmware': row}], 'apps': []}


def build(args):
    require(not args.output.exists() and args.output.parent.is_dir(), 'new output directory required')
    for folder in (args.source, args.source_native, args.source_platform, args.target, args.native,
                   args.source_runtime, args.runtime, args.tools):
        require(not args.output.resolve().is_relative_to(folder.resolve()), 'output overlaps input')
    native = json.loads((args.native / 'candidate.json').read_bytes())
    validate_native_composition(args.native, native, args.runtime, ROOT)
    source_native = json.loads((args.source_native / 'candidate.json').read_bytes())
    validate_native_composition(args.source_native, source_native, args.source_runtime, args.source_platform)
    bridge = json.loads((args.source / 'bridge.json').read_bytes())
    require(bridge['source'] == BRIDGE_SOURCE and bridge['target_cohort']['source_revision'] == BRIDGE_SOURCE,
            'wrong bridge source custody')
    require(source_native['source_sha'] == bridge['native_composition']['runtime']['commit'] and
            source_native['x4_native_composition']['platform']['commit'] == BRIDGE_SOURCE,
            'bridge native source mismatch')
    sys.path.insert(0, str(args.tools.resolve() / 'scripts'))
    from read_only_spiffs import read_image
    from current_bootfs import build as pack
    from check_runtime_store_admission import admit_cohort, store_digest
    source_image = (args.source / 'bootfs.bin').read_bytes()
    previous = files_at(args.source / 'store')
    require(bridge['store'] == {'bytes': len(source_image), 'sha256': sha(source_image)} and
            read_image(source_image, 0x510000) == previous and
            store_digest(previous) == bridge['target_store_inventory_sha256'] and
            identity(previous) == bridge['target_cohort'], 'bridge store mismatch')
    require(source_native['assets']['firmware.bin'] ==
            {'bytes': identity(previous)['firmware_size'], 'sha256': identity(previous)['firmware_sha256']},
            'bridge firmware/cohort mismatch')
    following = files_at(args.target / 'store')
    custody = json.loads((args.target / 'build-custody.json').read_bytes())
    require(custody['runtime'] == native and custody['x4_source'] == identity(following)['source_revision'] and
            custody['x4_source'] == native['x4_native_composition']['platform']['commit'] and
            custody['store_files'] == {n: {'bytes': len(raw), 'sha256': sha(raw)} for n, raw in following.items()},
            'target store/native custody mismatch')
    require(custody['extended_checks_skipped'] is False and custody['store_admission']['cohort_validated'],
            'target admission receipt missing')
    migrated = migration_variant(previous, following)
    old_elf = (args.source_native / 'firmware.elf').read_bytes()
    require(sha(old_elf) == bridge['target_runtime_admission']['native_elf_sha256'], 'bridge ELF mismatch')
    installed = admit_cohort(args.source_runtime, old_elf, previous, migrated, app_policy_rows=17)
    receiving = admit_cohort(args.runtime, (args.native / 'firmware.elf').read_bytes(),
                             migrated, migrated, app_policy_rows=17)
    image, packing = pack(migrated)
    require(read_image(image, 0x510000) == migrated, 'route image readback mismatch')
    firmware = (args.native / 'firmware.bin').read_bytes()
    require(identity(migrated)['firmware_sha256'] == sha(firmware), 'route firmware identity mismatch')
    payload = firmware + image
    asset = 'x4-cohort-0.1.26-from-0.1.25-' + BRIDGE_SOURCE[:8] + '.bin'
    catalog = catalog_route(json.loads((args.target / 'updates/release-index.local.json').read_bytes()),
                            identity(previous), identity(migrated), sha(source_image), firmware, image, payload, asset)
    receipt = {'schema': 1, 'kind': 'x4.source-bound-contexts-route',
               'source_cohort': identity(previous), 'target_cohort': identity(migrated),
               'active_store_sha256': sha(source_image), 'payload': {'asset': asset, 'bytes': len(payload), 'sha256': sha(payload)},
               'migration': json.loads(migrated['boot.json'])['cohort_migration'],
               'changed_target_files': ['boot.json'], 'installed_runtime_admission': installed,
               'target_runtime_admission': receiving, 'store_generator': packing,
               'store_files': {n: {'bytes': len(raw), 'sha256': sha(raw)} for n, raw in migrated.items()},
               'transaction_qualified': False, 'publication': 'none', 'feed_configured': False,
               'hardware_qualified': False}
    args.output.mkdir()
    for name, raw in migrated.items():
        path = args.output / 'store' / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)
    (args.output / asset).write_bytes(payload)
    (args.output / 'firmware.bin').write_bytes(firmware)
    (args.output / 'bootfs.bin').write_bytes(image)
    (args.output / 'route.json').write_bytes(encoded(receipt))
    (args.output / 'release-index.local.json').write_bytes(encoded(catalog))
    require((args.output / asset).read_bytes() == payload, 'route payload write mismatch')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'source-native', 'source-platform', 'source-runtime', 'target',
                 'native', 'runtime', 'tools', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    receipt = build(parser.parse_args())
    print(json.dumps({'payload': receipt['payload'], 'transaction_qualified': False, 'publication': 'none'}))
