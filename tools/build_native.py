from pathlib import Path
import os, re, subprocess

work = Path(__file__).resolve().parents[1]
sdk = work / '.deps/beken-sdk'
toolchain = work / '.deps/OpenBK7231N/platforms/bk7231n/toolchain/windows/gcc-arm-none-eabi-4_9-2015q1/bin'
utils = work / '.deps/w64devkit/bin'
gcc = toolchain / 'arm-none-eabi-gcc.exe'
(sdk / 'config').mkdir(exist_ok=True)
config = (sdk / 'beken378/app/config/sys_config_bk7252n.h').read_text()
config_path = sdk / 'config/sys_config.h'
if not config_path.exists() or config_path.read_text() != config:
    config_path.write_text(config)
names = sorted(set(re.findall(r'^#define\s+(\w+)(?!\()', config, re.M)))
probe = '#include "config/sys_config.h"\n' + '\n'.join(f'valueOf_{n}={n}' for n in names)
(sdk / 'config_probe.c').write_text(probe)
preprocessed = subprocess.check_output([str(gcc), '-E', 'config_probe.c'], cwd=sdk, text=True)
(sdk / '.config').write_text('\n'.join(line[8:] for line in preprocessed.splitlines() if line.startswith('valueOf_')) + '\n')
for name in ['rwnx', 'ble', 'usb', 'cal', 'supplicant', 'uart_debug', 'rf_test', 'rf_use']:
    lib = sdk / f'beken378/lib/lib{name}_bk7252n.a'
    if lib.exists():
        (sdk / f'beken378/lib/lib{name}.a').write_bytes(lib.read_bytes())
env = dict(os.environ)
env['PATH'] = str(utils) + os.pathsep + str(toolchain) + os.pathsep + env['PATH']
args = [str(utils/'make.exe'), '-f', 'application.mk', 'application', '-j4',
        'ROOT_DIR='+sdk.as_posix(), 'OS=Windows_NT', 'SHELL='+str(utils/'sh.exe').replace('\\','/'),
        'ARM_GCC_TOOLCHAIN='+toolchain.as_posix()+'/', 'USER_SW_VER=0.2.0-localcam', 'LOCALCAM_ONLY=1']
with (work/'build/native-build.log').open('w') as log:
    result = subprocess.run(args, cwd=sdk, env=env, stdout=log, stderr=subprocess.STDOUT)
print('Build exit:', result.returncode)
print('\n'.join((work/'build/native-build.log').read_text(errors='replace').splitlines()[-30:]))
if result.returncode: raise SystemExit(result.returncode)

if result.returncode == 0:
    import shutil
    shutil.copy2(sdk/'out/bk7231_bsp.bin', work/'build/firmware.bin')
