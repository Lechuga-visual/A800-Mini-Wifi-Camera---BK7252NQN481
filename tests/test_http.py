from pathlib import Path
import subprocess,os
root=Path(__file__).resolve().parents[1]
source=(root/'firmware/localcam.c').read_text()
tested=source[source.index('#define FRAME_CAPACITY'):source.index('static void web_main')]
wifi_source=(root/'firmware/localcam_wifi.c').read_text()
wifi_tested=wifi_source[wifi_source.index('/* Verified erased'):wifi_source.index('static void start_setup_ap')]
prefix=r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "localcam_wifi.h"
#include "localcam_device.h"
typedef void *beken_thread_t;
typedef void *beken_thread_arg_t;
#define os_malloc malloc
#define os_free free
#define rtos_delete_thread(x) ((void)0)
#define GLOBAL_INT_DECLARATION()
#define GLOBAL_INT_DISABLE()
#define GLOBAL_INT_RESTORE()
static unsigned char config_sector[4096];
static unsigned config_writes;
static int fail_verify;
static int bk_flash_abs_addr_read(unsigned address,unsigned char *buffer,unsigned size){
 assert(address==0x1e2000&&size<=4096);memcpy(buffer,config_sector,size);if(fail_verify&&size<4096)buffer[0]^=1;return 0;
}
static int bk_flash_abs_addr_write(unsigned address,const unsigned char *buffer,unsigned size,unsigned char erase){
 assert(address==0x1e2000&&size<=4096&&erase==1);memset(config_sector,255,4096);memcpy(config_sector,buffer,size);config_writes++;return 0;
}
static const char *input;
static size_t input_at;
static char output[65536];
static size_t output_at;
static int invalid_frame, closes;
static int send(int fd,const void *p,size_t n,int flags){
 (void)fd;(void)flags; if(n>7)n=7;
 assert(output_at+n<sizeof(output));memcpy(output+output_at,p,n);output_at+=n;output[output_at]=0;return (int)n;
}
static int recv(int fd,void *p,size_t n,int flags){
 size_t left=strlen(input)-input_at;(void)fd;(void)flags;
 if(n>3)n=3;
 if(n>left)n=left;
 memcpy(p,input+input_at,n);input_at+=n;return (int)n;
}
static int close(int fd){(void)fd;closes++;return 0;}
static int localcam_tls_write(int fd,const void *p,size_t n){return send(fd,p,n,0);}
static unsigned rtos_get_time(void){return 0;}
static int localcam_tls_read(int fd,void *p,size_t n){return recv(fd,p,n,0);}
static void localcam_tls_close(int fd){(void)fd;}
static unsigned localcam_tls_allocated(void){return 1234;}
static unsigned localcam_tls_peak(void){return 5678;}
static unsigned localcam_heap_free(void){return 123456;}
static int localcam_auth_role(const char *v,unsigned n){return n==11&&!memcmp(v,"Basic ADMIN",11)?2:n==12&&!memcmp(v,"Basic VIEWER",12)?1:0;}

