#!/usr/bin/env python3
"""Cross-build the locked shared telemetry chain into isolated product outputs."""
import argparse,hashlib,importlib.util,json,os,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];pins=json.loads((ROOT/'minimal/apps/telemetry-sources.json').read_text())
p=argparse.ArgumentParser();p.add_argument('--system-apps',type=Path,required=True);p.add_argument('--utilities',type=Path,required=True);p.add_argument('--drivers',type=Path,required=True);p.add_argument('--output',type=Path,default=ROOT/'build/telemetry-providers');a=p.parse_args();system=a.system_apps.resolve();utilities=a.utilities.resolve();drivers=a.drivers.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
for name,repo in [('system_apps',system),('utilities',utilities),('drivers',drivers)]:
 if subprocess.check_output(['git','-C',repo,'rev-parse','HEAD'],text=True).strip()!=pins[name] or subprocess.check_output(['git','-C',repo,'status','--porcelain','--untracked-files=no'],text=True).strip():raise ValueError('Clean locked source required: '+name)
cc=os.environ['NATIVE_APP_CC'];compiler=subprocess.check_output([cc,'--version'],text=True).splitlines()[0];assert '8.4.0' in compiler and '2021r2-patch5' in compiler
spec=importlib.util.spec_from_file_location('normalize_telemetry',drivers/'scripts/normalize_xtensa_relocations.py');normalizer=importlib.util.module_from_spec(spec);spec.loader.exec_module(normalizer)
sdk=out/'include';sdk.mkdir(exist_ok=True)
for directory in [drivers/'sdk/driver',system/'lib/PortableApps/include']:
 for header in directory.glob('*.h'):shutil.copyfile(header,sdk/header.name)
validator=out/'validate';subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(system/'test/native_apps/stubs'),'-I'+str(system/'lib/elf_loader/include'),system/'lib/elf_loader/src/esp_elf_validate.c',system/'test/native_apps/validate_test.c','-o',validator],check=True)
for identity,source in [('telemetry-battery',drivers/'Drivers/telemetry_battery'),('ble-telemetry',drivers/'Drivers/ble_telemetry'),('telemetry-broadcast',utilities/'Services/telemetry_broadcast')]:
 dest=out/identity;dest.mkdir(exist_ok=True);mapping=dest/'exports.map';mapping.write_text('{ global: t5_driver_get; local: *; };\n');elf=dest/'driver.elf';src=source/('service.c' if identity=='telemetry-broadcast' else 'driver.c')
 subprocess.run([cc,'-std=c11','-Os','-fPIC','-ffunction-sections','-fdata-sections','-fvisibility=hidden','-mtext-section-literals','-mlongcalls','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I'+str(sdk),'-shared','-nostdlib','-nostartfiles','-Wl,--no-relax','-Wl,--hash-style=sysv','-Wl,--gc-sections','-Wl,--exclude-libs,ALL','-Wl,--version-script='+str(mapping),src,'-lgcc','-o',elf],check=True);normalizer.normalize(elf)
 syms=subprocess.check_output([cc.removesuffix('gcc')+'nm','-D',elf],text=True);imports={r.split()[-1] for r in syms.splitlines() if ' U ' in ' '+r};exports={r.split()[-1] for r in syms.splitlines() if len(r.split())>=3 and r.split()[-2] in ('T','D','B','R')};assert imports<={'memcpy','memset','memcmp','memchr','strcmp','strlen'} and exports=={'t5_driver_get'};subprocess.run([validator,elf],check=True)
 raw=elf.read_bytes();record={'schema':1,'source_revision':pins['utilities' if identity=='telemetry-broadcast' else 'drivers'],'source_dirty':False,'compiler':compiler,'size_bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'imports':sorted(imports),'exports':sorted(exports),'hardware_verified':False};(dest/'build-record.json').write_text(json.dumps(record,indent=2)+'\n');shutil.copyfile(source/'manifest.json',dest/'manifest.json')
 for notice in ([utilities/'LICENSE'] if identity=='telemetry-broadcast' else list(source.glob('LICENSE*'))):shutil.copyfile(notice,dest/notice.name)
 (dest/'SOURCE.json').write_text(json.dumps({'repository':'michaelrolphone-cmyk/RiscRTE-Utilities' if identity=='telemetry-broadcast' else 'michaelrolphone-cmyk/RiscRTE-Drivers','commit':record['source_revision'],'path':str(source.relative_to(utilities if identity=='telemetry-broadcast' else drivers))},indent=2)+'\n')
 print(identity+' '+record['sha256'],flush=True)
