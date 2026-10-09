#!/usr/bin/env python3
"""Real frozen X4 payload regressions; no network requests or devices."""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import x4_image_provisioning as x
parser=argparse.ArgumentParser(description=__doc__)
for name in ('runtime','platform','tools','payload'):parser.add_argument('--'+name,type=Path,required=True)
args,remaining=parser.parse_known_args()
common=(args.runtime,args.platform,args.tools)

class FrozenProduct(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.receipt,cls.files,cls.raw,cls.seed,cls.m,_=x.verify(*common,args.payload)
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
    def payload(self,changes):
        folder=self.root/'payload';self.m['watch_image_provisioning'].write_files(folder,{**self.raw,**changes});return folder
    def changed(self,name,raw,field):
        receipt=copy.deepcopy(self.receipt);receipt[field]=x.meta(raw)
        return self.payload({name:raw,'payload.json':x.encoded(receipt)})
    def test_full_original_cohort_and_native_admission(self):
        self.assertEqual(len(self.files),85);self.assertEqual(self.receipt['admission']['elf_count'],41)
        self.assertEqual(self.receipt['cohort']['source_revision'],x.PLATFORM)
        self.assertEqual(self.receipt['cohort']['runtime_version'],'0.1.75')
        self.assertNotIn('cohort_migration',json.loads(self.files['boot.json']))
        self.assertEqual(self.receipt['admission']['hardware_calls'],0)
        self.assertEqual(self.receipt['admission']['storage_calls'],0)
    def test_rehashed_changed_image_refused(self):
        raw=bytearray(self.raw['image.bin']);raw[512]^=1
        with self.assertRaisesRegex(ValueError,'Frozen image'):x.verify(*common,self.changed('image.bin',bytes(raw),'image'))
    def test_rehashed_changed_build_custody_refused(self):
        value=json.loads(self.raw['build-custody.json']);value['x4_source']='a'*40
        with self.assertRaisesRegex(ValueError,'Frozen build-custody'):x.verify(*common,self.changed('build-custody.json',x.encoded(value),'build_custody'))
    def test_rehashed_changed_licenses_refused(self):
        raw=bytearray(self.raw['LICENSES.zip']);raw[-1]^=1
        with self.assertRaisesRegex(ValueError,'Frozen LICENSES'):x.verify(*common,self.changed('LICENSES.zip',bytes(raw),'licenses'))
    def test_rehashed_changed_native_seed_refused(self):
        seed=dict(self.seed);raw=bytearray(seed['firmware.bin']);raw[-1]^=1;seed['firmware.bin']=bytes(raw)
        archive=self.m['watch_image_provisioning'].archive_bytes(seed)
        with self.assertRaisesRegex(ValueError,'Frozen generic seed'):x.verify(*common,self.changed('seed.zip',archive,'seed_zip'))
    def test_incomplete_extra_and_recipe_mutations_refused(self):
        folder=self.payload({'COMPLETE':b'incomplete'})
        with self.assertRaisesRegex(ValueError,'Incomplete'):x.verify(*common,folder)
        (folder/'COMPLETE').write_bytes(self.raw['COMPLETE']);(folder/'extra').write_bytes(b'x')
        with self.assertRaisesRegex(ValueError,'inventory'):x.verify(*common,folder)
        (folder/'extra').unlink();receipt=copy.deepcopy(self.receipt);receipt['recipe_source']=x.PLATFORM
        (folder/'payload.json').write_bytes(x.encoded(receipt))
        with self.assertRaisesRegex(ValueError,'custody differs'):x.verify(*common,folder)
    def git(self,arguments,data=None,env=None):
        return subprocess.check_output(['git',*arguments],input=data,cwd=x.ROOT,env=env).decode().strip()
    def commit(self,changes=None):
        env={**os.environ,'GIT_INDEX_FILE':str(self.root/'index')};self.git(['read-tree','HEAD'],env=env)
        for name,raw in sorted({**self.raw,**(changes or {})}.items()):
            blob=self.git(['hash-object','-w','--stdin'],raw)
            self.git(['update-index','--add','--cacheinfo','100644,'+blob+','+x.PAYLOAD_PATH+'/'+name],env=env)
        tree=self.git(['write-tree'],env=env)
        commit=self.git(['-c','user.name=Codex','-c','user.email=codex@openai.com','commit-tree',tree,'-p','HEAD','-m','Unreferenced offline test'])
        return commit,tree
    def test_immutable_commit_binding_and_tree_refusal(self):
        commit,tree=self.commit();output=self.root/'deployment';record=x.bind(*common,args.payload,commit,output)
        self.assertEqual(record['payload_revision'],commit)
        self.assertEqual(len(json.loads((output/'inventory.json').read_bytes())['files']),85)
        with self.assertRaisesRegex(ValueError,'Immutable payload commit'):x.bind(*common,args.payload,tree,self.root/'bad')
    def test_committed_changed_bytes_refused(self):
        commit,_=self.commit({'COMPLETE':b'changed'})
        with self.assertRaisesRegex(ValueError,'Committed payload bytes'):x.bind(*common,args.payload,commit,self.root/'bad')
        self.assertFalse((self.root/'bad').exists())
    def test_noncanonical_download_refused_before_network(self):
        commit,_=self.commit();output=self.root/'deployment';x.bind(*common,args.payload,commit,output)
        record=json.loads((output/'deployment.json').read_bytes())
        for item in record['downloads'].values():item['url']=item['url'].replace('https://','http://')
        (output/'deployment.json').write_bytes(x.encoded(record))
        with self.assertRaisesRegex(ValueError,'Deployment image|Immutable download'):x.endpoints(*common,output)

if __name__=='__main__':unittest.main(argv=[sys.argv[0],*remaining])
