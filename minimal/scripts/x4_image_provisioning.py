#!/usr/bin/env python3
"""Exact initial-only X4 .26 compact-image provisioning. No device operations."""
import argparse
import hashlib
import importlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import urllib.request

import prepare_native_runtime as composition
import provision_seed_extension as extension

ROOT=Path(__file__).resolve().parents[2]
REPOSITORY='michaelrolphone-cmyk/RiskRTE-XTEINK-X4-PRO'
PAYLOAD_PATH='provisioning/x4-0.1.26'
NAMES={'image.bin','seed.zip','LICENSES.zip','build-custody.json','payload.json','COMPLETE'}
MARKER=b'x4.image-provisioning.v1\n'
DEPLOYMENT_MARKER=b'x4.image-deployment.v1\n'
RUNTIME='317b74e363877cb6e5a90198211f897744e4a6d3'
PLATFORM='9d40063f3869bf543727e38d2c093b5efc99cf37'
TOOLS='ec8a2c78ebe8a4ea32efab895d5d7e25d7128e4d'
require=composition.require

def meta(raw):return {'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
def encoded(value):return (json.dumps(value,sort_keys=True,indent=2,allow_nan=False)+'\n').encode()
def contract():return json.loads((ROOT/'minimal/provisioning-image-026.json').read_bytes())

def modules(runtime,platform,tools):
    runtime,platform,tools=map(lambda p:Path(p).resolve(),(runtime,platform,tools))
    for root,pin in ((runtime,RUNTIME),(platform,PLATFORM),(tools,TOOLS)):composition.clean_source(root,pin)
    sys.path[:0]=[str(runtime/'scripts'),str(tools/'scripts')]
    result={name:importlib.import_module(name) for name in
            ('provision_device','provision_profile','store_image_capacity','watch_image_provisioning','check_runtime_store_admission')}
    for name,module in result.items():
        expected=tools if name in ('watch_image_provisioning','check_runtime_store_admission') else runtime
        require(Path(module.__file__).resolve().parent==expected/'scripts','Imported helper source differs')
    return result,extension.make_validator(runtime,platform)

def recipe(source):
    require(isinstance(source,str) and re.fullmatch('[0-9a-f]{40}',source),'Exact recipe commit required')
    require(composition.git(ROOT,'cat-file','-t',source)=='commit','Recipe must name a commit')
    composition.clean_source(ROOT)
    paths=['.',':(exclude)docs/**',':(exclude)provisioning/**',':(exclude).github/**']
    require(subprocess.run(['git','diff','--quiet',source,'--',*paths],cwd=ROOT).returncode==0,'Recipe executable/source custody differs')

def checked(runtime,platform,tools,raw,source):
    recipe(source);m,validator=modules(runtime,platform,tools)
    p=m['provision_profile'];w=m['watch_image_provisioning'];c=contract()
    for name,key in (('image.bin','image'),('LICENSES.zip','licenses'),('build-custody.json','build_custody')):
        require(meta(raw[name])==c[key],'Frozen '+name+' differs')
    seed=w.unpack(raw['seed.zip'],p)
    require({name:meta(data) for name,data in seed.items()}==c['seed_assets'],'Frozen generic seed differs')
    with tempfile.TemporaryDirectory(prefix='x4-initial-seed-') as temp:
        work=Path(temp);w.write_files(work/'input',seed)
        record,blobs=m['provision_device'].verify_seed(work/'input',work,validator)
    require(record['source_sha']==RUNTIME and record['firmware_version']=='0.1.75' and
            record['extension']['metadata']['build_options']=={'app_policy_rows':17,'app_image_cache':True},'Selected native options differ')
    require(b'RISC_PROVISION_IMAGE:1\0' in blobs['firmware.bin'] and b'RISC_PROVISION_IMAGE:1\0' in blobs['firmware.elf'],'Native image provisioning marker absent')
    files=w.read_image(raw['image.bin'],0x510000)
    require({name:meta(data) for name,data in files.items()}==c['files'],'Complete frozen store differs')
    require(len(files)==85 and sum(n.endswith('.elf') for n in files)==41 and
            'cohort_migration' not in p.decode(files['boot.json']),'Initial-only complete store required')
    cohort=p.decode(files['cohort.json']);require(cohort==c['cohort'],'Exact original product identity differs')
    require(cohort['source_revision']==PLATFORM and cohort['runtime_version']=='0.1.75' and
            cohort['firmware_size']==len(blobs['firmware.bin']) and cohort['firmware_sha256']==p.sha(blobs['firmware.bin']),'Native/store binding differs')
    custody=p.decode(raw['build-custody.json'])
    require(custody['x4_source']==PLATFORM and custody['store_files']==c['files'] and
            custody['extended_checks_skipped'] is False and custody['store_image_verified'] is True,'Original build custody differs')
    capacity=m['store_image_capacity'].inspect_image(raw['image.bin'],0x510000)
    admission=m['check_runtime_store_admission'].admit_cohort(Path(runtime),blobs['firmware.elf'],files,files,app_policy_rows=17)
    require(admission['elf_count']==41 and admission['cohort_validated'] and
            not admission['hardware_calls'] and not admission['storage_calls'],'Native store admission failed')
    receipt={'schema':'x4.image-provisioning','schema_version':1,'version':'0.1.26','recipe_source':source,
             'platform_source':PLATFORM,'runtime_source':RUNTIME,'tools_source':TOOLS,
             'layout':'riscrte-paired-appdata-v2','target':record['target'],'cohort':cohort,
             'image':meta(raw['image.bin']),'seed_zip':meta(raw['seed.zip']),'licenses':meta(raw['LICENSES.zip']),
             'build_custody':meta(raw['build-custody.json']),'files':c['files'],'capacity':capacity,'admission':admission,
             'scope':'Initial-only frozen QIO X4 0.1.26, 19 apps. Existing-device preservation uses a separate route. Hardware UNRUN.'}
    return receipt,files,seed,m,validator

def freeze(runtime,platform,tools,bundle,seed,licenses,output,source):
    m,_=modules(runtime,platform,tools);p=m['provision_profile'];w=m['watch_image_provisioning'];output=p.destination(output)
    require(composition.git(ROOT,'rev-parse','HEAD')==source,'Freeze requires the exact clean recipe HEAD')
    raw={'image.bin':p.read(Path(bundle)/'bootfs.bin',0x510000),'build-custody.json':p.read(Path(bundle)/'build-custody.json',2*1024*1024),
         'LICENSES.zip':p.read(licenses,2*1024*1024),'seed.zip':w.archive_bytes(w.read_seed(seed,p))}
    receipt,_,_,_,_=checked(runtime,platform,tools,raw,source)
    w.publish_files(output,{**raw,'payload.json':encoded(receipt),'COMPLETE':MARKER},p)
    return receipt

def verify(runtime,platform,tools,payload):
    m,_=modules(runtime,platform,tools);p=m['provision_profile'];w=m['watch_image_provisioning'];payload=p.safe_path(payload)
    require(w.directory_names(payload,6)==NAMES,'Payload inventory differs')
    raw={n:p.read(payload/n,32*1024*1024) for n in NAMES};require(raw['COMPLETE']==MARKER,'Incomplete payload')
    supplied=p.decode(raw['payload.json']);expected,files,seed,_,validator=checked(runtime,platform,tools,raw,supplied['recipe_source'])
    require(encoded(supplied)==encoded(expected),'Payload receipt differs')
    return expected,files,raw,seed,m,validator

def bind(runtime,platform,tools,payload,revision,output):
    receipt,files,raw,_,m,_=verify(runtime,platform,tools,payload);p=m['provision_profile'];w=m['watch_image_provisioning'];output=p.destination(output)
    require(re.fullmatch('[0-9a-f]{40}',revision) and composition.git(ROOT,'cat-file','-t',revision)=='commit','Immutable payload commit required')
    rows=subprocess.check_output(['git','ls-tree','-rz',revision,'--',PAYLOAD_PATH],cwd=ROOT).split(b'\0');names=set()
    for row in rows:
        if not row:continue
        props,name=row.split(b'\t',1);mode,kind,_=props.split(b' ')
        require(mode==b'100644' and kind==b'blob','Payload Git entry differs');names.add(name.decode())
    require(names=={PAYLOAD_PATH+'/'+n for n in NAMES},'Committed payload inventory differs')
    for name,data in raw.items():require(subprocess.check_output(['git','show',revision+':'+PAYLOAD_PATH+'/'+name],cwd=ROOT)==data,'Committed payload bytes differ')
    base=f'https://raw.githubusercontent.com/{REPOSITORY}/{revision}/{PAYLOAD_PATH}/'
    inventory={'schema':'riscrte.provisioning-inventory','schema_version':2,'layout':receipt['layout'],'runtime_target':receipt['target'],
               'image':{'url':base+'image.bin',**meta(raw['image.bin'])},'files':[{'path':n,**meta(b)} for n,b in sorted(files.items())]}
    p.verify_inventory(inventory,files)
    record={'schema':'x4.image-deployment','schema_version':1,'payload_revision':revision,'inventory_sha256':p.sha(p.encode(inventory)),
            'downloads':{n:{'url':base+n,**meta(b)} for n,b in sorted(raw.items())}}
    w.publish_files(output,{'inventory.json':p.encode(inventory),'deployment.json':encoded(record),'COMPLETE':DEPLOYMENT_MARKER},p)
    return record

def deployment_inputs(deployment,p,w):
    deployment=p.safe_path(deployment)
    require(w.directory_names(deployment,3)=={'inventory.json','deployment.json','COMPLETE'} and p.read(deployment/'COMPLETE',64)==DEPLOYMENT_MARKER,'Deployment inventory differs')
    inventory_raw=p.read(deployment/'inventory.json',128*1024);inventory=p.decode(inventory_raw);p.validate_inventory(inventory)
    record=p.decode(p.read(deployment/'deployment.json',65536));revision=record.get('payload_revision')
    require(set(record)=={'schema','schema_version','payload_revision','inventory_sha256','downloads'} and record['schema']=='x4.image-deployment' and
            type(record['schema_version']) is int and record['schema_version']==1 and isinstance(revision,str) and re.fullmatch('[0-9a-f]{40}',revision) and
            record['inventory_sha256']==p.sha(inventory_raw) and set(record['downloads'])==NAMES,'Deployment identity differs')
    base=f'https://raw.githubusercontent.com/{REPOSITORY}/{revision}/{PAYLOAD_PATH}/'
    require(inventory['image']==record['downloads']['image.bin'],'Deployment image differs')
    for name,item in record['downloads'].items():
        require(set(item)=={'url','bytes','sha256'} and item['url']==base+name and type(item['bytes']) is int and 0<item['bytes']<=32*1024*1024 and
                isinstance(item['sha256'],str) and re.fullmatch('[0-9a-f]{64}',item['sha256']),'Immutable download identity differs')
    return inventory,record

def deployment_product(inventory,record,receipt,files,raw,p):
    p.verify_inventory(inventory,files)
    require(inventory['layout']==receipt['layout'] and inventory['runtime_target']==receipt['target'] and
            {k:inventory['image'][k] for k in ('bytes','sha256')}==receipt['image'],'Deployment product identity differs')
    require(all({k:item[k] for k in ('bytes','sha256')}==meta(raw[name]) for name,item in record['downloads'].items()),'Deployment payload differs')

def endpoints(runtime,platform,tools,deployment):
    m,_=modules(runtime,platform,tools);p=m['provision_profile'];w=m['watch_image_provisioning']
    inventory,record=deployment_inputs(deployment,p,w);revision=record['payload_revision']
    opener=urllib.request.build_opener(w.NoRedirect)
    with tempfile.TemporaryDirectory(prefix='x4-https-') as temp:
        folder=Path(temp)
        for name,item in record['downloads'].items():
            with opener.open(item['url'],timeout=60) as response:
                require(response.status==200,'Download status differs');data=response.read(item['bytes']+1)
            require(meta(data)=={k:item[k] for k in ('bytes','sha256')},'Downloaded bytes differ');w.write_files(folder,{name:data})
        receipt,files,raw,_,_,_=verify(runtime,platform,tools,folder)
        deployment_product(inventory,record,receipt,files,raw,p)
    return {'verified_https_files':6,'complete_store_files':len(files),'payload_revision':revision,'device_accessed':False,'target_instructions_executed':False}

def snapshot_owner(owner,output,p,w):
    owner=p.safe_path(owner)
    require(w.directory_names(owner,4)=={'profile.json','owner.json','inputs','COMPLETE'},'Owner input inventory differs')
    require(w.directory_names(p.safe_path(owner/'inputs'),5)=={'profile.bin','descriptor.bin','time.bin','nvs.csv','COMPLETE'},'Owner NVS inventory differs')
    limits={'profile.json':16384,'owner.json':4096,'COMPLETE':64,'inputs/profile.bin':16384,
            'inputs/descriptor.bin':384,'inputs/time.bin':384,'inputs/nvs.csv':40000,'inputs/COMPLETE':64}
    w.write_files(output,{name:p.read(owner/name,bound) for name,bound in limits.items()})
    return output

def compose_device(runtime,platform,tools,payload,deployment,owner,validator,nvs_generator,output,new_device=False):
    receipt,files,raw,seed,m,callback=verify(runtime,platform,tools,payload)
    p=m['provision_profile'];w=m['watch_image_provisioning'];inventory,record=deployment_inputs(deployment,p,w)
    deployment_product(inventory,record,receipt,files,raw,p)
    with tempfile.TemporaryDirectory(prefix='x4-private-seed-') as temp:
        directory=Path(temp)/'seed';w.write_files(directory,seed)
        frozen=snapshot_owner(owner,Path(temp)/'owner',p,w)
        profile=p.read(frozen/'profile.json',16384);owner_receipt=p.decode(p.read(frozen/'owner.json',4096))
        require(owner_receipt.get('inventory_sha256')==p.sha(p.encode(inventory)),'Owner inventory differs from selected deployment')
        require(profile==p.profile_bytes(inventory,p.decode(profile).get('wifi')),'Owner profile differs from selected image/files/URL')
        return m['provision_device'].compose(directory,frozen,validator,nvs_generator,output,new_device,callback)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('runtime','platform','tools'):parser.add_argument('--'+name,type=Path,required=True)
    commands=parser.add_subparsers(dest='command',required=True)
    item=commands.add_parser('freeze')
    for name in ('bundle','seed','licenses','output'):item.add_argument('--'+name,type=Path,required=True)
    item.add_argument('--source',required=True)
    item=commands.add_parser('verify');item.add_argument('--payload',type=Path,required=True)
    item=commands.add_parser('bind');item.add_argument('--payload',type=Path,required=True);item.add_argument('--revision',required=True);item.add_argument('--output',type=Path,required=True)
    item=commands.add_parser('verify-endpoints');item.add_argument('--deployment',type=Path,required=True)
    item=commands.add_parser('device')
    for name in ('payload','deployment','owner','validator','nvs-generator','output'):item.add_argument('--'+name,type=Path,required=True)
    item.add_argument('--new-device',action='store_true',required=True)
    args=vars(parser.parse_args());command=args.pop('command')
    if command=='verify':result=verify(**args)[0]
    else:result={'freeze':freeze,'bind':bind,'verify-endpoints':endpoints,'device':compose_device}[command](**args)
    print(json.dumps(result,sort_keys=True,indent=2))
if __name__=='__main__':main()
