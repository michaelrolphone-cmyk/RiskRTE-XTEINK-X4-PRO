#!/usr/bin/env python3
"""Run the production X4 idle helper with the production tagged alarm service."""
import argparse,json,os,subprocess
from pathlib import Path
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--system',type=Path,required=True);p.add_argument('--runtime',type=Path,required=True)
p.add_argument('--utilities',type=Path,required=True);p.add_argument('--candidate',type=Path,required=True)
a=p.parse_args();out=r/'build/idle-policy-tests';out.mkdir(parents=True,exist_ok=True)
include=a.candidate/'idle-sdk/include'
cases='prepare-unknown resume-unknown native-unsupported native-unknown ok off repeat refused held key-retained cancel-prepared panel-refused panel-retained panel-resume-retained touch-refused touch-retained touch-resume-retained sd-prepare-refused sd-commit-refused sd-retained sd-unavailable wifi-retained bt-retained acquire-retained expired api1 short tag descriptor-version output-modes features no-resume feature-mismatch no-status no-step no-refresh no-acknowledge no-prepare no-stop prepare-retained step-retained storage-retained native-retained unsupported-resume release-retained due near future resume-retained resume-rtc resume-backward ticket-stale descriptor-changed restore-failure'.split()
for san in (False,True):
 flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-DPORTABLE_ALARM_CLIENT','-DALARM_SERVICE_TAGGED_V2','-DPORTABLE_X4_IDLE_POLICY','-DPORTABLE_QUICK_ACTIONS']
 if san:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie']
 exe=out/('sanitized' if san else 'normal')
 subprocess.run([os.environ.get('CC','cc'),*flags,'-I'+str(include),'-I'+str(a.runtime/'sdk/app'),'-I'+str(a.runtime/'sdk/driver'),'-I'+str(a.utilities/'test/native_apps'),'-I'+str(a.utilities/'lib/Alarm/include'),str(r/'minimal/apps/portable_idle_sleep.c'),str(r/'minimal/test/idle_alarm_sleep_test.c'),str(a.system/'lib/PortableApps/src/PortableTimeZone.c'),str(a.system/'lib/PortableApps/src/PortableTimeZoneCatalog.c'),'-o',str(exe)],check=True)
 for case in cases:subprocess.run([exe,case],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),timeout=20)
print(f'{len(cases)} normal + {len(cases)} sanitizer production idle/alarm scenarios PASS')
