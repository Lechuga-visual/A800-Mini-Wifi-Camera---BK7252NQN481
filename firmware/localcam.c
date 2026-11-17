/* Experimental BK7252N application. No vendor cloud client is linked here. */
#include "include.h"
#include "rtos_pub.h"
#include "mem_pub.h"
#include "rw_pub.h"
#include "wlan_ui_pub.h"
#include "uart_pub.h"
#include "video_transfer.h"
#include "localcam_wifi.h"
#include "localcam_tls.h"
#include "localcam_security.h"
#include "localcam_device.h"
#include "lwip/sockets.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define FRAME_CAPACITY (64 * 1024)
static volatile int stream_busy;
static int camera_ready;
static unsigned long frames_sent;
static beken_thread_t web_thread, stream_thread;
static int last_capture_error;
static unsigned last_capture_bytes;

const char *CFG_GetOpenBekenHostName(void) { return LC_HOSTNAME; }
/* SDK fork expects its application to supply this libc symbol. */
char *strcpy(char *dst, const char *src)
{
    char *start = dst;
    while ((*dst++ = *src++) != 0) {}
    return start;
}

#include "localcam_page.h"

static int send_all(int fd, const void *data, size_t size)
{
    const unsigned char *p = data;
    while (size) {
        int n = localcam_tls_write(fd, p, size > 1400 ? 1400 : size);
        if (n <= 0) return -1;
        p += n; size -= (size_t)n;
    }
    return 0;
}

#define reply(fd,status,type,text) response(fd,status,type,text,sizeof(text)-1)

static int response(int fd, const char *status, const char *type, const void *body, size_t length)
{
    char header[256];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
        "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n", status, type, (unsigned)length);
    if (n <= 0 || n >= (int)sizeof(header)) return -1;
    return send_all(fd, header, n) || send_all(fd, body, length);
}

/* The SDK's validated frame includes one pad byte and a LE32 length trailer. */
static unsigned jpeg_length(const unsigned char *frame, unsigned length)
{
    unsigned stored;
    if (length < 4 || length > FRAME_CAPACITY || frame[0] != 0xff || frame[1] != 0xd8) return 0;
    if (frame[length-2] == 0xff && frame[length-1] == 0xd9) return length;
    if (length < 9 || frame[length-7] != 0xff || frame[length-6] != 0xd9) return 0;
    stored=(unsigned)frame[length-4] | ((unsigned)frame[length-3]<<8) |
        ((unsigned)frame[length-2]<<16) | ((unsigned)frame[length-1]<<24);
    return stored == length-5 ? length-5 : 0;
}

static void video_client(beken_thread_arg_t arg)
{
    int fd = (int)(intptr_t)arg;
    unsigned char *frame = os_malloc(FRAME_CAPACITY);
    const char *preamble = "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=a800frame\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    if (!frame) {
        reply(fd, "503 Service Unavailable", "text/plain", "No frame buffer");
    } else if (send_all(fd, preamble, strlen(preamble)) == 0) {
        unsigned failures = 0, generation=localcam_security_generation();
        while (failures < 5 && generation==localcam_security_generation()) {
            int error = -1;
            unsigned length = video_buffer_read_frame(frame, FRAME_CAPACITY, &error, 2000);
            char part[128];
            int n;
            last_capture_error=error;last_capture_bytes=length;
            length = error ? 0 : jpeg_length(frame, length);
            if (!length) {
                failures++; continue;
            }
            failures = 0;
            n = snprintf(part, sizeof(part), "--a800frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", length);
            if (n <= 0 || n >= (int)sizeof(part) || send_all(fd, part, n) ||
                send_all(fd, frame, length) || send_all(fd, "\r\n", 2)) break;
            frames_sent++;
        }
    }
    if (frame) os_free(frame);
    localcam_tls_close(fd);close(fd);
    stream_busy = 0;
    rtos_delete_thread(NULL);
}

static int name_equal(const char *a,const char *b,unsigned n)
{
    unsigned i;
    for(i=0;i<n;i++){
        char x=a[i],y=b[i];
        if(x>='A'&&x<='Z')x+=32;
        if(y>='A'&&y<='Z')y+=32;
        if(x!=y)return 0;
    }
    return 1;
}

