import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from build_test_bundle import app_grants,APPS
NEEDS=[{'capability':cap,'api':api} for cap,api in [('x4.power',1),('display.output',1),('input.touch.raw',1),('storage.volume',1),('net.wifi',1),('bluetooth.hci',1),('alarm.service',2),('storage.key-value',1)]]
class Idle(unittest.TestCase):
 def test_exact_physical_mapping(self):
  for name in APPS:
   if name=='default':continue
   grants=app_grants(name,NEEDS,sleep=True,idle_policy=True)
   actual={g['capability']:g['instance_id'] for g in grants}
   self.assertEqual({k:actual[k] for k in ('x4.power','display.output','input.touch.raw','storage.volume','net.wifi','bluetooth.hci','alarm.service')},{'x4.power':17,'display.output':3,'input.touch.raw':4,'storage.volume':9,'net.wifi':15,'bluetooth.hci':16,'alarm.service':0})
   self.assertFalse(any(g['capability']=='runtime.retained-wake' for g in grants))
 def test_no_implicit_permission(self):
  for name in APPS:
   if name=='default':continue
   with self.assertRaises(ValueError):app_grants(name,NEEDS,sleep=True)
   with self.assertRaises(ValueError):app_grants(name,NEEDS,idle_policy=True)
   for i in range(len(NEEDS)):
    with self.assertRaises(ValueError):app_grants(name,NEEDS[:i]+NEEDS[i+1:],sleep=True,idle_policy=True)
 def test_no_ordinary_retained_wake(self):
  with self.assertRaises(ValueError):app_grants('calculator',NEEDS+[{'capability':'runtime.retained-wake','api':1}],sleep=True,desk_clock=True,idle_policy=True)
if __name__=='__main__':unittest.main()
