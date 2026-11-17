#include "include.h"
#include "rtos_pub.h"
#include "mem_pub.h"
#include "drv_model_pub.h"
#include "irda_pub_bk7252n.h"
#include "arm_arch.h"
#include "lwip/sockets.h"
#include "localcam_tls.h"
#include <stdint.h>
#include <string.h>

static unsigned allocated,peak;
/* Prefix also keeps 8-byte alignment required by the TLS allocator. */
void *lc_port_calloc(size_t n,size_t size)
{
    size_t total;
    unsigned *p;
    GLOBAL_INT_DECLARATION();
    if(size && n>((size_t)-1-8)/size)return NULL;
    total=n*size;
    p=os_zalloc(total+8);
    if(!p)return NULL;
    p[0]=total;
    GLOBAL_INT_DISABLE();allocated+=total;if(allocated>peak)peak=allocated;GLOBAL_INT_RESTORE();
    return (unsigned char*)p+8;
}
void lc_port_free(void *ptr)
{
    unsigned *p;
    GLOBAL_INT_DECLARATION();
    if(!ptr)return;
    p=(unsigned*)((unsigned char*)ptr-8);
    GLOBAL_INT_DISABLE();allocated-=p[0];GLOBAL_INT_RESTORE();
    os_free(p);
}
unsigned localcam_tls_allocated(void){return allocated;}
unsigned localcam_tls_peak(void){return peak;}
extern size_t xPortGetFreeHeapSize(void);
unsigned localcam_heap_free(void){return xPortGetFreeHeapSize();}

/* Fail closed if the TRNG driver fails or returns a stuck output. */
int lc_port_entropy(void *ctx,unsigned char *output,size_t length)
{
    static uint32_t previous;
    static int have_previous;
    uint32_t word;
    size_t n;
    (void)ctx;
    while(length){
        if(sddev_control(IRDA_DEV_NAME,TRNG_CMD_GET,&word)!=IRDA_SUCCESS ||
           word==0 || word==0xffffffffu || (have_previous && word==previous))return -1;
        previous=word;have_previous=1;
        n=length<sizeof(word)?length:sizeof(word);
        memcpy(output,&word,n);output+=n;length-=n;
    }
    return 0;
}
int lc_port_recv(int fd,void *buffer,size_t length){return recv(fd,buffer,length,0);}
int lc_port_send(int fd,const void *buffer,size_t length){return send(fd,buffer,length,0);}
uint32_t lc_port_time(void){return rtos_get_time();}
int localcam_is_setup_interface(int fd)
{
    struct sockaddr_in local,peer;
    socklen_t local_size=sizeof(local),peer_size=sizeof(peer);
    memset(&local,0,sizeof(local));memset(&peer,0,sizeof(peer));
    return !getsockname(fd,(struct sockaddr*)&local,&local_size) &&
        !getpeername(fd,(struct sockaddr*)&peer,&peer_size) &&
        local.sin_family==AF_INET && peer.sin_family==AF_INET &&
        ntohl(local.sin_addr.s_addr)==0xc0a80401u &&
        (ntohl(peer.sin_addr.s_addr)&0xffffff00u)==0xc0a80400u;
}
