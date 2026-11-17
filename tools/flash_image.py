"""Physical BK flash framing. No serial I/O or device-specific secrets."""
import hashlib
import struct

FLASH_SIZE=0x200000
START=0x11000
LIMIT=0x132000
CONFIG=0x1e2000
TABLE_HASH='d2c09c2ebfabeb85d917ce83c944ae9b2624ab4f046f7685179d7ceec38f9452'

def crc16(block):
    c=0xffff
    for byte in block:
        c^=byte<<8
        for _ in range(8):c=(((c<<1)^0x8005) if c&0x8000 else c<<1)&0xffff
    return c.to_bytes(2,'big')

def encode(raw):
    if not 0<len(raw)<=0x110000:raise ValueError('Application exceeds logical slot or is empty')
    encoded=bytearray()
    for at in range(0,len(raw),32):
        block=raw[at:at+32].ljust(32,b'\xff')
        encoded.extend(block+crc16(block))
    encoded.extend(b'\xff'*((-len(encoded))%4096))
    if START+len(encoded)>LIMIT:raise ValueError('Application exceeds physical slot')
    return bytes(encoded)

def sensor_table(original):
    if len(original)!=FLASH_SIZE:raise ValueError('Expected a full 2 MiB physical factory backup')
    for at in range(0,LIMIT,34):
        unit=original[at:at+34]
        if unit!=b'\xff'*34 and crc16(unit[:32])!=unit[32:]:
            raise ValueError(f'Invalid factory CRC at physical 0x{at:x}')
    image=b''.join(original[at:at+32] for at in range(0,len(original)-33,34))
    start,end=struct.unpack_from('<II',image,0x9b4b0)
    if (start,end)!=(0x16e43a,0x275):raise ValueError('Unsupported factory firmware layout')
    table=image[start:start+end-1]
    if len(table)!=628 or hashlib.sha256(table).hexdigest()!=TABLE_HASH:
        raise ValueError('Unsupported GC0312 initialization sequence')
    return table
