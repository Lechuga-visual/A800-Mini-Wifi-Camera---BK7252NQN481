"""Inspect Git's index; refuse private/generated artifacts before publication."""
from pathlib import Path
import re,subprocess

root=Path(__file__).resolve().parents[1]
files=subprocess.check_output(['git','-C',str(root),'ls-files','-z'],text=True).split('\0')
failures=[]
blocked_roots={'.deps','build','private','.venv'}
blocked_suffixes={'.bin','.elf','.o','.a','.pem','.der','.crt','.log','.exe'}
private_key=re.compile(rb'-----BEGIN (?:EC |RSA |OPENSSH )?PRIVATE KEY-----\r?\n[A-Za-z0-9+/=\r\n]{32,}')
for name in filter(None,files):
    path=Path(name)
    if path.parts[0] in blocked_roots or path.suffix.lower() in blocked_suffixes or \
       path.name in {'localcam_identity.h','gc0312_original_init.h'}:
        failures.append(name);continue
    # Inspect staged content, not an unstaged replacement of the same file.
    data=subprocess.check_output(['git','-C',str(root),'show',':'+name])
    if private_key.search(data) or re.search(rb'static\s+const\s+unsigned\s+char\s+lc_key_der\s*\[',data):failures.append(name)
if failures:raise SystemExit('Private/generated material staged: '+', '.join(failures))
print('Publication gate passed: no staged keys, certificates, firmware dumps, generated sensor table or private folders.')
