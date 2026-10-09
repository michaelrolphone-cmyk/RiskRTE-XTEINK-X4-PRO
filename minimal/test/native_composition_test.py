#!/usr/bin/env python3
import importlib.util
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('composition', ROOT / 'minimal/scripts/prepare_native_runtime.py')
composition = importlib.util.module_from_spec(spec)
spec.loader.exec_module(composition)


def commit(root):
    subprocess.run(['git', 'init', '-q', str(root)], check=True)
    subprocess.run(['git', '-C', str(root), 'add', '.'], check=True)
    subprocess.run(['git', '-C', str(root), '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                    'commit', '-qm', 'Fixture'], check=True)
    return composition.git(root, 'rev-parse', 'HEAD')


class CompositionTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.runtime = self.root / 'runtime'
        (self.runtime / 'src').mkdir(parents=True)
        (self.runtime / 'src/main.cpp').write_text('extern "C" const char* risc_native_startup_error();\n')
        (self.runtime/'src/ports/esp32s3').mkdir(parents=True)
        (self.runtime/'src/ports/esp32s3/SleepDiagnostics.cpp').write_text('extern \"C\" void risc_native_diagnostic_observer(const char*);\n')
        with (self.runtime/'src/ports/esp32s3/SleepDiagnostics.cpp').open('a') as f:
            f.write('void risc_native_diagnostic_drain(); int risc_native_diagnostic_read();\n')
        (self.runtime/'sdk/driver').mkdir(parents=True)
        (self.runtime/'sdk/driver/RiscDiagnosticSourceV1.h').write_text('/* source fixture */\n')
        (self.runtime / 'platformio.ini').write_text('[riscrte]\nversion = 0.1.57\n'
            '[env:esp32s3-16mb-appdata-iq]\nextra_scripts = pre:scripts/reproducible_build.py\n')
        self.revision = commit(self.runtime)
        self.platform = self.root / 'platform'
        (self.platform / 'minimal/scripts').mkdir(parents=True)
        shutil.copytree(ROOT / 'minimal/native', self.platform / 'minimal/native')
        shutil.copyfile(ROOT / 'minimal/scripts/prepare_native_runtime.py', self.platform / 'minimal/scripts/prepare_native_runtime.py')
        (self.platform / 'minimal/sources.lock.json').write_text(json.dumps({'runtime': {
            'repository': 'michaelrolphone-cmyk/RiscRTE', 'commit': self.revision, 'version': '0.1.57'}}))
        self.platform_revision = commit(self.platform)
        self.output = self.root / 'composed'

    def prepare(self, **kwargs):
        return composition.compose(self.runtime, self.output, platform_root=self.platform, **kwargs)

    def test_exact_composition_preserves_inputs_and_records_overlay(self):
        original = (self.runtime / 'platformio.ini').read_bytes()
        record = self.prepare()
        self.assertEqual(record, composition.verify_composition(self.output))
        self.assertEqual(record['runtime']['commit'], self.revision)
        self.assertEqual(record['platform']['commit'], self.platform_revision)
        self.assertEqual(record['build_options'], {'app_policy_rows': 16, 'app_image_cache': False})
        self.assertFalse((self.output / '.git').exists())
        self.assertEqual((self.runtime / 'platformio.ini').read_bytes(), original)
        self.assertEqual((self.output / 'src/main.cpp').read_bytes(), (self.runtime / 'src/main.cpp').read_bytes())
        self.assertEqual(set(record['platform_source_sha256']), {
            'minimal/native/X4EarlyBoot.cpp', 'minimal/native/X4BootRecord.h', 'minimal/native/build.py', 'minimal/scripts/prepare_native_runtime.py'})
        self.assertIn(b'pre:x4-native/build.py', (self.output / 'platformio.ini').read_bytes())
        self.assertEqual(composition.git(self.runtime, 'status', '--porcelain'), '')
        composition.verify_source_custody(self.runtime, record, self.platform)

    def test_dirty_runtime_and_platform_refused(self):
        (self.runtime / 'src/main.cpp').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'Clean committed'): self.prepare()
        subprocess.run(['git', '-C', str(self.runtime), 'checkout', '--', 'src/main.cpp'], check=True)
        (self.platform / 'uncommitted').write_text('untracked')
        with self.assertRaisesRegex(ValueError, 'Clean committed'): self.prepare()

    def test_exact_commit_and_fresh_output(self):
        with self.assertRaisesRegex(ValueError, 'exact requested commit'): self.prepare(runtime_commit='a' * 40)
        self.prepare()
        with self.assertRaisesRegex(ValueError, 'Output must be new'): self.prepare()

    def test_mutated_source_and_record_refused(self):
        self.prepare()
        (self.output / 'x4-native/X4EarlyBoot.cpp').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'Composed source mismatch'): composition.verify_composition(self.output)
        path = self.output / 'x4-native-composition.json'
        record = json.loads(path.read_text()); record['runtime']['version'] = '9.9.9'
        path.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, 'Composition digest'): composition.verify_composition(self.output)

    def test_runtime_startup_gate_required(self):
        (self.runtime / 'src/main.cpp').write_text('void setup() {}\n')
        self.revision = commit(self.runtime)
        with self.assertRaisesRegex(ValueError, 'startup-status hook'): self.prepare(runtime_commit=self.revision)

    def test_unrecorded_source_and_symlink_refused(self):
        self.prepare()
        extra = self.output / 'src/unrecorded.cpp'
        extra.write_text('void hidden_code() {}')
        with self.assertRaisesRegex(ValueError, 'inventory differs'): composition.verify_composition(self.output)
        extra.unlink()
        (self.output / 'src/link').symlink_to(self.runtime / 'src', target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'Symlinked'): composition.verify_composition(self.output)

    def test_rehashed_source_change_cannot_impersonate_upstream(self):
        record = self.prepare()
        record['composed_source_sha256']['src/main.cpp'] = 'a' * 64
        with self.assertRaisesRegex(ValueError, 'Unapproved Runtime overlay'):
            composition.verify_source_custody(self.runtime, record, self.platform)

    def test_explicit_options_recorded_and_bound_to_identity(self):
        record = self.prepare(app_policy_rows=17, app_image_cache=True)
        self.assertEqual(record['build_options'], {'app_policy_rows': 17, 'app_image_cache': True})
        composition.verify_source_custody(self.runtime, record, self.platform)
        path = self.output / 'x4-native-composition.json'
        record['build_options']['app_image_cache'] = False
        path.write_bytes(composition.encoded(record))
        with self.assertRaisesRegex(ValueError, 'Composition digest mismatch'):
            composition.verify_composition(self.output)

    def test_invalid_options_fail_before_creating_workspace(self):
        for options in ({'app_policy_rows': 15}, {'app_policy_rows': 18}, {'app_policy_rows': '17'},
                        {'app_policy_rows': True}, {'app_policy_rows': 17.0},
                        {'app_image_cache': 1}, {'app_image_cache': 'yes'}, {'app_image_cache': None}):
            with self.subTest(options=options), self.assertRaisesRegex(ValueError, 'policy rows|image cache'):
                self.prepare(**options)
            self.assertFalse(self.output.exists())

    def test_rehashed_invalid_selection_is_rejected(self):
        record = self.prepare()
        record['build_options']['app_policy_rows'] = 18
        del record['composition_sha256']
        record['composition_sha256'] = composition.sha(composition.encoded(record))
        (self.output / 'x4-native-composition.json').write_bytes(composition.encoded(record))
        with self.assertRaisesRegex(ValueError, 'App policy rows'):
            composition.verify_composition(self.output)
        with self.assertRaisesRegex(ValueError, 'App policy rows'):
            composition.verify_source_custody(self.runtime, record, self.platform)

    def test_native_build_script_uses_only_selected_environment(self):
        record = self.prepare()
        class Environment(dict):
            def subst(env, text):
                return text.replace('$PROJECT_DIR', str(self.output)).replace('$BUILD_DIR', str(self.root / 'objects')).replace('$PIOENV', env['environment'])
            def Append(env, **kwargs): env['appended'].append(kwargs)
            def BuildSources(env, *args): env['sources'] = args
        env = Environment(ENV={}, environment=record['build_environment'], appended=[])
        script = self.output / 'x4-native/build.py'
        exec(compile(script.read_text(), str(script), 'exec'), {'env': env, 'Import': lambda _: None})
        self.assertTrue(sys.dont_write_bytecode)
        self.assertEqual(env['sources'][2], '+<X4EarlyBoot.cpp>')
        defines = [value for append in env['appended'] for value in append.get('CPPDEFINES', [])]
        self.assertIn(('RISC_APP_POLICY_ROWS', 16), defines)
        self.assertIn(('RISC_APP_IMAGE_CACHE', 0), defines)
        self.assertIn(record['composition_sha256'], (self.root / 'objects/X4NativeBuildIdentity.h').read_text())
        env['environment'] = 'esp32s3'
        with self.assertRaisesRegex(ValueError, 'recorded X4 environment'):
            exec(compile(script.read_text(), str(script), 'exec'), {'env': env, 'Import': lambda _: None})

    def test_build_hook_uses_explicit_record_and_rejects_external_option_flags(self):
        record = self.prepare(app_policy_rows=17, app_image_cache=True)
        class Environment(dict):
            def subst(env, text):
                return text.replace('$PROJECT_DIR', str(self.output)).replace('$BUILD_DIR', str(self.root / 'objects')).replace('$PIOENV', record['build_environment'])
            def Append(env, **kwargs): env['appended'].append(kwargs)
            def BuildSources(env, *args): pass
        script = self.output / 'x4-native/build.py'
        def run(extra=None):
            env = Environment(ENV={}, appended=[], **(extra or {}))
            exec(compile(script.read_text(), str(script), 'exec'), {'env': env, 'Import': lambda _: None})
            return env
        defines = [value for append in run()['appended'] for value in append.get('CPPDEFINES', [])]
        self.assertIn(('RISC_APP_POLICY_ROWS', 17), defines)
        self.assertIn(('RISC_APP_IMAGE_CACHE', 1), defines)
        for key, flags in [('BUILD_FLAGS', '-DRISC_APP_POLICY_ROWS=16'),
                           ('BUILD_FLAGS', ['-D', 'RISC_APP_IMAGE_CACHE=0']),
                           ('BUILD_FLAGS', '-URISC_APP_IMAGE_CACHE'),
                           ('BUILD_UNFLAGS', '-DRISC_APP_POLICY_ROWS=17'),
                           ('CPPDEFINES', [('RISC_APP_IMAGE_CACHE', 1)]),
                           ('CCFLAGS', ['-DRISC_APP_IMAGE_CACHE=1'])]:
            with self.subTest(key=key, flags=flags), self.assertRaisesRegex(ValueError, 'only from the composition record'):
                run({key: flags})

    @unittest.skipUnless(importlib.util.find_spec('elftools'), 'Requires pinned pyelftools')
    def test_compiled_runtime_option_proof_and_mismatches(self):
        record = self.prepare(app_policy_rows=17, app_image_cache=True)
        source, binary = self.root / 'options.c', self.root / 'options'
        code = ('const char risc_app_policy_rows[]="RISC_APP_POLICY_ROWS:17";\n'
                'void* owner __asm__("_ZN12_GLOBAL__N_118imagePressureOwnerE");\n'
                'void* runtime __asm__("_ZN12_GLOBAL__N_120imagePressureRuntimeE");\n'
                'void reclaim(void) __asm__("_ZN8RiscBoot7Runtime16reclaimAppImagesEv");\n'
                'void reclaim(void) {}\n'
                'int risc_runtime_reclaim_app_images(void) {reclaim();return 1;}\n'
                'void* esp_dl_image_cache_create(void) {return owner;}\n'
                'int main(void) {return 0;}\n')
        def compile_code(value):
            source.write_text(value)
            subprocess.run(['cc', str(source), '-o', str(binary)], check=True)
            return {'firmware.bin': b'RISC_APP_POLICY_ROWS:17\0', 'firmware.elf': binary.read_bytes()}
        blobs = compile_code(code)
        proof = composition.runtime_options_proof(blobs, record)
        self.assertTrue(proof['app_image_cache']['enabled'])
        self.assertEqual(proof['app_policy']['rows'], 17)
        self.assertEqual(proof['app_policy']['live_app_grants'], 16)
        self.assertEqual(proof['app_policy']['manifest_requirements'], 16)
        mismatch = copy.deepcopy(record)
        mismatch['build_options']['app_policy_rows'] = 16
        with self.assertRaisesRegex(ValueError, 'Compiled app policy row mismatch'):
            composition.runtime_options_proof(blobs, mismatch)
        for name in ('firmware.bin', 'firmware.elf'):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'Compiled app policy row mismatch'):
                composition.runtime_options_proof(dict(blobs, **{name: blobs[name] + b'RISC_APP_POLICY_ROWS:16\0'}), record)
        mismatch['build_options'] = {'app_policy_rows': 17, 'app_image_cache': False}
        with self.assertRaisesRegex(ValueError, 'Unexpected enabled app image cache'):
            composition.runtime_options_proof(blobs, mismatch)
        for symbol in ('_ZN12_GLOBAL__N_118imagePressureOwnerE', '_ZN12_GLOBAL__N_120imagePressureRuntimeE',
                       '_ZN8RiscBoot7Runtime16reclaimAppImagesEv', 'esp_dl_image_cache_create'):
            with self.subTest(symbol=symbol), self.assertRaisesRegex(ValueError, 'implementation missing'):
                composition.runtime_options_proof(compile_code(code.replace(symbol, symbol + '_wrong')), record)
        weak = code.replace('int risc_runtime_reclaim_app_images', '__attribute__((weak)) int risc_runtime_reclaim_app_images')
        with self.assertRaisesRegex(ValueError, 'Missing strong app image cache'):
            composition.runtime_options_proof(compile_code(weak), record)
        default_code = 'const char risc_app_policy_rows[]="RISC_APP_POLICY_ROWS:16";\nint main(void) {return 0;}\n'
        default = compile_code(default_code)
        default['firmware.bin'] = b'RISC_APP_POLICY_ROWS:16\0'
        mismatch['build_options'] = {'app_policy_rows': 16, 'app_image_cache': False}
        self.assertFalse(composition.runtime_options_proof(default, mismatch)['app_image_cache']['enabled'])
        mismatch['build_options']['app_image_cache'] = True
        with self.assertRaisesRegex(ValueError, 'implementation missing'):
            composition.runtime_options_proof(default, mismatch)

    @unittest.skipUnless(importlib.util.find_spec('elftools'), 'Requires pinned pyelftools from requirements-ci.txt')
    def test_linked_proof_requires_strong_hook_and_matching_marker(self):
        record = self.prepare()
        source = self.root / 'proof.c'
        binary = self.root / 'proof'
        code = ('void initVariant(void) {}\nvoid __wrap_app_main(void) {}\nvoid risc_native_diagnostic_observer(const char* p) {(void)p;}\nvoid app_main(void) {}\nchar risc_x4_boot_record[264];\nconst char* risc_native_startup_error(void) {return 0;}\n'
                'const char risc_x4_native_composition_identity[] = "X4_NATIVE_COMPOSITION:' +
                record['composition_sha256'] + '";\nint main(void) {initVariant(); return 0;}\n')
        source.write_text(code)
        subprocess.run(['cc', str(source), '-o', str(binary)], check=True)
        proof = composition.startup_proof(binary.read_bytes(), record)
        self.assertEqual(proof['elf_sha256'], composition.sha(binary.read_bytes()))
        with self.assertRaisesRegex(ValueError, 'compiled X4 composition'):
            composition.startup_proof(binary.read_bytes(), dict(record, composition_sha256='a' * 64))
        source.write_text(code.replace('void initVariant', '__attribute__((weak)) void initVariant'))
        subprocess.run(['cc', str(source), '-o', str(binary)], check=True)
        with self.assertRaisesRegex(ValueError, 'Missing strong X4 native symbol'):
            composition.startup_proof(binary.read_bytes(), record)


if __name__ == '__main__':
    unittest.main()
