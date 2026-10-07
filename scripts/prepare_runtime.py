#!/usr/bin/env python3
"""Compose X4-owned sources with the exact locked shared runtime checkout."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {'.git', '.github', '.pio', '.cache', 'dist', 'firmware', 'SD_fonts'}


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def safe_source(root, name):
    path = PurePosixPath(name)
    if not name or path.is_absolute() or '..' in path.parts or str(path) != name:
        raise ValueError(f'Unsafe source path: {name}')
    source = root / name
    if not source.resolve().is_relative_to(root):
        raise ValueError(f'Source escapes repository: {name}')
    current = source
    while current != root:
        if current.is_symlink():
            raise ValueError(f'Symlinked source path: {name}')
        current = current.parent
    if not source.is_file():
        raise ValueError(f'Expected regular source: {name}')
    return source


def compose(upstream, output, platform_root=ROOT):
    upstream, output, platform_root = map(lambda p: Path(p).resolve(), (upstream, output, platform_root))
    lock = json.loads((platform_root / 'migration-source.json').read_text())
    expected = lock['upstream']['commit']
    if git(upstream, 'rev-parse', 'HEAD') != expected:
        raise ValueError(f'Shared runtime must be checked out at {expected}')
    if git(upstream, 'rev-parse', 'HEAD^{tree}') != lock['upstream']['tree']:
        raise ValueError('Shared runtime tree does not match migration lock')
    if git(upstream, 'status', '--porcelain', '--untracked-files=no'):
        raise ValueError('Shared runtime has tracked modifications; use a clean checkout')
    if output.exists():
        raise ValueError('Output must not exist; choose a new build directory')
    if output == upstream or upstream in output.parents or output == platform_root or platform_root in output.parents:
        # A root-local build/ directory is supported, but never inside the dependency checkout.
        if not (platform_root in output.parents and output != platform_root and upstream not in output.parents):
            raise ValueError('Output must be separate from the source checkout')
    paths = git(upstream, 'ls-files', '--stage', '-z').split('\0')
    copied = []
    output.mkdir(parents=True)
    try:
        for entry in paths:
            if not entry:
                continue
            metadata, name = entry.split('\t', 1)
            mode, expected_blob, stage = metadata.split()
            path = PurePosixPath(name)
            if path.parts[0] in EXCLUDED:
                continue
            if mode not in ('100644', '100755') or stage != '0':
                raise ValueError(f'Unsupported tracked source: {name}')
            source = safe_source(upstream, name)
            data = source.read_bytes()
            observed = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
            if observed != expected_blob:
                raise ValueError(f'Tracked source content differs from locked runtime: {name}')
            target = output / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            target.chmod(0o755 if mode == '100755' else 0o644)
            copied.append(name)
        # Overlay only the explicitly migrated paths and platform-owned provisioning.
        overlay = [item['path'] for item in lock['files']]
        extra = platform_root / 'migration-additional-files.json'
        if extra.exists():
            overlay += [item['path'] for item in json.loads(extra.read_text())]
        overlay += [p.relative_to(platform_root).as_posix() for folder in ('profiles', 'provisioning')
                    for p in (platform_root / folder).rglob('*') if p.is_file()]
        for name in sorted(set(overlay)):
            source = safe_source(platform_root, name)
            target = output / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
        # Base settings, SDK/tool pins and other boards stay in the runtime repository.
        # The actual X4 environment is replaced by this repository's owned fragment.
        original = (output / 'platformio.ini').read_text()
        start = original.index('[env:xteink-x4-pro]')
        end = original.find('\n[', start + 1)
        if end < 0:
            end = len(original)
        fragment = (platform_root / 'platformio.x4.ini').read_text().rstrip()
        (output / 'platformio.ini').write_text(original[:start] + fragment + '\n' + original[end:])
        platform_commit = git(platform_root, 'rev-parse', 'HEAD')
        platform_dirty = bool(git(platform_root, 'status', '--porcelain', '--untracked-files=normal'))
        origin = {'schema': 1, 'platform': {'repository': 'michaelrolphone-cmyk/RiskRTE-XTEINK-X4-PRO',
                  'commit': platform_commit, 'dirty': platform_dirty}, 'runtime': lock['upstream'],
                  'copied_runtime_files': len(copied), 'overlay_files': sorted(set(overlay))}
        (output / 'build-origin.json').write_text(json.dumps(origin, indent=2) + '\n')
        return origin
    except Exception:
        # Output was newly created by this invocation; preserve source repositories.
        shutil.rmtree(output)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', type=Path, required=True, help='clean checkout at migration-source.json runtime commit')
    parser.add_argument('--output', type=Path, required=True, help='new generated build directory')
    args = parser.parse_args()
    origin = compose(args.upstream, args.output)
    print(json.dumps(origin, indent=2))


if __name__ == '__main__':
    main()
