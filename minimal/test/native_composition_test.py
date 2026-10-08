#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
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
        self.assertFalse((self.output / '.git').exists())
        self.assertEqual((self.runtime / 'platformio.ini').read_bytes(), original)
        self.assertEqual((self.output / 'src/main.cpp').read_bytes(), (self.runtime / 'src/main.cpp').read_bytes())
        self.assertEqual(set(record['platform_source_sha256']), {
            'minimal/native/X4EarlyBoot.cpp', 'minimal/native/build.py', 'minimal/scripts/prepare_native_runtime.py'})
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
        self.assertEqual(env['sources'][2], '+<X4EarlyBoot.cpp>')
        self.assertIn(record['composition_sha256'], (self.root / 'objects/X4NativeBuildIdentity.h').read_text())
        env['environment'] = 'esp32s3'
        with self.assertRaisesRegex(ValueError, 'recorded X4 environment'):
            exec(compile(script.read_text(), str(script), 'exec'), {'env': env, 'Import': lambda _: None})

    @unittest.skipUnless(importlib.util.find_spec('elftools'), 'Requires pinned pyelftools from requirements-ci.txt')
    def test_linked_proof_requires_strong_hook_and_matching_marker(self):
        record = self.prepare()
        source = self.root / 'proof.c'
        binary = self.root / 'proof'
        code = ('void initVariant(void) {}\nconst char* risc_native_startup_error(void) {return 0;}\n'
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
