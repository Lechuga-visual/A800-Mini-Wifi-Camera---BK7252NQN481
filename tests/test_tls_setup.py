"""Actual TLS and HTTP code: first boot, persistent identity, setup, roles and password changes.

Windows host uses BCrypt entropy and file-backed flash. Camera hardware still needs its own test.
"""
from pathlib import Path
import base64,concurrent.futures,hashlib,http.client,json,socket,ssl,subprocess,tempfile
from urllib.parse import urlencode

root=Path(__file__).resolve().parents[1]
source=root/'.deps/mbedtls-3.6.8'
out=root/'build/tls-host-test';out.mkdir(parents=True,exist_ok=True)
gcc=root/'.deps/w64devkit/bin/gcc.exe'
flags=['-std=c99','-Os','-ffunction-sections','-fdata-sections','-fno-builtin-strcpy',
       '-DLOCALCAM_HOST_TEST=1','-DMBEDTLS_CONFIG_FILE="localcam_tls_config.h"',
       '-I'+str(root/'firmware'),'-I'+str(root/'build/generated'),'-I'+str(root/'build/tls'),
       '-I'+str(source/'include'),'-I'+str(source/'library'),
       '-include',str(root/'build/tls/localcam_tls_symbols.h')]
def compile_one(path):
    obj=out/(path.stem+'.o')
    result=subprocess.run([str(gcc),*flags,'-c',str(path),'-o',str(obj)],capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stderr)
    return obj
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    objects=list(pool.map(compile_one,sorted((source/'library').glob('*.c'))+[root/'firmware/localcam_tls.c']))
html=(root/'firmware/index.html').read_text(encoding='utf-8')
(out/'localcam_page.h').write_text('static const char page[]='+json.dumps(html,ensure_ascii=False)+';\n',encoding='utf-8')
prefix=r'''
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "localcam_wifi.h"
#include "localcam_tls.h"
#include "localcam_security.h"
#include "localcam_device.h"
typedef void *beken_thread_t;
typedef void *beken_thread_arg_t;
#define os_malloc malloc
#define os_free free
#define rtos_delete_thread(x) ((void)0)
#define GLOBAL_INT_DECLARATION()
#define GLOBAL_INT_DISABLE()
#define GLOBAL_INT_RESTORE()
#define close(fd) closesocket((SOCKET)(fd))
static unsigned char flash[12288];
static const char *flash_path;
static int is_setup;
static unsigned clock_offset;
void *lc_port_calloc(size_t n,size_t s){return calloc(n,s);}
void lc_port_free(void *p){free(p);}
int lc_port_entropy(void *ctx,unsigned char *p,size_t n){(void)ctx;return BCryptGenRandom(NULL,p,(ULONG)n,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0?-1:0;}
int lc_port_recv(int fd,void *p,size_t n){return recv((SOCKET)fd,p,(int)n,0);}
int lc_port_send(int fd,const void *p,size_t n){return send((SOCKET)fd,p,(int)n,0);}
uint32_t lc_port_time(void){return GetTickCount()+clock_offset;}
static unsigned rtos_get_time(void){return lc_port_time();}
unsigned localcam_tls_allocated(void){return 0;}
unsigned localcam_tls_peak(void){return 0;}
unsigned localcam_heap_free(void){return 1000000;}
int localcam_is_setup_interface(int fd){(void)fd;return is_setup;}
int bk_flash_abs_addr_read(unsigned address,unsigned char *buffer,unsigned n){
 assert(address>=0x1e2000&&address+n<=0x1e5000);memcpy(buffer,flash+address-0x1e2000,n);return 0;
}
int bk_flash_abs_addr_write(unsigned address,const unsigned char *buffer,unsigned n,unsigned char erase){
 FILE *file;assert(address>=0x1e2000&&address+n<=0x1e5000&&erase==1);
 memset(flash+address-0x1e2000,255,4096);memcpy(flash+address-0x1e2000,buffer,n);
 file=fopen(flash_path,"wb");assert(file);assert(fwrite(flash,1,sizeof(flash),file)==sizeof(flash));fclose(file);return 0;
}
static unsigned video_buffer_read_frame(unsigned char *p,unsigned capacity,int *error,unsigned timeout){
 (void)capacity;(void)timeout;p[0]=255;p[1]=216;p[2]=255;p[3]=217;*error=0;return 4;
}
static int rtos_create_thread(beken_thread_t *t,unsigned priority,const char *name,void(*fn)(void*),unsigned stack,void *arg){
 (void)t;(void)priority;(void)name;(void)fn;(void)stack;(void)arg;return -1;
}
'''
main=r'''
int main(int argc,char **argv){
 WSADATA wsa;SOCKET listener;struct sockaddr_in addr;int length=sizeof(addr),r;FILE *file;
 unsigned char pem[1400];char token[65],fingerprint[65];
 if(argc!=5)return 1;flash_path=argv[1];is_setup=atoi(argv[3]);memset(flash,255,sizeof(flash));
 file=fopen(flash_path,"rb");if(file){assert(fread(flash,1,sizeof(flash),file)==sizeof(flash));fclose(file);}
 WSAStartup(MAKEWORD(2,2),&wsa);
 r=localcam_tls_init();if(r){printf("INIT_ERROR %d\n",r);fflush(stdout);return 2;}
 assert(!localcam_security_session_create(token));assert(localcam_security_session_role(token,64,1)==2);
 clock_offset=900001;assert(localcam_security_session_role(token,64,1)==0);clock_offset=0;localcam_security_session_clear();
 assert(!localcam_security_refresh_certificate(argv[4]));r=localcam_security_ca_pem(pem,sizeof(pem));assert(r>0);
 file=fopen(argv[2],"wb");assert(file);fwrite(pem,1,r,file);fclose(file);
 camera_ready=1;listener=socket(AF_INET,SOCK_STREAM,0);memset(&addr,0,sizeof(addr));
 addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 assert(!bind(listener,(struct sockaddr*)&addr,sizeof(addr)));assert(!listen(listener,2));
 assert(!localcam_security_ca_fingerprint(fingerprint));getsockname(listener,(struct sockaddr*)&addr,&length);printf("READY %u %s\n",ntohs(addr.sin_port),fingerprint);fflush(stdout);
 for(;;){SOCKET fd=accept(listener,NULL,NULL);DWORD timeout=3000;
  if(fd==INVALID_SOCKET)break;
  setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));
  setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,(char*)&timeout,sizeof(timeout));
  if(!localcam_tls_accept((int)fd)){serve_client((int)fd);localcam_tls_close((int)fd);}
  closesocket(fd);
 }
 return 0;
}
'''
app=(root/'firmware/localcam.c').read_text()
app=app[app.index('#define FRAME_CAPACITY'):app.index('static void web_main')]
wifi=(root/'firmware/localcam_wifi.c').read_text()
wifi=wifi[wifi.index('/* Verified erased'):wifi.index('static void start_setup_ap')].replace('checksum(','wifi_checksum(')
store=(root/'firmware/localcam_security_store.c').read_text()
(out/'harness.c').write_text(prefix+wifi+store+app+main,encoding='utf-8')
exe=out/'setup_test.exe'
result=subprocess.run([str(gcc),*flags,str(out/'harness.c'),*map(str,objects),'-Wl,--gc-sections',
                       '-lws2_32','-lbcrypt','-o',str(exe)],capture_output=True,text=True)
