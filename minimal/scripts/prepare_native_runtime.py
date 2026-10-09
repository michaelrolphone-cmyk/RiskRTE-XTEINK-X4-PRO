#!/usr/bin/env python3
"""Compose and freeze X4 native boot code over immutable Runtime source.

This is offline build tooling, not firmware installation. The generated tree is
explicitly a two-repository composition and never impersonates a Runtime commit.
"""
import argparse
import configparser
from functools import lru_cache
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ENVIRONMENTS = ('esp32s3-16mb-appdata-iq', 'esp32s3-16mb-appdata-iq-perf', 'esp32s3-16mb-appdata-iq-stage')
NATIVE_FILES = ('X4EarlyBoot.cpp', 'X4BootRecord.h', 'build.py')
SCHEMA = 'x4.native-composition'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def encoded(value):
    return (json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n').encode()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def clean_source(root, expected=None):
    revision = git(root, 'rev-parse', 'HEAD')
    require(re.fullmatch('[a-f0-9]{40}', revision), 'Invalid source revision')
    require(expected is None or revision == expected, 'Source differs from exact requested commit')
    require(not git(root, 'status', '--porcelain', '--untracked-files=normal'), 'Clean committed source required')
    return {'commit': revision, 'tree': git(root, 'rev-parse', 'HEAD^{tree}')}


def safe_relative(name):
    path = PurePosixPath(name)
    require(name and not path.is_absolute() and '..' not in path.parts and str(path) == name,
            'Unsafe source path')
    return path


def snapshot(root, revision):
    archive = subprocess.check_output(['git', '-C', str(root), 'archive', '--format=tar', revision])
    files = {}
    with tarfile.open(fileobj=io.BytesIO(archive)) as tree:
        for member in tree:
            safe_relative(member.name)
            if member.isdir():
                continue
            require(member.isfile(), 'Non-regular tracked source: ' + member.name)
            files[member.name] = (tree.extractfile(member).read(), member.mode & 0o777)
    return files


def validate_build_options(options):
    require(isinstance(options, dict) and set(options) == {'app_policy_rows', 'app_image_cache'},
            'Invalid native build options')
    require(type(options['app_policy_rows']) is int and options['app_policy_rows'] in (16, 17),
            'App policy rows must be 16 or 17')
    require(type(options['app_image_cache']) is bool, 'App image cache must be boolean')
    return options


def compose(runtime, output, runtime_commit=None, environment=ENVIRONMENTS[0], platform_root=ROOT,
            app_policy_rows=16, app_image_cache=False):
    runtime, output, platform_root = (Path(p).resolve() for p in (runtime, output, platform_root))
    require(environment in ENVIRONMENTS, 'Unsupported X4 native environment')
    options = validate_build_options({'app_policy_rows': app_policy_rows, 'app_image_cache': app_image_cache})
    lock = json.loads((platform_root / 'minimal/sources.lock.json').read_text())['runtime']
    expected = runtime_commit or lock['commit']
    require(re.fullmatch('[a-f0-9]{40}', expected), 'An exact Runtime commit is required')
    require(not output.exists() and runtime not in output.parents and output != runtime and output != platform_root,
            'Output must be new and outside the Runtime checkout')
    native = clean_source(runtime, expected)
    platform = clean_source(platform_root)
    native['repository'] = lock['repository']
    native['source_date_epoch'] = int(git(runtime, 'show', '-s', '--format=%ct', expected))
    platform['repository'] = 'michaelrolphone-cmyk/RiskRTE-XTEINK-X4-PRO'
    files = snapshot(runtime, expected)
    original = {name: sha(data) for name, (data, _) in files.items()}
    config = configparser.ConfigParser(interpolation=None)
    config.read_string(files['platformio.ini'][0].decode())
    native['version'] = config.get('riscrte', 'version')
    require(re.fullmatch(r'\d+\.\d+\.\d+', native['version']), 'Invalid Runtime version')
    require(runtime_commit is not None or native['version'] == lock['version'], 'Runtime lock version mismatch')
    require(config.has_section('env:' + environment), 'Runtime environment missing')
    # This contract is also checked in the linked native proof. An older Runtime
    # cannot silently continue past a failed early pin operation.
    require(b'risc_native_startup_error' in files['src/main.cpp'][0], 'Runtime lacks the generic startup-status hook')
    require(b'risc_native_diagnostic_observer' in files.get('src/ports/esp32s3/SleepDiagnostics.cpp',(b'',0))[0], 'Runtime lacks the optional native diagnostic observer')
    for hook in (b'risc_native_diagnostic_drain', b'risc_native_diagnostic_read'):
        require(hook in files.get('src/ports/esp32s3/SleepDiagnostics.cpp',(b'',0))[0],
                'Runtime lacks the persistent diagnostic handoff: ' + hook.decode())
    require('sdk/driver/RiscDiagnosticSourceV1.h' in files, 'Runtime diagnostic source SDK missing')
    original_config = files['platformio.ini'][0]
    needle = b'pre:scripts/reproducible_build.py'
    require(needle in original_config, 'Pinned Runtime pre-build script entry missing')
    files['platformio.ini'] = (original_config.replace(needle, b'pre:x4-native/build.py'), files['platformio.ini'][1])
    sources = {}
    for name in NATIVE_FILES:
        relative = 'minimal/native/' + name
        path = platform_root / relative
        require(path.is_file() and not path.is_symlink(), 'Invalid X4 native source')
        data = path.read_bytes()
        sources[relative] = sha(data)
        destination = 'x4-native/' + name
        require(destination not in files, 'Runtime/X4 native path collision')
        files[destination] = (data, 0o644)
    composer = 'minimal/scripts/prepare_native_runtime.py'
    sources[composer] = sha((platform_root / composer).read_bytes())
    record = {'schema': SCHEMA, 'schema_version': 1, 'runtime': native, 'platform': platform,
              'build_environment': environment, 'build_options': options, 'platform_source_sha256': sources,
              'upstream_source_sha256': original,
              'composed_source_sha256': {name: sha(data) for name, (data, _) in files.items()}}
    record['composition_sha256'] = sha(encoded(record))
    output.mkdir(parents=True)
    try:
        for name, (data, mode) in files.items():
            target = output / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            target.chmod(mode)
        (output / 'x4-native-composition.json').write_bytes(encoded(record))
        verify_composition(output)
    except BaseException:
        shutil.rmtree(output)
        raise
    return record


def verify_composition(workspace):
    workspace = Path(workspace)
    record = json.loads((workspace / 'x4-native-composition.json').read_text())
    require(record.get('schema') == SCHEMA and record.get('schema_version') == 1, 'Invalid composition schema')
    payload = {k: v for k, v in record.items() if k != 'composition_sha256'}
    require(sha(encoded(payload)) == record.get('composition_sha256'), 'Composition digest mismatch')
    require(record.get('build_environment') in ENVIRONMENTS, 'Invalid composition environment')
    validate_build_options(record.get('build_options'))
    observed = set()
    for folder, directories, names in os.walk(workspace):
        if Path(folder) == workspace:
            directories[:] = [name for name in directories if name not in ('.pio', 'build', 'dist', '.cache')]
        require(not any((Path(folder) / name).is_symlink() for name in directories),
                'Symlinked composed source directory')
        observed.update((Path(folder) / name).relative_to(workspace).as_posix() for name in names)
    require(observed == set(record['composed_source_sha256']) | {'x4-native-composition.json'},
            'Composed source inventory differs')
    for name, digest in record['composed_source_sha256'].items():
        safe_relative(name)
        path = workspace / name
        require(path.is_file() and not path.is_symlink() and sha(path.read_bytes()) == digest,
                'Composed source mismatch: ' + name)
    return record


def verify_source_custody(runtime, record, platform_root=ROOT):
    validate_build_options(record.get('build_options'))
    require(clean_source(runtime, record['runtime']['commit'])['tree'] == record['runtime']['tree'],
            'Runtime tree differs')
    platform = Path(platform_root)
    require(clean_source(platform, record['platform']['commit'])['tree'] == record['platform']['tree'],
            'X4 platform tree differs')
    original = snapshot(runtime, record['runtime']['commit'])
    require({name: sha(data) for name, (data, _) in original.items()} == record['upstream_source_sha256'],
            'Upstream source inventory differs from committed Runtime')
    expected = {name: digest for name, digest in record['upstream_source_sha256'].items()}
    expected['platformio.ini'] = sha(original['platformio.ini'][0].replace(
        b'pre:scripts/reproducible_build.py', b'pre:x4-native/build.py'))
    source_names = {'minimal/native/' + name for name in NATIVE_FILES} | {'minimal/scripts/prepare_native_runtime.py'}
    require(set(record['platform_source_sha256']) == source_names, 'Unexpected X4 source inventory')
    for name in source_names:
        data = (platform / name).read_bytes()
        require(sha(data) == record['platform_source_sha256'][name], 'Committed X4 source differs: ' + name)
        if name.startswith('minimal/native/'):
            expected['x4-native/' + Path(name).name] = sha(data)
    require(expected == record['composed_source_sha256'], 'Unapproved Runtime overlay')


def elf_symbol_bytes(elf, symbol):
    require(isinstance(symbol['st_shndx'], int), 'ELF symbol lacks a loaded section')
    section = elf.get_section(symbol['st_shndx'])
    offset = symbol['st_value'] - section['sh_addr']
    require(section['sh_type'] != 'SHT_NOBITS' and offset >= 0 and
            offset + symbol['st_size'] <= section['sh_size'], 'ELF symbol exceeds loaded section')
    return section.data()[offset:offset + symbol['st_size']]


@lru_cache(maxsize=4)
def verified_xtensa_objdump(path):
    version = subprocess.check_output([path, '--version'], text=True).splitlines()[0]
    require(version == 'GNU objdump (crosstool-NG esp-2021r2-patch5) 2.35.1.20201223',
            'X4 proof requires the pinned esp-2021r2-patch5 Xtensa objdump')
    return path


def xtensa_objdump():
    explicit = os.environ.get('X4_XTENSA_OBJDUMP')
    if explicit:
        return verified_xtensa_objdump(explicit)
    compiler = os.environ.get('NATIVE_DRIVER_CC', '')
    candidates = [compiler[:-3] + 'objdump'] if compiler.endswith('gcc') else []
    command = 'xtensa-esp32s3-elf-objdump'
    if shutil.which(command):
        candidates.append(shutil.which(command))
    core = Path(os.environ.get('PLATFORMIO_CORE_DIR', Path.home() / '.platformio'))
    candidates.append(str(core / 'packages/toolchain-xtensa-esp32s3/bin' / command))
    for candidate in candidates:
        if Path(candidate).is_file():
            return verified_xtensa_objdump(candidate)
    raise ValueError('Pinned Xtensa objdump unavailable; set X4_XTENSA_OBJDUMP or NATIVE_DRIVER_CC')


def xtensa_instructions(elf, symbol):
    """Use assembler metadata to distinguish instructions from embedded padding."""
    address, data = symbol['st_value'], elf_symbol_bytes(elf, symbol)
    cache = getattr(elf, '_x4_instruction_cache', None)
    if cache is None:
        cache = elf._x4_instruction_cache = {}
    key = (address, len(data))
    if key in cache:
        return cache[key]
    position = elf.stream.tell()
    try:
        elf.stream.seek(0)
        with tempfile.TemporaryDirectory(prefix='x4-elf-proof-') as directory:
            path = Path(directory) / 'firmware.elf'
            path.write_bytes(elf.stream.read())
            output = subprocess.check_output([xtensa_objdump(), '-d', '-z',
                '--start-address=' + hex(address), '--stop-address=' + hex(address + len(data)),
                str(path)], text=True)
    finally:
        elf.stream.seek(position)
    require('file format elf32-xtensa-le' in output, 'Unexpected Xtensa disassembly format')
    instructions, consumed = [], 0
    for line in output.splitlines():
        match = re.match(r'^\s*([0-9a-f]+):\s+([0-9a-f]+)\s+(.+)$', line)
        if not match:
            continue
        pc = int(match[1], 16)
        raw = bytes.fromhex(match[2])[::-1]
        require(pc == address + consumed and raw and data[consumed:consumed + len(raw)] == raw,
                'Xtensa disassembly differs from ELF instruction bytes')
        operation = match[3].split(None, 1)
        instructions.append((pc, raw, operation[0], operation[1] if len(operation) > 1 else ''))
        consumed += len(raw)
    require(consumed == len(data), 'Xtensa disassembly does not cover the complete caller')
    cache[key] = instructions
    return instructions


def xtensa_calls(elf, symbols, name):
    """Inspect pinned CALL8 / adjacent L32R + CALLX8 at verified boundaries."""
    symbol = symbols.get(name)
    require(symbol is not None and symbol['st_size'], 'Missing startup caller: ' + name)
    instructions = xtensa_instructions(elf, symbol)
    calls = []
    for index, (pc, raw, mnemonic, operands) in enumerate(instructions):
        if mnemonic == 'call8':
            require(len(raw) == 3 and raw[0] & 63 == 0x25, 'Invalid decoded Xtensa CALL8')
            immediate = int.from_bytes(raw, 'little') >> 6
            if immediate & (1 << 17):
                immediate -= 1 << 18
            target = (pc & ~3) + 4 + immediate * 4
            require(target == int(operands.split()[0], 16), 'Xtensa CALL8 target mismatch')
            calls.append({'instruction': pc, 'target': target})
            continue
        if mnemonic != 'l32r' or index + 1 == len(instructions):
            continue
        require(len(raw) == 3 and raw[0] & 15 == 1, 'Invalid decoded Xtensa L32R')
        reg = raw[0] >> 4
        next_pc, next_raw, next_mnemonic, next_operands = instructions[index + 1]
        if next_mnemonic != 'callx8':
            continue
        decoded_reg = re.fullmatch(r'a(\d+)', next_operands)
        require(next_pc == pc + 3 and decoded_reg and int(decoded_reg[1]) < 16 and
                next_raw == bytes((0xe0, int(decoded_reg[1]), 0)), 'Invalid decoded Xtensa CALLX8')
        if int(decoded_reg[1]) != reg:
            continue  # The adjacent literal loads an argument, not the call target.
        # L32R always uses a negative 18-bit word displacement. Its high two
        # bits are implicit ones, including when encoded imm16 has bit15 clear.
        # Treating imm16 as signed16 fails for literals more than 128 KiB back.
        immediate = int.from_bytes(raw[1:], 'little') - 65536
        literal = ((pc + 3) & ~3) + immediate * 4
        decoded = re.match(r'a(\d+),\s*([0-9a-f]+)(?:\s|$)', operands)
        require(decoded and int(decoded[1]) == reg and int(decoded[2], 16) == literal,
                'Xtensa L32R literal address mismatch')
        for section in elf.iter_sections():
            if section['sh_type'] != 'SHT_NOBITS' and section['sh_addr'] <= literal and \
                    literal + 4 <= section['sh_addr'] + section['sh_size']:
                start = literal - section['sh_addr']
                target = int.from_bytes(section.data()[start:start + 4], 'little')
                calls.append({'instruction': pc, 'literal': literal, 'target': target})
                break
        else:
            raise ValueError('X4 startup instruction/literal is outside a loaded section')
    return calls


def runtime_options_proof(blobs, record):
    """Prove the compiled selection, not only flags or an echoed build request."""
    from elftools.elf.elffile import ELFFile
    options = validate_build_options(record.get('build_options'))
    rows = options['app_policy_rows']
    marker = ('RISC_APP_POLICY_ROWS:' + str(rows)).encode() + b'\0'
    other = ('RISC_APP_POLICY_ROWS:' + str(33 - rows)).encode() + b'\0'
    for name in ('firmware.bin', 'firmware.elf'):
        require(marker in blobs[name] and other not in blobs[name], 'Compiled app policy row mismatch: ' + name)
    elf = ELFFile(io.BytesIO(blobs['firmware.elf']))
    symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    policy = symbols.get('risc_app_policy_rows')
    require(policy is not None and policy['st_info']['bind'] == 'STB_GLOBAL' and
            elf_symbol_bytes(elf, policy) == marker, 'Missing compiled policy-row symbol')
    # These owner/Runtime atomics and creation call exist only in the enabled
    # Runtime implementation. The loader's cached-open API exists even when off
    # and is therefore insufficient evidence by itself.
    state = ('_ZN12_GLOBAL__N_120imagePressureRuntimeE', '_ZN12_GLOBAL__N_118imagePressureOwnerE')
    functions = ('risc_runtime_reclaim_app_images', '_ZN8RiscBoot7Runtime16reclaimAppImagesEv',
                 'esp_dl_image_cache_create')
    cache = {'enabled': options['app_image_cache'], 'implementation_symbols': {}, 'target_call_edges': {}}
    if options['app_image_cache']:
        for name in state + functions:
            symbol = symbols.get(name)
            require(symbol is not None and isinstance(symbol['st_shndx'], int) and symbol['st_size'] > 0,
                    'Enabled app image cache implementation missing: ' + name)
            section = elf.get_section(symbol['st_shndx'])
            if name in state:
                require(symbol['st_size'] == elf.elfclass // 8 and section['sh_flags'] & 2,
                        'Invalid app image cache owner state: ' + name)
            else:
                require(symbol['st_info']['bind'] == 'STB_GLOBAL' and section['sh_flags'] & 4,
                        'Missing strong app image cache function: ' + name)
            cache['implementation_symbols'][name] = {'bytes': symbol['st_size'], 'section': section.name}
            if name in functions:
                cache['implementation_symbols'][name]['sha256'] = sha(elf_symbol_bytes(elf, symbol))
        if elf['e_machine'] == 'EM_XTENSA':
            edges = (('_ZN8RiscBoot7Runtime3runEv', 'esp_dl_image_cache_create'),
                     ('risc_runtime_reclaim_app_images', '_ZN8RiscBoot7Runtime16reclaimAppImagesEv'),
                     ('_ZN8RiscBoot7Runtime16reclaimAppImagesEv', 'esp_dl_image_cache_reclaim'))
            for caller, callee in edges:
                target = symbols.get(callee)
                require(target is not None, 'Missing app image cache callee: ' + callee)
                matches = [call for call in xtensa_calls(elf, symbols, caller) if call['target'] == target['st_value']]
                require(len(matches) == 1, 'Unproven app image cache call: ' + caller + ' -> ' + callee)
                cache['target_call_edges'][caller + ' -> ' + callee] = matches[0]
    else:
        absent = state + ('esp_dl_image_cache_create', '_ZN8RiscBoot7Runtime16reclaimAppImagesEv')
        require(not any(name in symbols and symbols[name]['st_shndx'] != 'SHN_UNDEF' for name in absent),
                'Unexpected enabled app image cache implementation')
        cache['absent_enabled_symbols'] = list(absent)
    return {'schema': 'x4.runtime-options-proof', 'schema_version': 1,
            'composition_sha256': record['composition_sha256'], 'build_options': options,
            'elf_sha256': sha(blobs['firmware.elf']), 'firmware_sha256': sha(blobs['firmware.bin']),
            'app_policy': {'rows': rows, 'live_app_grants': 16, 'manifest_requirements': 16,
                           'marker': marker[:-1].decode(), 'symbol': 'risc_app_policy_rows'},
            'app_image_cache': cache, 'hardware_qualified': False}


def startup_proof(elf_data, record):
    from elftools.elf.elffile import ELFFile
    elf = ELFFile(io.BytesIO(elf_data))
    symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    names = ('initVariant', '__wrap_app_main', 'app_main', 'risc_x4_boot_record',
             'risc_native_startup_error', 'risc_native_diagnostic_observer', 'risc_native_diagnostic_drain',
             'risc_native_diagnostic_read', 'risc_x4_native_composition_identity')
    for name in names:
        symbol = symbols.get(name)
        require(symbol is not None and symbol['st_shndx'] != 'SHN_UNDEF' and
                symbol['st_info']['bind'] == 'STB_GLOBAL', 'Missing strong X4 native symbol: ' + name)
    marker = ('X4_NATIVE_COMPOSITION:' + record['composition_sha256']).encode() + b'\0'
    require(marker in elf_data, 'Missing compiled X4 composition identity')
    linked = {}
    if elf['e_machine'] == 'EM_XTENSA':
        # The pinned compiler uses CALL8 and literal-loaded CALLX8 long calls.
        # Check the actual entry edges, not merely the existence of a wrapper.
        edges = [('main_task', '__wrap_app_main'), ('__wrap_app_main', 'app_main'),
                 ('app_main', 'initArduino'),
                 ('_ZN15RiscDiagnostics4lineEPKc','risc_native_diagnostic_observer')]
        for caller, callee in edges:
            target = symbols.get(callee)
            require(target is not None, 'Missing startup callee: ' + callee)
            matches = [edge for edge in xtensa_calls(elf, symbols, caller) if edge['target'] == target['st_value']]
            require(len(matches) == 1, 'Unproven X4 startup call: ' + caller + ' -> ' + callee)
            linked[caller + ' -> ' + callee] = matches[0]
        require(not any(edge['target'] == symbols['app_main']['st_value'] for edge in xtensa_calls(elf, symbols, 'main_task')),
                'IDF main_task bypasses X4 startup wrapper')
        rtc = symbols['risc_x4_boot_record']
        section = elf.get_section(rtc['st_shndx'])
        require(section.name == '.rtc_noinit' and rtc['st_size'] == 264,
                'X4 reset breadcrumb is not in the retained RTC no-init section')
    return {'schema': 'x4.native-startup-proof', 'schema_version': 1,
            'composition_sha256': record['composition_sha256'], 'elf_sha256': sha(elf_data),
            'required_strong_symbols': list(names), 'pin': 1, 'initial_level': 1,
            'hold': True, 'startup_status': 'risc_native_startup_error',
            'entry_hook': '__wrap_app_main', 'target_call_edges': linked,
            'earliest_scope': 'IDF app_main; after IDF hardware/PSRAM/core initialization',
            'rtc_record_bytes': 264,
            'persistent_capture': 'app-main-after-existing-rail-before-Arduino',
            'sd_export_path': '/x4-boot.log', 'sd_export_owner': 'x4pro-sd',
            'sd_export_start': 'after validated provider activation and successful FatFs mount',
            'rom_and_rail_failure_coverage': False,
            'hardware_qualified': False}


def load_shared(runtime, name):
    scripts = Path(runtime) / 'scripts'
    sys.path.insert(0, str(scripts))
    spec = importlib.util.spec_from_file_location('x4_native_' + name, scripts / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def stage(runtime, workspace, output, appdata):
    runtime, workspace, output = (Path(p).resolve() for p in (runtime, workspace, output))
    record = verify_composition(workspace)
    verify_source_custody(runtime, record)
    require(not output.exists() and output != workspace and workspace not in output.parents,
            'Candidate output must be new and outside the build workspace')
    shared = load_shared(runtime, 'paired_candidate')
    image_tool = load_shared(runtime, 'app_data_image')
    iq_tool = load_shared(runtime, 'radio_iq_proof')
    initial = image_tool.verify_initial(Path(appdata))
    environment = record['build_environment']
    build = workspace / '.pio/build' / environment
    blobs = {name: shared.file_bytes(build / name) for name in
             ('firmware.bin', 'firmware.elf', 'bootloader.bin', 'partitions.bin')}
    for name in ('firmware.bin', 'bootloader.bin'):
        shared.esp_image(blobs[name])
        require(blobs[name][3] >> 4 == 4, 'Native image must declare 16 MiB flash')
    shared.elf(blobs['firmware.elf'])
    shared.partitions(blobs['partitions.bin'], shared.APP_DATA_EXPECTED)
    require(sha(blobs['bootloader.bin']) == shared.BOOTLOADER_SHA256, 'Unreviewed bootloader binary')
    target = ENVIRONMENTS[0]
    require(target.encode() + b'\0' in blobs['firmware.bin'], 'Native target mismatch')
    native = record['runtime']
    markers = ('RTE_SOURCE=' + native['commit'], 'RISC_RUNTIME_VERSION:' + native['version'],
               'RISC_PAIRED_STORE_ABI:2', 'X4_NATIVE_COMPOSITION:' + record['composition_sha256'])
    for marker in markers:
        require(all(marker.encode() + b'\0' in blobs[name] for name in ('firmware.bin', 'firmware.elf')),
                'Native source/version/composition marker mismatch')
    require(b'RISC_PAIRED_STORE_ABI:1\0' not in blobs['firmware.bin'] and
            len(blobs['firmware.bin']) <= shared.APP_DATA_EXPECTED['app0'][3], 'Native ABI/size mismatch')
    proof = shared.native_proof(blobs['firmware.elf'])
    proof['radio_iq'] = iq_tool.prove(blobs['firmware.elf'])
    proof['app_policy'] = shared.policy_rows_proof(blobs,record['build_options']['app_policy_rows'])
    performance = environment.endswith('-perf')
    if performance:
        from elftools.elf.elffile import ELFFile
        symbols = {s.name: s for s in ELFFile(io.BytesIO(blobs['firmware.elf'])).get_section_by_name('.symtab').iter_symbols()}
        recorder = symbols.get('_ZN8RiscPerf4dataE')
        require(recorder is not None and recorder['st_shndx'] != 'SHN_UNDEF' and recorder['st_size'] >= 4096,
                'Performance recorder absent')
        proof['performance_trace'] = {'enabled': True, 'recorder_bytes': recorder['st_size'], 'symbol': '_ZN8RiscPerf4dataE'}
    plain_stages = environment.endswith('-stage')
    if plain_stages:
        from elftools.elf.elffile import ELFFile
        symbols = {s.name: s for s in ELFFile(io.BytesIO(blobs['firmware.elf'])).get_section_by_name('.symtab').iter_symbols()}
        sink=symbols.get('_ZN15RiscDiagnostics11timestampedEPKcz')
        require(sink is not None and sink['st_shndx'] != 'SHN_UNDEF' and sink['st_size'] > 0,
                'Plain stage logger absent')
        require('_ZN8RiscPerf4dataE' not in symbols, 'Unexpected enabled performance recorder')
        proof['stage_logs']={'enabled':True,'automatic':True,'recorder':False,'symbol':sink.name}
    x4_proof = startup_proof(blobs['firmware.elf'], record)
    options_proof = runtime_options_proof(blobs, record)
    blobs['radio-iq-proof.json'] = encoded(proof['radio_iq'])
    blobs['x4-native-proof.json'] = encoded(x4_proof)
    blobs['x4-native-composition.json'] = encoded(record)
    blobs['x4-runtime-options-proof.json'] = encoded(options_proof)
    for name in ('platformio.ini', 'partitions-paired-appdata.csv', 'requirements-ci.txt'):
        blobs[name] = (workspace / name).read_bytes()
    for name in ('appdata.bin', 'appdata-image.json'):
        blobs[name] = (Path(appdata) / name).read_bytes()
    candidate = {'schema': 1, 'target': target, 'build_environment': environment, 'build_options': record['build_options'],
                 'performance_trace': performance, 'stage_logs':plain_stages,'source_sha': native['commit'],
                 'firmware_version': native['version'], 'layout': 'riscrte-paired-appdata-v2',
                 'store_abi': 2, 'flash_bytes': 0x1000000, 'partitions': shared.APP_DATA_EXPECTED,
                 'native_proof': proof, 'initial_appdata': initial,
                 'x4_native_composition': {'composition_sha256': record['composition_sha256'],
                                           'runtime': native, 'platform': record['platform'],
                                           'platform_source_sha256': record['platform_source_sha256'],
                                           'build_options': record['build_options'],
                                           'runtime_options_proof': options_proof,
                                           'startup_proof': x4_proof},
                 'assets': {name: {'bytes': len(data), 'sha256': sha(data)} for name, data in blobs.items()},
                 'scope': 'X4 platform-owned early boot linked over immutable Runtime source. Empty app-data is only for explicit new installation. No device action or hardware qualification.'}
    blobs['candidate.json'] = encoded(candidate)
    blobs['SHA256SUMS'] = ''.join(f'{sha(data)}  {name}\n' for name, data in sorted(blobs.items())).encode()
    output.mkdir(parents=True)
    try:
        for name, data in blobs.items():
            (output / name).write_bytes(data)
    except BaseException:
        shutil.rmtree(output)
        raise
    return candidate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    prepare = actions.add_parser('prepare')
    prepare.add_argument('--runtime', type=Path, required=True)
    prepare.add_argument('--output', type=Path, required=True)
    prepare.add_argument('--runtime-commit')
    prepare.add_argument('--environment', choices=ENVIRONMENTS, default=ENVIRONMENTS[0])
    prepare.add_argument('--app-policy-rows', type=int, choices=(16, 17), default=16,
                         help='Immutable app policy rows; live grants and manifest requirements remain 16')
    prepare.add_argument('--app-image-cache', action='store_true',
                         help='Explicitly enable the Runtime app image cache and qualified pressure retry paths')
    freeze = actions.add_parser('stage')
    for name in ('runtime', 'workspace', 'output', 'appdata'):
        freeze.add_argument('--' + name, type=Path, required=True)
    args = vars(parser.parse_args())
    action = args.pop('action')
    result = compose(**args) if action == 'prepare' else stage(**args)
    print('Verified X4 native ' + action + ': ' +
          (result['composition_sha256'] if action == 'prepare' else result['assets']['firmware.bin']['sha256']))


if __name__ == '__main__':
    main()
