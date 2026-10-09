#!/usr/bin/env python3
"""Explicit, source-bound X4 validator for the generic offline seed API.

Pass ``make_validator(runtime, platform_root)`` as ``extension_validator`` to
Runtime's provision_seed.candidate/compose or provision_device.verify_seed/
compose. Both roots must be clean checkouts of the exact composition commits.
The callback is supplied by trusted Python code; no candidate JSON can select
or import it. Generic rollback, TLS, IQ and app-policy validation stays upstream.
"""
from functools import partial
import io
import json
from pathlib import Path
import tempfile

from build_test_bundle import validate_native_composition
import prepare_native_runtime as composition

IDENTITY = 'x4.native-composition-v1'
ASSETS = ('x4-native-composition.json', 'x4-native-proof.json',
          'x4-runtime-options-proof.json')
METADATA = ('build_environment', 'build_options', 'stage_logs', 'x4_native_composition')
CORE_ASSETS = {'bootloader.bin', 'partitions.bin', 'firmware.bin', 'firmware.elf',
               'appdata.bin', 'appdata-image.json', 'radio-iq-proof.json'}
AUXILIARY_ASSETS = {'platformio.ini', 'partitions-paired.csv',
                    'partitions-paired-appdata.csv', 'requirements-ci.txt'}


def stage_log_proof(elf_data, enabled):
    """Recompute the same linked stage-logger proof as the native stager."""
    if not enabled:
        return {}
    from elftools.elf.elffile import ELFFile
    elf = ELFFile(io.BytesIO(elf_data))
    symbols = {symbol.name: symbol for symbol in elf.get_section_by_name('.symtab').iter_symbols()}
    sink = symbols.get('_ZN15RiscDiagnostics11timestampedEPKcz')
    composition.require(sink is not None and sink['st_shndx'] != 'SHN_UNDEF' and sink['st_size'] > 0,
                        'Plain stage logger absent')
    composition.require('_ZN8RiscPerf4dataE' not in symbols, 'Unexpected enabled performance recorder')
    return {'stage_logs': {'enabled': True, 'automatic': True, 'recorder': False, 'symbol': sink.name}}


def validate_extension(candidate, blobs, *, runtime, platform_root):
    """Validate frozen bytes and return only the permitted X4 extension."""
    require = composition.require
    required = set(ASSETS) | {'firmware.bin', 'firmware.elf'}
    assets = candidate.get('assets')
    require(isinstance(assets, dict) and required <= assets.keys() and required <= blobs.keys(),
            'X4 extension assets missing')
    require(set(assets) - CORE_ASSETS - AUXILIARY_ASSETS == set(ASSETS) and
            set(blobs) - CORE_ASSETS - AUXILIARY_ASSETS == set(ASSETS),
            'Unexpected X4 extension asset inventory')
    for name in required:
        data = blobs[name]
        require(type(data) is bytes and composition.encoded(assets[name]) == composition.encoded(
            {'bytes': len(data), 'sha256': composition.sha(data)}), 'X4 extension asset digest: ' + name)
    record = json.loads(blobs[ASSETS[0]])
    environment = record.get('build_environment')
    require(environment in (composition.ENVIRONMENTS[0], composition.ENVIRONMENTS[2]),
            'X4 seed requires base or plain stage-log native environment')
    enabled = environment == composition.ENVIRONMENTS[2]
    require(candidate.get('stage_logs') is enabled and candidate.get('performance_trace') is False,
            'X4 stage-log metadata mismatch')
    composition.validate_build_options(candidate.get('build_options'))

    # Reuse the product admission checks on these exact in-memory bytes. Its
    # filesystem interface is isolated from the caller's candidate directory;
    # auxiliary build config files are not retained by the generic seed API.
    frozen_candidate = dict(candidate, assets={name: assets[name] for name in required})
    with tempfile.TemporaryDirectory(prefix='x4-seed-extension-') as temporary:
        folder = Path(temporary)
        for name in required:
            (folder / name).write_bytes(blobs[name])
        summary = validate_native_composition(folder, frozen_candidate, runtime, platform_root)
    require(composition.encoded(candidate.get('x4_native_composition')) == composition.encoded(summary),
            'X4 composition summary differs from recomputed proof')
    for name, key in ((ASSETS[1], 'startup_proof'), (ASSETS[2], 'runtime_options_proof')):
        require(composition.encoded(json.loads(blobs[name])) == composition.encoded(summary[key]),
                'X4 proof sidecar differs from recomputed proof: ' + name)
    require(record['runtime']['source_date_epoch'] == int(composition.git(
        runtime, 'show', '-s', '--format=%ct', record['runtime']['commit'])),
        'X4 Runtime source epoch mismatch')
    require(record['platform']['repository'] == 'michaelrolphone-cmyk/RiskRTE-XTEINK-X4-PRO',
            'X4 platform repository mismatch')
    proof = stage_log_proof(blobs['firmware.elf'], enabled)
    actual = candidate.get('native_proof', {})
    require(isinstance(actual, dict) and composition.encoded(
        {key: actual[key] for key in ('stage_logs',) if key in actual}) == composition.encoded(proof),
        'X4 linked stage-log proof mismatch')
    return {'id': IDENTITY, 'native_proof': proof,
            'assets': {name: blobs[name] for name in ASSETS},
            'metadata': {name: candidate[name] for name in METADATA}}


def make_validator(runtime, platform_root=composition.ROOT):
    """Bind explicit source roots, never roots or executable paths from JSON."""
    return partial(validate_extension, runtime=Path(runtime).resolve(),
                   platform_root=Path(platform_root).resolve())
