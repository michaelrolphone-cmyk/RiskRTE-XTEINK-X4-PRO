import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import contexts_cohort as c
import telemetry_cohort as t
from build_test_bundle import app_grants, SPARSE_REQUIREMENTS

class ContextsCohort(unittest.TestCase):
    def test_exact_editor_grants_and_no_implicit_selection(self):
        req=[{'capability':g['capability'],'api':g['api']} for g in c.PROFILE['app_grants']]
        self.assertEqual(app_grants('contexts',req,True,True,True,True,True,True),c.PROFILE['app_grants'])
        with self.assertRaises(ValueError):app_grants('contexts',req,True,True,True,True,True)
        for name in ('settings','file_browser','calculator'):
            with self.assertRaises(ValueError):app_grants(name,[c.CAPABILITY],True,True,True,True,False,True)

    def test_default_seventeen_metadata_rows_keep_sixteen_requirements(self):
        pairs=(SPARSE_REQUIREMENTS-{('alarm.service',1)})|{('alarm.service',2),('contexts.service',1)}
        req=[{'capability':n,'api':v} for n,v in sorted(pairs)]
        grants=app_grants('default',req,True,True,True,True,True,True)
        self.assertEqual(len(req),15)
        self.assertEqual(len(grants),16)
        self.assertEqual([g['instance_id'] for g in grants if g['capability']=='storage.key-value'],[5,1])
        manifests={name:{'version':version,'requires':[{'capability':'storage.key-value','api':1},{'capability':'alarm.service','api':2},t.CAPABILITY]} for name,version in t.VERSIONS.items()}
        rows=[{'manifest':name+'.json','grants':[{'capability':'storage.key-value','api':1,'instance_id':1},{'capability':'alarm.service','api':2,'instance_id':0}]} for name in t.VERSIONS]
        manifests['default']['requires']=req+[t.CAPABILITY];rows[0]['grants']=grants
        manifests['contexts']={'version':c.PROFILE['app_version'],'requires':[{'capability':g['capability'],'api':g['api']} for g in c.PROFILE['app_grants']]}
        rows.append({'manifest':'contexts.json','grants':copy.deepcopy(c.PROFILE['app_grants'])})
        boot={'provider_activation':'demand-retained','drivers':[],'app_capabilities':rows}
        selected=t.extend_boot(boot,manifests,contexts=True)
        self.assertEqual(len(selected['app_capabilities'][0]['grants']),17)
        self.assertEqual(len(manifests['default']['requires']),16)
        self.assertEqual(selected['app_capabilities'][-1],rows[-1])
        with self.assertRaises(ValueError):t.extend_boot(boot,manifests)
        rows[0]['grants'].append({'capability':'file.open','api':1,'instance_id':0})
        manifests['default']['requires'].append({'capability':'file.open','api':1})
        with self.assertRaises(ValueError):t.extend_boot(boot,manifests,contexts=True)

    def provider(self):
        deps=[{'capability':'platform.clock','api':1},{'capability':'radio.iq','api':1}]
        manifest={'id':'contexts-service','version':c.PROFILE['service_version'],'driver_abi':2,'architecture':'xtensa-esp32s3','file_name':'driver.elf','requires':deps,'provides':[c.CAPABILITY]}
        record={'profile':'rf-only','source_mask':2,'requires':deps,'version':c.PROFILE['service_version'],'elf_sha256':c.sha(b'elf'),'size_bytes':3,'sources':{name:{'commit':pin,'dirty':False} for name,pin in c.PROFILE['service_sources'].items()},'models':{'audio':False,'radio':True},'imports':['memcpy'],'exports':['t5_driver_get']}
        return manifest,record

    def test_provider_rejects_full_audio_profile_and_wrong_custody(self):
        m,r=self.provider();self.assertEqual(c.validate_provider(m,b'elf',r),r)
        for fault in ('source','mask','audio','bytes','bool','dependency'):
            m,r=self.provider()
            if fault=='source':r['sources']['utilities']['dirty']=True
            elif fault=='mask':r['source_mask']=3
            elif fault=='audio':r['models']['audio']=True
            elif fault=='bytes':r['elf_sha256']='0'*64
            elif fault=='bool':m['requires'][0]['api']=True
            else:m['requires'].append({'capability':'audio.input','api':1})
            with self.subTest(fault=fault),self.assertRaises(ValueError):c.validate_provider(m,b'elf',r)

if __name__=='__main__':unittest.main()