static const char *header_value(const char *request,const char *name,unsigned *length)
{
    unsigned keylen=strlen(name);
    const char *p=strstr(request,"\r\n");
    while(p && p[2] && p[2]!='\r'){
        const char *end;
        p+=2;end=strstr(p,"\r\n");
        if(!end)return NULL;
        if((unsigned)(end-p)>keylen && name_equal(p,name,keylen)&&p[keylen]==':'){
            p+=keylen+1;while(p<end&&(*p==' '||*p=='\t'))p++;
            *length=(unsigned)(end-p);return p;
        }
        p=end;
    }
    return NULL;
}

static unsigned header_count(const char *request,const char *name)
{
    const char *p=strstr(request,"\r\n");
    unsigned count=0,keylen=strlen(name);
    while(p && p[2] && p[2]!='\r'){
        const char *end;
        p+=2;end=strstr(p,"\r\n");if(!end)break;
        if((unsigned)(end-p)>keylen && name_equal(p,name,keylen) && p[keylen]==':')count++;
        p=end;
    }
    return count;
}

static void unauthorized(int fd)
{
    static const char challenge[]="HTTP/1.1 401 Unauthorized\r\n"
        "WWW-Authenticate: Basic realm=\"A800 Local\", charset=\"UTF-8\"\r\n"
        "Content-Length: 0\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    send_all(fd,challenge,sizeof(challenge)-1);
}

static int valid_origin(const char *request)
{
    unsigned origin_length=0,host_length=0;
    const char *origin=header_value(request,"Origin",&origin_length);
    const char *host=header_value(request,"Host",&host_length);
    return origin && host && host_length && header_count(request,"Origin")==1 &&
        origin_length==host_length+8 && !memcmp(origin,"https://",8) &&
        name_equal(origin+8,host,host_length);
}
static int cookie_role(const char *value,unsigned length,int setup_interface)
{
    unsigned at=0,found=0;
    const char *token=NULL;
    unsigned token_length=0;
    while(at<length){
        unsigned start,end;
        while(at<length && (value[at]==' ' || value[at]==';'))at++;
        start=at;while(at<length && value[at]!=';')at++;end=at;
        while(end>start && value[end-1]==' ')end--;
        if(end-start>=13 && !memcmp(value+start,"a800_session=",13)){
            found++;token=value+start+13;token_length=end-start-13;
        }
    }
    return found==1?localcam_security_session_role(token,token_length,setup_interface):0;
}
static void session_response(int fd,int clear)
{
    char token[65]={0},header[320];
    const char body[]="{\"saved\":true}";
    int n;
    if(!clear && localcam_security_session_create(token)){
        reply(fd,"503 Service Unavailable","application/json","{\"error\":\"Could not create session\"}");return;
    }
    n=snprintf(header,sizeof(header),"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Content-Length: %u\r\nCache-Control: no-store\r\n"
        "Set-Cookie: a800_session=%s; Path=/; Secure; HttpOnly; SameSite=Strict; Max-Age=%u\r\nConnection: close\r\n\r\n",
        (unsigned)sizeof(body)-1,token,clear?0:900);
    if(n>0 && n<(int)sizeof(header)){send_all(fd,header,n);send_all(fd,body,sizeof(body)-1);}
    memset(token,0,sizeof(token));
}
static void certificate_response(int fd)
{
    unsigned char pem[1400];
    char header[256];
    int length=localcam_security_ca_pem(pem,sizeof(pem)),n;
    if(length<=0){reply(fd,"503 Service Unavailable","text/plain","Certificate unavailable");return;}
    n=snprintf(header,sizeof(header),"HTTP/1.1 200 OK\r\nContent-Type: application/x-pem-file\r\n"
        "Content-Disposition: attachment; filename=\"a800-local-ca.crt\"\r\n"
        "Content-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",(unsigned)length);
    if(n>0 && n<(int)sizeof(header)){send_all(fd,header,n);send_all(fd,pem,length);}
}
static int read_form(int fd,char *request,size_t capacity,size_t *used,unsigned started,const char **body,unsigned *length)
{
    unsigned n,i,content_length=0;
    const char *value=header_value(request,"Content-Type",&n);
    if(!value || n<33 || !name_equal(value,"application/x-www-form-urlencoded",33) || (n>33 && value[33]!=';')){
        reply(fd,"415 Unsupported Media Type","application/json","{\"error\":\"Unsupported form format\"}");return -1;
    }
    value=header_value(request,"Content-Length",&n);
    if(!value || !n || n>3){reply(fd,"411 Length Required","text/plain","Length required");return -1;}
    for(i=0;i<n;i++){
        if(value[i]<'0'||value[i]>'9'){reply(fd,"400 Bad Request","text/plain","Invalid length");return -1;}
        content_length=content_length*10+value[i]-'0';
    }
    *body=strstr(request,"\r\n\r\n")+4;
    if(!content_length || content_length>512 || (size_t)(*body-request)+content_length>=capacity){
        reply(fd,"413 Content Too Large","text/plain","Form too large");return -1;
    }
    while(*used<(size_t)(*body-request)+content_length){
        int got=localcam_tls_read(fd,request+*used,(size_t)(*body-request)+content_length-*used);
        if(got<=0 || (unsigned)(rtos_get_time()-started)>5000)return -1;
        *used+=got;
    }
    *length=content_length;return 0;
}
static int nibble(char c)
{
    if(c>='0'&&c<='9')return c-'0';
    if(c>='A'&&c<='F')return c-'A'+10;
    if(c>='a'&&c<='f')return c-'a'+10;
    return -1;
}
static int parse_passwords(const char *body,unsigned length,char admin[65],char viewer[65])
{
    unsigned at=0,seen=0;
    while(at<length){
        unsigned start=at,end,i,out=0,flag;
        char *destination;
        while(at<length && body[at]!='=' && body[at]!='&')at++;
        if(at>=length || body[at]!='=')return -1;
        if(at-start==14 && !memcmp(body+start,"admin_password",14)){destination=admin;flag=1;}
        else if(at-start==11 && !memcmp(body+start,"ha_password",11)){destination=viewer;flag=2;}
        else return -1;
        if(seen&flag)return -1;
        seen|=flag;
        start=++at;while(at<length && body[at]!='&')at++;end=at;
        for(i=start;i<end;i++){
            unsigned char c=body[i];
            if(c=='+')c=' ';
            else if(c=='%'){
                int a,b;
                if(i+2>=end || (a=nibble(body[i+1]))<0 || (b=nibble(body[i+2]))<0)return -1;
                c=a*16+b;i+=2;
            }
            if(c<32 || c>126 || out>=64)return -1;
            destination[out++]=c;
        }
        destination[out]=0;
        if(at<length)at++;
    }
    return seen==3?0:-1;
}

