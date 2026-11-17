"""Read, prepare, install or restore this supported BK7252N board over 3.3 V UART."""
from pathlib import Path
import argparse,hashlib,json,time
from flash_image import FLASH_SIZE,START,LIMIT,CONFIG,encode,sensor_table

ROOT=Path(__file__).resolve().parents[1]
STATE=ROOT/'private'

def digest(data):return hashlib.sha256(data).hexdigest()

def write_verified(chip,address,data,window,erase_size):
    low,high=window
    if address%4096 or len(data)!=4096 or not low<=address<high:
        raise ValueError('Write outside the explicitly selected partition')
    for _ in range(3):
        chip.flash_unprotect()
        chip.flash_erase_block(address,erase_size)
        if chip.flash_read_4k(address,False)!=b'\xff'*4096:continue
        chip.flash_write_4k(address,data,crc_check=False)
        if chip.flash_read_4k(address,False)==data:return
        if chip.flash_read_4k(address,False)==data:return
    raise RuntimeError(f'Flash verification failed at 0x{address:x}')

def read_flash(chip):
    image=bytearray()
    for address in range(0,FLASH_SIZE,4096):
        image.extend(chip.flash_read_4k(address,False))
        if address%0x40000==0:print(f'Read 0x{address:x}/0x{FLASH_SIZE:x}',flush=True)
    return bytes(image)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=['backup','prepare','install','restore','reset-access'])
    p.add_argument('--port',help='Serial port, for example COM3')
    p.add_argument('--backup',type=Path,default=STATE/'factory.bin')
    p.add_argument('--firmware',type=Path,default=ROOT/'build/firmware.bin')
    p.add_argument('--write',action='store_true',help='Required for any erase/write operation')
    a=p.parse_args();STATE.mkdir(exist_ok=True)
    original=None;candidate=None;window=(START,LIMIT)
    if a.action!='backup':
        original=a.backup.read_bytes();sensor_table(original)
        if a.action in ['prepare','install']:
            candidate=encode(a.firmware.read_bytes());window=(START,START+len(candidate))
        elif a.action=='restore':candidate=original[START:LIMIT]
        elif a.action=='reset-access':
            window=(0x1e3000,0x1e5000);candidate=b'\xff'*8192
        if a.action in ['prepare','install']:
            (ROOT/'build/application-crc.bin').write_bytes(candidate)
            (ROOT/'build/application.json').write_text(json.dumps({'start':START,'end':window[1],
                'sha256':digest(candidate),'raw_sha256':digest(a.firmware.read_bytes())},indent=2))
        print(f'Physical range 0x{window[0]:x}..0x{window[1]:x}; SHA256 {digest(candidate)}',flush=True)
        if a.action=='prepare' or not a.write:
            print('Preparation only. No serial port opened.');return
    elif a.backup.exists():raise SystemExit('Refusing to replace an existing factory backup')
    if not a.port:p.error('--port is required for serial I/O')
    from bk7231tools.serial import BK7231Serial
    from bk7231tools.serial.base import BkProtocolType,EraseSize
    chip=BK7231Serial(a.port,baudrate=115200,cmnd_timeout=2)
    linked=False;original_sr=None;current=None;touched=[]
    try:
        chip.serial.dtr=False;chip.serial.rts=False;chip.serial.reset_input_buffer()
        print('Waiting for physical power cycle (180 seconds). Keep UART/power connected after reset.',flush=True)
        linked=chip.wait_for_link(180)
        if not linked:raise RuntimeError('No bootloader handshake; nothing written')
        chip.protocol_type=BkProtocolType.FULL;chip.read_retries=1;chip.write_retries=1;chip.flash_erase_checked=True
        device=chip.flash_read_id()
        if device['id']!=bytes.fromhex('eb6015') or device['size']!=FLASH_SIZE:
            raise RuntimeError('Unsupported flash; expected EB6015, 2 MiB')
        current=read_flash(chip)
        if a.action=='backup':
            # A second read catches intermittent UART problems before trusting a backup.
            if read_flash(chip)!=current:raise RuntimeError('Backup reads disagree; check power and UART wiring')
            a.backup.parent.mkdir(parents=True,exist_ok=True);a.backup.write_bytes(current)
            a.backup.with_suffix('.sha256').write_text(digest(current)+'\n')
            print('Verified full 2 MiB backup saved. Keep it private.');return
        if current[:4096]!=original[:4096] or current[0x1e0000:0x1e1000]!=original[0x1e0000:0x1e1000]:
            raise RuntimeError('Bootloader/RF region does not match this backup; refusing another unit backup')
        for address,magic in [(CONFIG,b'WLC1'),(0x1e3000,b'SLC2'),(0x1e4000,b'SLC2')]:
            sector=current[address:address+4096]
            if sector!=b'\xff'*4096 and sector[:4]!=magic:
                raise RuntimeError(f'Unknown data in reserved sector 0x{address:x}; nothing erased')
        snapshot=STATE/('before-'+time.strftime('%Y%m%d-%H%M%S')+'.bin')
        snapshot.write_bytes(current)
        original_sr=chip.flash_read_sr(2)
        try:
            for address in range(window[0],window[1],4096):
                block=candidate[address-window[0]:address-window[0]+4096]
                if current[address:address+4096]==block:continue
                touched.append(address)
                write_verified(chip,address,block,window,EraseSize.SECTOR_4K)
                if (address-window[0])%32768==0:print(f'Written and verified 0x{address:x}',flush=True)
            if chip.flash_read_4k(0,False)!=original[:4096]:raise RuntimeError('Bootloader verification failed')
        except BaseException:
            print('Install interrupted; attempting to restore touched sectors while UART remains connected.',flush=True)
            for address in touched:write_verified(chip,address,current[address:address+4096],window,EraseSize.SECTOR_4K)
            raise
        (STATE/'last-install.json').write_text(json.dumps({'action':a.action,'start':window[0],'end':window[1],
            'sha256':digest(candidate),'before_backup':snapshot.name},indent=2))
        print('All changed sectors verified. Original bootloader preserved.',flush=True)
    finally:
        try:
            if linked:
                if original_sr is not None:chip.flash_write_sr(original_sr,size=2,mask=0xfffc)
                try:chip.reboot_chip()
                except Exception:pass
        finally:chip.close()

if __name__=='__main__':main()
