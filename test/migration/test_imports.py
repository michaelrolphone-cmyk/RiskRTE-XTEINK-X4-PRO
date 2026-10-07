#!/usr/bin/env python3
"""Use upstream relocation validation and production exact-import matching."""
import argparse
import importlib.util
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runtime', type=Path)
    parser.add_argument('--package', required=True, choices=(
        'platform-clock-v1', 'x4pro-panel', 'x4pro-buttons', 'x4pro-rtc',
        'x4pro-frontlight', 'x4pro-sd', 'x4pro-i2c', 'x4pro-gt911', 'x4pro-battery'))
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    sys.path.insert(0, str(runtime/'scripts'))
    from generate_privileged_imports_v1 import extract_imports
    from validate_xtensa_relative_targets import validate
    elf = runtime/'dist/experimental'/args.package/'driver.elf'
    print(f'{args.package}: {validate(elf)} relative relocation targets verified', flush=True)
    specification = importlib.util.spec_from_file_location(
        'upstream_import_match', runtime/'test/x4pro_import_match_test.py')
    matcher = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(matcher)
    matcher.ROOT = runtime
    matcher.PACKAGES['x4pro-battery'] = []
    if args.package == 'x4pro-gt911':
        # Optimizers may inline these two memory primitives; no other raw imports are expected.
        imports = extract_imports(elf)
        if not set(imports) <= {'memcpy', 'memset'}:
            raise ValueError(f'Unexpected GT911 privileged imports: {imports}')
        matcher.PACKAGES['x4pro-gt911'] = imports
    sys.argv = [sys.argv[0], '--package', args.package]
    matcher.main()


if __name__ == '__main__':
    main()
