import sys, unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from generate_profile import profile,selections
from build_test_bundle import app_grants
class SleepProfile(unittest.TestCase):
 def test_scope(self):
  for panel in ('ssd1677','uc8279'):
   legacy=profile(panel);sleep=profile(panel,True)
   self.assertEqual(len(legacy['devices']),9);self.assertEqual(len(sleep['devices']),10)
   old={d['instance_id']:d for d in legacy['devices']};new={d['instance_id']:d for d in sleep['devices']}
   for i in range(1,10):
    if i!=6:self.assertEqual(old[i],new[i])
   self.assertEqual(old[6]['config']['pins'],[0,7,3]);self.assertEqual(new[6]['config']['pins'],[0,7])
   self.assertEqual(new[6]['bindings'],{'x4.power':17});self.assertEqual(new[17]['config']['pins'],[3])
   self.assertEqual(selections(True)['buttons'],(6,'buttons','power_buttons'))
 def test_authority(self):
  nav=[{'capability':'input.navigation','api':1}];power=[{'capability':'x4.power','api':1}]
  self.assertEqual(app_grants('default',nav),[dict(nav[0],instance_id=6)])
  self.assertEqual(app_grants('default',nav+power,True),[dict(nav[0],instance_id=6),dict(power[0],instance_id=17)])
  with self.assertRaises(ValueError):app_grants('default',nav,True)
  with self.assertRaises(ValueError):app_grants('default',nav+power)
  for name in ('springboard','file_browser','settings','ble_scanner','points_in_time'):
   with self.assertRaises(ValueError):app_grants(name,power,True)
if __name__=='__main__':unittest.main()
