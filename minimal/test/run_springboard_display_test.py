#!/usr/bin/env python3
"""Actual Springboard display path, Runtime scheduler and selected fast provider.

Deterministic GPIO/SPI/time model; does not qualify physical hardware. The
radio/alarm/power helper paths are excluded from this display scheduling test.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]
def run(args,**kwargs):
    try:
        return subprocess.run(list(map(str,args)),check=True,**kwargs)
    except subprocess.CalledProcessError as e:
        if e.stderr: print(e.stderr,flush=True)
        raise
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('runtime','reader','system','output','catalog'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--expect-yield-service',action='store_true')
    p.add_argument('--sanitize',action='store_true')
    a=p.parse_args();r=a.runtime.resolve();s=a.system.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    f=ROOT/'minimal/test/springboard_display';sdk=out/'sdk'
    if not sdk.exists():run(['python3',ROOT/'minimal/scripts/prepare_sdk.py','--runtime',r,'--reader',a.reader,'--output',sdk])
    inc=out/'include';shutil.copytree(s/'lib/PortableApps/include',inc,dirs_exist_ok=True);shutil.copytree(s/'lib/PortableApps/time',out/'time',dirs_exist_ok=True)
    for name in ('RiscRuntimeV1.h','RiscRealtimeV1.h'):shutil.copyfile(r/'sdk/app'/name,inc/name)
    for name in ('RiscDisplayOutputV1.h','RiscDisplayOutputPowerV1.h','RiscDisplayOutputMetricsV1.h','RiscDisplayOutputSnapshotV1.h'):shutil.copyfile(sdk/name,inc/name)
    catalog=json.loads(a.catalog.read_text())['catalog']['apps']
    (out/'catalog.c').write_text('#include "PortableApps.h"\nconst t5_app_manifest_t portable_catalog[]={\n'+',\n'.join('{.display_name='+json.dumps(x['display_name'])+',.file_name='+json.dumps(x['file_name'])+',.icon='+json.dumps(x['icon'])+',.compatible=true}' for x in catalog)+'\n};\nconst unsigned portable_catalog_count='+str(len(catalog))+';\n')
    san=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie'] if a.sanitize else []
    includes=['-I'+str(x) for x in (sdk,r/'sdk/app',r/'sdk/driver',r/'sdk/hardware')]
    cc=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',*san]
    run([*cc,*includes,'-c',f/'panel_model.c','-o',out/'panel.o'])
    for provider in (False,True):
        run([*cc,*includes,'-fPIC','-shared','-fvisibility=hidden',*(['-DCADENCE_PROVIDER'] if provider else []),f/'modules.c','-o',out/('provider.elf' if provider else 'app.elf')])
    flags=['-D'+x for x in ('PORTABLE_NATIVE_TIME_TOOLBAR','PORTABLE_NATIVE_CUSTODY_FENCE','PORTABLE_INPUT_NAVIGATION','PORTABLE_PAPER_CROSSFADE','PORTABLE_PAPER_TRANSITIONS','PORTABLE_TOUCH_SCROLL','PORTABLE_APP_TOUCH_SCROLL','PORTABLE_SPRINGBOARD_TOUCH_SCROLL','PORTABLE_NOVA_UI','PORTABLE_STAGE_LOGS','PORTABLE_STAGE_DISPLAY_METRICS')]
    appcc=[*cc,'-I'+str(inc),'-I'+str(s/'lib/NativeApps/include'),*flags]
    run([*appcc,'-DPANEL_ADAPTER_FIXTURE="'+str(s/'test/native_apps/portable_native_toolbar_test.c')+'"','-c',f/'adapter_bridge.c','-o',out/'adapter.o'])
    run([*appcc,'-Dapp_main=springboard_controller_main','-c',s/'Apps/springboard.c','-o',out/'controller.o'])
    run([*appcc,'-c',out/'catalog.c','-o',out/'catalog.o'])
    # Match the existing Runtime sanitizer fixtures: unoptimized host C++ avoids
    # GCC's sanitizer/inlining false positive inside vendored ArduinoJson.
    cpp=['c++','-std=c++17','-O0' if a.sanitize else '-O1','-g','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',*san,*includes,'-I'+str(r/'src'),'-I'+str(r/'lib/ArduinoJson/src'),'-I'+str(r/'test/drivers/stubs'),'-rdynamic']
    src=[r/x for x in ('src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp','src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp','src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp')]
    binary=out/'test'
    run([*cpp,*src,f/'runtime_bridge.cpp',*[out/(x+'.o') for x in ('panel','adapter','controller','catalog')],'-ldl','-o',binary])
    results=[]
    for case in ('normal','slow-service','slow-log','nonblocking-log','missed-busy','observed-busy-gap','absent-busy','stuck-busy'):
        z=run([binary,out],env=dict(os.environ,DISPLAY_CASE=case,ASAN_OPTIONS='detect_leaks=0'),capture_output=True,text=True,timeout=30)
        d=json.loads(z.stdout);print(json.dumps(d),flush=True);results.append(d)
        provider=d['provider']
        io_fail=case=='slow-log' or (case=='slow-service' and a.expect_yield_service)
        busy_fail=case in ('missed-busy','absent-busy','stuck-busy')
        assert d['app_retained']==(io_fail or busy_fail)
        assert bool(d['completed_frames'])==(not (io_fail or busy_fail))
        assert provider['retained']==io_fail
        if io_fail:
            assert 'error=display-status' in d['app_failure'] and provider['deadline_rejections']==1
            assert 'spi exchange retained' in provider['diagnostic'] and provider['refresh_ms']==0
        if busy_fail:
            assert 'error=display-failed' in d['app_failure'] and provider['deadline_rejections']==0
            assert ('busy completion timeout' if case=='stuck-busy' else 'busy never asserted') in provider['diagnostic']
    (out/'evidence.json').write_text(json.dumps({'hardware':'not run','expect_yield_service':a.expect_yield_service,'sanitized':a.sanitize,'display_defines':flags,'catalog':catalog,'runs':results,'source_sha256':{str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in [*src,*sorted(f.glob('*')) ,Path(__file__).resolve(),s/'Apps/springboard.c',s/'Apps/springboard_scroll.inc',s/'lib/PortableApps/src/adapter.c',ROOT/'minimal/drivers/x4pro_uc8279_fast/driver.c']}},indent=2)+'\n')
if __name__=='__main__':main()
