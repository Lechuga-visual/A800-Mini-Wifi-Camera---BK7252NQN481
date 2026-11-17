"""Compile and namespace TLS independently of the SDK's legacy Wi-Fi crypto."""
from pathlib import Path
import concurrent.futures
import hashlib
import subprocess

work = Path(__file__).resolve().parents[1]
src = work / '.deps/mbedtls-3.6.8'
out = work / 'build/tls'
out.mkdir(parents=True,exist_ok=True)
tc = work / '.deps/OpenBK7231N/platforms/bk7231n/toolchain/windows/gcc-arm-none-eabi-4_9-2015q1/bin'
expected = 'bc54b818e87f49c0b3281dd85d24adaede0e63b4e822e3fbd49c005a38b9595b'
assert hashlib.sha256((work/'.deps/mbedtls-3.6.8.tar.bz2').read_bytes()).hexdigest() == expected
flags = ['-c', '-Os', '-std=c99', '-mthumb', '-mcpu=arm968e-s',
         '-march=armv5te', '-mthumb-interwork', '-mlittle-endian',
         '-ffunction-sections', '-fdata-sections', '-fno-common',
         '-DMBEDTLS_CONFIG_FILE="localcam_tls_config.h"',
         '-I'+str(work/'firmware'), '-I'+str(work/'build/generated'), '-I'+str(src/'include'), '-I'+str(src/'library')]

def compile_one(path):
    obj = out/(path.stem+'.o')
    result = subprocess.run([str(tc/'arm-none-eabi-gcc.exe'), *flags, str(path), '-o', str(obj)],
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(path.name+'\n'+result.stdout+result.stderr)
    return obj

with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    objects = list(pool.map(compile_one, sorted((src/'library').glob('*.c'))))
symbols = set()
for obj in objects:
    result = subprocess.check_output([str(tc/'arm-none-eabi-nm.exe'), '-g', '--defined-only', str(obj)], text=True)
    for line in result.splitlines():
        fields = line.split()
        if len(fields) == 3:
            name = fields[-1]
            assert name.startswith(('mbedtls_', 'psa_', 'mbedtls', 'everest_', 'Hacl_')), name
            symbols.add(name)
mapping = out/'symbols.map'
mapping.write_text(''.join(f'{s} lc_{s}\n' for s in sorted(symbols)))
(out/'localcam_tls_symbols.h').write_text(''.join(f'#define {s} lc_{s}\n' for s in sorted(symbols)))
renamed = []
for obj in objects:
    target = out/('lc_'+obj.name)
    subprocess.run([str(tc/'arm-none-eabi-objcopy.exe'), '--redefine-syms='+str(mapping), str(obj), str(target)], check=True)
    renamed.append(target)
archive = out/'liblocalcam_tls.a'
if archive.exists():
    archive.unlink()
subprocess.run([str(tc/'arm-none-eabi-ar.exe'), 'rcs', str(archive), *map(str, renamed)], check=True)
subprocess.run([str(tc/'arm-none-eabi-gcc.exe'), *flags, '-I'+str(out),
                str(work/'firmware/localcam_tls.c'), '-o', str(out/'localcam_tls.o')], check=True)
print(f'TLS 3.6.8: {len(objects)} objects, {len(symbols)} isolated symbols, {archive.stat().st_size} archive bytes')
