#!/usr/bin/env python3
"""Recover checksum-valid X4 diagnostic records from an offline flash read.

This is a forensic scan, not an NVS transaction/entry-state parser. It can find
old or not-yet-indexed blob data as well as live blobs; reports explicitly mark
that distinction. It never accesses a device and never writes to firmware.
"""
import argparse
import json
import struct
from pathlib import Path

SESSION_MAGIC=0x58424c31
RECORD_MAGIC=0x58344233
SESSION_SIZE=456
PHASE={1:'app-main',2:'rail-ready',3:'arduino-variant',4:'setup-gate'}
KIND={0:'none',1:'boot',2:'provider',3:'app',4:'first-display',5:'failure'}

def fnv(data: bytes) -> int:
    value=2166136261
    for byte in data:value=((value^byte)*16777619)&0xffffffff
    return value

def cstring(raw: bytes) -> str:
    if b'\0' not in raw:raise ValueError('unterminated field')
    return raw.split(b'\0',1)[0].decode('utf-8',errors='replace')

def parse(raw: bytes, offset: int) -> dict:
    if len(raw)!=SESSION_SIZE:raise ValueError('incomplete record')
    magic,size,seq,revision,writes=struct.unpack_from('<IIQII',raw)
    if magic!=SESSION_MAGIC or size!=SESSION_SIZE or not seq or not revision:
        raise ValueError('invalid session header')
    if struct.unpack_from('<I',raw,448)[0]!=fnv(raw[:448]):
        raise ValueError('session checksum mismatch')
    r=raw[24:288]
    values=struct.unpack_from('<18I',r)
    (rmagic,rcrc,boot,phase,op,reset,raw0,raw1,wake,gpio,hold,brownout,
     entry_us,variant_us,gate_us,gpio1,strap,reserved)=values
    last_us,display_us,kind,count,display=struct.unpack_from('<QQIII',r,72)
    truncated=struct.unpack_from('<I',r,260)[0]
    if (rmagic!=RECORD_MAGIC or rcrc!=fnv(r[8:]) or not boot or phase not in PHASE or
        op>7 or reserved or kind not in KIND or display>1 or truncated>1 or
        ((count==0)!=(kind==0))):raise ValueError('invalid/checksum-failed checkpoint')
    return dict(sequence=seq,revision=revision,flash_offset=hex(offset),
        evidence='checksum-valid forensic candidate; NVS commit/index state not checked',
        power_source='unmeasured',phase=PHASE[phase],operation=op,rtc_boot=boot,
        reset=reset,raw_reset=[raw0,raw1],wake=wake,entry_us=entry_us,
        variant_us=variant_us,gate_us=gate_us,gpio_before=hex(gpio),gpio_high_before=hex(gpio1),
        strap=hex(strap),hold=hex(hold),brownout_register=hex(brownout),
        milestone_count=count,milestone_kind=KIND[kind],last_us=last_us,
        first_display=bool(display),first_display_us=display_us,
        last_line=cstring(r[100:260]),first_failure=cstring(raw[288:448]),
        checkpoint_writes=writes)

def recover(data: bytes) -> list[dict]:
    marker=struct.pack('<I',SESSION_MAGIC)
    found={};cursor=0
    while True:
        offset=data.find(marker,cursor)
        if offset<0:break
        cursor=offset+1
        try:record=parse(data[offset:offset+SESSION_SIZE],offset)
        except (ValueError,struct.error):continue
        key=(record['sequence'],record['revision'])
        found.setdefault(key,record)
    return [found[key] for key in sorted(found)]

def render(records: list[dict]) -> str:
    lines=['X4 saved boot diagnostics — offline flash recovery',
      'No USB/serial capture was needed during the recorded attempt.',
      'Records below are checksum-valid forensic candidates. NVS transaction/index state is NOT checked.',
      'Raw scanning may recover superseded writes; a missing record does NOT prove that the CPU never started.','']
    for r in records:
        lines.append(f"sequence={r['sequence']} revision={r['revision']} offset={r['flash_offset']} phase={r['phase']} operation={r['operation']}")
        for key in ('reset','raw_reset','wake','entry_us','variant_us','gate_us','gpio_before',
                    'gpio_high_before','strap','hold','brownout_register','milestone_kind',
                    'last_us','first_display','first_display_us','last_line','first_failure'):
            lines.append(f'  {key}={r[key]}')
        lines.append('')
    if not records:lines.append('No matching intact records recovered; cause remains undetermined.')
    return '\n'.join(lines)+'\n'

def main() -> None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('flash_read',type=Path,help='offline unencrypted NVS or full-flash binary')
    p.add_argument('--output',type=Path,default=Path('x4-boot.log'))
    p.add_argument('--json',action='store_true')
    args=p.parse_args()
    data=args.flash_read.read_bytes()
    if len(data)>32*1024*1024:raise SystemExit('Input exceeds 32 MiB.')
    if args.output.resolve()==args.flash_read.resolve():raise SystemExit('Output must not overwrite input.')
    records=recover(data)
    args.output.write_text(json.dumps(records,indent=2)+'\n' if args.json else render(records))
    print(f'Recovered {len(records)} candidate revisions into {args.output}')

if __name__=='__main__':main()
