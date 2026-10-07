#!/usr/bin/env python3
"""Exercise composition with disposable repositories; no network or device access."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('prepare_runtime', ROOT/'scripts/prepare_runtime.py')
PREPARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PREPARE)


def git(root, *arguments):
    environment = dict(os.environ, GIT_AUTHOR_NAME='Composition Test',
                       GIT_AUTHOR_EMAIL='test@example.invalid',
                       GIT_COMMITTER_NAME='Composition Test',
                       GIT_COMMITTER_EMAIL='test@example.invalid')
    return subprocess.check_output(['git', '-C', str(root), *arguments],
                                   env=environment, text=True, stderr=subprocess.STDOUT).strip()


def write(root, name, content):
    path = root/name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)
    return path


class PrepareRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='x4-compose-test-')
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.upstream = self.folder/'upstream'
        self.platform = self.folder/'platform'
        self.output = self.folder/'build/runtime'
        for root in (self.upstream, self.platform):
            root.mkdir()
            git(root, 'init', '-q')
            git(root, 'config', 'commit.gpgsign', 'false')
        write(self.upstream, 'platformio.ini',
              '[platformio]\ndefault_envs = other\n\n[base]\nplatform = pinned-toolchain\n\n'
              '[env:xteink-x4-pro]\nextends = base\nbuild_flags = old-x4\n\n'
              '[env:other]\nextends = base\nbuild_flags = unchanged-other\n')
        write(self.upstream, 'src/main.cpp', 'shared runtime\n')
        write(self.upstream, 'Drivers/x4pro_panel/driver.c', 'original panel\n')
        write(self.upstream, '.github/workflows/upstream.yml', 'do not copy workflows\n')
        write(self.upstream, 'SD_fonts/font.bin', 'excluded asset\n')
        self.commit(self.upstream)
        self.lock = dict(schema=1, upstream=dict(repository='example/runtime',
                         url='https://example.invalid/runtime.git',
                         commit=git(self.upstream, 'rev-parse', 'HEAD'),
                         tree=git(self.upstream, 'rev-parse', 'HEAD^{tree}')),
                         files=[dict(path='Drivers/x4pro_panel/driver.c',
                                     blob=self.blob(self.upstream/'Drivers/x4pro_panel/driver.c'),
                                     mode='100644')])
        write(self.platform, 'Drivers/x4pro_panel/driver.c', 'platform panel\n')
        write(self.platform, 'platformio.x4.ini',
              '[env:xteink-x4-pro]\nextends = base\nbuild_flags = new-x4\n')
        write(self.platform, 'profiles/x4.json', '{"board":"xteink-x4-pro"}\n')
        write(self.platform, '.gitignore', 'build/\n')
        self.save_lock()
        self.commit(self.platform)

    @staticmethod
    def blob(path):
        data = path.read_bytes()
        return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()

    @staticmethod
    def commit(root):
        git(root, 'add', '--all')
        git(root, 'commit', '-qm', 'Disposable test fixture')

    def save_lock(self):
        write(self.platform, 'migration-source.json', json.dumps(self.lock)+'\n')

    def compose(self, output=None):
        return PREPARE.compose(self.upstream, output or self.output, self.platform)

    def test_composes_owned_overlay_without_mutating_dependency(self):
        write(self.upstream, 'untracked-private.txt', 'not a tracked dependency\n')
        additional = 'test/x4_fixture.c'
        write(self.platform, additional, 'extra fixture\n')
        write(self.platform, 'migration-additional-files.json', json.dumps([
            dict(path=additional, blob=self.blob(self.platform/additional), mode='100644')]))
        origin = self.compose()
        self.assertEqual((self.output/'Drivers/x4pro_panel/driver.c').read_text(), 'platform panel\n')
        self.assertEqual((self.upstream/'Drivers/x4pro_panel/driver.c').read_text(), 'original panel\n')
        self.assertEqual((self.output/'src/main.cpp').read_text(), 'shared runtime\n')
        self.assertEqual((self.output/additional).read_text(), 'extra fixture\n')
        self.assertTrue((self.output/'profiles/x4.json').is_file())
        for excluded in ('.git', '.github', 'SD_fonts', 'untracked-private.txt'):
            self.assertFalse((self.output/excluded).exists(), excluded)
        configuration = (self.output/'platformio.ini').read_text()
        self.assertIn('platform = pinned-toolchain', configuration)
        self.assertIn('[env:other]\nextends = base\nbuild_flags = unchanged-other', configuration)
        self.assertEqual(configuration.count('[env:xteink-x4-pro]'), 1)
        self.assertIn('build_flags = new-x4', configuration)
        self.assertNotIn('build_flags = old-x4', configuration)
        self.assertEqual(origin['runtime'], self.lock['upstream'])
        self.assertEqual(origin['platform']['commit'], git(self.platform, 'rev-parse', 'HEAD'))
        self.assertTrue(origin['platform']['dirty'])
        self.assertEqual(json.loads((self.output/'build-origin.json').read_text()), origin)
        self.assertEqual(git(self.upstream, 'status', '--porcelain', '--untracked-files=no'), '')

    def test_wrong_commit_and_tree_rejected_before_output(self):
        for field in ('commit', 'tree'):
            with self.subTest(field=field):
                original = self.lock['upstream'][field]
                self.lock['upstream'][field] = '0'*40
                self.save_lock()
                with self.assertRaises(ValueError):
                    self.compose()
                self.assertFalse(self.output.exists())
                self.lock['upstream'][field] = original

    def test_tracked_modifications_rejected(self):
        write(self.upstream, 'src/main.cpp', 'changed after pin\n')
        with self.assertRaises(ValueError):
            self.compose()
        self.assertFalse(self.output.exists())

    def test_existing_output_preserved(self):
        sentinel = write(self.output, 'sentinel.txt', 'do not overwrite\n')
        with self.assertRaises(ValueError):
            self.compose()
        self.assertEqual(sentinel.read_text(), 'do not overwrite\n')

    def test_output_inside_dependency_rejected(self):
        forbidden = self.upstream/'build/runtime'
        with self.assertRaises(ValueError):
            self.compose(forbidden)
        self.assertFalse(forbidden.exists())

    def test_platform_local_ignored_build_allowed(self):
        output = self.platform/'build/runtime'
        origin = self.compose(output)
        self.assertTrue((output/'build-origin.json').is_file())
        self.assertFalse(origin['platform']['dirty'])

    def test_missing_overlay_cleans_only_new_output(self):
        self.lock['files'].append(dict(path='Drivers/missing/driver.c', blob='0'*40, mode='100644'))
        self.save_lock()
        with self.assertRaises(ValueError):
            self.compose()
        self.assertFalse(self.output.exists())
        self.assertTrue((self.upstream/'src/main.cpp').is_file())
        self.assertTrue((self.platform/'Drivers/x4pro_panel/driver.c').is_file())

    def test_overlay_parent_traversal_rejected_without_outside_write(self):
        write(self.folder, 'foreign.txt', 'outside platform\n')
        self.lock['files'].append(dict(path='../foreign.txt', blob='0'*40, mode='100644'))
        self.save_lock()
        with self.assertRaises(ValueError):
            self.compose()
        self.assertFalse((self.folder/'build/foreign.txt').exists())
        self.assertFalse(self.output.exists())

    def test_overlay_symlink_parent_rejected(self):
        outside = self.folder/'outside'
        write(outside, 'driver.c', 'outside source\n')
        (self.platform/'Drivers/linked').symlink_to(outside, target_is_directory=True)
        self.lock['files'].append(dict(path='Drivers/linked/driver.c', blob='0'*40, mode='100644'))
        self.save_lock()
        with self.assertRaises(ValueError):
            self.compose()
        self.assertFalse(self.output.exists())

    def test_masked_upstream_mutation_is_not_clean_pinned_source(self):
        git(self.upstream, 'update-index', '--skip-worktree', 'src/main.cpp')
        write(self.upstream, 'src/main.cpp', 'hidden local modification\n')
        self.assertEqual(git(self.upstream, 'status', '--porcelain', '--untracked-files=no'), '')
        with self.assertRaises(ValueError):
            self.compose()
        self.assertFalse(self.output.exists())

    def test_leading_space_in_tracked_path_preserved(self):
        write(self.upstream, ' leading.txt', 'legal tracked name\n')
        self.commit(self.upstream)
        self.lock['upstream']['commit'] = git(self.upstream, 'rev-parse', 'HEAD')
        self.lock['upstream']['tree'] = git(self.upstream, 'rev-parse', 'HEAD^{tree}')
        self.save_lock()
        self.compose()
        self.assertEqual((self.output/' leading.txt').read_text(), 'legal tracked name\n')


if __name__ == '__main__':
    unittest.main(verbosity=2)
