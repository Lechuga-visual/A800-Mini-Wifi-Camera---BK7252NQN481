"""Apply the SDK patch and stage only application source plus local generated headers."""
from pathlib import Path
import json,shutil,subprocess

def main():
    root=Path(__file__).resolve().parents[1];sdk=root/'.deps/beken-sdk'
    expected=json.loads((root/'dependencies.lock.json').read_text())['sdk']['commit']
    actual=subprocess.check_output(['git','-C',str(sdk),'rev-parse','HEAD'],text=True).strip()
    if actual!=expected:raise SystemExit('Unsupported SDK revision')
    for name in ['gc0312_original_init.h','localcam_device.h']:
        if not (root/'build/generated'/name).is_file():raise SystemExit(f'Missing {name}; run extraction and provisioning first')
    patch=root/'patches/bk7252n-localcam.patch'
    check=subprocess.run(['git','-C',str(sdk),'apply','--check',str(patch)],capture_output=True)
    if check.returncode==0:
        subprocess.run(['git','-C',str(sdk),'apply',str(patch)],check=True)
    else:
        subprocess.run(['git','-C',str(sdk),'apply','--reverse','--check',str(patch)],check=True)
    for name in ['localcam.c','localcam_wifi.c','localcam_wifi.h','localcam_tls.h','localcam_tls_port.c','localcam_security.h','localcam_security_store.c']:
        shutil.copy2(root/'firmware'/name,sdk/name)
    shutil.copy2(root/'build/generated/localcam_device.h',sdk/'localcam_device.h')
    shutil.copy2(root/'build/generated/gc0312_original_init.h',sdk/'beken378/func/camera_intf/gc0312_original_init.h')
    html=(root/'firmware/index.html').read_text(encoding='utf-8')
    page='static const char page[] =\n'+'\n'.join(json.dumps(line+'\n',ensure_ascii=False) for line in html.splitlines())+';\n'
    (sdk/'localcam_page.h').write_text(page,encoding='utf-8')
    print('SDK staged. No serial device opened.')

if __name__=='__main__':main()
