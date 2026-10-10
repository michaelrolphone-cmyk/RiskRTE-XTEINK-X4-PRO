import copy,hashlib,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import idle_cohort as idle
class IdleAdmission(unittest.TestCase):
 def fixture(self):
  h={n:'0'*64 for n in (*idle.HEADERS,*idle.RUNTIME_HEADERS)};helper=b'helper'
  manifest={'requires':[{'capability':c,'api':1} for c in idle.INSTANCES]+[{'capability':'alarm.service','api':2},{'capability':'storage.key-value','api':1}]}
  record={'build_defines':['-DPORTABLE_X4_IDLE_POLICY','-DPORTABLE_LOW_BATTERY','-DPORTABLE_APP_SLEEP_LOCAL','-DPORTABLE_QUICK_RADIOS','-DALARM_SERVICE_TAGGED_V2'],
  'idle_policy':{'enabled':True,'mode':'Light only; foreground state retained','low_battery_threshold_percent':10,'idle_default_ms':60000,'low_battery_idle_ms':20000,'foreground_capture_restart':False,'helper_sha256':hashlib.sha256(helper).hexdigest(),'sdk_sha256':h,'physical_instances':dict(idle.INSTANCES),'preferences_instance':1,'alarm_service':{'api':2,'instance':0}}}
  return manifest,record,helper,h
 def test_exact(self):
  m,r,b,h=self.fixture();idle.validate_app('timecard',m,r,b,h)
 def test_custody_and_authority(self):
  for fault in ('helper','sdk','instance','flag','alarm','deep','capture'):
   m,r,b,h=self.fixture();r=copy.deepcopy(r);name='timecard'
   if fault=='helper':b=b'changed'
   if fault=='sdk':r['idle_policy']['sdk_sha256']['RiscTouchV1.h']='1'*64
   if fault=='instance':r['idle_policy']['physical_instances']['x4.power']=3
   if fault=='flag':r['build_defines'].remove('-DPORTABLE_QUICK_RADIOS')
   if fault=='alarm':m['requires'][-2]['api']=1
   if fault=='deep':r['idle_policy']['mode']='Deep'
   if fault=='capture':name='waterfall'
   with self.assertRaises(ValueError,msg=fault):idle.validate_app(name,m,r,b,h)
 def test_capture_opt_in(self):
  m,r,b,h=self.fixture();r['build_defines'].append('-DPORTABLE_RADIO_CONTINUOUS_CAPTURE');idle.validate_app('waterfall',m,r,b,h)
if __name__=='__main__':unittest.main()
