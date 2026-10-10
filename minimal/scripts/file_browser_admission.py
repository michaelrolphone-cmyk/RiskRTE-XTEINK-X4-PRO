"""Tie the selected X4 Files ELF configuration to its real SD boot grant."""
import hashlib

SELECTION = {'primary': {'capability': 'storage.volume', 'api': 1, 'instance_id': 9},
             'secondary': None}
GRANTS = [{'capability': 'storage.volume', 'api': 1, 'instance_id': 9},
          {'capability': 'file.open', 'api': 1, 'instance_id': 0}]
SELECTORS = {'-DPORTABLE_FILE_BROWSER_CAPABILITY': '"storage.volume"',
             '-DPORTABLE_FILE_BROWSER_INSTANCE': '9u',
             '-DPORTABLE_FILE_BROWSER_HANDLERS': None}


def validate(manifest, blob, record, receipt, grants, source):
    """Manifest admission alone cannot establish a compiled acquire instance."""
    expected = {'repository_commit': source, 'working_tree_dirty': False,
                'version': manifest['version'], 'sha256': hashlib.sha256(blob).hexdigest(),
                'size_bytes': len(blob), 'storage_selection': SELECTION,
                'requested_capabilities': manifest['requires']}
    if any(record.get(key) != value for key, value in expected.items()):
        raise ValueError('Files source/ELF/storage receipt mismatch')
    if (receipt.get('storage_selection') != SELECTION or
            receipt.get('source_revision') != source or
            receipt.get('elf_sha256') != expected['sha256'] or
            receipt.get('elf_bytes') != len(blob) or
            receipt.get('requires') != manifest['requires']):
        raise ValueError('Files native storage admission mismatch')
    flags = record.get('defines')
    if not isinstance(flags, list) or not all(isinstance(flag, str) for flag in flags):
        raise ValueError('Files compiled selectors missing')
    for name, value in SELECTORS.items():
        expected_flag = name if value is None else name + '=' + value
        if [flag for flag in flags if flag.split('=', 1)[0] == name] != [expected_flag]:
            raise ValueError('Files compiled selector differs from SD deployment: ' + name)
    if any(flag.startswith('-DPORTABLE_FILE_BROWSER_SECONDARY_INSTANCE') for flag in flags):
        raise ValueError('Files secondary volume is not selected by this deployment')
    selected = lambda rows: [row for row in rows if row.get('capability') in
                            ('storage.volume', 'storage.installed-files', 'file.open')]
    requirements = [{'capability': grant['capability'], 'api': grant['api']} for grant in GRANTS]
    order = lambda rows: sorted(rows, key=lambda row: row['capability'])
    if (order(selected(manifest['requires'])) != order(requirements) or
            order(selected(grants)) != order(GRANTS) or
            order(selected(record.get('required_grants', []))) != order(GRANTS)):
        raise ValueError('Files compiled acquisition and boot authority disagree')
    return {'storage_selection': SELECTION, 'grants': GRANTS,
            'file_open': 'declared installed receiver required; no implicit handlers'}