static int serve_client(int fd)
{
    char request[2048];
    size_t used = 0;
    char method[8], target[128], version[16];
    int parsed,role;
    unsigned request_started=rtos_get_time();
    while (used < sizeof(request)-1) {
        int n = localcam_tls_read(fd, request+used, sizeof(request)-1-used);
        if (n <= 0) return 0;
        if((unsigned)(rtos_get_time()-request_started)>5000)return 0;
        if(memchr(request+used,0,n)){reply(fd,"400 Bad Request","text/plain","Bad request");return 0;}
        used += n; request[used] = 0;
        if (strstr(request, "\r\n\r\n")) break;
    }
    if (!strstr(request, "\r\n\r\n")) {
        reply(fd,"431 Request Header Fields Too Large","text/plain","Header too large"); return 0;
    }
    parsed = sscanf(request, "%7s %127s %15s", method, target, version);
    if (parsed != 3 || (strcmp(version,"HTTP/1.1") && strcmp(version,"HTTP/1.0"))) {
        reply(fd,"400 Bad Request","text/plain","Bad request"); return 0;
    }
    {char *query=strchr(target,'?');if(query)*query=0;}
    if(!strcmp(method,"GET") && !strcmp(target,"/")){
        response(fd,"200 OK","text/html; charset=utf-8",page,sizeof(page)-1);return 0;
    }
    /* A CA certificate is public material, not an authentication credential. */
    if(!strcmp(method,"GET") && !strcmp(target,"/certificate")){
        certificate_response(fd);return 0;
    }
    if(header_count(request,"Host")!=1 || header_count(request,"Content-Length")>1 ||
       header_count(request,"Transfer-Encoding")){
        reply(fd,"400 Bad Request","text/plain","Ambiguous request");return 0;
    }
    {
        unsigned length=0;
        const char *value=header_value(request,"Authorization",&length);
        int setup_interface=localcam_is_setup_interface(fd);
        role=0;
        if(header_count(request,"Authorization")<=1 && header_count(request,"Cookie")<=1){
            /* Upgraded browsers may still send cached Basic credentials from 0.1. */
            if(strcmp(target,"/login") && header_count(request,"Cookie")==1){
                unsigned cookie_length=0;
                const char *cookie=header_value(request,"Cookie",&cookie_length);
                role=cookie_role(cookie,cookie_length,setup_interface);
            }
            if(!role && header_count(request,"Authorization")==1)role=localcam_auth_role(value,length);
        }
        if(!localcam_security_configured() && !setup_interface)role=0;
        if(!role){
            if(!strcmp(target,"/stream.mjpg") || !strcmp(target,"/snapshot.jpg"))unauthorized(fd);
            else reply(fd,"401 Unauthorized","application/json","{\"error\":\"Sign in required\"}");
            return 0;
        }
    }
    if(role<2 && strcmp(target,"/stream.mjpg") && strcmp(target,"/snapshot.jpg")){
        reply(fd,"403 Forbidden","text/plain","Administrator required");return 0;
    }
    if(!strcmp(method,"POST")){
        if(!valid_origin(request)){
            reply(fd,"403 Forbidden","application/json","{\"error\":\"Origin not allowed\"}");return 0;
        }
        if(!strcmp(target,"/login")){session_response(fd,0);return 0;}
        if(!strcmp(target,"/logout")){localcam_security_session_clear();session_response(fd,1);return 0;}
        if(!strcmp(target,"/passwords") || !strcmp(target,"/wifi")){
            const char *body=NULL;
            unsigned length=0;
            int result;
            if(read_form(fd,request,sizeof(request),&used,request_started,&body,&length))return 0;
            if(!strcmp(target,"/passwords")){
                char admin[65]={0},viewer[65]={0};
                if(parse_passwords(body,length,admin,viewer))result=-1;
                else result=localcam_security_set_passwords(admin,viewer);
                memset(admin,0,sizeof(admin));memset(viewer,0,sizeof(viewer));
                if(!result)session_response(fd,0);
                else if(result==-1)reply(fd,"400 Bad Request","application/json","{\"error\":\"Use distinct passwords, 12 to 64 printable ASCII characters\"}");
                else reply(fd,"500 Internal Server Error","application/json","{\"error\":\"Could not save passwords\"}");
            }else if(!localcam_security_configured()){
                reply(fd,"403 Forbidden","application/json","{\"error\":\"Set both passwords first\"}");
            }else{
                result=localcam_wifi_submit(body,length);
                if(!result)reply(fd,"200 OK","application/json","{\"saved\":true}");
                else if(result==-1)reply(fd,"400 Bad Request","application/json","{\"error\":\"Check network name and password\"}");
                else reply(fd,"500 Internal Server Error","application/json","{\"error\":\"Could not save Wi-Fi\"}");
            }
            return 0;
        }
        reply(fd,"404 Not Found","text/plain","Not found");return 0;
    }
    if (strcmp(method,"GET")) {
        reply(fd,"405 Method Not Allowed","text/plain","GET only"); return 0;
    }
    if (!strcmp(target,"/certificate")) {
        certificate_response(fd);
    } else if (!strcmp(target,"/status")) {
        char body[512];
        localcam_wifi_status wifi;
        localcam_wifi_status_get(&wifi);
        int n=snprintf(body,sizeof(body),"{\"camera_ready\":%s,\"streaming\":%s,\"frames_sent\":%lu,\"ota_enabled\":false,\"experimental\":true,\"wifi_configured\":%s,\"wifi_connected\":%s,\"setup_ap\":%s,\"station_ip\":\"%s\",\"capture_error\":%d,\"capture_bytes\":%u}",camera_ready?"true":"false",stream_busy?"true":"false",frames_sent,wifi.configured?"true":"false",wifi.connected?"true":"false",wifi.setup_ap?"true":"false",wifi.ip,last_capture_error,last_capture_bytes);
        if(n>1 && n<(int)sizeof(body)){
            int added=snprintf(body+n-1,sizeof(body)-n+1,",\"https\":true,\"tls_bytes\":%u,\"tls_peak\":%u,\"heap_free\":%u,\"security_configured\":%s}",localcam_tls_allocated(),localcam_tls_peak(),localcam_heap_free(),localcam_security_configured()?"true":"false");
            if(added>0 && added<(int)(sizeof(body)-n+1))response(fd,"200 OK","application/json",body,n-1+added);
        }
    } else if (!strcmp(target,"/stream.mjpg") || !strcmp(target,"/snapshot.jpg")) {
        if (!localcam_security_configured() || !camera_ready || stream_busy) {
            reply(fd,"503 Service Unavailable","text/plain","Camera unavailable or busy"); return 0;
        }
        stream_busy=1;
        if (!strcmp(target,"/stream.mjpg")) {
            if (rtos_create_thread(&stream_thread,4,"mjpeg",video_client,4096,(beken_thread_arg_t)(intptr_t)fd)==0) return 1;
            stream_busy=0;
            reply(fd,"503 Service Unavailable","text/plain","Cannot start stream");
        } else {
            unsigned char *frame=os_malloc(FRAME_CAPACITY);
            int error=-1;
            unsigned n=frame?video_buffer_read_frame(frame,FRAME_CAPACITY,&error,2000):0;
            last_capture_error=error;last_capture_bytes=n;
            n=error?0:jpeg_length(frame,n);
            if(n)
                response(fd,"200 OK","image/jpeg",frame,n);
            else reply(fd,"503 Service Unavailable","text/plain","No valid frame");
            if(frame)os_free(frame);
            stream_busy=0;
        }
    } else reply(fd,"404 Not Found","text/plain","Not found");
    return 0;
}

