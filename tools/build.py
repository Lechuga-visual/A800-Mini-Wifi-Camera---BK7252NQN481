"""Build the personalized application without opening the serial port."""
from pathlib import Path
import subprocess,sys

root=Path(__file__).resolve().parents[1]
for script in ['prepare.py','build_tls.py','build_native.py']:
    subprocess.run([sys.executable,str(root/'tools'/script)],check=True)
print('Application ready at build/firmware.bin. No device keys embedded. Review third-party notices before distribution.')
