"""Test the actual destination/source subnet gate used for generic setup login."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'firmware/localcam_tls_port.c').read_text()
source=source[source.index('int localcam_is_setup_interface'):]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define AF_INET 2
typedef unsigned socklen_t;
struct sockaddr { unsigned family; };
struct sockaddr_in { unsigned sin_family;struct {uint32_t s_addr;}sin_addr;};
static struct sockaddr_in local_value,peer_value;
static int fail_local,fail_peer;
static uint32_t ntohl(uint32_t value){return value;}
static int getsockname(int fd,struct sockaddr *out,socklen_t *size){(void)fd;(void)size;memcpy(out,&local_value,sizeof(local_value));return fail_local;}
static int getpeername(int fd,struct sockaddr *out,socklen_t *size){(void)fd;(void)size;memcpy(out,&peer_value,sizeof(peer_value));return fail_peer;}
'''
suffix=r'''
int main(void){
 local_value.sin_family=AF_INET;peer_value.sin_family=AF_INET;
 local_value.sin_addr.s_addr=0xc0a80401u;peer_value.sin_addr.s_addr=0xc0a80402u;
 assert(localcam_is_setup_interface(3));
 peer_value.sin_addr.s_addr=0xc0a80102u;assert(!localcam_is_setup_interface(3));
 peer_value.sin_addr.s_addr=0xc0a80402u;local_value.sin_addr.s_addr=0xc0a80117u;assert(!localcam_is_setup_interface(3));
 local_value.sin_addr.s_addr=0xc0a80401u;fail_peer=1;assert(!localcam_is_setup_interface(3));
 fail_peer=0;fail_local=1;assert(!localcam_is_setup_interface(3));
 puts("Setup interface gate tests passed.");return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)/'interface.c';p.write_text(prefix+source+suffix)
    exe=Path(tmp)/('interface.exe' if os.name=='nt' else 'interface')
    compiler=str(root/'.deps/w64devkit/bin/gcc.exe') if os.name=='nt' else 'cc'
    subprocess.run([compiler,str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
