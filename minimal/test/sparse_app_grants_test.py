"""Future sparse profile authority boundary; no current product activation."""
import sys, unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from build_test_bundle import app_grants, APPS, DESK_REQUIREMENTS

class SparseGrants(unittest.TestCase):
 def requirements(self):
  return [{'capability':cap,'api':api} for cap,api in sorted(DESK_REQUIREMENTS | {
   ('runtime.realtime-control',1),('runtime.provider-promotion',1)})]
 def test_exact_clock_authority(self):
  grants=app_grants('default',self.requirements(),True,True,True)
  self.assertEqual(len(grants),14)
  for name in ('runtime.realtime-control','runtime.provider-promotion','runtime.retained-wake'):
   self.assertEqual([g for g in grants if g['capability']==name],[{'capability':name,'api':1,'instance_id':0}])
 def test_no_implicit_native_authority(self):
  for cap in ('runtime.realtime-control','runtime.provider-promotion'):
   for name in APPS:
    for sparse in (False,True):
     if sparse and (name=='default' or (name=='settings' and cap=='runtime.realtime-control')):continue
     with self.subTest(cap=cap,name=name,sparse=sparse),self.assertRaises(ValueError):
      app_grants(name,[{'capability':cap,'api':1}],True,True,sparse)
 def test_settings_control_only(self):
  self.assertEqual(app_grants('settings',[{'capability':'runtime.realtime-control','api':1}],True,True,True),
   [{'capability':'runtime.realtime-control','api':1,'instance_id':0}])
 def test_missing_authority_and_bad_profile(self):
  for missing in ('runtime.realtime-control','runtime.provider-promotion'):
   with self.assertRaises(ValueError):app_grants('default',[r for r in self.requirements() if r['capability']!=missing],True,True,True)
  for sleep,desk in ((False,False),(True,False),(False,True)):
   with self.assertRaises(ValueError):app_grants('default',self.requirements(),sleep,desk,True)
 def test_native_version_exact(self):
  for cap in ('runtime.realtime-control','runtime.provider-promotion'):
   for api in (0,2,True,'1'):
    req=self.requirements()
    for item in req:
     if item['capability']==cap:item['api']=api
    with self.subTest(cap=cap,api=api),self.assertRaises(ValueError):app_grants('default',req,True,True,True)
 def test_bounded_declared_and_live_capacity(self):
  req=self.requirements()+[{'capability':'file.open','api':1},{'capability':'storage.installed-files','api':1}]
  self.assertEqual(len(app_grants('default',req,True,True,True)),16)
  with self.assertRaises(ValueError):app_grants('default',req+[{'capability':'radio.iq','api':1}],True,True,True)
  old=[{'capability':cap,'api':api} for cap,api in sorted(DESK_REQUIREMENTS)]
  with self.assertRaises(ValueError):app_grants('default',old+[{'capability':'file.open','api':1}],True,True)
 def test_existing_cohort_grants_unchanged(self):
  for name in APPS:
   req=[{'capability':'board.battery','api':1},{'capability':'storage.key-value','api':1}]
   if name=='default':continue
   self.assertEqual(app_grants(name,req,True,True),app_grants(name,req,True,True,True))
 def test_duplicate_does_not_multiply_authority(self):
  req=self.requirements()
  self.assertEqual(app_grants('default',req,True,True,True),app_grants('default',req+req,True,True,True))

if __name__=='__main__':unittest.main()
