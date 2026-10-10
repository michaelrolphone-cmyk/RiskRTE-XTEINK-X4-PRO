"""Direct BIN resolver proof and rejection tests; no native instructions."""
import hashlib,struct,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from native_binary_exports import exports,reference_tables,segments
class BinaryExports(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.raw=Path(sys.argv_input[0]).read_bytes();cls.tables=reference_tables(Path(sys.argv_input[1]).read_bytes())
  cls.exports,cls.proof=exports(cls.raw,cls.tables)
 def resign(self,data):
  checksum=0xef
  for part in segments(self.raw):
   for byte in data[part['offset']:part['offset']+part['size']]:checksum^=byte
  data[-33]=checksum;data[-32:]=hashlib.sha256(data[:-32]).digest();return bytes(data)
 def test_expected(self):self.assertEqual(len(self.exports),39)
 def test_truncated(self):
  with self.assertRaises(ValueError):exports(self.raw[:-1],self.tables)
 def test_checksum(self):
  data=bytearray(self.raw);data[100]^=1
  with self.assertRaises(ValueError):exports(bytes(data),self.tables)
 def test_appended_hash(self):
  data=bytearray(self.raw);data[-1]^=1
  with self.assertRaises(ValueError):exports(bytes(data),self.tables)
 def test_null_pointer_valid_checksum(self):
  data=bytearray(self.raw);at=self.proof['tables'][0]['file_offset'];data[at:at+4]=bytes(4)
  with self.assertRaises(ValueError):exports(self.resign(data),self.tables)
 def test_null_address_valid_checksum(self):
  data=bytearray(self.raw);at=self.proof['tables'][0]['file_offset'];data[at+4:at+8]=bytes(4)
  with self.assertRaises(ValueError):exports(self.resign(data),self.tables)
 def test_bad_terminator_valid_checksum(self):
  data=bytearray(self.raw);row=self.proof['tables'][0];data[row['file_offset']+row['bytes']-1]=1
  with self.assertRaises(ValueError):exports(self.resign(data),self.tables)
 def test_unexpected_name(self):
  tables={k:list(v) for k,v in self.tables.items()};tables[next(iter(tables))][1]='unavailable_export'
  with self.assertRaises(ValueError):exports(self.raw,tables)
if __name__=='__main__':
 sys.argv_input=sys.argv[1:3];sys.argv=sys.argv[:1];unittest.main()
