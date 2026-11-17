"""Pure Python framing and partition guards; never touches hardware."""
from pathlib import Path
import sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from flash_image import START,LIMIT,FLASH_SIZE,crc16,encode,sensor_table

class FlashImageTests(unittest.TestCase):
    def test_known_crc(self):
        self.assertEqual(crc16(b'123456789'),bytes.fromhex('aee7'))

    def test_roundtrip_and_padding(self):
        raw=bytes(range(256))*33+b'last'
        image=encode(raw)
        self.assertEqual(len(image)%4096,0)
        decoded=b''.join(image[i:i+32] for i in range(0,((len(raw)+31)//32)*34,34))
        self.assertEqual(decoded[:len(raw)],raw)
        for i in range(0,((len(raw)+31)//32)*34,34):self.assertEqual(crc16(image[i:i+32]),image[i+32:i+34])
        self.assertLessEqual(START+len(image),LIMIT)

    def test_invalid_size(self):
        for data in [b'',b'x'*(0x110000+1)]:
            with self.assertRaises(ValueError):encode(data)

    def test_backup_rejection(self):
        for data in [b'',b'\xff'*FLASH_SIZE,b'\x00'*FLASH_SIZE]:
            with self.assertRaises(ValueError):sensor_table(data)

if __name__=='__main__':unittest.main()
