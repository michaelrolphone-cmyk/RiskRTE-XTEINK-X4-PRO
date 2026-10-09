#!/usr/bin/env python3
"""Exercise the real bundle admission guard with staged receipt/ELF fixtures."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import unittest

import native_composition_test as fixture
composition=fixture.composition
commit=fixture.commit
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from build_test_bundle import validate_native_composition


@unittest.skipUnless(importlib.util.find_spec('elftools'),'Requires pinned pyelftools')
class NativeBundleCompositionTest(unittest.TestCase):
    def setUp(self):
        fixture.CompositionTest.setUp(self)
        self.record=fixture.CompositionTest.prepare(self)
        self.native=self.root/'native';self.native.mkdir()
        self.markers=['RTE_SOURCE='+self.revision,'RISC_RUNTIME_VERSION:0.1.57','RISC_PAIRED_STORE_ABI:2',
                      'X4_NATIVE_COMPOSITION:'+self.record['composition_sha256'],'RISC_APP_POLICY_ROWS:16']
        self.compile_elf()
        self.write('firmware.bin',b'\0'.join(marker.encode() for marker in self.markers)+b'\0')
        self.write('x4-native-composition.json',composition.encoded(self.record))
        self.proof=composition.startup_proof((self.native/'firmware.elf').read_bytes(),self.record)
        self.write('x4-native-proof.json',composition.encoded(self.proof))
        self.options_proof=composition.runtime_options_proof({name:(self.native/name).read_bytes() for name in ('firmware.bin','firmware.elf')},self.record)
        self.write('x4-runtime-options-proof.json',composition.encoded(self.options_proof))
        self.candidate={'schema':1,'source_sha':self.revision,'firmware_version':'0.1.57',
                        'target':composition.ENVIRONMENTS[0],'build_environment':composition.ENVIRONMENTS[0],
                        'layout':'riscrte-paired-appdata-v2','store_abi':2,'assets':{},'build_options':self.record['build_options'],
                        'x4_native_composition':self.summary()}
        self.refresh_assets()

    def write(self,name,data):
        (self.native/name).write_bytes(data)

    def compile_elf(self,weak=False,markers=None):
        source=self.root/'native_fixture.c'
        marker_values=markers or self.markers
        code=('__attribute__((weak)) ' if weak else '')+'void initVariant(void) {}\n'
        code+='void __wrap_app_main(void) {}\nvoid app_main(void) {}\nchar risc_x4_boot_record[264];\nvoid risc_native_diagnostic_observer(const char* p){(void)p;}\n'
        code+='void risc_native_diagnostic_drain(void) {}\nint risc_native_diagnostic_read(void) {return 0;}\n'
        code+='const char* risc_native_startup_error(void) {return 0;}\n'
        for index,marker in enumerate(marker_values):
            name='risc_x4_native_composition_identity' if index==3 else 'risc_app_policy_rows' if index==4 else 'marker_'+str(index)
            code+='const char '+name+'[] = "'+marker+'";\n'
        code+='int main(void) {initVariant();return 0;}\n'
        source.write_text(code)
        subprocess.run(['cc',str(source),'-o',str(self.native/'firmware.elf')],check=True)

    def summary(self):
        return {'build_options':self.record['build_options'],'runtime_options_proof':self.options_proof,'composition_sha256':self.record['composition_sha256'],'runtime':self.record['runtime'],
                'platform':self.record['platform'],'platform_source_sha256':self.record['platform_source_sha256'],
                'startup_proof':self.proof}

    def refresh_assets(self):
        self.candidate['assets']={path.name:{'bytes':len(path.read_bytes()),'sha256':composition.sha(path.read_bytes())}
                                  for path in self.native.iterdir()}

    def validate(self):
        return validate_native_composition(self.native,self.candidate,self.runtime,self.platform)

    def test_exact_staged_native_passes_without_source_files_in_stage(self):
        self.assertEqual(self.validate(),self.candidate['x4_native_composition'])
        self.assertFalse((self.native/'src').exists())

    def test_options_request_and_compiled_proof_must_match(self):
        self.candidate['build_options']={'app_policy_rows':17,'app_image_cache':False}
        with self.assertRaisesRegex(ValueError,'options differ'):self.validate()
        self.candidate['build_options']=self.record['build_options']
        self.write('x4-runtime-options-proof.json',b'{}');self.refresh_assets()
        with self.assertRaisesRegex(ValueError,'option proof mismatch'):self.validate()

    def test_plain_runtime_rejected_at_matching_version(self):
        del self.candidate['x4_native_composition']
        with self.assertRaisesRegex(ValueError,'generic Runtime is insufficient'):self.validate()

    def test_missing_receipts_rejected(self):
        for name in ('x4-native-composition.json','x4-native-proof.json','x4-runtime-options-proof.json'):
            previous=self.candidate['assets'].pop(name)
            with self.assertRaisesRegex(ValueError,'assets missing'):self.validate()
            self.candidate['assets'][name]=previous

    def test_asset_bytes_and_receipt_digest_rejected(self):
        self.write('x4-native-proof.json',b'{}')
        with self.assertRaisesRegex(ValueError,'asset hash mismatch'):self.validate()
        self.refresh_assets()
        with self.assertRaisesRegex(ValueError,'startup proof mismatch'):self.validate()

    def test_firmware_and_elf_require_compiled_marker(self):
        self.write('firmware.bin',b'generic runtime only')
        self.refresh_assets()
        with self.assertRaisesRegex(ValueError,'marker mismatch: firmware.bin'):self.validate()
        self.write('firmware.bin',b'\0'.join(marker.encode() for marker in self.markers)+b'\0')
        self.compile_elf(markers=self.markers[:3]+['X4_NATIVE_COMPOSITION:wrong'])
        self.refresh_assets()
        with self.assertRaisesRegex(ValueError,'marker mismatch: firmware.elf'):self.validate()

    def test_matching_strings_do_not_substitute_for_strong_hook(self):
        self.compile_elf(weak=True)
        self.refresh_assets()
        with self.assertRaisesRegex(ValueError,'Missing strong X4 native symbol'):self.validate()

    def test_runtime_lock_and_current_platform_commit_required(self):
        self.candidate['firmware_version']='0.1.56'
        with self.assertRaisesRegex(ValueError,'product lock'):self.validate()
        self.candidate['firmware_version']='0.1.57'
        (self.platform/'next-source').write_text('new committed product source')
        commit(self.platform)
        with self.assertRaisesRegex(ValueError,'exact requested commit'):self.validate()

    def test_rehashed_source_receipt_cannot_replace_committed_source(self):
        record=copy.deepcopy(self.record)
        record['platform_source_sha256']['minimal/native/X4EarlyBoot.cpp']='a'*64
        del record['composition_sha256'];record['composition_sha256']=composition.sha(composition.encoded(record))
        self.write('x4-native-composition.json',composition.encoded(record));self.refresh_assets()
        with self.assertRaisesRegex(ValueError,'Committed X4 source differs'):self.validate()

    def test_summary_cannot_disagree_with_actual_linked_proof(self):
        self.candidate['x4_native_composition']=copy.deepcopy(self.candidate['x4_native_composition'])
        self.candidate['x4_native_composition']['startup_proof']['pin']=5
        with self.assertRaisesRegex(ValueError,'composition summary mismatch'):self.validate()


if __name__=='__main__':unittest.main()
