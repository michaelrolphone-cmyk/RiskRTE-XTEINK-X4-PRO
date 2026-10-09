#!/usr/bin/env python3
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from build_test_bundle import configure_boot_logging
class BootLoggingProfile(unittest.TestCase):
    def setUp(self):
        self.boot={'provider_activation':'demand-retained','drivers':[
            {'manifest':'panel/manifest.json','instance_id':3},
            {'manifest':'sd/manifest.json','instance_id':9}]}
    def test_cold_sd_only(self):
        configure_boot_logging(self.boot,True)
        self.assertEqual(self.boot['provider_activation'],'demand-retained')
        self.assertEqual(self.boot['drivers'][1]['boot_start'],'cold')
        self.assertNotIn('boot_start',self.boot['drivers'][0])
        self.assertNotIn('apps',self.boot)
    def test_omission_preserves_profile(self):
        before=copy.deepcopy(self.boot);configure_boot_logging(self.boot,False)
        self.assertEqual(self.boot,before)
    def test_refuses_eager_missing_duplicate_and_existing_policy(self):
        for mutation in (lambda b:b.update(provider_activation='eager'),
                         lambda b:b['drivers'].pop(),
                         lambda b:b['drivers'].append(copy.deepcopy(b['drivers'][1])),
                         lambda b:b['drivers'][1].update(boot_start='cold')):
            boot=copy.deepcopy(self.boot);mutation(boot)
            with self.assertRaises(ValueError):configure_boot_logging(boot,True)
if __name__=='__main__':unittest.main()