static int mock_configured=1,mock_setup=1;
static unsigned localcam_security_generation(void){return 1;}
static int localcam_security_configured(void){return mock_configured;}
static int localcam_is_setup_interface(int fd){(void)fd;return mock_setup;}
static int localcam_security_session_create(char out[65]){memset(out,'a',64);out[64]=0;return 0;}
static int localcam_security_session_role(const char *token,unsigned n,int setup){(void)setup;unsigned i;if(n!=64)return 0;for(i=0;i<n;i++)if(token[i]!='a')return 0;return 2;}
static void localcam_security_session_clear(void){}
static int localcam_security_set_passwords(const char *admin,const char *ha){if(strlen(admin)<12||strlen(ha)<12||!strcmp(admin,ha))return -1;mock_configured=1;return 0;}
static int localcam_security_ca_pem(unsigned char *out,size_t size){const char *pem="-----BEGIN CERTIFICATE-----\nTEST\n-----END CERTIFICATE-----\n";assert(size>strlen(pem));strcpy((char*)out,pem);return strlen(pem);}
static unsigned video_buffer_read_frame(unsigned char *p,unsigned size,int *error,unsigned timeout){
 (void)size;(void)timeout;memcpy(p,invalid_frame?"BAD!":"\xff\xd8\xff\xd9",4);*error=0;return 4;
}
static int rtos_create_thread(beken_thread_t *t,unsigned pri,const char *name,void(*fn)(void*),unsigned stack,void *arg){
 (void)t;(void)pri;(void)name;(void)fn;(void)stack;(void)arg;return -1;
}
'''
suffix=r'''
static void raw_request(const char *text){input=text;input_at=0;output_at=0;output[0]=0;assert(serve_client(3)==0);}
static void request(const char *text){char auth[4096];const char *e=strstr(text,"\r\n");size_t head=e?(size_t)(e-text)+2:0;memcpy(auth,text,head);snprintf(auth+head,sizeof(auth)-head,"Authorization: Basic ADMIN\r\n%s%s",strstr(text,"Host:")?"":"Host: test\r\n",text+head);raw_request(auth);}
static void status(const char *wanted){if(!strstr(output,wanted))fprintf(stderr,"Expected %s, got %s\n",wanted,output);assert(strstr(output,wanted));}
static void length_ok(void){char *head=strstr(output,"Content-Length: ");char *body=strstr(output,"\r\n\r\n");unsigned n=0;assert(head&&body);assert(sscanf(head,"Content-Length: %u",&n)==1);assert(strlen(body+4)==n);}
int main(void){
 memset(config_sector,255,sizeof(config_sector));
 {wifi_config cfg;const char *form="ssid=Casa+%26+Patio&password=abcdefgh";assert(parse_config(form,strlen(form),&cfg)==0);assert(!strcmp(cfg.ssid,"Casa & Patio"));assert(valid_config(&cfg));cfg.ssid[0]^=1;assert(!valid_config(&cfg));}
 {const char *bad[]={"ssid=&password=abcdefgh","ssid=Casa&password=short","ssid=Casa%00x&password=abcdefgh","ssid=Casa%ZZ&password=abcdefgh","ssid=Casa&ssid=Otra&password=abcdefgh","ssid=Casa","ssid=Casa&password=abcdefgh&extra=1"};unsigned i;for(i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(localcam_wifi_submit(bad[i],strlen(bad[i]))==-1);assert(config_writes==0);}
 {const unsigned char sdk[]={255,216,255,217,0,4,0,0,0};const unsigned char bad[]={255,216,255,217,0,5,0,0,0};assert(jpeg_length(sdk,9)==4);assert(jpeg_length(bad,9)==0);assert(jpeg_length(sdk,3)==0);}
 raw_request("GET / HTTP/1.1\r\nHost: test\r\n\r\n");status("200 OK");length_ok();status("/passwords");
 raw_request("GET /status HTTP/1.1\r\nHost: test\r\n\r\n");status("401 Unauthorized");
 raw_request("GET /snapshot.jpg HTTP/1.1\r\nHost: test\r\n\r\n");status("401 Unauthorized");status("WWW-Authenticate: Basic");
 raw_request("GET /status HTTP/1.1\r\nHost: test\r\nAuthorization: Basic VIEWER\r\n\r\n");status("403 Forbidden");
 raw_request("GET /status HTTP/1.1\r\nHost: test\r\nCookie: a800_session=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n\r\n");status("200 OK");length_ok();
 raw_request("GET /status HTTP/1.1\r\nHost: test\r\nCookie: a800_session=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa; a800_session=bad\r\n\r\n");status("401 Unauthorized");
 request("POST /login HTTP/1.1\r\nOrigin: https://test\r\n\r\n");status("200 OK");status("Secure; HttpOnly; SameSite=Strict");length_ok();
 request("POST /logout HTTP/1.1\r\nOrigin: https://test\r\n\r\n");status("200 OK");status("Max-Age=0");
 request("POST /login HTTP/1.1\r\n\r\n");status("403 Forbidden");
 request("POST /login HTTP/1.1\r\nOrigin: https://evil\r\n\r\n");status("403 Forbidden");
 request("GET /certificate HTTP/1.1\r\n\r\n");status("Content-Disposition: attachment");status("BEGIN CERTIFICATE");length_ok();
 mock_configured=0;mock_setup=0;request("GET /status HTTP/1.1\r\n\r\n");status("401 Unauthorized");mock_setup=1;
 request("GET /snapshot.jpg HTTP/1.1\r\n\r\n");status("503 Service Unavailable");
 {const char *form="admin_password=abcdefghijkl&ha_password=mnopqrstuvwx";char req[900];snprintf(req,sizeof(req),"POST /passwords HTTP/1.1\r\nOrigin: https://test\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %u\r\n\r\n%s",(unsigned)strlen(form),form);request(req);status("200 OK");assert(mock_configured);}
 camera_ready=1;request("GET /snapshot.jpg HTTP/1.1\r\n\r\n");status("200 OK");status("Content-Length: 4");assert(!stream_busy);
 stream_busy=1;request("GET /stream.mjpg?t=123 HTTP/1.1\r\n\r\n");status("503 Service Unavailable");stream_busy=0;
 invalid_frame=1;request("GET /snapshot.jpg HTTP/1.1\r\n\r\n");status("503 Service Unavailable");assert(!stream_busy);
 {char big[2200];memset(big,'a',sizeof(big)-1);memcpy(big,"GET / HTTP/1.1\r\nX: ",strlen("GET / HTTP/1.1\r\nX: "));big[2199]=0;request(big);status("431 Request Header Fields Too Large");}
 {char admin[65],ha[65];assert(!parse_passwords("admin_password=abcdefghijkl&ha_password=mnop%2Bqrstuvw",strlen("admin_password=abcdefghijkl&ha_password=mnop%2Bqrstuvw"),admin,ha));assert(!strcmp(ha,"mnop+qrstuvw"));assert(parse_passwords("admin_password=one&admin_password=two",strlen("admin_password=one&admin_password=two"),admin,ha));}
 request("GET /status HTTP/1.1\r\nHost: test\r\nHost: evil\r\n\r\n");status("400 Bad Request");
 request("GET /status HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");status("400 Bad Request");
 raw_request("GET /status HTTP/1.1\r\nHost: test\r\nAuthorization: Basic ADMIN\r\nAuthorization: Basic VIEWER\r\n\r\n");status("401 Unauthorized");
 puts("HTTP setup, sessions, roles, CSRF, parser and JPEG tests passed.");return 0;
}
'''
import json
(root/'build').mkdir(exist_ok=True)
html=(root/'firmware/index.html').read_text(encoding='utf-8')
(root/'build/localcam_page.h').write_text('static const char page[]='+json.dumps(html,ensure_ascii=False)+';\n',encoding='utf-8')
test=root/'build/http_test.c';test.write_text(prefix+wifi_tested+tested+suffix)
compiler=root/'.deps/w64devkit/bin/gcc.exe' if os.name=='nt' else 'cc';exe=root/'build'/('http_test.exe' if os.name=='nt' else 'http_test')
subprocess.run([str(compiler),'-std=c99','-fno-builtin-strcpy','-Wall','-Wextra','-I'+str(root/'firmware'),'-I'+str(root/'build/generated'),str(test),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
