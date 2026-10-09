#!/usr/bin/env python3
"""Verify the experiment against decoded call sites in the exact Xtensa ELF."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
from elftools.elf.elffile import ELFFile

import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--target',type=Path,required=True)
parser.add_argument('--baseline',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
a=parser.parse_args()
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('composition',root/'minimal/scripts/prepare_native_runtime.py')
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
target=a.target;base=a.baseline

def inspect(raw):
    elf=ELFFile(io.BytesIO(raw))
    symbols={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    calls={}
    def edge(caller,callee):
        found=[x for x in c.xtensa_calls(elf,symbols,caller) if x['target']==symbols[callee]['st_value']]
        assert len(found)==1,(caller,callee,len(found))
        calls[caller+' -> '+callee]=found[0]
        return found[0]['instruction']
    stages=['__wrap_spi_flash_init_chip_state','__wrap_rtc_clk_recalib_bbpll',
            'spi_timing_flash_tuning','esp_spiram_init','__wrap_esp_clk_init']
    positions=[edge('call_start_cpu0',s) for s in stages]
    assert positions==sorted(positions)
    assert edge('call_start_cpu0','bootloader_flash_update_id') < positions[0]
    assert edge('call_start_cpu0','Cache_Resume_DCache') < positions[0]
    early=[edge('__wrap_spi_flash_init_chip_state',s) for s in
           ['rtc_clk_recalib_bbpll','rtc_init','spi_flash_init_chip_state']]
    assert early==sorted(early)
    edge('__wrap_esp_clk_init','esp_clk_init')
    edge('esp_clk_init','__wrap_rtc_init')
    edge('__wrap_rtc_init','rtc_init')
    edge('__wrap_rtc_clk_recalib_bbpll','rtc_clk_recalib_bbpll')
    sections={}
    for name in stages[:2]+['__wrap_esp_clk_init','__wrap_rtc_init']:
        s=symbols[name];section=elf.get_section(s['st_shndx'])
        assert section.name=='.iram0.text'
        sections[name]={'address':s['st_value'],'section':section.name,'bytes':s['st_size']}
    for name in ['risc_x4_rtc_order_state','risc_x4_rtc_order_skipped']:
        s=symbols[name];section=elf.get_section(s['st_shndx'])
        assert section.name=='.dram0.data' and c.elf_symbol_bytes(elf,s)==b'\0'*4
        sections[name]={'address':s['st_value'],'section':section.name,'initial':0}
    assert b'X4_STARTUP_EXPERIMENT:rtc-before-mspi:1:upstream-5b71b949\0' in raw
    return elf,symbols,{'calls':calls,'sections':sections}

raw=(target/'firmware.elf').read_bytes();elf,symbols,proof=inspect(raw)
base_raw=(base/'firmware.elf').read_bytes();old=ELFFile(io.BytesIO(base_raw))
old_symbols={s.name:s for s in old.get_section_by_name('.symtab').iter_symbols()}
for name in ['esp_clk_init','rtc_init','rtc_clk_recalib_bbpll']:
    # Instructions remain byte-identical; bound addresses/literal targets can
    # differ because the native wrapper redirects one RTC call.
    unchanged=c.elf_symbol_bytes(old,old_symbols[name])==c.elf_symbol_bytes(elf,symbols[name])
    proof.setdefault('original_function_instruction_bytes_equal',{})[name]=unchanged
    old_calls=[x['target'] for x in c.xtensa_calls(old,old_symbols,name)]
    new_calls=[x['target'] for x in c.xtensa_calls(elf,symbols,name)]
    old_names={s['st_value']:n for n,s in old_symbols.items()}
    new_names={s['st_value']:n for n,s in symbols.items()}
    mapped_old=[old_names.get(x,hex(x)) for x in old_calls]
    mapped_new=[new_names.get(x,hex(x)) for x in new_calls]
    if name=='esp_clk_init': mapped_old=['__wrap_rtc_init' if n=='rtc_init' else n for n in mapped_old]
    assert mapped_old==mapped_new,(name,mapped_old,mapped_new)
    proof.setdefault('original_function_call_sequence_preserved',{})[name]=mapped_new

# Removing the entry hook cannot leave a false-positive symbol-only proof.
try: inspect(base_raw)
except (AssertionError,KeyError): proof['original_without_wrappers_rejected']=True
else: raise AssertionError('Base image unexpectedly passed experiment proof')
assert (target/'bootloader.bin').read_bytes()==(base/'bootloader.bin').read_bytes()
assert (target/'partitions.bin').read_bytes()==(base/'partitions.bin').read_bytes()
proof.update({'schema':'x4.rtc-before-mspi-experiment-proof','hardware_qualified':False,
 'elf_sha256':hashlib.sha256(raw).hexdigest(),
 'firmware_sha256':hashlib.sha256((target/'firmware.bin').read_bytes()).hexdigest(),
 'source_sha256':hashlib.sha256((root/'minimal/native/X4RtcBeforeMspi.c').read_bytes()).hexdigest(),
 'bootloader_and_partition_bytes_unchanged':True,
 'config_preserved':['RTC_CONFIG_DEFAULT','power-on-only cali_ocode=1','PLL recalibration',
                    '240MHz CPU','80MHz DIO flash','80MHz octal PSRAM','brownout level7','RTC watchdog'],
 'limitation':'rtc_init and its configuration literal still use already-mapped flash; this is not a pre-cache hook'})
a.output.write_text(json.dumps(proof,indent=2)+'\n')
print('Exact Xtensa early/late call order, four wrapper bindings, original SDK call sequences, DRAM/IRAM placement, base-image rejection, unchanged bootloader/partitions PASS')
print('ELF:',proof['elf_sha256'])
