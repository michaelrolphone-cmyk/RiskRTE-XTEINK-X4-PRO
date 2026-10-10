# Recovery integration copy of Watch5b9fd573 check_runtime_store_admission.py.
# Upstream SHA256 98482349b428d0891a5b13237a9721ff5c6a1eb44f95af76270ba87ea26b46d5.
# Only the explicit native-marker-guarded17/18 profile is added. ROOT is set
# by the final composer to the preserved Watch test/packaging source root.
#!/usr/bin/env python3
"""Admit untouched archive/SPIFFS/BIN stores using the paired production Runtime.

This closes the gap between policy-custody checks and executable boot admission.
It does not run target instructions or qualify physical hardware. The separate
production-store execution test covers actual app/provider source and lifecycle.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import tempfile
import zipfile
from read_only_spiffs import read_image
from pmu_sleep_custody import PMU_FILES, current_pmu_custody

ROOT = Path(__file__).resolve().parents[1]
BOOTFS_OFFSET = 0x310000
BOOTFS_SIZE = 0x4f0000
BIN_SIZE = 0x800000
MKSPIFFS_SHA256 = '4ddf79a1ab9a3baf502cdb979bea7ed173bbe46727a9902649cc09e6a28a5ad2'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def store_digest(store):
    entries = [{'path': name, 'size_bytes': len(data), 'sha256': sha(data)}
               for name, data in sorted(store.items())]
    return sha(json.dumps(entries, sort_keys=True, separators=(',', ':')).encode())


def preserve_store(store, baseline, *, current_pmu=False, root=ROOT):
    """Strict historical custody by default; optionally the exact named PMU repair."""
    expected = json.loads(Path(baseline).read_text())['files']
    if current_pmu:
        if not PMU_FILES <= set(expected):
            raise ValueError('PMU repair requires an existing complete PMU baseline')
        # Change only the expected hashes, never the input store or history.
        # Source-bound custody permits this one driver generation, no fallback.
        expected = {**expected, **current_pmu_custody(root)['files']}
    actual = {name: {'size_bytes': len(data), 'sha256': sha(data)}
              for name, data in store.items()}
    if actual != expected:
        changed = sorted(name for name in set(actual) | set(expected)
                         if actual.get(name) != expected.get(name))
        raise ValueError('Delivered store changed: ' + ', '.join(changed))


def compile_harness(runtime, output, app_data=False, native_elf=None, app_policy_rows=16, *, cohort_policy=False):
    if cohort_policy and (not app_data or native_elf is not None):
        raise ValueError('Policy-only cohort admission requires app-data and no native ELF')
    runtime, output = Path(runtime).resolve(), Path(output)
    includes = [runtime / p for p in ('src', 'sdk/app', 'sdk/driver', 'sdk/hardware',
                                     'lib/ArduinoJson/src', 'test/drivers/stubs')]
    sources = [runtime / p for p in ('src/bootstrap/Json.cpp', 'src/bootstrap/Board.cpp',
               'src/bootstrap/Runtime.cpp', 'src/runtime/drivers/ProviderGraphV2.cpp',
               'src/runtime/drivers/ProviderModuleV2.cpp', 'src/ports/esp32s3/CpuPort.cpp')]
    command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
               '-Wno-missing-field-initializers', '-rdynamic']
    if type(app_policy_rows) is not int or app_policy_rows not in (16, 17, 18):
        raise ValueError('App policy rows must be exactly 16, 17 or 18')
    marker = b'RISC_APP_POLICY_ROWS:' + str(app_policy_rows).encode() + b'\0'
    if app_policy_rows == 18:
        if native_elf is None or marker not in native_elf or b'RISC_APP_REQUIREMENT_ROWS:17\0' not in native_elf:
            raise ValueError('Policy18 admission requires native policy18/requirements17 markers')
        command += ['-DRISC_APP_REQUIREMENT_ROWS=17']
    if app_policy_rows == 17:
        if not (runtime / 'src/bootstrap/AppPolicyLimits.h').is_file() or native_elf is None or marker not in native_elf:
            raise ValueError('Policy17 admission requires the matching compiled native marker')
    if native_elf is not None and b'RISC_APP_POLICY_ROWS:' in native_elf and marker not in native_elf:
        raise ValueError('Native and admission app policy row bounds differ')
    if (runtime / 'src/bootstrap/AppPolicyLimits.h').is_file():
        command += ['-DRISC_APP_POLICY_ROWS=' + str(app_policy_rows)]
    streams = [runtime / 'src/runtime/streams' / name for name in
               ('AppStreamSessions.cpp', 'ProviderQueueHost.cpp')]
    if any(path.is_file() for path in streams):
        if not all(path.is_file() for path in streams):
            raise ValueError('Incomplete Runtime stream implementation')
        sources += streams

    if os.environ.get('SANITIZE') == '1':
        command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer', '-no-pie']
    if app_data:
        command += ['-DSTORE_ADMISSION_APP_DATA','-DRISC_PAIRED_APP_DATA=1']
    if cohort_policy:
        command += ['-DSTORE_ADMISSION_COHORT', '-DSTORE_ADMISSION_COHORT_POLICY']
    if native_elf is not None:
        from verify_update_elf import cohort_admission_header
        output = output.resolve()
        (output.parent / 'cohort_elf_admission.h').write_text(cohort_admission_header(runtime, native_elf))
        includes += [output.parent, runtime / 'lib/elf_loader/include', runtime / 'test/native_bank_stubs']
        command += ['-DSTORE_ADMISSION_COHORT', '-Wno-misleading-indentation']
        obj = output.parent / 'cohort-validate.o'
        cflags = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if os.environ.get('SANITIZE') == '1' else []
        subprocess.run(['cc', '-std=c11', *cflags, *['-I' + str(p) for p in includes],
                        '-c', str(runtime / 'lib/elf_loader/src/esp_elf_validate.c'), '-o', str(obj)], check=True)
        command += [str(obj)]
    cpu_header=(runtime / 'src/ports/esp32s3/CpuPort.h').read_text()
    if native_elf is not None:
        from elftools.elf.elffile import ELFFile
        selected_elf = ELFFile(io.BytesIO(native_elf))
        selected_symbols = selected_elf.get_section_by_name('.symtab')
        usb = selected_symbols.get_symbol_by_name('risc_usb_phy_resource_enabled') if selected_symbols else None
        if usb:
            if len(usb) != 1 or not isinstance(usb[0]['st_shndx'], int):
                raise ValueError('Invalid native USB PHY selection marker')
            symbol = usb[0]
            section = selected_elf.get_section(symbol['st_shndx'])
            offset = symbol['st_value'] - section['sh_addr']
            if symbol['st_size'] != 4 or offset < 0 or section.data()[offset:offset+4] != b'\x01\x00\x00\x00':
                raise ValueError('Native USB PHY selection is not enabled')
            if 'usbPhySuspend' not in cpu_header:
                raise ValueError('Runtime lacks the selected native USB PHY API')
            command += ['-DSTORE_ADMISSION_USB_PHY']
    # Optional native source exists only when the supplied native ELF actually
    # defines its product hook. Header presence alone is not capability proof.
    if (runtime/'sdk/driver/RiscDiagnosticSourceV1.h').is_file() and native_elf is not None:
        from elftools.elf.elffile import ELFFile
        symbols=ELFFile(io.BytesIO(native_elf)).get_section_by_name('.symtab')
        hooked=symbols and any(symbol.name=='risc_native_diagnostic_read' and
            symbol['st_shndx']!='SHN_UNDEF' for symbol in symbols.iter_symbols())
        if hooked:
            command += ['-DSTORE_ADMISSION_DIAGNOSTIC_SOURCE']
    if 'bool (*coldBoot)()' in (runtime/'src/bootstrap/Runtime.h').read_text():
        command += ['-DSTORE_ADMISSION_COLD_BOOT']
    # Match the selected native backend's advertised bound, including API2.
    # Historical Runtime sources without that backend keep their old fixture.
    native_kv=runtime/'src/ports/esp32s3/NvsKeyValue.h'
    if native_kv.is_file() and 'RISC_KEY_VALUE_V2_BLOB_MAX' in native_kv.read_text():
        command += ['-DSTORE_ADMISSION_KV_V2']
    if 'realtimeRead' in cpu_header and (runtime/'sdk/app/RiscRetainedWakeV1.h').is_file():
        command += ['-DSTORE_ADMISSION_RUNTIME_FEATURES']
    if 'radioIqReady' in cpu_header:
        command += ['-DSTORE_ADMISSION_RADIO_IQ']
    if 'radioIqPrepare' in cpu_header and 'radioIqCleanup' in cpu_header:
        command += ['-DSTORE_ADMISSION_IQ_LIFECYCLE']
    if 'radioJoin' in cpu_header:
        command += ['-DSTORE_ADMISSION_RADIO']
    if 'hciOpen' in cpu_header:
        command += ['-DSTORE_ADMISSION_HCI']
    if 'i2sOpenRx' in cpu_header:
        command += ['-DSTORE_ADMISSION_I2S_RX']
    if (runtime/'sdk/driver/RiscHttpClientV1.h').is_file() and (runtime/'sdk/driver/RiscBankStoreV1.h').is_file():
        command += ['-DSTORE_ADMISSION_UPDATE_PLATFORMS']
    command += ['-I' + str(p) for p in includes]
    command += [str(p) for p in sources]
    command += [str(ROOT / 'tests/runtime_store_admission.cpp'), '-ldl', '-o', str(output)]
    subprocess.run(command, check=True, timeout=180)
    return output


def archive_store(raw):
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError('Duplicate archive member')
        store = {name[6:]: archive.read(name) for name in names
                 if name.startswith('store/') and not name.endswith('/')}
    validate_paths(store)
    return store


def validate_paths(store):
    if not {'boot.json', 'board.json', 'default.json', 'default.elf'} <= set(store):
        raise ValueError('Complete production store required')
    for name in store:
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or str(path) != name or '\\' in name:
            raise ValueError('Unsafe store member: ' + name)


def unpack_image(raw, tool=None, expected_size=BOOTFS_SIZE):
    if expected_size not in (BOOTFS_SIZE, 0x510000):
        raise ValueError('Unknown SPIFFS geometry')
    if tool is None:
        files = read_image(raw, expected_size)
        validate_paths(files)
        return files
    tool = Path(tool).resolve()
    if sha(tool.read_bytes()) != MKSPIFFS_SHA256:
        raise ValueError('SPIFFS tool differs from pinned Arduino ESP32 binary')
    if len(raw) != expected_size:
        raise ValueError('Incorrect SPIFFS partition size')
    with tempfile.TemporaryDirectory(prefix='risc-spiffs-') as temporary:
        root = Path(temporary)
        image = root / 'bootfs.bin'
        image.write_bytes(raw)
        store = root / 'store'
        store.mkdir()
        subprocess.run([str(tool), '-u', str(store), '-p', '256', '-b', '4096',
                        '-s', str(expected_size), str(image)], check=True, timeout=60,
                       stdout=subprocess.DEVNULL)
        files = {p.relative_to(store).as_posix(): p.read_bytes()
                 for p in store.rglob('*') if p.is_file()}
    validate_paths(files)
    return files


def admit(harness, store, expected_error=None):
    validate_paths(store)
    before = store_digest(store)
    with tempfile.TemporaryDirectory(prefix='risc-store-') as temporary:
        root = Path(temporary)
        for name, data in store.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        result = subprocess.run([str(harness), str(root)], check=True, timeout=30,
                                capture_output=True, text=True)
        outcome = json.loads(result.stdout)
        after = {p.relative_to(root).as_posix(): p.read_bytes()
                 for p in root.rglob('*') if p.is_file()}
        if after != store:
            raise ValueError('Admission modified the actual store')
    if outcome['hardware_calls'] or outcome['storage_calls']:
        raise ValueError('Boot admission invoked native I/O')
    if expected_error is None:
        if not outcome['prepared']:
            raise ValueError('Production store admission failed: ' + str(outcome))
    elif outcome['prepared'] or outcome['error'] != expected_error:
        raise ValueError('Baseline did not fail with the expected admission error: ' + str(outcome))
    return dict(outcome, store_files=len(store), store_sha256=before)


def admit_many(runtime, stores, expected_error=None, app_data=False):
    with tempfile.TemporaryDirectory(prefix='risc-admission-') as temporary:
        harness = compile_harness(runtime, Path(temporary) / 'admit', app_data)
        results = [dict(label=label, **admit(harness, store, expected_error))
                   for label, store in stores]
    return results


def admit_cohort(runtime, native_elf, active, candidate, expected_valid=True, app_policy_rows=16):
    """Use the real Runtime comparison and native ELF checks without native I/O."""
    validate_paths(active); validate_paths(candidate)
    with tempfile.TemporaryDirectory(prefix='risc-cohort-admission-') as temporary:
        root = Path(temporary)
        harness = compile_harness(runtime, root / 'admit', True, native_elf, app_policy_rows)
        for label, files in [('active', active), ('candidate', candidate)]:
            for name, data in files.items():
                path = root / label / name
                path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
        result = subprocess.run([str(harness), str(root / 'active'), str(root / 'candidate')],
                                check=True, timeout=60, capture_output=True, text=True)
        outcome = json.loads(result.stdout)
        for label, files in [('active', active), ('candidate', candidate)]:
            after = {p.relative_to(root / label).as_posix(): p.read_bytes()
                     for p in (root / label).rglob('*') if p.is_file()}
            if after != files: raise ValueError('Cohort validation changed ' + label + ' files')
    if not outcome['prepared'] or outcome['cohort_validated'] is not expected_valid or outcome['hardware_calls'] or outcome['storage_calls']:
        raise ValueError('Production cohort admission failed: ' + str(outcome))
    return dict(outcome, active_store_sha256=store_digest(active), candidate_store_sha256=store_digest(candidate),
                native_elf_sha256=sha(native_elf), app_policy_rows=app_policy_rows, target_instructions_executed=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', required=True, type=Path)
    parser.add_argument('--archive', nargs='+', type=Path, default=[])
    parser.add_argument('--image', nargs='+', type=Path, default=[])
    parser.add_argument('--bin', nargs='+', type=Path, default=[])
    parser.add_argument('--mkspiffs', type=Path)
    parser.add_argument('--read-only-spiffs', action='store_true',
                        help='Decode the pinned SPIFFS format without running a packer')
    parser.add_argument('--expect-error')
    parser.add_argument('--expect-count', type=int)
    parser.add_argument('--preserved-store', type=Path)
    parser.add_argument('--current-pmu-sleep-repair', action='store_true',
                        help='Require exact PMU 0.5.3 custody; preserve all other baseline bytes')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.current_pmu_sleep_repair and not args.preserved_store:
        parser.error('--current-pmu-sleep-repair requires --preserved-store')
    if not (args.archive or args.image or args.bin):
        parser.error('At least one actual archive, image or BIN is required')
    if (args.image or args.bin) and not (args.mkspiffs or args.read_only_spiffs):
        parser.error('--mkspiffs or --read-only-spiffs is required for image/BIN extraction')
    if args.expect_count is not None and len(args.archive) + len(args.image) + len(args.bin) != args.expect_count:
        parser.error('Actual store input count differs from --expect-count')
    inputs, stores = [], []
    for path in [*args.archive, *args.image, *args.bin]:
        raw = path.read_bytes()
        if path in args.archive:
            store = archive_store(raw)
        else:
            if path in args.bin:
                if len(raw) != BIN_SIZE:
                    raise ValueError('Incorrect full BIN size')
                image = raw[BOOTFS_OFFSET:BOOTFS_OFFSET + BOOTFS_SIZE]
            else:
                image = raw
            store = unpack_image(image, args.mkspiffs)
        inputs.append({'file': path.name, 'sha256': sha(raw), 'size_bytes': len(raw)})
        if args.preserved_store:
            preserve_store(store, args.preserved_store, current_pmu=args.current_pmu_sleep_repair)
        stores.append((path.name, store))
    results = admit_many(args.runtime, stores, args.expect_error)
    record = {'schema': 1, 'runtime_source': subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=args.runtime, text=True).strip(),
        'expected_error': args.expect_error, 'inputs': inputs, 'results': results,
        'policy_substitutions': 0, 'physical_verification': 'pending',
        'preservation_overlays': ['pmu-sleep-0.5.3'] if args.current_pmu_sleep_repair else []}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    main()
