#!/usr/bin/env python3
"""Stage qualified completed apps/providers over the frozen .37 store.

This is an integration store, not a flash image or publication receipt. The
final composer must bind the selected Springboard, native and cohort identity.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
from build_recovered_diagnostic import require, sha, encoded, inventory, digest_inventory

ROOT = Path(__file__).resolve().parents[2]

def read_json(path):
    return json.loads(path.read_text())

def stage(args):
    require(not args.output.exists(), 'Output already exists')
    spec = read_json(ROOT/'minimal/apps/consolidated-sources.json')
    baseline = read_json(args.baseline/'build-custody.json')
    require(baseline['source_revision']==spec['baseline']['source'], 'Wrong baseline source')
    files = inventory(args.baseline/'store')
    require(digest_inventory(files)==baseline['store_files'], 'Frozen baseline files differ')
    original = dict(files)
    receipts = {}
    def install(name, folder, receipt_name, expected, size_key, hash_key, source_key):
        receipt = read_json(folder/receipt_name)
        driver = '/' in name
        leaf = 'driver' if driver else name
        blob = (folder/(leaf+'.elf')).read_bytes()
        manifest = read_json(folder/('manifest.json' if driver else leaf+'.json'))
        require(receipt[source_key]==expected['source'], 'Source differs: '+name)
        require(not receipt.get('source_dirty',receipt.get('dirty',False)), 'Dirty source: '+name)
        require(manifest['version']==expected['version'], 'Version differs: '+name)
        require(len(blob)==receipt[size_key] and sha(blob)==receipt[hash_key], 'Receipt bytes differ: '+name)
        if 'elf_sha256' in expected:
            require(sha(blob)==expected['elf_sha256'], 'Qualified ELF differs: '+name)
        manifest_name = name.rsplit('/',1)[0]+'/manifest.json' if driver else name+'.json'
        if name+'.elf' in files:
            prior = json.loads(files[manifest_name])
            require(manifest['requires']==prior['requires'], 'Unexpected authority change: '+name)
        files[name+'.elf']=blob
        files[manifest_name]=encoded(manifest)
        receipts[name]={'receipt':receipt,'elf_sha256':sha(blob),'bytes':len(blob)}
        return manifest

    for app in ('ble_touchpad','ble_buttons'):
        install(app,args.hid_apps/app,'x4-native-app.json',spec['ready']['hid_apps'],
                'elf_bytes','elf_sha256','source_revision')
    install('hid/driver',args.hid_provider,'build-record.json',spec['ready']['hid_provider'],
            'size_bytes','sha256','source_revision')
    install('touch/driver',args.touch,'target-proof.json',spec['ready']['touch'],
            'elf_bytes','elf_sha256','source_revision')
    gb=read_json(args.gameboy/'build.json'); expected=spec['ready']['gameboy']
    blob=(args.gameboy/'gameboy.elf').read_bytes(); manifest=read_json(args.gameboy/'gameboy.json')
    require(gb['source']==expected['source'] and gb['dirty'] is False, 'GameBoy source differs')
    require(gb['elf']=={'size':len(blob),'sha256':sha(blob)} and sha(blob)==expected['elf_sha256'], 'GameBoy bytes differ')
    require(manifest['version']==expected['version'] and manifest['supported_file_types']==['.gb','.gbc'], 'GameBoy manifest differs')
    grants=expected['grants']
    require({x['capability'] for x in manifest['requires']}==set(grants) and all(x['api']==1 for x in manifest['requires']), 'GameBoy authority differs')
    files['gameboy.elf']=blob; files['gameboy.json']=encoded(manifest)
    boot=json.loads(files['boot.json'])
    require(not any(x['manifest']=='gameboy.json' for x in boot['app_capabilities']), 'Duplicate GameBoy policy')
    boot['app_capabilities'].append({'manifest':'gameboy.json','grants':[
        {'capability':x['capability'],'api':1,'instance_id':grants[x['capability']]}
        for x in manifest['requires']]})
    files['boot.json']=encoded(boot)
    receipts['gameboy']={'receipt':gb,'elf_sha256':sha(blob),'bytes':len(blob)}
    expected_changes={'ble_buttons.elf','ble_buttons.json','ble_touchpad.elf','ble_touchpad.json',
        'hid/driver.elf','hid/manifest.json','touch/driver.elf','touch/manifest.json',
        'gameboy.elf','gameboy.json','boot.json'}
    changed={name for name in files if files[name]!=original.get(name)}
    require(changed==expected_changes and len(files)==91, 'Unexpected store change')
    args.output.mkdir(parents=True)
    for name, blob in files.items():
        path=args.output/'store'/name; path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(blob)
    for name,folder in [('gameboy',args.gameboy),('hid-apps',args.hid_apps),('hid-provider',args.hid_provider),('touch',args.touch)]:
        dest=args.output/'inputs'/name; dest.mkdir(parents=True)
        for path in folder.rglob('*'):
            if path.is_file() and (path.suffix in ('.json','.elf') or 'licenses' in path.parts):
                rel=path.relative_to(folder); target=dest/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
    sys.path.insert(0,str(args.watch/'scripts'))
    from check_runtime_store_admission import admit_cohort
    admission=admit_cohort(args.runtime,args.native.read_bytes(),files,files,app_policy_rows=17)
    result={'schema':'x4.completed-app-integration','schema_version':1,
        'source_revision':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
        'baseline':spec['baseline'],'inputs':receipts,'changed_paths':sorted(changed),
        'store_files':digest_inventory(files),'admission':admission,
        'delivery_ready':False,'remaining':'Select matching 19-app Springboard and final native/cohort, then package and qualify'}
    (args.output/'integration.json').write_bytes(encoded(result))
    print(json.dumps({'files':len(files),'changed_paths':sorted(changed),'admission':admission}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('baseline','hid-apps','hid-provider','touch','gameboy','runtime','native','watch','output'):
        p.add_argument('--'+key,type=Path,required=True)
    stage(p.parse_args())
