"""Selected X4 foreground build support; shared System owns all shell logic."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

SYSTEM='9f9393eaa9a11b32be0758ef4d503fa8583820a5'
RUNTIME='83cc1f8f661fb39c5028ce669887629de9423bf7'
EXPORTS={'app_main','app_module_init','app_module_fini','risc_resident_app_descriptor_v1'}
IMPORTS={'risc_runtime_get_api','memcpy','memset','memcmp','memmove','memchr','strcmp','strncmp','strlen','snprintf','malloc','calloc','free','strcpy'}
TIME=['PortableNativeTimeSource.c','PortableRealtimeClient.c','PortableTimeZone.c','PortableTimeZoneCatalog.c','PortableTimeZonePreference.c']

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def git(repo,*args):return subprocess.check_output(['git','-C',str(repo),*args],text=True).strip()
def run(cmd,**kw):return subprocess.run(list(map(str,cmd)),check=True,**kw)
def write(path,value):Path(path).write_text(json.dumps(value,indent=2)+'\n')
def exact(path,pin):
    if git(path,'rev-parse','HEAD')!=pin or git(path,'status','--porcelain','--untracked-files=no'):
        raise ValueError('Clean exact selected dependency required: '+str(path)+' @ '+pin)

def options(parser):
    parser.add_argument('--resident-shell-client',action='store_true',required=True)
    parser.add_argument('--raster-snapshot',action='store_true',help='Select bounded shared-System raster replay explicitly')
    parser.add_argument('--system-apps',type=Path,required=True)
    parser.add_argument('--system-revision',default=SYSTEM)
    parser.add_argument('--development-system',action='store_true',help='Review-only dirty System build; never final custody')
    parser.add_argument('--runtime',type=Path,required=True)
    parser.add_argument('--runtime-revision',default=RUNTIME,help='Explicit clean SDK source pin; legacy default remains unchanged')
    parser.add_argument('--display-sdk',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)

def prepare(a,p,utilities):
    system=a.system_apps.resolve();runtime=a.runtime.resolve();output=a.output.resolve()
    if a.development_system:
        if git(system,'rev-parse','HEAD')!=a.system_revision:raise ValueError('Development System base differs')
    else:exact(system,a.system_revision)
    exact(runtime,a.runtime_revision)
    output.mkdir(parents=True,exist_ok=True);inc=output/'sdk/include';inc.mkdir(parents=True,exist_ok=True)
    # Real copied staging avoids mutating frozen source headers via a symlink.
    for folder in (a.display_sdk,system/'lib/PortableApps/include',runtime/'sdk/app',utilities/'lib/Alarm/include'):
        for source in folder.glob('*.h'):shutil.copyfile(source,inc/source.name)
    for name in ('RiscDisplayOutputV1.h','RiscDisplayOutputPowerV1.h','RiscDisplayOutputMetricsV1.h','RiscDisplayOutputSnapshotV1.h'):
        shutil.copyfile(a.display_sdk/name,inc/name)
    for name in ('PaperPresentation.h','PaperFrame.h'):
        target=inc/name
        if target.exists():target.unlink()
    shutil.copytree(system/'lib/PortableApps/time',inc.parent/'time',dirs_exist_ok=True)
    sys.path.insert(0,str(system/'scripts'))
    spec=importlib.util.spec_from_file_location('selected_resident_quick',system/'scripts/portable_quick_build.py')
    shared=importlib.util.module_from_spec(spec);spec.loader.exec_module(shared)
    selected=argparse.Namespace(resident_shell_client=True,resident_shell_host=False,resident_policy=True,
      resident_runtime_sdk=runtime/'sdk/app',alarm_client=True,quick_actions=False,quick_radios=False,
      quick_usb_transfer=False,paper_transitions=False,home_app=None,
      raster_snapshot=getattr(a,'raster_snapshot',False))
    flags,sources=shared.configure(selected,p,system,output,inc)
    if sources:raise ValueError('Foreground unexpectedly has shared renderer sources')
    if getattr(a,'raster_snapshot',False) and '-DPORTABLE_RASTER_SNAPSHOT' not in flags:
        raise ValueError('Selected System builder does not support --raster-snapshot')
    cc=os.environ.get('NATIVE_APP_CC')
    if not cc:raise ValueError('Set NATIVE_APP_CC to existing pinned GCC8.4')
    compiler=subprocess.check_output([cc,'--version'],text=True).splitlines()[0]
    if '8.4.0' not in compiler or '2021r2-patch5' not in compiler:raise ValueError('Pinned GCC8.4 required')
    return dict(system=system,runtime=runtime,runtime_revision=a.runtime_revision,out=output,inc=inc,flags=flags,receipt=selected.resident_shell_receipt,cc=cc,compiler=compiler,utilities=utilities)

def build(c,root,name,version,defines,sources,grants,features):
    system=c['system'];out=c['out']/name;out.mkdir(exist_ok=True);elf=out/(name+'.elf')
    defines=list(dict.fromkeys([*defines,*c['flags']]))
    if any(x.startswith(('-DPORTABLE_QUICK_','-DPORTABLE_PAPER_TRANSITIONS','-DPORTABLE_APP_SLEEP_LOCAL','-DPORTABLE_X4_IDLE')) for x in defines):
        raise ValueError('Foreground cannot compile renderer or independent sleep: '+name)
    includes=[c['inc'],system/'lib/NativeApps/include',root/'lib/NativeApps/include',root/'lib/Bluetooth/include',root/'lib/Contexts/include',root/'Apps',system/'Apps']
    sources=[*sources,system/'lib/PortableApps/src/adapter.c',*[system/'lib/PortableApps/src'/n for n in TIME if n!='PortableNativeTimeSource.c' or name not in ('points_in_time','alarms','countdown')]]
    catalog=out/'catalog.c';catalog.write_text('#include "PortableApps.h"\nconst t5_app_manifest_t portable_catalog[1]={{.compatible=false}};\nconst unsigned portable_catalog_count=0;\n')
    mapping=out/'exports.map';mapping.write_text('{ global: '+'; '.join(sorted(EXPORTS))+'; local: *; };\n')
    command=[c['cc'],'-std=c11','-Os','-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden','-ffreestanding','-fno-builtin','-fstack-usage','-nostdlib','-nostartfiles','-shared','-Wl,--no-relax','-Wl,--hash-style=sysv','-Wl,--version-script='+str(mapping),'-Wall','-Wextra','-Werror',*defines,*['-I'+str(x) for x in includes],*sources,catalog,'-lgcc','-o',elf]
    run(command,cwd=out)
    symbols=subprocess.check_output([c['cc'].removesuffix('gcc')+'nm','-D',elf],text=True)
    imports={x.split()[-1] for x in symbols.splitlines() if ' U ' in ' '+x};exports={x.split()[-1] for x in symbols.splitlines() if len(x.split())>=3 and x.split()[-2] in ('T','D','B','R')}
    if not imports<=IMPORTS or exports!=EXPORTS:raise ValueError(('Unexpected target ABI',name,imports-IMPORTS,exports))
    all_symbols=subprocess.check_output([c['cc'].removesuffix('gcc')+'nm',elf],text=True)
    defined=[x.split()[-1] for x in all_symbols.splitlines() if len(x.split())>=3 and x.split()[-2]!='U']
    if any(x.startswith(('pqa_render','pqa_sheet','pqa_font')) for x in defined) or b'QUICK ACTIONS' in elf.read_bytes():raise ValueError('Foreground embeds shared controls')
    if defined.count('risc_resident_app_descriptor_v1')!=1:raise ValueError('Missing unique foreground descriptor')
    validator=c['out']/'validate-elf'
    if not validator.exists():run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(root/'test/native_apps/stubs'),'-I'+str(root/'lib/elf_loader/include'),root/'lib/elf_loader/src/esp_elf_validate.c',root/'test/native_apps/validate_test.c','-o',validator])
    run([validator,elf])
    grants=[{k:g[k] for k in ('capability','api','instance_id')} for g in grants]
    keys=[tuple(g.values()) for g in grants]
    if len(keys)!=len(set(keys)) or len(keys)>16:raise ValueError('Invalid grants')
    for g in grants:
        if g['capability']=='bluetooth.hci':g['instance_id']=16
    needs=[dict(capability=k,api=v) for k,v in dict.fromkeys((g['capability'],g['api']) for g in grants)]
    manifest=dict(type='application',id=name,version=version,architecture='xtensa-esp32s3',file_name=name+'.elf',entry='app_main',requires=needs)
    write(elf.with_suffix('.json'),manifest);write(out/(name+'.boot-policy.json'),dict(manifest=name+'.json',grants=grants))
    deps={}
    roots=[('CompiledSDK',c['inc']),('Source',root),('System',system)]
    for source in sources:
        output=subprocess.check_output([c['cc'],'-std=c11','-M',*defines,*['-I'+str(x) for x in includes],source],text=True).replace('\\\n',' ')
        for token in output.split()[1:]:
            path=Path(token).resolve()
            for label,base in roots:
                if path.is_relative_to(base):deps[label+'/'+str(path.relative_to(base))]=sha(path);break
    record=dict(schema=1,app=name,version=version,source_revision=git(root,'rev-parse','HEAD'),source_dirty=bool(git(root,'status','--porcelain')),system_source_revision=git(system,'rev-parse','HEAD'),system_dirty=bool(git(system,'status','--porcelain','--untracked-files=no')),runtime_source_revision=c['runtime_revision'],compiler=c['compiler'],resident_shell=c['receipt'],build_defines=defines,compile_command=list(map(str,command)),imports=sorted(imports),exports=sorted(exports),elf_sha256=sha(elf),elf_bytes=elf.stat().st_size,quick_render_definitions=0,requires=needs,required_grants=grants,features=features,compiled_dependencies_sha256=deps,build_helper_sha256=sha(Path(__file__)),sdk_sha256={p.name:sha(p) for p in c['inc'].glob('*.h')},utilities_source_revision=git(c['utilities'],'rev-parse','HEAD'),target_validation='passed',hardware_verified=False,installable=False)
    write(out/'x4-native-app.json',record)
    dest=out/'licenses';dest.mkdir(exist_ok=True)
    for label,repo in [('App',root),('System',system),('Runtime',c['runtime']),('Utilities',c['utilities'])]:shutil.copyfile(repo/'LICENSE',dest/(label+'-MIT.txt'))
    for folder in ('fonts','paper_fonts','settings_fonts','quick_fonts','time'):
        for source in (system/'lib/PortableApps'/folder).rglob('*'):
            if source.is_file() and ('LICENSE' in source.name or 'OFL' in source.name or source.name.endswith('SOURCES.json') or source.name=='TIMEZONE_PROVENANCE.json'):
                target=dest/folder/source.relative_to(system/'lib/PortableApps'/folder);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
    print(name+' '+version+': target, descriptor, zero shared renderers, ABI, loader PASS',flush=True)
    return record
