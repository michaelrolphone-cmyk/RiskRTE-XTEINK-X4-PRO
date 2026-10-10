"""Witness native public resolver tables in an exact ESP32 application BIN.

The ELF is optional: table names/row order come from a qualified reference and
are checked against unchanged resolver source. No native instructions execute.
"""
import hashlib, io, struct
from pathlib import Path

def sha(b): return hashlib.sha256(b).hexdigest()
def require(ok, message):
    if not ok: raise ValueError(message)

def segments(raw):
    require(len(raw) >= 32 and raw[0] == 0xe9 and 0 < raw[1] < 17, 'Invalid ESP image header')
    at=24; result=[]; checksum=0xef
    for _ in range(raw[1]):
        require(at+8<=len(raw),'Truncated ESP segment header')
        address,size=struct.unpack_from('<II',raw,at);at+=8
        require(size and at+size<=len(raw),'Truncated ESP segment')
        data=raw[at:at+size]
        for byte in data: checksum^=byte
        result.append({'address':address,'offset':at,'size':size,'data':data});at+=size
    checksum_at=(at+16)//16*16-1
    require(checksum_at<len(raw) and raw[checksum_at]==checksum,'ESP checksum mismatch')
    require(raw[23]==1 and len(raw)==checksum_at+1+32,'Expected appended ESP SHA256')
    require(raw[-32:]==hashlib.sha256(raw[:checksum_at+1]).digest(),'ESP appended SHA256 mismatch')
    return result

def reference_tables(elf_raw):
    from elftools.elf.elffile import ELFFile
    e=ELFFile(io.BytesIO(elf_raw));symbols={s.name:s for s in e.get_section_by_name('.symtab').iter_symbols()}
    def mapped(address,count):
        found=[s.data()[address-s['sh_addr']:address-s['sh_addr']+count] for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']!='SHT_NOBITS' and s['sh_addr']<=address and address+count<=s['sh_addr']+s['sh_size']]
        require(len(found)==1,'Reference address is ambiguous/unmapped');return found[0]
    result={}
    for name in ('g_esp_libc_elfsyms','_ZZN8RiscBoot7Runtime3runEvE7symbols'):
        symbol=symbols[name];raw=mapped(symbol['st_value'],symbol['st_size']);names=[]
        require(raw[-8:]==bytes(8),'Reference table terminator missing')
        for off in range(0,len(raw)-8,8):
            ptr,address=struct.unpack_from('<II',raw,off);require(ptr and address,'Reference empty row')
            text=bytearray()
            for i in range(96):
                b=mapped(ptr+i,1)[0]
                if b==0:break
                text.append(b)
            else: raise ValueError('Unterminated reference export')
            names.append(text.decode('ascii'))
        result[name]=names
    return result

def exports(raw,tables):
    parts=segments(raw)
    def mapped(address,count):
        matches=[s['data'][address-s['address']:address-s['address']+count] for s in parts if s['address']<=address and address+count<=s['address']+s['size']]
        if len(matches)!=1:raise ValueError('Address ambiguous/unmapped')
        return matches[0]
    def name_at(address):
        text=bytearray()
        for i in range(96):
            b=mapped(address+i,1)[0]
            if b==0:return text.decode('ascii')
            text.append(b)
        raise ValueError('Unterminated export name')
    result={};evidence=[]
    for table,names in tables.items():
        matches=[];size=(len(names)+1)*8
        # Anchor on actual NUL-terminated first-name bytes, then prove every
        # pointer/name row and the exact final zero record.
        pointers=[]
        needle=names[0].encode()+b'\0'
        for part in parts:
            at=part['data'].find(needle)
            while at>=0:
                pointers.append(part['address']+at);at=part['data'].find(needle,at+1)
        for part in parts:
            for pointer in pointers:
                needle=struct.pack('<I',pointer);at=part['data'].find(needle)
                while at>=0:
                    data=part['data'][at:at+size]
                    if at%4==0 and len(data)==size and data[-8:]==bytes(8):
                        rows=[]
                        try:
                            for index,expected in enumerate(names):
                                ptr,address=struct.unpack_from('<II',data,index*8)
                                require(ptr and address and name_at(ptr)==expected,'Resolver row differs')
                                rows.append({'name':expected,'name_address':ptr,'address':address})
                        except (ValueError,UnicodeDecodeError):pass
                        else:matches.append({'table':table,'address':part['address']+at,'file_offset':part['offset']+at,'bytes':size,'sha256':sha(data),'rows':rows})
                    at=part['data'].find(needle,at+1)
        require(len(matches)==1,'Missing or ambiguous complete resolver table: '+table)
        witness=matches[0];evidence.append(witness)
        for row in witness['rows']:
            require(row['name'] not in result,'Duplicate export');result[row['name']]=row['address']
    return result, {'firmware_sha256':sha(raw),'elf_used_for_candidate':False,'esp_checksum_and_appended_sha256':True,'segments':[{k:v for k,v in s.items() if k!='data'} for s in parts],'tables':evidence,'exports':result}

def qualified_exports(runtime,raw,reference_runtime,reference_elf,reference_bin):
    import verify_update_elf
    from elftools.elf.elffile import ELFFile
    table_file='lib/elf_loader/src/esp_elf_symbol.c'
    require((runtime/table_file).read_bytes()==(reference_runtime/table_file).read_bytes(),'Public resolver source differs from reference')
    line='static const esp_elfsym symbols[]={{"risc_runtime_get_api",reinterpret_cast<const void*>(&risc_runtime_get_api)},ESP_ELFSYM_END};'
    require(line in (runtime/'src/bootstrap/Runtime.cpp').read_text() and line in (reference_runtime/'src/bootstrap/Runtime.cpp').read_text(),'Runtime export table source differs')
    elf=reference_elf.read_bytes();tables=reference_tables(elf)
    reference,proof=exports(reference_bin.read_bytes(),tables)
    require(reference==verify_update_elf.public_exports(ELFFile(io.BytesIO(elf))),'BIN decoder differs from complete reference ELF symbol witness')
    result,evidence=exports(raw,tables)
    evidence['decoder_reference']={'bin_sha256':sha(reference_bin.read_bytes()),'elf_sha256':sha(elf),'all_export_names_and_addresses_equal':True}
    evidence['unchanged_public_resolver_source_sha256']=sha((runtime/table_file).read_bytes())
    evidence['native_elf_check']='Not run: original .99 firmware.elf was not delivered; exact firmware BIN resolver tables checked instead.'
    return result,evidence
