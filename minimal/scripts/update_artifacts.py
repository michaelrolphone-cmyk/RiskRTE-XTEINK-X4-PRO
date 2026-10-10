"""Local X4 OTA artifacts. No tag, release, upload, device or feed mutation."""
import hashlib
import json
import re
import shutil
from pathlib import Path

REPOSITORY='michaelrolphone-cmyk/RiskRTE-XTEINK-X4-PRO'
PRODUCT='xteink-x4-pro'
LAYOUT='riscrte-paired-appdata-v2'
SERVICES={'firmware':('software-update-firmware','software.update.firmware'),
          'apps':('software-update-apps','software.update.apps')}
def sha(data):return hashlib.sha256(data).hexdigest()
def encoded(value):return (json.dumps(value,sort_keys=True,indent=2)+'\n').encode()
def require(condition,reason):
 if not condition:raise ValueError(reason)
def validate_provider(kind,manifest,blob,record,source,source_routes=False):
 require(type(source_routes) is bool and (not source_routes or kind=='firmware'), 'Source routes require the explicit firmware provider')
 version='0.1.5' if source_routes else '0.1.4'
 identity,cap=SERVICES[kind]
 require(manifest.get('type')=='driver' and manifest.get('driver_abi')==2 and
  manifest.get('id')==identity and manifest.get('version')==version and record.get('version')==version and
  manifest.get('file_name')=='driver.elf' and manifest.get('architecture')=='xtensa-esp32s3',
  'Update provider identity mismatch')
 require(manifest.get('provides')==[{'capability':cap,'api':1}], 'Update action mismatch')
 require(manifest.get('requires')==[{'capability':cap,'api':1} for cap in
  ('platform.http-client','platform.bank-store','platform.clock')], 'Update provider raw authority mismatch')
 require(record.get('repository_commit')==source and record.get('working_tree_dirty') is False and
  record.get('sha256')==sha(blob) and record.get('size_bytes')==len(blob), 'Update provider source/ELF mismatch')
 require(record.get('update_policy')=={'product':PRODUCT,'repository':REPOSITORY,
  'catalog_url':'','feed_configured':False,'runtime_only':False}, 'X4 feed policy differs from selected product')
 require('-DUPDATE_PRODUCT_X4' in record.get('build_defines',[]) and
  '-DUPDATE_FIRMWARE='+str(int(kind=='firmware')) in record['build_defines'], 'Update provider build selection mismatch')
 routes={flag for flag in record['build_defines'] if flag.startswith('-DUPDATE_SOURCE_ROUTES')}
 require(routes==({'-DUPDATE_SOURCE_ROUTES=1'} if source_routes else set()), 'Update route selection mismatch')
 return {'manifest':manifest,'build_record':record}
def validate_app(name,manifest,blob,record,source):
 cap='software.update.firmware' if name=='ota_update' else 'software.update.apps'
 require(name in ('ota_update','app_store') and manifest.get('id')==name, 'Unknown update app')
 require(record.get('repository_commit')==source and record.get('working_tree_dirty') is False and
  record.get('sha256')==sha(blob) and record.get('size_bytes')==len(blob), 'Update app source/ELF mismatch')
 needs={(r['capability'],r['api']) for r in manifest.get('requires',[])}
 require((cap,1) in needs and all(c==cap for c,v in needs if c.startswith('software.update.')), 'Update app action authority mismatch')
 require(record.get('update_policy')=={'product':PRODUCT,'repository':REPOSITORY,'catalog_url':'','feed_configured':False,'runtime_only':False}, 'Update app product policy mismatch')
 flags=record.get('build_defines',[])
 require(all(flag in flags for flag in ('-DPORTABLE_UPDATE_FEED_DISABLED','-DPORTABLE_NATIVE_TIME_TOOLBAR','-DALARM_SERVICE_TAGGED_V2','-DPORTABLE_WIFI_INSTANCE=15','-DPORTABLE_DISPLAY_ROTATION=90')), 'Update app deployment defines mismatch')
 return dict(record)
