from pathlib import Path
import sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from flash import write_verified

class Chip:
    def __init__(self):self.data=b'old!'*1024;self.calls=[];self.fail=0
    def flash_unprotect(self):self.calls.append('unprotect')
    def flash_erase_block(self,address,size):self.calls.append('erase');self.data=b'\xff'*4096
    def flash_read_4k(self,address,crc):return self.data
    def flash_write_4k(self,address,data,crc_check):
        self.calls.append('write')
        if self.fail:self.fail-=1;self.data=b'bad!'*1024
        else:self.data=data

class WriterTests(unittest.TestCase):
    def test_unprotect_per_attempt(self):
        chip=Chip();chip.fail=1
        write_verified(chip,0x11000,b'x'*4096,(0x11000,0x12000),4096)
        self.assertEqual(chip.data,b'x'*4096)
        self.assertEqual(chip.calls,['unprotect','erase','write','unprotect','erase','write'])

    def test_no_write_outside_window(self):
        chip=Chip()
        for address in [0,0x11001,0x12000]:
            with self.assertRaises(ValueError):write_verified(chip,address,b'x'*4096,(0x11000,0x12000),4096)
        self.assertEqual(chip.calls,[])

    def test_stops_after_three_failures(self):
        chip=Chip();chip.fail=3
        with self.assertRaises(RuntimeError):write_verified(chip,0x11000,b'x'*4096,(0x11000,0x12000),4096)
        self.assertEqual(chip.calls.count('erase'),3)

if __name__=='__main__':unittest.main()
