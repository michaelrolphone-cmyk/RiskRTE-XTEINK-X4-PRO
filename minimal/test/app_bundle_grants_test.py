import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from build_test_bundle import app_grants,APPS
class Grants(unittest.TestCase):
 def test_eighteen_completed_apps(self):
  self.assertEqual(len(APPS),18)
  self.assertEqual(len(set(APPS)),18)
 def test_storage_namespaces(self):
  for name,expected in [('points_in_time',[5,1]),('stopwatch',[2,1]),('countdown',[3,1]),('timecard',[1]),('calculator',[1])]:
   req={'capability':'storage.key-value','api':1,'instance_id':999,'access':'read-write'}
   grants=app_grants(name,[req,req])
   self.assertEqual([g['instance_id'] for g in grants],expected)
   self.assertTrue(all(set(g)=={'capability','api','instance_id'} for g in grants))
 def test_sensor_logical_and_appdata_namespace(self):
  self.assertEqual(app_grants('ble_scanner',[{'capability':'bluetooth.sensors','api':1}])[0]['instance_id'],0)
  self.assertEqual(app_grants('timecard',[{'capability':'storage.app-data','api':1}])[0]['instance_id'],1)
  with self.assertRaises(ValueError):app_grants('calculator',[{'capability':'storage.app-data','api':1}])
 def test_rf_namespaces(self):
  req=[{'capability':'storage.key-value','api':2},{'capability':'storage.key-value','api':1},{'capability':'storage.app-data','api':1}]
  self.assertEqual([g['instance_id'] for g in app_grants('waterfall',req)],[8,1,3])
 def test_power_remains_clock_only(self):
  power={'capability':'x4.power','api':1}
  for name in APPS:
   if name=='default':continue
   with self.assertRaises(ValueError):app_grants(name,[power],True)
if __name__=='__main__':unittest.main()