def stage_providers(store,output,inputs,source,firmware_routes_source=None):
 require(firmware_routes_source is None or isinstance(firmware_routes_source,str) and
  re.fullmatch('[0-9a-f]{40}',firmware_routes_source), 'Exact source-routes provider revision required')
 custody={}
 for kind in SERVICES:
  src=Path(inputs['update_'+kind]);folder='upd-fw' if kind=='firmware' else 'upd-app'
  manifest=json.loads((src/'manifest.json').read_text());blob=(src/'driver.elf').read_bytes()
  record=json.loads((src/'build-record.json').read_text())
  routes=kind=='firmware' and firmware_routes_source is not None
  custody[kind]=validate_provider(kind,manifest,blob,record,firmware_routes_source if routes else source,routes)
  dest=store/folder;dest.mkdir();(dest/'manifest.json').write_bytes(encoded(manifest));(dest/'driver.elf').write_bytes(blob)
  evidence=output/'build-records/providers'/manifest['id'];evidence.mkdir(parents=True)
  (evidence/'build-record.json').write_bytes(encoded(record))
  require((src/'licenses/System-Apps-LICENSE.txt').is_file(),'Update provider license missing')
  shutil.copytree(src/'licenses',output/'licenses/providers'/manifest['id'],dirs_exist_ok=True)
 return custody,[{'manifest':folder+'/manifest.json'} for folder in ('upd-fw','upd-app')]
def create(output,cohort,firmware,store_image,files,usb):
 """Called only after full Runtime store/ELF admission by the bundle composer."""
 require(re.fullmatch(r'[0-9a-f]{40}',cohort.get('source_revision','')) and
  re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)',cohort.get('version','')),'Unpinned or nonnumeric cohort identity')
 require(cohort.get('product')==PRODUCT and cohort.get('source_repo')==REPOSITORY and
  cohort.get('layout')==LAYOUT and cohort.get('store_abi')==2,'Wrong X4 cohort/product')
 require(len(firmware)==cohort['firmware_size'] and sha(firmware)==cohort['firmware_sha256'] and
  32<=len(firmware)<=0x260000 and len(store_image)==0x510000,'OTA requires native firmware plus exact immutable store; full USB image forbidden')
 require(firmware[0]==0xe9 and b'RISC_PAIRED_STORE_ABI:2\0' in firmware and
  ('RISC_RUNTIME_VERSION:'+cohort['runtime_version']).encode()+b'\0' in firmware,'Native firmware marker mismatch')
 require(len(usb)==0x1000000 and usb[0x10000:0x10000+len(firmware)]==firmware and
  usb[0x2f0000:0x800000]==store_image,'USB source differs from the admitted pair')
 require(json.loads(files['cohort.json'])==cohort,'Store cohort identity mismatch')
 directory=Path(output)/'updates';directory.mkdir()
 version=cohort['version'];tag='firmware-v'+version
 def row(asset,blob,release):
  (directory/asset).write_bytes(blob)
  return {'asset':asset,'url':'https://github.com/'+REPOSITORY+'/releases/download/'+release+'/'+asset,
   'size':len(blob),'sha256':sha(blob),'source_repo':REPOSITORY}
 payload=firmware+store_image
 ota=dict(row(PRODUCT+'-cohort-'+version+'.bin',payload,tag),kind='paired-cohort',
  product=PRODUCT,version=version,runtime_version=cohort['runtime_version'],
  source_revision=cohort['source_revision'],layout=LAYOUT,store_abi=2,
  firmware_size=len(firmware),firmware_sha256=sha(firmware),store_size=len(store_image),store_sha256=sha(store_image))
 # Separate informational USB asset; never the paired OTA download.
 full=row(PRODUCT+'-launcher-'+version+'.bin',usb,tag)
 apps=[]
 boot=json.loads(files['boot.json'])
 for policy in boot['app_capabilities']:
  manifest=json.loads(files[policy['manifest']]);name=manifest['file_name'][:-4]
  blob=files[manifest['file_name']];app_tag='app-'+name+'-v'+manifest['version']
  apps.append(dict(row(name+'.elf',blob,app_tag),kind='app',id=name,version=manifest['version'],tag=app_tag,manifest=manifest))
 catalog={'schema':1,'product':PRODUCT,'source_repo':REPOSITORY,
  'firmware':dict(full,kind='firmware',version=version,tag=tag,ota=ota),'apps':apps}
 (directory/'release-index.local.json').write_bytes(encoded(catalog))
 receipt={'schema':1,'publication':'none','feed_configured':False,'source_revision':cohort['source_revision'],
  'cohort':cohort,'payload_sha256':sha(payload),'store_files':{name:sha(data) for name,data in sorted(files.items())},
  'assets':{p.name:{'bytes':p.stat().st_size,'sha256':sha(p.read_bytes())} for p in directory.iterdir()},
  'scope':'Local candidate paths only. Catalog URLs are future release naming, not published assets. No boot hash scans. Full installation/update admission remains mandatory.'}
 (directory/'update-artifacts.json').write_bytes(encoded(receipt))
 return receipt
