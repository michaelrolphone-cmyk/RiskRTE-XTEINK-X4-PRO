"""X4 composition pre-build hook; never imported by an ordinary Runtime build."""
import hashlib
import json
import os
from pathlib import Path

Import('env')
root = Path(env.subst('$PROJECT_DIR'))
record = json.loads((root / 'x4-native-composition.json').read_text())
payload = {key: value for key, value in record.items() if key != 'composition_sha256'}
digest = hashlib.sha256((json.dumps(payload, sort_keys=True, separators=(',', ':')) + '\n').encode()).hexdigest()
if record.get('schema') != 'x4.native-composition' or record.get('schema_version') != 1 or digest != record.get('composition_sha256'):
    raise ValueError('Invalid X4 composition identity')
if env.subst('$PIOENV') != record['build_environment']:
    raise ValueError('This composed workspace only builds its recorded X4 environment')
observed = set()
for folder, directories, names in os.walk(root):
    if Path(folder) == root:
        directories[:] = [name for name in directories if name not in ('.pio', 'build', 'dist', '.cache')]
    if any((Path(folder) / name).is_symlink() for name in directories):
        raise ValueError('Symlinked composed source directory')
    observed.update((Path(folder) / name).relative_to(root).as_posix() for name in names)
if observed != set(record['composed_source_sha256']) | {'x4-native-composition.json'}:
    raise ValueError('Composed source inventory differs')
for name, digest in record['composed_source_sha256'].items():
    path = root / name
    if path.is_symlink() or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise ValueError('Composed source differs: ' + name)
env['ENV']['SOURCE_DATE_EPOCH'] = str(record['runtime']['source_date_epoch'])
env.Append(CCFLAGS=['-ffile-prefix-map=' + str(root) + '=.'])
build = Path(env.subst('$BUILD_DIR'))
build.mkdir(parents=True, exist_ok=True)
runtime = record['runtime']
identity = ('#define RISC_BUILD_SOURCE_SHA "' + runtime['commit'] + '"\n'
            '#define RISC_BUILD_IDENTITY "RTE_SOURCE=' + runtime['commit'] + '"\n'
            '#define RISC_BUILD_VERSION "' + runtime['version'] + '"\n')
composition = '#define X4_NATIVE_COMPOSITION_IDENTITY "X4_NATIVE_COMPOSITION:' + record['composition_sha256'] + '"\n'
for name, content in [('RiscBuildIdentity.h', identity), ('X4NativeBuildIdentity.h', composition)]:
    path = build / name
    if not path.exists() or path.read_text() != content:
        path.write_text(content)
env.Append(CPPPATH=[str(build)])
env.Append(LINKFLAGS=['-Wl,-u,risc_x4_native_composition_identity'])
env.BuildSources('$BUILD_DIR/x4-native', str(root / 'x4-native'), '+<X4EarlyBoot.cpp>')
