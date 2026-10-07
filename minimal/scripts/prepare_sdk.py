#!/usr/bin/env python3
"""Compose one canonical include directory; reject divergent shared headers."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def prepare(runtime, reader, output):
    output = Path(output)
    if output.exists():
        raise ValueError('SDK output must be new')
    headers = {}
    for label, folder in (('runtime/driver', Path(runtime)/'sdk/driver'),
                          ('runtime/hardware', Path(runtime)/'sdk/hardware'),
                          ('reader/driver', Path(reader)/'sdk/driver')):
        if not folder.is_dir():
            raise ValueError('Missing SDK input: ' + label)
        for path in sorted(folder.glob('*.h')):
            if path.is_symlink() or not path.is_file():
                raise ValueError('Expected regular SDK header: ' + path.name)
            data = path.read_bytes()
            if path.name in headers:
                if headers[path.name]['data'] != data:
                    raise ValueError('Shared SDK header differs: ' + path.name)
                headers[path.name]['origins'].append(label)
            else:
                headers[path.name] = {'data': data, 'origins': [label]}
    output.mkdir(parents=True)
    try:
        records = {}
        for name, item in headers.items():
            (output/name).write_bytes(item['data'])
            records[name] = {'sha256': hashlib.sha256(item['data']).hexdigest(), 'origins': item['origins']}
        (output/'source-hashes.json').write_text(json.dumps(records, indent=2)+'\n')
    except Exception:
        shutil.rmtree(output)
        raise
    return records


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--reader', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print('Canonical SDK headers:', len(prepare(args.runtime, args.reader, args.output)))
