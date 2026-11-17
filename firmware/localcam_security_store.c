#ifndef LOCALCAM_HOST_TEST
#include "include.h"
#include "BkDriverFlash.h"
#endif
#include "localcam_security.h"
#include <string.h>

static unsigned active_address;
static uint32_t checksum(const localcam_identity *identity)
{
    const unsigned char *p=(const unsigned char*)identity;
    uint32_t c=2166136261u;
    unsigned i;
    for(i=0;i<sizeof(*identity);i++){
        if(i>=12 && i<16)continue;
        c=(c^p[i])*16777619u;
    }
    return c;
}
static int valid(const localcam_identity *identity)
{
    return identity->magic==LC_SECURITY_MAGIC && identity->version==1 && identity->configured<=1 &&
        identity->ca_length>0 && identity->ca_length<=sizeof(identity->ca_der) &&
        identity->ca_key_length>0 && identity->ca_key_length<=sizeof(identity->ca_key_der) &&
        identity->server_key_length>0 && identity->server_key_length<=sizeof(identity->server_key_der) &&
        identity->checksum==checksum(identity);
}
static int erased(const localcam_identity *identity)
{
    const unsigned char *p=(const unsigned char*)identity;
    unsigned i;
    for(i=0;i<sizeof(*identity);i++)if(p[i]!=255)return 0;
    return 1;
}
int localcam_store_load(localcam_identity *out)
{
    localcam_identity a,b;
    int va,vb;
    active_address=0;
    if(bk_flash_abs_addr_read(LC_SECURITY_A,(unsigned char*)&a,sizeof(a)) ||
       bk_flash_abs_addr_read(LC_SECURITY_B,(unsigned char*)&b,sizeof(b)))return -1;
    va=valid(&a);vb=valid(&b);
    if(va || vb){
        int newer_b=vb && (!va || (int32_t)(b.generation-a.generation)>0);
        *out=newer_b?b:a;active_address=newer_b?LC_SECURITY_B:LC_SECURITY_A;
        return 1;
    }
    return erased(&a)&&erased(&b)?0:-1;
}
int localcam_store_save(localcam_identity *identity)
{
    unsigned address=active_address==LC_SECURITY_A?LC_SECURITY_B:LC_SECURITY_A;
    localcam_identity verify,candidate=*identity;
    unsigned char sector[4096];
    unsigned i;
    uint32_t magic;
    /* Inspect the full sector before erase, including bytes beyond our record. */
    if(bk_flash_abs_addr_read(address,sector,sizeof(sector)))return -1;
    memcpy(&magic,sector,sizeof(magic));
    if(magic!=LC_SECURITY_MAGIC){
        for(i=0;i<sizeof(sector);i++)if(sector[i]!=255)return -1;
    }
    candidate.magic=LC_SECURITY_MAGIC;candidate.version=1;candidate.generation++;
    candidate.checksum=checksum(&candidate);
    if(!valid(&candidate))return -1;
    if(bk_flash_abs_addr_write(address,(const unsigned char*)&candidate,sizeof(candidate),1) ||
       bk_flash_abs_addr_read(address,(unsigned char*)&verify,sizeof(verify)) ||
       memcmp(&candidate,&verify,sizeof(candidate)))return -1;
    *identity=candidate;active_address=address;return 0;
}
