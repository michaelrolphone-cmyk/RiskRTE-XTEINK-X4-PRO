"""Reject a declared sparse profile whose source, SDK or authority is mixed."""
import sys, unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from build_test_bundle import validate_sparse_clock_profile,SPARSE_REQUIREMENTS,sha
from native_time_cohort import VERSIONS

class SparseProfile(unittest.TestCase):
 def setUp(self):
  self.blob=b'compiled Clock';self.local=b'local sleep source';self.source='a'*40
  self.headers={name:'b'*64 for name in ('RiscRuntimeV1.h','RiscRealtimeV1.h','RiscProviderPromotionV1.h',
    'RiscRetainedWakeV1.h','RiscDisplayOutputPowerV1.h','RiscTouchPowerV1.h','RiscStorageVolumeV1.h',
    'RiscTimedSleepV1.h','RiscDeepSleepV1.h','RiscLightSleepV1.h')}
  self.manifest={'id':'paper_clock','version':'0.3.2','requires':[{'capability':c,'api':v} for c,v in sorted(SPARSE_REQUIREMENTS)]}
  self.record={'working_tree_dirty':False,'desk_clock':True,'version':'0.3.2','clock_policy':'native-realtime-iana',
    'sparse_start':True,'provider_activation':'demand','timer_preferences':'retained-only','foreground_promotion':True,
    'invocation_retention':True,'display_rotation':90,'launcher_app':'springboard.elf','navigation':True,
    'sleep_capability':'x4.power','alarm_client':True,'quick_actions':True,'quick_radios':True,'grant_count':14,
    'repository_commit':self.source,'sha256':sha(self.blob),'size_bytes':len(self.blob),
    'local_sleep_source_sha256':sha(self.local),'desk_sdk_headers':self.headers,
    'retained_wake_sdk_sha256':self.headers['RiscRetainedWakeV1.h']}
 def check(self,record=None,manifest=None,headers=None):
  return validate_sparse_clock_profile(manifest or self.manifest,self.blob,record or self.record,self.source,self.local,self.headers if headers is None else headers)
 def test_exact_profile(self):
  result=self.check();self.assertEqual(result['grant_count'],14);self.assertFalse(result['hardware_qualified'])
 def test_no_wall_time_or_eager_mix(self):
  for key,value in [('clock_policy','rtc-wall-time'),('provider_activation','eager'),('timer_preferences','storage'),
    ('foreground_promotion',False),('invocation_retention',False),('sparse_start',False),('version','0.3.1'),
    ('display_rotation',0),('quick_radios',False),('grant_count',12),('sleep_capability','platform.power')]:
   with self.subTest(key=key),self.assertRaises(ValueError):self.check({**self.record,key:value})
 def test_source_and_artifact_custody(self):
  for key,value in [('working_tree_dirty',True),('repository_commit','c'*40),('sha256','c'*64),('size_bytes',0),
                    ('local_sleep_source_sha256','c'*64),('retained_wake_sdk_sha256','c'*64)]:
   with self.subTest(key=key),self.assertRaises(ValueError):self.check({**self.record,key:value})
 def test_every_required_header(self):
  for name in self.headers:
   with self.subTest(name=name),self.assertRaises(ValueError):self.check(headers={k:v for k,v in self.headers.items() if k!=name})
  with self.assertRaises(ValueError):self.check(headers={**self.headers,'RiscRuntimeV1.h':'invalid'})
  with self.assertRaises(ValueError):self.check({**self.record,'desk_sdk_headers':{**self.headers,'RiscRealtimeV1.h':'c'*64}})
 def test_missing_extra_duplicate_or_wrong_authority(self):
  req=self.manifest['requires']
  for changed in (req[:-1],req+[{'capability':'radio.iq','api':1}],req[:-1]+req[:1],
                  [{**r,'api':2} if r['capability']=='runtime.provider-promotion' else r for r in req]):
   with self.assertRaises(ValueError):self.check(manifest={**self.manifest,'requires':changed})

 def test_selected_lock_motion_identity(self):
  flags=['-DPORTABLE_DESK_LOCK_HOME','-DPORTABLE_PAPER_TRANSITIONS','-DPORTABLE_PAPER_CROSSFADE','-DPORTABLE_STAGE_LOGS']
  record={**self.record,'version':VERSIONS['default'],'build_defines':flags,
   'paper_motion':{'enabled':True},'paper_transition':{'enabled':True},
   'home_points':{'clock_policy':'native-utc','storage_instance':5,'foreground_only':True,
    'records':['points_utc_cfg','points_utc_meta'],'projection':'Utilities PointsUtcSchedule',
    'model':'Watch nova_points_state','tap_app':'points_in_time.elf'}}
  manifest={**self.manifest,'version':VERSIONS['default'],'requires':[
   {**row,'api':2} if row['capability']=='alarm.service' else row for row in self.manifest['requires']]}
  def check(value):return validate_sparse_clock_profile(manifest,self.blob,value,self.source,self.local,self.headers,True)
  self.assertEqual(check(record)['grant_count'],15)
  for flag in flags:
   with self.subTest(flag=flag),self.assertRaises(ValueError):check({**record,'build_defines':[f for f in flags if f!=flag]})
  for field in ('paper_motion','paper_transition'):
   with self.subTest(field=field),self.assertRaises(ValueError):check({**record,field:{'enabled':False}})

 def test_telemetry_is_explicit_and_bounded(self):
  manifest={**self.manifest,'version':VERSIONS['default'],'requires':[
   {**row,'api':2} if row['capability']=='alarm.service' else row for row in self.manifest['requires']]+[{'capability':'telemetry.broadcast','api':1}]}
  record={**self.record,'version':VERSIONS['default'],'grant_count':15,
   'build_defines':['-DPORTABLE_DESK_LOCK_HOME','-DPORTABLE_PAPER_TRANSITIONS','-DPORTABLE_PAPER_CROSSFADE','-DPORTABLE_STAGE_LOGS'],
   'paper_motion':{'enabled':True},'paper_transition':{'enabled':True},
   'home_points':{'clock_policy':'native-utc','storage_instance':5,'foreground_only':True,
    'records':['points_utc_cfg','points_utc_meta'],'projection':'Utilities PointsUtcSchedule',
    'model':'Watch nova_points_state','tap_app':'points_in_time.elf'}}
  value=validate_sparse_clock_profile(manifest,self.blob,record,self.source,self.local,self.headers,True,True)
  self.assertEqual(value['grant_count'],16)
  with self.assertRaises(ValueError):validate_sparse_clock_profile(manifest,self.blob,record,self.source,self.local,self.headers,True)
  with self.assertRaises(ValueError):validate_sparse_clock_profile(manifest,self.blob,{**record,'grant_count':14},self.source,self.local,self.headers,True,True)

if __name__=='__main__':unittest.main()
