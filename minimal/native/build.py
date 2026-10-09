"""X4 composition pre-build hook; never imported by an ordinary Runtime build."""
import importlib.util
import hashlib
import json
import os
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
Import('env')
root = Path(env.subst('$PROJECT_DIR'))
record = json.loads((root / 'x4-native-composition.json').read_text())
payload = {key: value for key, value in record.items() if key != 'composition_sha256'}
digest = hashlib.sha256((json.dumps(payload, sort_keys=True, separators=(',', ':')) + '\n').encode()).hexdigest()
if record.get('schema') != 'x4.native-composition' or record.get('schema_version') != 1 or digest != record.get('composition_sha256'):
    raise ValueError('Invalid X4 composition identity')
if env.subst('$PIOENV') != record['build_environment']:
    raise ValueError('This composed workspace only builds its recorded X4 environment')
options = record.get('build_options')
if not isinstance(options, dict) or set(options) not in ({'app_policy_rows', 'app_image_cache'},
                                                       {'app_policy_rows', 'app_image_cache', 'usb_phy'}):
    raise ValueError('Invalid native build options')
if type(options['app_policy_rows']) is not int or options['app_policy_rows'] not in (16, 17):
    raise ValueError('App policy rows must be 16 or 17')
if type(options['app_image_cache']) is not bool:
    raise ValueError('App image cache must be boolean')
if 'usb_phy' in options and options['usb_phy'] is not True:
    raise ValueError('USB PHY must be an explicit true opt-in')
# SCons processes BUILD_FLAGS after this pre-build hook. Refuse preexisting
# definitions and undefines, including command-line/environment overrides,
# instead of allowing flag order to replace the recorded selection.
for key in ('BUILD_FLAGS', 'BUILD_UNFLAGS', 'CCFLAGS', 'CFLAGS', 'CXXFLAGS', 'CPPDEFINES'):
    if re.search(r'(?:\b|-[DU])(?:RISC_APP_(?:POLICY_ROWS|IMAGE_CACHE)|RISC_ENABLE_USB_PHY|CONFIG_(?:ESPTOOLPY_FLASH\w*|SPIRAM_(?:MODE|SPEED)_\w*))\b', str(env.get(key, ''))):
        raise ValueError('Native option flags must come only from the composition record: ' + key)
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
flash_spec = importlib.util.spec_from_file_location('x4_flash_profile', root / 'x4-native/flash_profile.py')
flash_profile = importlib.util.module_from_spec(flash_spec)
flash_spec.loader.exec_module(flash_profile)
flash_profile.verify_build(env, record)
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
env.Append(CPPDEFINES=[('RISC_NATIVE_DIAGNOSTIC_OBSERVER',1),
                      ('RISC_APP_POLICY_ROWS', options['app_policy_rows']),
                      ('RISC_APP_IMAGE_CACHE', int(options['app_image_cache']))])
if options.get('usb_phy'):
    env.Append(CPPDEFINES=[('RISC_ENABLE_USB_PHY', 1)])
env.Append(LINKFLAGS=['-Wl,-u,risc_x4_native_composition_identity', '-Wl,--wrap=app_main'])
env.BuildSources('$BUILD_DIR/x4-native', str(root / 'x4-native'), '+<X4EarlyBoot.cpp>')
