from pathlib import Path
import importlib.util
import sys
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('recover',root/'scripts/recover_bootlog.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
data=(Path(sys.argv[1])).read_bytes()
r=m.recover(data)
assert len(r)==8
assert [x['sequence'] for x in r]==list(range(5,13))
print('PASS offline extractor reads actual C++-serialized session records')
assert m.recover(b'\xff'*4096)==[]
assert m.recover(data[:data.index(b'1LBX')+80])==[]
print('PASS erased/truncated data is not presented as a boot record')
corrupt=bytearray(data)
for x in r:corrupt[int(x['flash_offset'],16)+80]^=1
assert m.recover(corrupt)==[]
print('PASS altered records are rejected by checksums')
text=m.render(r)
assert 'NOT checked' in text and 'does NOT prove' in text
assert 'sequence=5' in text
print('PASS recovery distinguishes forensic data from committed NVS state')
