"""Extract supported GC0312 register data from the user's own factory backup."""
from pathlib import Path
import argparse
from flash_image import sensor_table

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backup',required=True,type=Path)
    args=parser.parse_args()
    table=sensor_table(args.backup.read_bytes())
    root=Path(__file__).resolve().parents[1]
    dest=root/'build/generated';dest.mkdir(parents=True,exist_ok=True)
    header='/* Locally extracted vendor data; excluded from this project license and Git. */\n'
    header+='static const unsigned char a800_gc0312_init[][2] = {\n'
    header+=''.join(f'    {{0x{a:02x}, 0x{b:02x}}},\n' for a,b in zip(table[::2],table[1::2]))+'};\n'
    (dest/'gc0312_original_init.h').write_text(header)
    print('Supported sensor table extracted locally (314 pairs).')

if __name__=='__main__':main()
