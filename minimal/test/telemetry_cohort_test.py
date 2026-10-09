import copy,importlib.util,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];spec=importlib.util.spec_from_file_location('telemetry',ROOT/'minimal/scripts/telemetry_cohort.py');t=importlib.util.module_from_spec(spec);spec.loader.exec_module(t)
class TestTelemetry(unittest.TestCase):
 def fixture(self):
  common=[{'capability':'storage.key-value','api':1},{'capability':'alarm.service','api':2}];manifests={n:{'version':v,'requires':common+[t.CAPABILITY]} for n,v in t.VERSIONS.items()};boot={'provider_activation':'demand','drivers':[],'app_capabilities':[{'manifest':n+'.json','grants':[{'capability':'storage.key-value','api':1,'instance_id':1},{'capability':'alarm.service','api':2,'instance_id':0}]} for n in t.VERSIONS]};return boot,manifests
 def test_exact(self):
  b,m=self.fixture();r=t.extend_boot(b,m);self.assertEqual(len(r['drivers']),3);self.assertTrue(all(len(x['grants'])==3 for x in r['app_capabilities']));self.assertEqual(len(b['drivers']),0)
 def test_retained_demand_preserved(self):
  b,m=self.fixture();b['provider_activation']='demand-retained'
  self.assertEqual(t.extend_boot(b,m)['provider_activation'],'demand-retained')
 def test_boundaries(self):
  b,m=self.fixture();row=b['app_capabilities'][0]
  for i in range(13):c='test.'+str(i);row['grants'].append({'capability':c,'api':1,'instance_id':0});m['default']['requires'].append({'capability':c,'api':1})
  self.assertEqual(len(t.extend_boot(b,m)['app_capabilities'][0]['grants']),16)
  row['grants'].append({'capability':'test.overflow','api':1,'instance_id':0});m['default']['requires'].append({'capability':'test.overflow','api':1})
  with self.assertRaises(ValueError):t.extend_boot(b,m)
 def test_missing(self):
  for fault in range(5):
   b,m=self.fixture()
   if fault==0:b['provider_activation']='eager'
   if fault==1:m['battery']['version']='1.1.6'
   if fault==2:m['battery']['requires']=[]
   if fault==3:b['app_capabilities'][0]['grants'][0]['instance_id']=2
   if fault==4:b['drivers']=[{'manifest':str(i)+'.json'} for i in range(22)]
   with self.assertRaises(ValueError):t.extend_boot(b,m)
if __name__=='__main__':unittest.main()
