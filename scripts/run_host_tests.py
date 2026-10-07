#!/usr/bin/env python3
"""Run X4 host regressions against a composed runtime; never access a device."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
IMPORT_PACKAGES = ('platform-clock-v1', 'x4pro-panel', 'x4pro-buttons',
                   'x4pro-rtc', 'x4pro-frontlight', 'x4pro-sd', 'x4pro-i2c',
                   'x4pro-gt911', 'x4pro-battery')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runtime', type=Path, help='directory from prepare_runtime.py')
    parser.add_argument('--source-fixture', action='store_true',
                        help='validate a byte-verified source fixture without claiming composed provenance')
    parser.add_argument('--built', action='store_true',
                        help='require all nine provider artifacts/import checks and the staged boot profile')
    parser.add_argument('--log-dir', type=Path, default=ROOT / 'dist/validation',
                        help='host-tests.log and host-tests.json destination')
    parser.add_argument('--timeout', type=int, default=180, help='seconds per host command')
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    if not runtime.is_dir():
        parser.error('runtime directory does not exist')
    if args.timeout < 1:
        parser.error('timeout must be positive')
    if not args.source_fixture and not (runtime / 'build-origin.json').is_file():
        parser.error('build-origin.json is missing; compose the runtime first')
    args.log_dir.mkdir(parents=True, exist_ok=True)
    results = []
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    with (args.log_dir / 'host-tests.log').open('w') as log, tempfile.TemporaryDirectory(prefix='x4-host-') as tmp:
        folder = Path(tmp)

        def note(message):
            print(message, flush=True)
            log.write(message + '\n')
            log.flush()

        def execute(name, command):
            command = [str(part) for part in command]
            note(f'\n[{name}] $ {shlex.join(command)}')
            started = time.monotonic()
            try:
                result = subprocess.run(command, cwd=runtime, env=environment, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        timeout=args.timeout, check=False)
                output, code = result.stdout, result.returncode
            except subprocess.TimeoutExpired as error:
                output = error.stdout or ''
                if isinstance(output, bytes):
                    output = output.decode(errors='replace')
                output += f'\nCommand exceeded {args.timeout} seconds.\n'
                code = 124
            except OSError as error:
                output, code = str(error) + '\n', 127
            log.write(output)
            log.flush()
            if code:
                print(output, end='', flush=True)
            status = 'passed' if code == 0 else 'failed'
            results.append(dict(name=name, status=status, returncode=code,
                                seconds=round(time.monotonic()-started, 3), command=command))
            note(f'{name}: {status.upper()}')
            return code == 0

        def skip(name, reason):
            results.append(dict(name=name, status='skipped', reason=reason))
            note(f'{name}: NOT RUN ({reason})')

        def unavailable(name, reason):
            if args.built:
                results.append(dict(name=name, status='failed', reason=reason))
                note(f'{name}: FAILED ({reason}; required by --built)')
            else:
                skip(name, reason)

        boundary = [sys.executable, ROOT/'test/migration/test_boundary.py', '--runtime', runtime]
        if args.source_fixture:
            boundary.append('--source-fixture')
        if args.built:
            boundary.append('--built')
        if not execute('migration-boundary', boundary):
            note('Source/provenance verification failed; no source tests will execute.')
        else:
            for name in ('battery', 'rtc', 'i2c', 'frontlight'):
                execute(name, ['bash', runtime/f'test/run_x4pro_{name}_test.sh'])
            for name, path in (
                ('boot-power', 'test/desk_clock/BootPowerTest.py'),
                ('boot-diagnostics', 'test/desk_clock/BootDiagnosticsTest.py'),
                ('boot-isolation', 'test/x4pro_boot_isolation_test.py'),
                ('battery-boot-hook', 'test/native_battery/boot_hook_test.py'),
                ('panel-sequence', 'test/x4pro_panel_sequence_test.py'),
                ('display-flip', 'test/display/x4_flip_bridge_test.py'),
                ('reader-entry', 'test/reader_entry/x4_integration_test.py'),
                ('page-button-direction', 'test/page_button_direction/run_test.py'),
            ):
                execute(name, [sys.executable, runtime/path])

            cc = shlex.split(os.environ.get('CC', 'cc'))
            common = ['-std=c11', '-Wall', '-Wextra', '-Werror']

            def compile_run(name, sources, includes=(), flags=()):
                binary = folder/name
                command = [*cc, *common, *flags,
                           *[f'-I{runtime/p}' for p in includes],
                           *[runtime/p for p in sources], '-o', binary]
                if execute(name+'-compile', command):
                    execute(name, [binary])

            compile_run('gpio', ['test/x4pro_gpio_test.c'], ['Drivers/x4pro_board'],
                        ['-Wno-int-to-pointer-cast'])
            compile_run('protocol', ['test/x4pro_proto_test.c'], ['Drivers/x4pro_board'])
            for name, test, provider, fake in (
                ('buttons', 'buttons', 'buttons', 'buttons'),
                ('touch', 'gt911', 'gt911', 'touch'),
                ('panel', 'panel', 'panel', 'panel'),
            ):
                compile_run(name, [f'test/x4pro_{test}_state_test.c', f'Drivers/x4pro_{provider}/driver.c'],
                            ['sdk/driver', f'test/x4pro_{fake}_fake', 'Drivers/x4pro_board'])
            compile_run('sd-absent', ['Drivers/x4pro_sd/driver.c', 'test/x4pro_sd_absent_test.c',
                        'test/storage_volume/os_cpu_fake.c', 'Drivers/storage_fatfs/fatfs/ff.c',
                        'Drivers/storage_fatfs/fatfs/ffunicode.c'],
                        ['test/x4pro_sd_fake', 'sdk/driver', 'Drivers/x4pro_board'],
                        ['-D_XOPEN_SOURCE=700', '-O1', '-pthread', '-Wno-overflow',
                         '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
            frontlight = folder/'board-frontlight'
            cxx = shlex.split(os.environ.get('CXX', 'c++'))
            if execute('board-frontlight-compile', [*cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                       *[f'-I{runtime/p}' for p in ('test/bootstrap_store/stubs', 'lib/Board',
                                                  'lib/Board_X4Pro', 'sdk/driver')],
                       runtime/'test/bootstrap_store/frontlight_test.cpp',
                       runtime/'lib/Board_X4Pro/BoardX4Pro.cpp', '-o', frontlight]):
                execute('board-frontlight', [frontlight])

            sd = runtime/'dist/x4-independent-packages/sdcard'
            if sd.exists():
                execute('staged-boot-profile', [sys.executable, runtime/'test/x4pro_boot_profile_test.py', sd])
            else:
                unavailable('staged-boot-profile', 'X4 package staging has not been run')
            built = runtime/'dist/experimental'
            available = [name for name in IMPORT_PACKAGES if (built/name/'driver.elf').is_file()]
            for name in available:
                execute('exact-imports-'+name, [sys.executable, ROOT/'test/migration/test_imports.py',
                                               runtime, '--package', name])
            missing = sorted(set(IMPORT_PACKAGES)-set(available))
            if missing:
                unavailable('remaining-provider-imports', 'provider ELFs missing: '+', '.join(missing))
            compiler = os.environ.get('NATIVE_DRIVER_CC')
            pinned = False
            if compiler:
                try:
                    version = subprocess.check_output([compiler, '--version'], text=True, timeout=10)
                    pinned = 'esp-14.2.0_20260121' in version
                except (OSError, subprocess.SubprocessError):
                    pass
            if (built/'x4pro-panel/driver.elf').is_file() and pinned:
                execute('provider-link', [sys.executable, runtime/'test/x4pro_provider_link_test.py'])
            else:
                skip('provider-link', 'GCC14-specific time32 negative test requires esp-14.2.0_20260121 NATIVE_DRIVER_CC')
        summary = dict(schema=1, runtime=str(runtime), source_fixture=args.source_fixture,
                       built_required=args.built, sanitizer_options={key: environment.get(key, '')
                           for key in ('ASAN_OPTIONS', 'UBSAN_OPTIONS', 'LSAN_OPTIONS')},
                       hardware_tested=False, results=results,
                       passed=sum(r['status']=='passed' for r in results),
                       failed=sum(r['status']=='failed' for r in results),
                       skipped=sum(r['status']=='skipped' for r in results))
        (args.log_dir/'host-tests.json').write_text(json.dumps(summary, indent=2)+'\n')
        note(f"\nHost checks: {summary['passed']} passed, {summary['failed']} failed, {summary['skipped']} not run.")
        note('Hardware testing: NOT RUN. No device controller was invoked.')
    return 1 if summary['failed'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
