#!/usr/bin/env python3
"""Pure identity/hash checks; no target execution or device I/O."""
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from build_test_bundle import cohort_identity,sha

class CohortIdentity(unittest.TestCase):
    def setUp(self):
        self.firmware=b'F'*64
        self.product={'schema':1,'product':'xteink-x4-pro','version':'0.1.0','source_repo':'owner/product'}
        self.native={'layout':'riscrte-paired-appdata-v2','store_abi':2,'firmware_version':'0.1.44',
                     'assets':{'firmware.bin':{'bytes':64,'sha256':sha(self.firmware)}}}
    def test_exact_identity(self):
        got=cohort_identity(self.product,self.native,self.firmware,'a'*40)
        self.assertEqual(got['firmware_size'],64)
        self.assertEqual(got['firmware_sha256'],sha(self.firmware))
        self.assertEqual(got['source_revision'],'a'*40)
        self.assertEqual(got['runtime_version'],'0.1.44')
    def test_wrong_firmware(self):
        for data in (b'',b'G'*64,b'F'*65):
            with self.assertRaises(ValueError):cohort_identity(self.product,self.native,data,'a'*40)
    def test_invalid_product(self):
        for key,value in [('schema',2),('version','latest'),('product','../x4'),('source_repo','owner/../product')]:
            p={**self.product,key:value}
            with self.assertRaises(ValueError):cohort_identity(p,self.native,self.firmware,'a'*40)
    def test_native_layout_and_version(self):
        for key,value in [('store_abi',1),('layout','riscrte-paired-16m-v1'),('firmware_version','latest')]:
            n={**self.native,key:value}
            with self.assertRaises(ValueError):cohort_identity(self.product,n,self.firmware,'a'*40)
    def test_bad_source_and_receipt(self):
        for revision in ('a'*39,'A'*40,'../HEAD'):
            with self.assertRaises(ValueError):cohort_identity(self.product,self.native,self.firmware,revision)
        n=copy.deepcopy(self.native);n['assets']['firmware.bin']['sha256']='b'*64
        with self.assertRaises(ValueError):cohort_identity(self.product,n,self.firmware,'a'*40)

if __name__=='__main__':unittest.main()
