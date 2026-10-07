"""Fail closed on mismatched opt-in Clock/Settings authority and source custody."""
import copy,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from build_test_bundle import app_grants,sha,validate_desk_clock_profile,validate_settings_profile,DESK_REQUIREMENTS,APPS,validate_provider_source
class DeskBundle(unittest.TestCase):
 def setUp(self):
  self.blob=b'qualified target';self.local=b'typed local source';self.source='a'*40
  self.headers={'RiscRetainedWakeV1.h':'b'*64,'RiscDisplayOutputPowerV1.h':'c'*64}
  self.manifest={'id':'paper_clock','version':'0.3.0','requires':[{'capability':n,'api':v} for n,v in sorted(DESK_REQUIREMENTS)]}
  self.record={'working_tree_dirty':False,'desk_clock':True,'version':'0.3.0','clock_policy':'rtc-wall-time','display_rotation':90,'launcher_app':'springboard.elf','navigation':True,'sleep_capability':'x4.power','alarm_client':True,'quick_actions':True,'quick_radios':True,'grant_count':12,'repository_commit':self.source,'sha256':sha(self.blob),'size_bytes':len(self.blob),'local_sleep_source_sha256':sha(self.local),'desk_sdk_headers':self.headers,'retained_wake_sdk_sha256':self.headers['RiscRetainedWakeV1.h']}
 def check(self,record=None,manifest=None):
  return validate_desk_clock_profile(manifest or self.manifest,self.blob,record or self.record,self.source,self.local,self.headers)
 def test_exact_clock(self):
  self.assertEqual(self.check()['grant_count'],12)
  grants=app_grants('default',self.manifest['requires'],True,True)
  self.assertEqual(len(grants),12)
  self.assertEqual({g['capability']:g['instance_id'] for g in grants},{'alarm.service':0,'bluetooth.hci':16,'board.battery':7,'display.output':3,'input.navigation':6,'input.touch.raw':4,'net.wifi':15,'rtc.clock':8,'runtime.retained-wake':0,'storage.key-value':1,'storage.volume':9,'x4.power':17})
 def test_corrupt_clock_receipts(self):
  for key,value in [('working_tree_dirty',True),('desk_clock',False),('repository_commit','d'*40),('local_sleep_source_sha256','0'*64),('retained_wake_sdk_sha256','0'*64),('desk_sdk_headers',{}),('quick_radios',False),('grant_count',11),('version','0.2.2'),('clock_policy','utc'),('size_bytes',0),('sha256','0'*64)]:
   with self.subTest(key=key),self.assertRaises(ValueError):self.check({**self.record,key:value})
 def test_missing_extra_duplicate_requirement(self):
  cases=[self.manifest['requires'][:-1],self.manifest['requires']+[{'capability':'radio.iq','api':1}],self.manifest['requires'][:-1]+self.manifest['requires'][:1]]
  for requirements in cases:
   with self.assertRaises(ValueError):self.check(manifest={**self.manifest,'requires':requirements})
 def test_retained_authority_is_explicit_clock_only(self):
  req=[{'capability':'runtime.retained-wake','api':1}]
  for app in APPS:
   if app!='default':
    with self.assertRaises(ValueError):app_grants(app,req,True,True)
  for sleep,desk in [(False,False),(True,False),(False,True)]:
   with self.assertRaises(ValueError):app_grants('default',req,sleep,desk)
  with self.assertRaises(ValueError):app_grants('default',[{'capability':'x4.power','api':1}],True,True)
  with self.assertRaises(ValueError):app_grants('default',[{'capability':'runtime.retained-wake','api':2}],True,True)
 def test_driver_source_custody(self):
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);(root/'minimal').mkdir();(root/'minimal/driver.c').write_bytes(b'source')
   validate_provider_source({'local_source_hashes':{'minimal/driver.c':sha(b'source')}},root)
   for mapping in [{},{'minimal/driver.c':'0'*64},{'minimal/missing.h':'0'*64},{'../outside':'0'*64},{'/tmp/outside':'0'*64},{'sdk/header.h':'0'*64}]:
    with self.assertRaises(ValueError):validate_provider_source({'local_source_hashes':mapping},root)
 def test_desk_settings_exact_policy(self):
  flags=['-DPORTABLE_SLEEP_SETTINGS','-DPORTABLE_SETTINGS_X4_DESK_CLOCK','-DPORTABLE_DISPLAY_ROTATION=90','-DPORTABLE_RTC_WALL_TIME','-DPORTABLE_INPUT_NAVIGATION']
  record={'working_tree_dirty':False,'version':'1.3.5','sha256':sha(self.blob),'size_bytes':len(self.blob),'build_defines':flags,'settings_profile':'x4-desk-clock','sleep_modes':['light','deep'],'sleep_fallback':'light','desk_clock_faces':['Segments','Sans','Serif','Minimal','Railway','Deco'],'preferences':{'instance':1,'sleep_key':'sleep_mode','face_key':'desk_clock_face'},'repository_commit':self.source}
  def check(r):return validate_settings_profile({'version':'1.3.5'},self.blob,r,True,self.source)
  self.assertTrue(check(record)['deep_desk_clock'])
  for key,value in [('settings_profile','default'),('sleep_modes',['light','deep','hybrid']),('sleep_fallback','hybrid'),('preferences',{'instance':2}),('desk_clock_faces',[]),('repository_commit','f'*40),('build_defines',flags+['-DPORTABLE_SLEEP_SETTINGS=0']),('build_defines',flags[:-1])]:
   with self.subTest(key=key),self.assertRaises(ValueError):check({**record,key:value})
  with self.assertRaises(ValueError):validate_settings_profile({'version':'1.3.5'},self.blob,record)
if __name__=='__main__':unittest.main()