static void web_main(beken_thread_arg_t arg)
{
    int listener;
    struct sockaddr_in address;
    (void)arg;
    os_printf("LOCALCAM: web task starting, opening sensor\r\n");
    camera_ready=video_buffer_open()>0;
    if(localcam_tls_init()){
        os_printf("LOCALCAM: TLS initialization failed %d; no plaintext fallback\r\n",localcam_tls_last_error());goto done;
    }
    {
        char fingerprint[65];
        if(!localcam_security_ca_fingerprint(fingerprint))os_printf("LOCALCAM: CA SHA256 %s\r\n",fingerprint);
    }
    listener=socket(AF_INET,SOCK_STREAM,0);
    if(listener<0)goto done;
    memset(&address,0,sizeof(address));
    address.sin_family=AF_INET;address.sin_port=htons(443);address.sin_addr.s_addr=htonl(INADDR_ANY);
    if(bind(listener,(struct sockaddr*)&address,sizeof(address)) || listen(listener,2))goto cleanup;
    os_printf("LOCALCAM: HTTPS listening, camera_ready=%d TLS bytes=%u\r\n",camera_ready,localcam_tls_allocated());
    for(;;) {
        int fd=accept(listener,NULL,NULL);
        int no_delay=1;
        struct timeval timeout={10,0};
        if(fd<0){rtos_delay_milliseconds(100);continue;}
        setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&no_delay,sizeof(no_delay));
        setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
        localcam_wifi_status wifi;
        localcam_wifi_status_get(&wifi);
        if(localcam_security_refresh_certificate(wifi.connected?wifi.ip:"")<0){close(fd);continue;}
        unsigned started=rtos_get_time();
        if(localcam_tls_accept(fd)){
            os_printf("LOCALCAM: TLS handshake rejected %d\r\n",localcam_tls_last_error());close(fd);continue;
        }
        os_printf("LOCALCAM: TLS handshake %u ms, memory %u peak %u free %u\r\n",rtos_get_time()-started,localcam_tls_allocated(),localcam_tls_peak(),localcam_heap_free());
        timeout.tv_sec=3;
        setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
        int transferred=serve_client(fd);
        /* A successful stream transfers ownership to the worker. */
        if(!transferred){localcam_tls_close(fd);close(fd);}
    }
cleanup: close(listener);
done: rtos_delete_thread(NULL);
}

void user_main(void)
{
    os_printf("LOCALCAM: native application entered\r\n");
    tvideo_set_sensor(VGA_640_480,TYPE_10FPS);
    localcam_wifi_init();
    rtos_create_thread(&web_thread,4,"localweb",web_main,20480,0);
}



