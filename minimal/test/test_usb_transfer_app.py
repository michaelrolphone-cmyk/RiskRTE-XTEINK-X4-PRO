#!/usr/bin/env python3
import copy,hashlib,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import usb_transfer_app as app
class UsbTransferAdmission(unittest.TestCase):
 def setUp(self):
  self.blob=b'fixture';self.header=b'canonical';self.source='a'*40
  self.manifest={'id':'usb_sd_transfer','version':app.PROFILE['version'],'file_name':'usb_sd_transfer.elf','requires':[{'capability':c,'api':v} for c,v in app.REQUIREMENTS]}
  self.record={'version':app.PROFILE['version'],'repository_commit':self.source,'working_tree_dirty':False,'sha256':hashlib.sha256(self.blob).hexdigest(),'size_bytes':len(self.blob),'defines':list(app.FLAGS),'sdk_sha256':{'RiscUsbDeviceMscV1.h':hashlib.sha256(self.header).hexdigest()},'capability':{'name':'usb.device.msc','api':1,'instance_id':0}}
  self.record['sd_preparation']=copy.deepcopy(app.PROFILE['sd_preparation'])
  self.record.update({k:app.PROFILE[k] for k in ('usb_role','poll_interval_ms','sleep','navigation_requires_release','configured_stop_requires_cable_confirmation')})
 def validate(self):return app.validate(self.manifest,self.blob,self.record,self.source,self.header)
 def test_exact_owner_grants(self):self.assertEqual(self.validate(),app.PROFILE['grants'])
 def test_no_other_app_or_extra_power(self):
  for key,value in [('id','default'),('version','0.1.0'),('file_name','default.elf')]:
   old=self.manifest[key];self.manifest[key]=value
   with self.assertRaises(ValueError):self.validate()
   self.manifest[key]=old
  self.manifest['requires'].append({'capability':'x4.power','api':1})
  with self.assertRaises(ValueError):self.validate()
 def test_changed_sdk_or_elf(self):
  self.header=b'changed'
  with self.assertRaises(ValueError):self.validate()
  self.header=b'canonical';self.blob=b'corrupt'
  with self.assertRaises(ValueError):self.validate()
 def test_unsafe_or_unverified_selection(self):
  for k,v in [('sleep',True),('poll_interval_ms',50),('working_tree_dirty',True),('repository_commit','b'*40),('navigation_requires_release',False),('configured_stop_requires_cable_confirmation',False),('usb_role','host'),('defines',list(app.FLAGS)+['-DPORTABLE_APP_SLEEP_LOCAL'])]:
   old=copy.deepcopy(self.record[k]);self.record[k]=v
   with self.assertRaises(ValueError):self.validate()
   self.record[k]=old
 def test_preparation_must_be_explicit_and_settled(self):
  good=copy.deepcopy(self.record['sd_preparation'])
  for key,value in [('extension_tag','0x00000000'),('extension_version',2),('explicit_step',False),('settled_screen_required',False),('max_steps_per_input_poll',2),('max_steps_per_input_poll',True),('wait_poll_prepares',True)]:
   self.record['sd_preparation']=copy.deepcopy(good);self.record['sd_preparation'][key]=value
   with self.assertRaises(ValueError):self.validate()
  self.record.pop('sd_preparation')
  with self.assertRaises(ValueError):self.validate()
 def test_catalog_retains_existing_entries(self):
  data=json.loads((app.ROOT/'minimal/apps/catalog.json').read_text())['apps']
  self.assertEqual(len(data),19);self.assertEqual(data[-2]['file_name'],'usb_sd_transfer.elf')
  self.assertEqual(data[-1],{'display_name':'GameBoy','file_name':'gameboy.elf','icon':'solid:f11b'})
  self.assertEqual(len({a['file_name'] for a in data}),19)
if __name__=='__main__':unittest.main()
