"""Compile and exercise the actual two-bank flash storage with injected faults."""
from pathlib import Path
import os,subprocess,tempfile

root=Path(__file__).resolve().parents[1]
prefix=r'''
#define LOCALCAM_HOST_TEST 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "localcam_security.h"
static unsigned char flash[8192];
static int broken_write,unknown_erase,read_failure;
int bk_flash_abs_addr_read(unsigned at,unsigned char *buffer,unsigned n){
 assert((at==LC_SECURITY_A||at==LC_SECURITY_B)&&n<=4096);
 if(read_failure)return -1;
 memcpy(buffer,flash+at-LC_SECURITY_A,n);return 0;
}
int bk_flash_abs_addr_write(unsigned at,const unsigned char *buffer,unsigned n,unsigned char erase){
 assert((at==LC_SECURITY_A||at==LC_SECURITY_B)&&n<=4096&&erase==1);
 if(unknown_erase)return -1;
 memset(flash+at-LC_SECURITY_A,255,4096);
 memcpy(flash+at-LC_SECURITY_A,buffer,broken_write?n/2:n);return broken_write?-1:0;
}
'''
suffix=r'''
int main(void){
 localcam_identity original,state,loaded;
 memset(flash,255,sizeof(flash));assert(localcam_store_load(&loaded)==0);
 memset(&state,0,sizeof(state));state.ca_length=500;state.ca_key_length=120;state.server_key_length=120;
 assert(!localcam_store_save(&state));assert(state.generation==1);original=state;
 assert(localcam_store_load(&loaded)==1&&!memcmp(&loaded,&state,sizeof(state)));
 state.configured=1;state.admin_hash[0]=42;
 broken_write=1;assert(localcam_store_save(&state)==-1);assert(state.generation==1);
 assert(localcam_store_load(&loaded)==1&&!memcmp(&loaded,&original,sizeof(loaded)));
 broken_write=0;assert(!localcam_store_save(&state));assert(state.generation==2);
 assert(localcam_store_load(&loaded)==1&&loaded.configured==1&&loaded.admin_hash[0]==42);
 flash[4096+100]^=1;assert(localcam_store_load(&loaded)==1&&loaded.generation==1);
 memset(flash,255,sizeof(flash));flash[4000]=0;active_address=0;state=original;
 assert(localcam_store_save(&state)==-1&&flash[4000]==0);
 memset(flash,0,sizeof(flash));assert(localcam_store_load(&loaded)==-1);
 read_failure=1;assert(localcam_store_load(&loaded)==-1);
 puts("Two-bank storage: persistence, power-cut fallback, corruption, foreign-sector and read-failure tests passed.");
 return 0;
}
'''
source=(root/'firmware/localcam_security_store.c').read_text()
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)/'test.c';path.write_text(prefix+source+suffix)
    compiler=str(root/'.deps/w64devkit/bin/gcc.exe') if os.name=='nt' else 'cc'
    exe=Path(tmp)/('test.exe' if os.name=='nt' else 'test')
    subprocess.run([compiler,'-std=c99','-Wall','-Wextra','-I'+str(root/'firmware'),str(path),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
