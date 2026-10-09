import copy
import hashlib
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import native_time_cohort as n
from build_test_bundle import app_grants, SPARSE_REQUIREMENTS

class NativeCohort(unittest.TestCase):
 def setUp(self):
  self.blob=b'compiled native foreground app';self.source='a'*40
  self.manifest={'file_name':'alarms.elf','version':n.VERSIONS['alarms'],'requires':[
   {'capability':c,'api':v} for c,v in [('display.output',1),('input.touch.raw',1),
    ('input.navigation',1),('board.battery',1),('runtime.realtime',1),
    ('storage.key-value',1),('alarm.service',2)]]}
  self.receipt={'schema':1,'app':'alarms','version':n.VERSIONS['alarms'],
   'source_repo':'michaelrolphone-cmyk/RiscRTE-Utilities','source_revision':self.source,
   'system_source_revision':'b'*40,'runtime_source_revision':n.RUNTIME,
   'alarm_source_revision':n.ALARMS,'alarm_api':2,'time_policy':'native-realtime-iana',
   'elf_sha256':hashlib.sha256(self.blob).hexdigest(),'elf_bytes':len(self.blob),
   'requires':self.manifest['requires'],'sdk_sha256':dict(n.SDK)}
 def check(self,manifest=None,receipt=None):
  return n.validate_app('alarms',manifest or self.manifest,self.blob,receipt or self.receipt,self.source)
 def test_complete_identity(self):self.assertEqual(self.check(),self.receipt)
 def test_exact_sources_and_elf(self):
  for key,value in [('source_revision','c'*40),('system_source_revision','moving'),
    ('runtime_source_revision','c'*40),('alarm_source_revision','c'*40),('alarm_api',1),
    ('elf_sha256','c'*64),('elf_bytes',0),('time_policy','rtc-wall-time'),('version','0.2.4')]:
   with self.subTest(key=key),self.assertRaises(ValueError):self.check(receipt={**self.receipt,key:value})
 def test_each_compiled_header(self):
  for name in n.SDK:
   r=copy.deepcopy(self.receipt);r['sdk_sha256'][name]='c'*64
   with self.subTest(name=name),self.assertRaises(ValueError):self.check(receipt=r)
 def test_no_abi_authority_mix(self):
  for req in [self.manifest['requires']+[{'capability':'rtc.clock','api':2}],
   self.manifest['requires']+[{'capability':'runtime.realtime-control','api':1}],
   self.manifest['requires']+[{'capability':'alarm.service','api':1}],
   self.manifest['requires']+self.manifest['requires'][:1]]:
   m={**self.manifest,'requires':req};r={**self.receipt,'requires':req}
   with self.assertRaises(ValueError):self.check(m,r)
 def test_no_implicit_native_grant(self):
  req=[{'capability':'runtime.realtime','api':1}]
  for name in ('alarms','default','settings'):
   with self.assertRaises(ValueError):app_grants(name,req,True,True,True)
  self.assertEqual(app_grants('alarms',req,True,True,True,True),
                   [{'capability':'runtime.realtime','api':1,'instance_id':0}])
 def test_home_points_grant_is_explicit_and_bounded(self):
  req=[{'capability':c,'api':2 if c=='alarm.service' else v} for c,v in SPARSE_REQUIREMENTS]
  grants=app_grants('default',req,True,True,True,True)
  self.assertEqual(len(grants),15)
  self.assertEqual([g['instance_id'] for g in grants if g['capability']=='storage.key-value'],[5,1])
  old=app_grants('default',req,True,True,True,False)
  self.assertEqual(len(old),14)
  self.assertEqual([g['instance_id'] for g in old if g['capability']=='storage.key-value'],[1])
 def test_nine_explicit_provider_keys(self):
  self.assertEqual(len(n.ALARM_KEYS),9)
  self.assertEqual(n.ALARM_KEYS[-1],('time_zone',1,'read'))
  self.assertEqual(len({key for key,_,_ in n.ALARM_KEYS}),9)
  self.assertFalse(any(key in ('alarm_cfg','timer_cfg','points_cfg') for key,_,_ in n.ALARM_KEYS))

if __name__=='__main__':unittest.main()
