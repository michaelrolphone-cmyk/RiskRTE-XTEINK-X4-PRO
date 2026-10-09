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
if __name__=='__main__':unittest.main()
