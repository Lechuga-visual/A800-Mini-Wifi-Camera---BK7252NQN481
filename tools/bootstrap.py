"""Fetch pinned upstream dependencies; Windows x64 build environment."""
from pathlib import Path
import hashlib,json,os,subprocess,tarfile,urllib.request

ROOT=Path(__file__).resolve().parents[1]
DEPS=ROOT/'.deps'
LOCK=json.loads((ROOT/'dependencies.lock.json').read_text())

def run(*args,**kwargs):subprocess.run(list(map(str,args)),check=True,**kwargs)

def download(spec,dest):
    if not dest.exists():
        request=urllib.request.Request(spec['url'],headers={'User-Agent':'a800-local-build'})
        with urllib.request.urlopen(request,timeout=120) as response:dest.write_bytes(response.read())
    if hashlib.sha256(dest.read_bytes()).hexdigest()!=spec['sha256']:
        raise ValueError(f'Dependency checksum mismatch: {dest.name}')

def checkout(name,sparse=None):
    spec=LOCK[name];dest=DEPS/('beken-sdk' if name=='sdk' else 'OpenBK7231N')
    if not (dest/'.git').exists():
        run('git','clone','--filter=blob:none','--no-checkout',spec['url'],dest)
        if sparse:run('git','-C',dest,'sparse-checkout','set',sparse)
        run('git','-C',dest,'fetch','--depth','1','origin',spec['commit'])
        run('git','-c','core.symlinks=false','-C',dest,'checkout','--detach',spec['commit'])
    actual=subprocess.check_output(['git','-C',str(dest),'rev-parse','HEAD'],text=True).strip()
    if actual!=spec['commit']:raise ValueError(f'{name}: wrong upstream commit; do not reset a modified SDK automatically')

def main():
    if os.name!='nt':raise SystemExit('Build supported on Windows x64; pure Python tests also run on Linux.')
    if any(c.isspace() for c in str(ROOT)):raise SystemExit('The upstream makefiles require a checkout path without spaces.')
    DEPS.mkdir(exist_ok=True);(ROOT/'build').mkdir(exist_ok=True)
    checkout('sdk')
    checkout('toolchain','platforms/bk7231n/toolchain/windows')
    archive=DEPS/'mbedtls-3.6.8.tar.bz2';download(LOCK['mbedtls'],archive)
    if not (DEPS/'mbedtls-3.6.8/include').exists():
        with tarfile.open(archive) as tar:tar.extractall(DEPS,filter='data')
    kit=DEPS/'w64devkit.7z.exe';download(LOCK['w64devkit'],kit)
    if not (DEPS/'w64devkit/bin/make.exe').exists():run(kit,'-y','-o'+str(DEPS))
    print('Pinned dependencies verified and ready.')

if __name__=='__main__':main()
