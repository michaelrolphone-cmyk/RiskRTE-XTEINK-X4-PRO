#!/usr/bin/env python3
"""Rebuild real SD custody/export regressions from explicit source checkouts."""
import argparse, hashlib, json, os, shutil, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
CASES = ('normal', 'unlock', 'log-close', 'absent', 'exported', 'writer-fault', 'matrix')
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime', type=Path, required=True)
    p.add_argument('--reader', type=Path, required=True)
    p.add_argument('--system', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    runtime, reader, system, out = (v.resolve() for v in (a.runtime, a.reader, a.system, a.output))
    out.mkdir(parents=True, exist_ok=True)
    sdk = out/'sdk'; sdk.mkdir(exist_ok=True)
    for part in ('hardware', 'driver'):
        for header in (runtime/'sdk'/part).glob('*.h'):
            shutil.copyfile(header, sdk/header.name)
    for name in ('RiscStorageVolumeV1.h', 'RiscStorageExportV1.h'):
        shutil.copyfile(reader/'sdk/driver'/name, sdk/name)
    shutil.copyfile(ROOT/'minimal/interfaces/RiscStorageVolumeStateV1.h', sdk/'RiscStorageVolumeStateV1.h')
    shutil.copyfile(system/'lib/PortableApps/include/CrashReportSd.h', sdk/'CrashReportSd.h')
    sources = [ROOT/'minimal/test/crash_report_sd_integration.c',
               reader/'Drivers/storage_fatfs/fatfs/ff.c', reader/'Drivers/storage_fatfs/fatfs/ffunicode.c']
    custody = sources + [ROOT/'minimal/test/sd_test.c', ROOT/'minimal/test/sdmmc_storage_test.inc',
        ROOT/'minimal/test/sd_export_test.inc', ROOT/'minimal/drivers/x4pro_sd/driver.c',
        ROOT/'minimal/drivers/x4pro_sd/BootLog.h', ROOT/'minimal/drivers/x4pro_sd/Export.h',
        reader/'Drivers/storage_fatfs/volume.c', reader/'Drivers/storage_fatfs/sd_protocol.h',
        reader/'Drivers/storage_fatfs/fatfs/ff.h', reader/'Drivers/storage_fatfs/fatfs/ffconf.h',
        reader/'test/storage_volume/fake/x4pro_mmio.h', ROOT/'Drivers/x4pro_board/x4pro_pins.h',
        ROOT/'Drivers/x4pro_board/x4pro_proto.h', ROOT/'minimal/drivers/x4pro_board_power/PowerReadyV1.h']
    before = {str(f): digest(f) for f in custody}
    runs = []
    for sanitized in (False, True):
        mode = 'sanitized' if sanitized else 'normal'; binary = out/('sd-crash-'+mode)
        flags = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-no-pie'] if sanitized else []
        command = [os.environ.get('CC', 'cc'), '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
            *flags, '-I'+str(sdk), '-I'+str(ROOT/'Drivers/x4pro_board'), '-I'+str(reader),
            '-I'+str(reader/'Drivers/storage_fatfs'), '-I'+str(reader/'Drivers/storage_fatfs/fatfs'),
            *map(str, sources), '-o', str(binary)]
        subprocess.run(command, check=True)
        for case in CASES:
            result = subprocess.run([str(binary), case], check=True, timeout=60, text=True, capture_output=True,
                env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
            print(mode, case, result.stdout.strip(), flush=True)
            runs.append({'mode': mode, 'case': case, 'result': result.stdout.strip()})
    assert before == {str(f): digest(f) for f in custody}, 'Input changed during qualification'
    (out/'evidence.json').write_text(json.dumps({'hardware_tested': False, 'old_results_reused': False,
        'scope': 'Real recovered SD provider and shared FatFs with simulated native card, GPIO, clock and locks',
        'source_sha256': before, 'sdk_sha256': {f.name: digest(f) for f in sdk.glob('*.h')}, 'runs': runs}, indent=2)+'\n')
if __name__ == '__main__':
    main()
