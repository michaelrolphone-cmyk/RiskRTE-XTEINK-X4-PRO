#!/usr/bin/env python3
"""Compile recovered HID/IQ/radio/telemetry source into fresh isolated outputs."""
import argparse,hashlib,importlib.util,json,os,shutil,subprocess,sys
from pathlib import Path

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def build(a):
    out=a.output.resolve();assert not out.exists(),'Fresh output required';out.mkdir(parents=True)
    cc=str(a.cc.resolve());prefix=cc.removesuffix('gcc');compiler=subprocess.check_output([cc,'--version'],text=True).splitlines()[0]
    assert '8.4.0' in compiler and '2021r2-patch5' in compiler
    sys.path.insert(0,str(a.drivers/'scripts'));from normalize_xtensa_relocations import normalize
    receipts={}
    def run(cmd):subprocess.run(list(map(str,cmd)),check=True)
    def finish(name,source,commands,depfiles,allowed,normal=True):
        target=out/name;elf=target/'driver.elf'
        if normal:normalize(elf)
        rows=[r.split() for r in subprocess.check_output([prefix+'readelf','--dyn-syms','--wide',str(elf)],text=True).splitlines()]
        imports={r[7] for r in rows if len(r)>=8 and r[4]=='GLOBAL' and r[6]=='UND'}
        exports={r[7] for r in rows if len(r)>=8 and r[4]=='GLOBAL' and r[6]!='UND' and r[3]=='FUNC'}
        assert imports<=set(allowed) and exports=={'t5_driver_get'},(name,imports,exports)
        data=elf.read_bytes();assert data[:7]==b'\x7fELF\x01\x01\x01' and data[16:20]==b'\x03\x00\x5e\x00'
        deps={}
        for dep in depfiles:
            for path in dep.read_text().replace('\\\n',' ').split(':',1)[1].split():
                p=Path(path).resolve();assert p.suffix not in ('.o','.elf','.bin','.a');deps[str(p)]=hashlib.sha256(p.read_bytes()).hexdigest()
        meta=json.loads((source/'manifest.json').read_text());(target/'manifest.json').write_text(json.dumps(meta,indent=2)+'\n')
        row={'id':meta['id'],'version':meta['version'],'compiler':compiler,'commands':[[str(x) for x in c] for c in commands],'source_dependencies':deps,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'imports':sorted(imports),'exports':sorted(exports),'old_product_inputs':False}
        (target/'build.json').write_text(json.dumps(row,indent=2)+'\n');receipts[name]=row
    # Reuse the pinned NimBLE source inventory checker, not its source/dist output default.
    hid=load('recovery_hid_inputs',a.hid/'scripts/build_ble_hid.py');includes,sources,pin=hid.inputs()
    target=out/'hid';target.mkdir();commands=[];objects=[];deps=[]
    flags=['-std=c11','-Os','-fPIC','-ffunction-sections','-fdata-sections','-fvisibility=hidden','-D_DEFAULT_SOURCE','-Dmalloc=hid_malloc','-Dcalloc=hid_calloc','-Drealloc=hid_realloc','-Dfree=hid_free','-mtext-section-literals','-mlongcalls','-ffreestanding','-fno-builtin']
    for i,source in enumerate(sources):
        obj=target/(str(i)+'.o');dep=target/(str(i)+'.d');objects.append(obj);deps.append(dep)
        warnings=['-w'] if source.is_relative_to(hid.VENDOR) else ['-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-parameter']
        cmd=[cc,*flags,*warnings,*['-I'+str(p) for p in includes],'-MMD','-MF',dep,'-c',source,'-o',obj];commands.append(cmd);run(cmd)
    mapping=target/'exports.map';mapping.write_text('{ global: t5_driver_get; local: *; };\n')
    cmd=[cc,*flags,'-shared','-nostdlib','-nostartfiles','-Wl,--no-relax','-Wl,--hash-style=sysv','-Wl,--exclude-libs,ALL','-Wl,--gc-sections','-Wl,--version-script='+str(mapping),*objects,'-lgcc','-o',target/'driver.elf'];commands.append(cmd);run(cmd)
    finish('hid',hid.SOURCE,commands,deps,{'memcpy','memset','memcmp','memmove','strlen','strcmp','strncat'})
    # IQ source/flags match its pinned canonical builder, with an explicit fresh output.
    target=out/'iq';target.mkdir();source=a.drivers/'Drivers/s3_radio_iq_v1';dep=target/'driver.d'
    cmd=[cc,'-std=c11','-Os','-fPIC','-Wall','-Wextra','-Werror','-mtext-section-literals','-mlongcalls','-fvisibility=hidden','-ffreestanding','-fno-builtin','-nostdlib','-nostartfiles','-shared','-I'+str(a.drivers/'sdk/driver'),'-I'+str(source),'-Wl,--no-relax','-Wl,--hash-style=sysv','-Wl,--exclude-libs,ALL','-MMD','-MF',dep,source/'driver.c','-lgcc','-o',target/'driver.elf'];run(cmd);finish('iq',source,[cmd],[dep],{'strcmp','memcpy','memset'})
    for folder,name in [('twatch_ble','ble'),('twatch_wifi','wifi')]:
        source=a.watch/'drivers'/folder;target=out/name;target.mkdir();dep=target/'driver.d'
        cmd=[cc,'-std=c11','-shared','-fPIC','-fvisibility=hidden','-nostdlib','-mlongcalls','-Os','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Wno-misleading-indentation','-Wno-unused-function','-Werror','-I'+str(a.watch/'sdk/driver'),'-I'+str(a.watch/'include'),'-Wl,--version-script='+str(a.watch/'exports.map')]
        if folder=='twatch_ble':cmd+=['-mtext-section-literals','-nostartfiles','-Wl,--no-relax','-Wl,--hash-style=sysv','-Wl,--exclude-libs,ALL']
        else:cmd+=['-Wl,-soname,driver.elf']
        cmd+=['-MMD','-MF',dep,source/'driver.c','-lgcc','-o',target/'driver.elf'];run(cmd);finish(name,source,[cmd],[dep],{'memcpy','memset','strcmp','strlen'},normal=False)
    # Preserve the historical flags-layout battery interface while using the selected shared ABI.
    sdk=out/'telemetry-sdk';sdk.mkdir()
    for folder in [a.drivers/'sdk/driver',a.system/'lib/PortableApps/include']:
        for p in folder.glob('*.h'):shutil.copyfile(p,sdk/p.name)
    for folder,name in [('ble_sensors','sensors'),('ble_telemetry','ble-telem'),('telemetry_battery','battery-telem'),('telemetry_broadcast','broadcast')]:
        source=a.utilities/'Services'/folder if name=='broadcast' else a.drivers/'Drivers'/folder
        target=out/name;target.mkdir();dep=target/'driver.d';mapping=target/'exports.map';mapping.write_text('{ global: t5_driver_get; local: *; };\n')
        headers=sdk if name in ('battery-telem','broadcast') else a.drivers/'sdk/driver'
        cmd=[cc,'-std=c11','-Os','-fPIC','-ffunction-sections','-fdata-sections','-fvisibility=hidden','-mtext-section-literals','-mlongcalls','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I'+str(headers),'-shared','-nostdlib','-nostartfiles','-Wl,--no-relax','-Wl,--hash-style=sysv','-Wl,--gc-sections','-Wl,--exclude-libs,ALL','-Wl,--version-script='+str(mapping),'-MMD','-MF',dep,source/('service.c' if name=='broadcast' else 'driver.c'),'-lgcc','-o',target/'driver.elf'];run(cmd);finish(name,source,[cmd],[dep],{'memcpy','memset','memcmp','memchr','strcmp','strlen'})
    (out/'build.json').write_text(json.dumps(receipts,indent=2)+'\n');print('Eight shared providers compiled from source')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('watch','drivers','hid','system','utilities','cc','output'):p.add_argument('--'+name,type=Path,required=True)
    build(p.parse_args())
