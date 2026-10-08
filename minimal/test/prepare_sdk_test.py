#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('prepare_sdk',ROOT/'scripts/prepare_sdk.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class SdkTest(unittest.TestCase):
 def test_unique_shared_header_and_conflict(self):
  with tempfile.TemporaryDirectory() as temporary:
   root=Path(temporary);runtime=root/'runtime';reader=root/'reader'
   for folder in [runtime/'sdk/driver',runtime/'sdk/hardware',reader/'sdk/driver']:folder.mkdir(parents=True)
   (runtime/'sdk/driver/Shared.h').write_text('#pragma once\n')
   (reader/'sdk/driver/Shared.h').write_text('#pragma once\n')
   (runtime/'sdk/hardware/Hardware.h').write_text('/* typed hardware */\n')
   result=module.prepare(runtime,reader,root/'sdk')
   self.assertEqual(result['Shared.h']['origins'],['runtime/driver','reader/driver'])
   self.assertEqual(result['RiscDisplayOutputMetricsV1.h']['origins'],['x4/interfaces'])
   self.assertEqual((root/'sdk/RiscDisplayOutputMetricsV1.h').read_bytes(),
                    (ROOT/'interfaces/RiscDisplayOutputMetricsV1.h').read_bytes())
   self.assertEqual(len(list((root/'sdk').glob('*.h'))),3)
   with self.assertRaises(ValueError):module.prepare(runtime,reader,root/'sdk')
   (reader/'sdk/driver/Shared.h').write_text('/* divergent */\n')
   with self.assertRaisesRegex(ValueError,'Shared SDK header differs'):module.prepare(runtime,reader,root/'conflict')
   self.assertFalse((root/'conflict').exists())
   (reader/'sdk/driver/Shared.h').write_text('#pragma once\n')
   (reader/'sdk/driver/RiscDisplayOutputMetricsV1.h').write_text('/* divergent diagnostic contract */\n')
   with self.assertRaisesRegex(ValueError,'Shared SDK header differs: RiscDisplayOutputMetricsV1.h'):
    module.prepare(runtime,reader,root/'metric-conflict')
if __name__=='__main__':unittest.main()
