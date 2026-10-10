#!/usr/bin/env python3
"""Actual fast0.1.13 provider and production Light idle helper, host only."""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
r=Path(__file__).resolve().parents[2];p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--candidate',type=Path,required=True);p.add_argument('--provider-sdk',type=Path,required=True);p.add_argument('--sleep-frame',type=Path);a=p.parse_args()
out=r/'build/idle-policy-panel';out.mkdir(parents=True,exist_ok=True);include=out/'include'
shutil.copytree(a.candidate/'idle-sdk/include',include,dirs_exist_ok=True)
# One canonical directory prevents pragma-once prefix/suffix duplication.
for path in a.provider_sdk.glob('*.h'):
 if path.name not in ('RiscStorageVolumeV1.h','RiscTouchV1.h','RiscTouchPowerV1.h'):shutil.copyfile(path,include/path.name)
assert json.loads((r/'minimal/drivers/x4pro_uc8279_fast/manifest.json').read_text())['version']=='0.1.13'
for san in (False,True):
 flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-DTEST_X4_IDLE_POLICY','-DPORTABLE_X4_IDLE_POLICY','-DPORTABLE_ALARM_CLIENT','-DALARM_SERVICE_TAGGED_V2']
 if san:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie']
 exe=out/('sanitized' if san else 'normal')
 subprocess.run([os.environ.get('CC','cc'),*flags,'-I'+str(include),str(r/'minimal/test/uc8279_fast_test.c'),str(r/'minimal/apps/portable_idle_sleep.c'),'-o',str(exe)],check=True)
 for case in ('policy-settle','policy-finalized','policy-frame','policy-finalized-frame')+(('policy-overlay-settled',) if a.sleep_frame else ()):
  subprocess.run([exe,case],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',SLEEP_OVERLAY_FRAME=str(a.sleep_frame or '')),check=True,timeout=20)
print('Actual fast panel/idle helper matrix PASS; hardware not tested')
