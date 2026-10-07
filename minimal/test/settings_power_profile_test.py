import copy,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from build_test_bundle import sha,validate_settings_profile
class SettingsPowerProfile(unittest.TestCase):
 def setUp(self):
  self.blob=b'qualified Settings';self.manifest={'version':'1.3.4'}
  self.record={'working_tree_dirty':False,'version':'1.3.4','sha256':sha(self.blob),'size_bytes':len(self.blob),'build_defines':['-DPORTABLE_SETTINGS_APP','-DPORTABLE_ALARM_SETTINGS']}
 def check(self,r):return validate_settings_profile(self.manifest,self.blob,r)
 def test_light_only(self):self.assertEqual(self.check(self.record),{'manual_light_sleep':True,'sleep_mode_selector':False})
 def test_unsupported_selector(self):
  for flag in ['-DPORTABLE_SLEEP_SETTINGS','-DPORTABLE_SLEEP_SETTINGS=1','-DPORTABLE_SLEEP_SETTINGS=0']:
   r=copy.deepcopy(self.record);r['build_defines'].append(flag)
   with self.assertRaises(ValueError):self.check(r)
 def test_receipt_identity(self):
  for key,value in [('working_tree_dirty',True),('version','1.3.3'),('sha256','0'*64),('size_bytes',0),('build_defines',None),('build_defines',[1])]:
   r={**self.record,key:value}
   with self.assertRaises(ValueError):self.check(r)
 def test_missing_feature_receipt(self):
  r=copy.deepcopy(self.record);del r['build_defines']
  with self.assertRaises(ValueError):self.check(r)
if __name__=='__main__':unittest.main()
