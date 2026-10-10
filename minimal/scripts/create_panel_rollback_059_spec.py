import hashlib,json,subprocess
from pathlib import Path
P=Path('/workspace/shared/x4-panel-rollback-plan-059');R=Path('/workspace/shared/x4-panel-rollback-composition-059')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def asset(p):p=Path(p);return {'path':str(p),'bytes':p.stat().st_size,'sha256':sha(p)}
def src(p):return {'path':str(p),'commit':subprocess.check_output(['git','-C',str(p),'rev-parse','HEAD'],text=True).strip(),'tree':subprocess.check_output(['git','-C',str(p),'rev-parse','HEAD^{tree}'],text=True).strip()}
s=json.load(open('/workspace/shared/x4-latest-plan-058/assembly-spec.json'))
ledgerpath=Path('/workspace/shared/x4-clean-product-build-055-r2/build-ledger.json');ledger=json.loads(ledgerpath.read_text());prefix='providers/x4/x4pro-uc8279-fast/'
elf=Path('/workspace/shared/x4-clean-product-build-055-r2')/(prefix+'driver.elf');meta=elf.with_name('manifest.json')
for p in [elf,meta]:
 row=ledger['artifacts'][prefix+p.name];assert asset(p)['sha256']==row['sha256'] and p.stat().st_size==row['bytes']
oldsrc=ledger['plan']['sources']['x4'];oldbytes=subprocess.check_output(['git','-C',oldsrc['path'],'show',oldsrc['commit']+':minimal/drivers/x4pro_uc8279_fast/driver.c']);assert oldbytes==(R/'minimal/drivers/x4pro_uc8279_fast/driver.c').read_bytes()
receipt={'schema':'source-backed-panel-013-restore','bytes':elf.stat().st_size,'sha256':sha(elf),'version':'0.1.13','original_clean_build_ledger':asset(ledgerpath),'original_build_source':oldsrc,'driver_sha256':hashlib.sha256(oldbytes).hexdigest(),'producer_step':next(x for x in ledger['steps'] if x['id']=='x4-providers'),'comparison':'Assembler requires compacted ELF and manifest equal exact delivered .57 panel'}
(P/'panel-013-custody.json').write_text(json.dumps(receipt,indent=2)+'\n')
s['scope']='exact-.58-panel-rollback';s['schema']='x4.panel-rollback-059-source-bound';s['x4_source']=src(R)['commit'];s['sources']={k:v for k,v in s['sources'].items() if k in ['runtime','native_platform']};s['sources']['panel_selection']=src(R)
s['modules']={'x4pro-uc8279-fast':{'qualified':True,'scope_accepted':True,'elf':asset(elf),'manifest':asset(meta),'build_receipt':asset(P/'panel-013-custody.json'),'qualification_receipts':[asset(ledgerpath),asset('/workspace/shared/x4-wifi-ui-image-057/build-custody.json')]}}
s['baseline']=asset('/workspace/shared/x4-latest-image-058/X4-0.1.58-Panel-0.1.14-Latest-full-0x0.bin');s['baseline_custody']=asset('/workspace/shared/x4-latest-image-058/build-custody.json');s['panel_reference_elf']=asset('/workspace/shared/x4-wifi-ui-image-057/store/panel/driver.elf');s['panel_reference_manifest']=asset('/workspace/shared/x4-wifi-ui-image-057/store/panel/manifest.json')
s['compiled_inputs']={str(R/'minimal/drivers/x4pro_uc8279_fast'/n):sha(R/'minimal/drivers/x4pro_uc8279_fast'/n) for n in ['driver.c','manifest.json']};s['compiled_inputs'][str(ledgerpath)]=sha(ledgerpath)
s['tool_inputs']={}
for root in [R/'minimal/scripts',R/'minimal/recovery_tools',Path(s['tools'])/'scripts',Path(s['runtime'])/'scripts']:
 for p in root.rglob('*.py'):s['tool_inputs'][str(p)]=sha(p)
s['inclusions']=['Exact .57 panel0.1.13 restored','All46 other .58 module ELFs/manifests and native unchanged, including GT9110.1.10 and Buttons0.1.22']
s['excluded']=[];s['limitations']=['User-requested panel-only causal comparison; no established attribution for reported touch latency','Physical hardware not tested'];s['publication_mapping']['panel']='restored source from exact delivered .57 composition';(P/'assembly-spec.json').write_text(json.dumps(s,indent=2)+'\n')
