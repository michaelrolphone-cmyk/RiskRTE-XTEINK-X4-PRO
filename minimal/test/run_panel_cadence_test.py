#!/usr/bin/env python3
"""Run the actual System adapter, Runtime scheduler and X4 panel together.

GPIO and BUSY time are deterministic fixture inputs, not hardware measurements.
The optional baseline System checkout must be the immutable synchronous adapter.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
def run(args,**kwargs):
    try:
        return subprocess.run(list(map(str,args)),check=True,**kwargs)
    except subprocess.CalledProcessError as error:
        if error.stdout: print(error.stdout,flush=True)
        if error.stderr: print(error.stderr,flush=True)
        raise

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--runtime',type=Path,required=True)
    ap.add_argument('--reader',type=Path,required=True)
    ap.add_argument('--system',type=Path,required=True)
    ap.add_argument('--baseline-system',type=Path)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--sanitize',action='store_true')
    ap.add_argument('--ssd',action='store_true',help='Exercise normal SSD1677 provider polling')
    ap.add_argument('--refresh-ms',type=int,default=3,help='Modeled refresh latency; no hardware timing claim')
    args=ap.parse_args();runtime=args.runtime.resolve();args.output.mkdir(parents=True,exist_ok=True)
    if not 0<=args.refresh_ms<=9000:ap.error('--refresh-ms must be within0..9000')
    fixture=ROOT/'minimal/test/panel_cadence';results=[]
    source_inputs=[ROOT/'minimal/drivers/x4pro_panel/driver.c',ROOT/'minimal/interfaces/RiscDisplayOutputMetricsV1.h',
        ROOT/'minimal/test/panel_test.c',ROOT/'minimal/test/panel_ssd_async_test.inc',Path(__file__).resolve(),
        *sorted(fixture.glob('*')),
        args.system/'lib/PortableApps/src/adapter.c',args.system/'test/native_apps/portable_native_toolbar_test.c',
        runtime/'src/bootstrap/Runtime.cpp',runtime/'src/runtime/streams/AppStreamSessions.cpp',
        runtime/'src/runtime/streams/ProviderQueueHost.cpp',runtime/'src/runtime/drivers/ProviderGraphV2.cpp']
    source_inputs=[p for p in source_inputs if p.is_file()]
    source_before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in source_inputs}
    san=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie'] if args.sanitize else []
    with tempfile.TemporaryDirectory(prefix='panel-cadence-') as temp:
        work=Path(temp);sdk=work/'sdk'
        run(['python3',ROOT/'minimal/scripts/prepare_sdk.py','--runtime',runtime,'--reader',args.reader,'--output',sdk])
        includes=['-I'+str(p) for p in [sdk,runtime/'sdk/app',runtime/'sdk/driver',runtime/'sdk/hardware']]
        cc=[os.environ.get('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror',*san,*includes]
        run([*cc,'-c',fixture/'panel_model.c','-o',work/'panel.o'])
        run([*cc,'-fPIC','-shared','-fvisibility=hidden','-DCADENCE_PROVIDER',fixture/'modules.c','-o',work/'provider.elf'])
        run([*cc,'-fPIC','-shared','-fvisibility=hidden',fixture/'modules.c','-o',work/'app.elf'])
        for label,system,baseline in [('current',args.system,False),('baseline',args.baseline_system,True)]:
            if system is None:continue
            system=system.resolve();include=work/label/'include'
            shutil.copytree(system/'lib/PortableApps/include',include)
            for header in ['RiscRuntimeV1.h','RiscRealtimeV1.h','RiscKeyValueV1.h']:
                shutil.copyfile(runtime/'sdk/app'/header,include/header)
            # PortableTime includes a sibling ../time path.
            shutil.copytree(system/'lib/PortableApps/time',include.parent/'time')
            flags=['-DPANEL_BASELINE_ADAPTER'] if baseline else []
            adapter_cc=[os.environ.get('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror',*san]
            run([*adapter_cc,'-I'+str(include),'-I'+str(system/'lib/NativeApps/include'),
                '-DPORTABLE_NATIVE_TIME_TOOLBAR','-DPORTABLE_NATIVE_CUSTODY_FENCE',*flags,
                '-DPANEL_ADAPTER_FIXTURE="'+str(system/'test/native_apps/portable_native_toolbar_test.c')+'"',
                '-c',fixture/'adapter_bridge.c','-o',work/'adapter.o'])
            cpp=[os.environ.get('CXX','c++'),'-std=c++17','-O0' if args.sanitize else '-O1','-g','-Wall','-Wextra','-Werror',
                '-Wno-missing-field-initializers',*san,*includes,'-I'+str(runtime/'src'),
                '-I'+str(runtime/'lib/ArduinoJson/src'),'-I'+str(runtime/'test/drivers/stubs'),'-rdynamic']
            sources=[runtime/path for path in ['src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp','src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp',
                'src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp']]
            binary=args.output/(label+('-sanitized' if args.sanitize else ''))
            run([*cpp,*sources,fixture/'runtime_bridge.cpp',work/'panel.o',work/'adapter.o','-Wl,--wrap=free','-ldl','-o',binary])
            for cost in [0,1000,500]:
                for interval in [1,8,20,50]:
                    env=dict(os.environ,PANEL_APP_WAIT_MS=str(interval),PANEL_GPIO_WRITES_PER_MS=str(cost),ASAN_OPTIONS='detect_leaks=0')
                    env['PANEL_REFRESH_MS']=str(args.refresh_ms)
                    if args.ssd:env['PANEL_CONTROLLER_SSD']='1'
                    result=run([binary,work],env=env,capture_output=True,text=True,timeout=60)
                    data=json.loads(result.stdout);data['adapter']=label;results.append(data)
                    for frame in data['frames']:
                        metrics=frame.get('full',frame.get('partial'))
                        assert metrics['bytes']==(96000 if args.ssd else 120000) and metrics['state']==3
                        assert 1<=frame['max_requested_wait_ms']<=max(1,min(interval,4))
                        assert metrics['budget_ms']==[8,8]
                        assert frame['scheduler_waits']<=frame['scheduler_wait_ms']<=frame['scheduler_waits']*max(1,min(interval,4))
                        if not baseline:
                            assert frame['controller_polls']>0
                            assert frame['max_controller_gap_ms']<=interval+8
                    # This fixture intentionally exposes the old base-only display table.
                    # SSD requires explicit history seeding, unavailable through that prefix.
                    assert not data['frames'][0]['full']['partial']
                    assert data['frames'][1]['partial']['partial']==(not args.ssd)
                    print(json.dumps(data),flush=True)
    if args.baseline_system:
        for current in [r for r in results if r['adapter']=='current']:
            previous=next(r for r in results if r['adapter']=='baseline' and
                r['app_wait_ms']==current['app_wait_ms'] and r['gpio_writes_per_ms']==current['gpio_writes_per_ms'])
            for kind,before,after in zip(['full','partial'],previous['frames'],current['frames']):
                for field in ['bytes','gpio_writes','payload_polls','wire_hash','transfer_ms','damage','partial']:
                    assert before[kind][field]==after[kind][field],(field,before,after)
                assert after[kind]['elapsed_ms']<=before[kind]['elapsed_ms']+current['app_wait_ms']+8
    assert source_before=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in source_inputs},'Source changed during qualification'
    receipt={'hardware':'not run','controller':'ssd1677' if args.ssd else 'uc8279','modeled_refresh_ms':args.refresh_ms,'gpio_cost':'deterministic writes per simulated millisecond; zero means no GPIO time',
        'sources':source_before,'runs':results}
    (args.output/'evidence.json').write_text(json.dumps(receipt,indent=2)+'\n')

if __name__=='__main__':main()
