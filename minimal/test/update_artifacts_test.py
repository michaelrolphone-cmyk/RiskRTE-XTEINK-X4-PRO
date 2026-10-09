import copy,importlib.util,json,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import update_artifacts as u
from build_test_bundle import app_grants
class Updates(unittest.TestCase):
 def setUp(self):
  self.firmware=b'\xe9'+b'RISC_PAIRED_STORE_ABI:2\0RISC_RUNTIME_VERSION:0.1.62\0'+bytes(64)
  self.store=bytes(0x510000)
  self.cohort={'product':u.PRODUCT,'version':'0.1.16','source_repo':u.REPOSITORY,'source_revision':'a'*40,'layout':u.LAYOUT,'store_abi':2,'runtime_version':'0.1.62','firmware_size':len(self.firmware),'firmware_sha256':u.sha(self.firmware)}
  self.usb=bytearray(0x1000000);self.usb[0x10000:0x10000+len(self.firmware)]=self.firmware
  self.files={'cohort.json':u.encoded(self.cohort),'boot.json':u.encoded({'app_capabilities':[{'manifest':'ota_update.json'}]}),'ota_update.json':u.encoded({'id':'ota_update','version':'1.2.0','file_name':'ota_update.elf','requires':[]}), 'ota_update.elf':b'\x7fELF-local-fixture'}
 def test_source_bound_ota(self):
  with tempfile.TemporaryDirectory() as tmp:
   record=u.create(tmp,self.cohort,self.firmware,self.store,self.files,self.usb)
   catalog=json.loads((Path(tmp)/'updates/release-index.local.json').read_text())
   ota=catalog['firmware']['ota'];blob=(Path(tmp)/'updates'/ota['asset']).read_bytes()
   self.assertEqual(blob,self.firmware+self.store)
   self.assertEqual(ota['sha256'],u.sha(blob));self.assertEqual(record['publication'],'none')
   self.assertFalse(record['feed_configured']);self.assertEqual(catalog['product'],u.PRODUCT)
   self.assertTrue(all('T-Watch' not in row['url'] for row in catalog['apps']))
 def test_foreign_or_full_usb_rejected(self):
  for key,value in [('product','twatch-s3'),('store_abi',1),('source_repo','owner/watch'),('layout','other')]:
   with self.subTest(key=key),tempfile.TemporaryDirectory() as tmp,self.assertRaises(ValueError):u.create(tmp,{**self.cohort,key:value},self.firmware,self.store,self.files,self.usb)
  with tempfile.TemporaryDirectory() as tmp,self.assertRaises(ValueError):u.create(tmp,self.cohort,self.usb,self.store,self.files,self.usb)
 def test_mismatched_source_rejected(self):
  for field in ('cohort','store','usb','firmware'):
   cohort,store,usb,fw=copy.deepcopy(self.cohort),self.store,bytearray(self.usb),self.firmware
   if field=='cohort':cohort['source_revision']='b'*40
   if field=='store':store=store[:-1]
   if field=='usb':usb[0x10000]^=1
   if field=='firmware':fw=fw[:-1]+b'X'
   with self.subTest(field=field),tempfile.TemporaryDirectory() as tmp,self.assertRaises(ValueError):u.create(tmp,cohort,fw,store,self.files,usb)
 def test_action_separated_grants(self):
  for name,cap in [('ota_update','software.update.firmware'),('app_store','software.update.apps')]:
   needs=[{'capability':c,'api':v} for c,v in [(cap,1),('runtime.realtime',1),('alarm.service',2),('storage.key-value',1),('net.wifi',1)]]
   grants=app_grants(name,needs,True,True,True,True)
   self.assertEqual([r['instance_id'] for r in grants if r['capability']=='storage.key-value'],[6,1])
   self.assertIn({'capability':cap,'api':1,'instance_id':0},grants)
   with self.assertRaises(ValueError):app_grants('settings',needs,True,True,True,True)
   needs[0]['capability']='software.update.apps' if name=='ota_update' else 'software.update.firmware'
   with self.assertRaises(ValueError):app_grants(name,needs,True,True,True,True)
 def provider(self,kind='firmware',routes=False):
  identity,cap=u.SERVICES[kind];blob=b'provider-fixture';version='0.1.5' if routes else '0.1.4'
  manifest={'type':'driver','driver_abi':2,'id':identity,'version':version,'file_name':'driver.elf',
   'architecture':'xtensa-esp32s3','provides':[{'capability':cap,'api':1}],
   'requires':[{'capability':c,'api':1} for c in ('platform.http-client','platform.bank-store','platform.clock')]}
  record={'repository_commit':'a'*40,'working_tree_dirty':False,'version':version,
   'sha256':u.sha(blob),'size_bytes':len(blob),
   'update_policy':{'product':u.PRODUCT,'repository':u.REPOSITORY,'catalog_url':'','feed_configured':False,'runtime_only':False},
   'build_defines':['-DUPDATE_PRODUCT_X4','-DUPDATE_FIRMWARE='+str(int(kind=='firmware'))]+(['-DUPDATE_SOURCE_ROUTES=1'] if routes else [])}
  return manifest,blob,record
 def test_legacy_and_explicit_source_routes(self):
  for kind,routes in [('firmware',False),('apps',False),('firmware',True)]:
   m,b,r=self.provider(kind,routes)
   self.assertEqual(u.validate_provider(kind,m,b,r,'a'*40,routes)['manifest'],m)
 def test_source_routes_need_exact_identity_and_selection(self):
  m,b,r=self.provider('firmware',True)
  with self.assertRaises(ValueError):u.validate_provider('firmware',m,b,r,'a'*40)
  with self.assertRaises(ValueError):u.validate_provider('firmware',m,b,r,'b'*40,True)
  with self.assertRaises(ValueError):u.validate_provider('firmware',m,b,r,'a'*40,1)
  for flags in [[],['-DUPDATE_SOURCE_ROUTES=0'],['-DUPDATE_SOURCE_ROUTES=2'],['-DUPDATE_SOURCE_ROUTES=1','-DUPDATE_SOURCE_ROUTES=0']]:
   changed=copy.deepcopy(r);changed['build_defines']=r['build_defines'][:2]+flags
   with self.subTest(flags=flags),self.assertRaises(ValueError):u.validate_provider('firmware',m,b,changed,'a'*40,True)
  m,b,r=self.provider('apps',True)
  with self.assertRaises(ValueError):u.validate_provider('apps',m,b,r,'a'*40,True)
if __name__=='__main__':unittest.main()
