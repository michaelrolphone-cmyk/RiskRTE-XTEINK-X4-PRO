#!/usr/bin/env python3
"""Check actual Xtensa startup linkage and reject a wrapper-bypass mutation."""
import argparse
import importlib.util
import io
import json
from pathlib import Path
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('composition', ROOT / 'minimal/scripts/prepare_native_runtime.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
p = argparse.ArgumentParser()
p.add_argument('--workspace', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
record = c.verify_composition(a.workspace)
path = a.workspace / '.pio/build' / record['build_environment'] / 'firmware.elf'
data = path.read_bytes()
elf = ELFFile(io.BytesIO(data))
assert elf['e_machine'] == 'EM_XTENSA'
proof = c.startup_proof(data, record)
symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
edge = proof['target_call_edges']['main_task -> __wrap_app_main']
# Mutate the actual IDF task's linked target to the original Arduino entry.
# Retaining the wrapper symbol/marker must not make a bypassed image pass.
literal = edge['literal']
for section in elf.iter_sections():
    if section['sh_addr'] <= literal < section['sh_addr'] + section['sh_size']:
        offset = section['sh_offset'] + literal - section['sh_addr']
        break
else:
    raise AssertionError('Missing startup literal')
mutated = bytearray(data)
mutated[offset:offset + 4] = symbols['app_main']['st_value'].to_bytes(4, 'little')
try:
    c.startup_proof(bytes(mutated), record)
except ValueError as error:
    assert 'Unproven X4 startup call: main_task -> __wrap_app_main' in str(error)
else:
    raise AssertionError('Startup bypass mutation was admitted')
proof['verification'] = {'actual_target': True, 'bypass_mutation_rejected': True}
a.output.write_text(json.dumps(proof, indent=2) + '\n')
print('X4 actual Xtensa entry/RTC proof and bypass rejection PASS')
