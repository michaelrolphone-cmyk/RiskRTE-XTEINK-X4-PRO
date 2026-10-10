#!/usr/bin/env python3
"""Run the actual System adapter, Runtime scheduler and X4 panel together.

GPIO and BUSY time are deterministic fixture inputs, not hardware measurements.
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
    except subprocess.CalledProcessError as exc:
        if exc.stderr:
            print(exc.stderr,flush=True)
        raise

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--runtime',type=Path,required=True)
    ap.add_argument('--reader',type=Path,required=True)
    ap.add_argument('--system',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--sanitize',action='store_true')
    ap.add_argument('--snapshot',action='store_true',help='Use the deployed sliced renderer and four-ms input service cadence')
    ap.add_argument('--paper-transitions',action='store_true',help='Pair with the selected LOW_LATENCY interactive adapter')
    args=ap.parse_args();runtime=args.runtime.resolve();args.output.mkdir(parents=True,exist_ok=True)
    fixture=ROOT/'minimal/test/uc8279_fast_cadence';results=[]
    san=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie'] if args.sanitize else []
    with tempfile.TemporaryDirectory(prefix='panel-cadence-') as temp:
        work=Path(temp);sdk=work/'sdk'
        run(['python3',ROOT/'minimal/scripts/prepare_sdk.py','--runtime',runtime,'--reader',args.reader,'--output',sdk])
        includes=['-I'+str(p) for p in [sdk,runtime/'sdk/app',runtime/'sdk/driver',runtime/'sdk/hardware']]
        cc=[os.environ.get('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',*san,*includes]
        run([*cc,'-c',fixture/'panel_model.c','-o',work/'panel.o'])
        run([*cc,'-fPIC','-shared','-fvisibility=hidden','-DCADENCE_PROVIDER',fixture/'modules.c','-o',work/'provider.elf'])
        run([*cc,'-fPIC','-shared','-fvisibility=hidden',fixture/'modules.c','-o',work/'app.elf'])
        for label,system,baseline in [('current',args.system,False)]:
            if system is None:continue
            system=system.resolve();include=work/label/'include'
            shutil.copytree(system/'lib/PortableApps/include',include)
            for header in ['RiscRuntimeV1.h','RiscRealtimeV1.h']:
                shutil.copyfile(runtime/'sdk/app'/header,include/header)
            # PortableTime includes a sibling ../time path.
            shutil.copytree(system/'lib/PortableApps/time',include.parent/'time')
            flags=['-DPANEL_BASELINE_ADAPTER'] if baseline else []
            if args.snapshot:flags.append('-DPORTABLE_RASTER_SNAPSHOT')
            if args.paper_transitions:flags.append('-DPORTABLE_PAPER_TRANSITIONS')
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
            for cost in [0]:
                for interval in [1,8,20,50]:
                    env=dict(os.environ,PANEL_APP_WAIT_MS=str(interval),PANEL_GPIO_WRITES_PER_MS=str(cost),ASAN_OPTIONS='detect_leaks=0')
                    result=run([binary,work],env=env,capture_output=True,text=True,timeout=60)
                    data=json.loads(result.stdout);data['adapter']=label;results.append(data)
                    for frame in data['frames']:
                        metrics=frame.get('full',frame.get('partial'))
                        assert metrics['state']==3
                        if metrics['partial']:
                            planes=2 if metrics.get('directional') else 1
                            assert metrics['bytes']==planes*(metrics['damage'][2]//8)*metrics['damage'][3]
                        else:
                            assert metrics['bytes']==180000
                        assert metrics['max_slice_bytes']<=16384 and metrics['max_slice_ms']<=8
                        assert metrics['gpio_writes']<200
                        assert 1<=frame['max_requested_wait_ms']<=(4 if args.snapshot else 1)
                        assert metrics['budget_ms']==[8,8]
                        assert frame['scheduler_waits']<=frame['scheduler_wait_ms']<=frame['scheduler_waits']*(4 if args.snapshot else 1)
                        if not baseline:
                            assert frame['controller_polls']>0
                            assert frame['max_controller_gap_ms']<=interval+8
                    assert not data['frames'][0]['full']['partial'] and data['frames'][1]['partial']['partial']
                    idle=data['idle']
                    assert idle['bytes']==180000 and idle['max_slice_bytes']<=16384
                    assert idle['repeats']==idle['completed_repeats'] and idle['repeats']>1
                    # Finalize both retained planes in bounded slices, then POF.
                    assert 2300<=idle['elapsed_ms']<=2300+20*interval+300
                    assert idle['provider_polls']>0 and idle['max_slice_ms']<=8
                    assert idle['controller_polls']>0 and idle['max_controller_gap_ms']<=interval+8
                    assert idle['touch_samples']>0 and idle['max_touch_gap_ms']<=max(20,interval)+8
                    assert data['after_settle']['scheduler_wait_ms']==interval
                    maintenance=data['maintenance']
                    assert maintenance['bytes']==0 and maintenance['max_slice_bytes']==0 and maintenance['repeats']==0
                    assert 30000-interval<=maintenance['elapsed_ms']<=30000+3*interval+20
                    assert maintenance['max_slice_ms']<=8 and maintenance['max_requested_wait_ms']==(min(4,interval) if args.snapshot else interval)
                    assert maintenance['controller_polls']>0 and maintenance['max_controller_gap_ms']<=interval+8
                    assert maintenance['touch_samples']>0 and maintenance['max_touch_gap_ms']<=max(20,interval)+8
                    print(json.dumps(data),flush=True)
    sources=[ROOT/'minimal/drivers/x4pro_uc8279_fast/driver.c',ROOT/'minimal/interfaces/RiscDisplayOutputMetricsV1.h',ROOT/'minimal/interfaces/RiscDisplayOutputSnapshotV1.h',
        ROOT/'minimal/interfaces/RiscFrontlightToneV1.h',ROOT/'minimal/interfaces/RiscDisplayOutputFrontlightV1.h',
        ROOT/'minimal/test/uc8279_fast_test.c',fixture/'panel_model.c',ROOT/'minimal/test/panel_cadence/adapter_bridge.c',
        args.system/'lib/PortableApps/src/adapter.c',
        runtime/'src/bootstrap/Runtime.cpp',runtime/'src/runtime/streams/AppStreamSessions.cpp',runtime/'src/runtime/streams/ProviderQueueHost.cpp',runtime/'src/runtime/drivers/ProviderGraphV2.cpp']
    receipt={'hardware':'not run','snapshot':args.snapshot,'paper_transitions':args.paper_transitions,'timing_model':'20 MHz payload clock only; 20 ms BUSY fixture; no SDK/CPU cost; not hardware timing',
        'sources':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},'runs':results}
    (args.output/'evidence.json').write_text(json.dumps(receipt,indent=2)+'\n')

if __name__=='__main__':main()