if result.returncode:raise RuntimeError(result.stderr)

def basic(name,password):return 'Basic '+base64.b64encode((name+':'+password).encode()).decode()
class Server:
    def __init__(self,directory,setup=True,ip='192.168.1.123'):
        self.ca=directory/'ca.crt';self.ip=ip
        self.process=subprocess.Popen([str(exe),str(directory/'flash.bin'),str(self.ca),str(int(setup)),ip],
                                      stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        line=self.process.stdout.readline().strip()
        if not line.startswith('READY '):
            self.process.terminate();stdout,stderr=self.process.communicate(timeout=5)
            raise AssertionError(line+' '+stdout+' '+stderr)
        self.port=int(line.split()[1]);self.fingerprint=line.split()[2];self.ctx=ssl.create_default_context(cafile=str(self.ca))
    def close(self):self.process.terminate();self.process.communicate(timeout=5)
    def request(self,path,method='GET',credential=None,cookie=None,form=None,hostname='192.168.4.1',context=None):
        headers={'Host':hostname}
        if credential:headers['Authorization']=basic(*credential)
        if cookie:headers['Cookie']=cookie
        body=None
        if method=='POST':headers['Origin']='https://'+hostname
        if form is not None:headers['Content-Type']='application/x-www-form-urlencoded';body=urlencode(form)
        with socket.create_connection(('127.0.0.1',self.port),timeout=10) as raw:
            with (context or self.ctx).wrap_socket(raw,server_hostname=hostname) as client:
                connection=http.client.HTTPConnection('127.0.0.1',self.port,timeout=10);connection.sock=client
                connection.request(method,path,body=body,headers=headers)
                response=connection.getresponse();result=(response.status,dict(response.getheaders()),response.read())
                connection.close();return result

with tempfile.TemporaryDirectory() as temporary:
    directory=Path(temporary);server=Server(directory)
    try:
        ca=server.ca.read_bytes()
        assert hashlib.sha256(ssl.PEM_cert_to_DER_cert(ca.decode())).hexdigest()==server.fingerprint
        assert server.request('/certificate')[2]==ca
        assert server.request('/')[0]==200
        assert server.request('/status')[0]==401
        response=server.request('/login','POST',('admin','admin'));assert response[0]==200,response[0]
        cookie=response[1]['Set-Cookie'].split(';')[0]
        assert 'Secure; HttpOnly; SameSite=Strict' in response[1]['Set-Cookie']
        assert json.loads(server.request('/status',cookie=cookie)[2])['security_configured'] is False
        assert server.request('/certificate',cookie=cookie)[2]==ca
        assert server.request('/snapshot.jpg',cookie=cookie)[0]==503
        assert server.request('/wifi','POST',cookie=cookie,form={'ssid':'Home','password':'abcdefgh'})[0]==403
        assert server.request('/passwords','POST',cookie=cookie,form={'admin_password':'short','ha_password':'also-short'})[0]==400
        response=server.request('/passwords','POST',cookie=cookie,
            form={'admin_password':'admin-test-123456','ha_password':'viewer-test-123456'})
        assert response[0]==200,response[0]
        new_cookie=response[1]['Set-Cookie'].split(';')[0]
        assert server.request('/status',cookie=cookie)[0]==401
        assert server.request('/status',credential=('admin','admin'),cookie=new_cookie)[0]==200
        assert json.loads(server.request('/status',cookie=new_cookie)[2])['security_configured'] is True
        assert server.request('/status',credential=('admin','admin'))[0]==401
        assert server.request('/status',credential=('ha','viewer-test-123456'))[0]==403
        assert server.request('/snapshot.jpg',credential=('ha','viewer-test-123456'))[0]==200
        assert server.request('/certificate',cookie=new_cookie)[2]==ca
        response=server.request('/passwords','POST',cookie=new_cookie,
            form={'admin_password':'changed-admin-123456','ha_password':''})
        assert response[0]==200
        assert server.request('/status',cookie=new_cookie)[0]==401
        assert server.request('/status',credential=('admin','admin-test-123456'))[0]==401
        assert server.request('/status',credential=('admin','changed-admin-123456'))[0]==200
        assert server.request('/snapshot.jpg',credential=('ha','viewer-test-123456'))[0]==200
    finally:server.close()
    server=Server(directory,setup=False,ip='192.168.1.124')
    try:
        assert server.ca.read_bytes()==ca,'CA changed across reboot'
        assert server.request('/status',credential=('admin','changed-admin-123456'),hostname='192.168.1.124')[0]==200
        assert server.request('/snapshot.jpg',credential=('ha','viewer-test-123456'),hostname='192.168.1.124')[0]==200
        try:server.request('/status',hostname='192.168.1.123')
        except ssl.SSLCertVerificationError:pass
        else:raise AssertionError('Old IP remained in renewed server certificate')
    finally:server.close()
    second=directory/'second';second.mkdir();server=Server(second,setup=False)
    try:
        assert server.ca.read_bytes()!=ca,'Two devices generated the same identity'
        assert server.request('/login','POST',('admin','admin'))[0]==401,'Generic setup login accessible on LAN'
    finally:server.close()
print('TLS setup tests passed: unique first-boot CA, persistence, CA download, session expiry, roles, password changes and IP renewal.')
