#!/usr/bin/env python3
import hashlib,json,subprocess
from pathlib import Path
R=Path('/workspace/shared/x4-latest-composition-058');P=Path('/workspace/shared/x4-latest-plan-058')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def asset(p):p=Path(p);return {'path':str(p),'bytes':p.stat().st_size,'sha256':sha(p)}
def source(p):
 p=Path(p);assert not subprocess.check_output(['git','-C',str(p),'status','--porcelain'],text=True).strip()
 return {'path':str(p),'commit':subprocess.check_output(['git','-C',str(p),'rev-parse','HEAD'],text=True).strip(),'tree':subprocess.check_output(['git','-C',str(p),'rev-parse','HEAD^{tree}'],text=True).strip()}
old=json.load(open('/workspace/shared/x4-wifi-ui-plan-057/assembly-spec.json'))
sources={k:source(v['path']) for k,v in old['sources'].items() if k in ['runtime','native_platform','system']}
for k,p in [('panel','/workspace/shared/x4-fast-panel-014-external-source'),('gt911',str(P/'gt911-source')),('buttons','/workspace/shared/ble-buttons-report-recovery-20261010/utilities')]:sources[k]=source(p)
compiled={}
panel=Path('/workspace/shared/x4-fast-panel-014-qualification');gt=Path('/workspace/shared/gt911-contact-order-proof');buttons=Path('/workspace/shared/ble-buttons-report-recovery-20261010')
compiled.update(json.load(open(panel/'target/build-receipt.json'))['source_dependencies'])
g=json.load(open(gt/'target-source-bound/qualification.json'))
for n,h in g['sdk_sha256'].items():compiled[str(gt/'ordered-final/sdk'/n)]=h
for n in ['minimal/drivers/x4pro_gt911/driver.c','minimal/drivers/x4pro_board_power/PowerReadyV1.h']:
 p=P/'gt911-source'/n;compiled[str(p)]=sha(p)
b=json.load(open(buttons/'target-x4/ble_buttons/x4-native-app.json'))
roots={'Source':buttons/'utilities','System':Path(sources['system']['path']),'CompiledSDK':buttons/'target-x4/sdk/include'}
for n,h in b['compiled_dependencies_sha256'].items():prefix,path=n.split('/',1);compiled[str(roots[prefix]/path)]=h
for p in [buttons/'target-x4/ble_buttons/catalog.c',buttons/'target-x4/ble_buttons/exports.map']:
 compiled[str(p)]=sha(p)
# Bind the selected source overlay as well as original compiled source closure.
for n in ['minimal/drivers/x4pro_uc8279_fast/driver.c','minimal/drivers/x4pro_uc8279_fast/manifest.json','minimal/drivers/x4pro_gt911/driver.c','minimal/drivers/x4pro_gt911/manifest.json']:
 compiled[str(R/n)]=sha(R/n)
for n,h in compiled.items():assert sha(n)==h,n
tools={}
for root in [R/'minimal/scripts',R/'minimal/recovery_tools',Path(old['tools']['watch'])/'scripts',Path(sources['runtime']['path'])/'scripts']:
 for p in root.rglob('*.py'):tools[str(p)]=sha(p)
for p in [gt/'rebuild-target.sh',Path(old['tools']['compiler']),Path(old['tools']['mkspiffs'])]:tools[str(p)]=sha(p)
modules={}
for ident,elf,meta,receipt,qs in [
 ('x4pro-uc8279-fast',panel/'target/driver.elf',panel/'target/manifest.json',panel/'target/build-receipt.json',[panel/'qualification.json']),
 ('x4pro-gt911',gt/'target-source-bound/driver.elf',gt/'target-source-bound/manifest.json',gt/'target-source-bound/qualification.json',[gt/'qualification.json',gt/'qualification-checkpoint.json']),
 ('ble_buttons',buttons/'target-x4/ble_buttons/ble_buttons.elf',buttons/'target-x4/ble_buttons/ble_buttons.json',buttons/'target-x4/ble_buttons/x4-native-app.json',[buttons/'qualification.json'])]:
 modules[ident]={'qualified':True,'scope_accepted':True,'elf':asset(elf),'manifest':asset(meta),'build_receipt':asset(receipt),'qualification_receipts':[asset(p) for p in qs]}
spec={'schema':'x4.latest-058-source-bound','scope':'exact-.57-latest-qualified','x4_source':source(R)['commit'],'sources':sources,'compiled_inputs':compiled,'tool_inputs':tools,'runtime':sources['runtime']['path'],'tools':old['tools']['watch'],'native':old['native_directory'],'native_platform':sources['native_platform']['path'],'native_assets':old['native'],'native_candidate':old['native']['candidate.json'],'baseline':asset('/workspace/shared/x4-wifi-ui-image-057/X4-0.1.57-Wifi-UI-full-0x0.bin'),'baseline_custody':asset('/workspace/shared/x4-wifi-ui-image-057/build-custody.json'),'modules':modules,'inclusions':['External fast panel 0.1.14 complementary directional overdrive','GT911 0.1.10 contact-before-motion ordering','BLE Buttons 0.1.22 queued-tap preservation','All44 other .57 modules, .57 native .2.2, full graph and grants byte-identical'],'excluded':['Unfinished Files sharing frontend/native coexistence cohort','Watch-only touch and power work','Unused Runtime .2.3/.2.4/.2.5 foundations'],'limitations':['No physical hardware verification','Optional 8 ms cadence idle fixture fails identically for panel .13 and .14; 1 ms, focused panel and production Light-idle qualification pass.'],'publication_mapping':{'runtime':'b25b1d467a557e8d693211cff2eff959eb9c79de','panel':'879ebef75b47f723655253ec04428f7a48e487b2','gt911':'6590cbdcd7203ac1c2e58c84225e34d8df8602a3','buttons':'e620160c48c11fb074378cd31a20ff8362a9659b','system':'8a75862929e4e83c8f66b3d58cc1c36d44830c84'}}
(P/'assembly-spec.json').write_text(json.dumps(spec,indent=2)+'\n')
print('Pinned',len(compiled),'compiled inputs and',len(tools),'tools')
