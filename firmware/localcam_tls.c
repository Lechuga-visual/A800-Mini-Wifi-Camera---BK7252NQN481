/* Compiled separately against modern, namespaced Mbed TLS. */
#include "localcam_tls_symbols.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "mbedtls/platform_util.h"
#include "localcam_tls.h"
#include "localcam_security.h"
#include "localcam_device.h"
#include "mbedtls/pem.h"
#include "mbedtls/oid.h"
#include "mbedtls/pkcs5.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern void *lc_port_calloc(size_t, size_t);
extern void lc_port_free(void *);
extern int lc_port_entropy(void *, unsigned char *, size_t);
extern int lc_port_recv(int, void *, size_t);
extern int lc_port_send(int, const void *, size_t);
extern uint32_t lc_port_time(void);

typedef struct { int used, fd,handshaking; uint32_t started; mbedtls_ssl_context ssl; } tls_client;
static tls_client clients[2];
static mbedtls_ssl_config config;
static mbedtls_ctr_drbg_context drbg;
static mbedtls_x509_crt cert;
static mbedtls_pk_context key;
static int ready, last_error;
static localcam_identity identity;
static mbedtls_pk_context ca_key;
static mbedtls_x509_crt ca_cert;
static char certified_ip[16];
static unsigned char session_token[32];
static unsigned session_started;
static int session_valid;
static int create_identity(void);
static int issue_server_certificate(const char *station_ip);

static tls_client *find_client(int fd)
{
    unsigned i;
    for(i=0;i<2;i++)if(clients[i].used && clients[i].fd==fd)return &clients[i];
    return NULL;
}
static int tls_recv(void *ctx,unsigned char *buffer,size_t length)
{
    tls_client *c=ctx;
    int n;
    if(c->handshaking && (uint32_t)(lc_port_time()-c->started)>10000)return MBEDTLS_ERR_SSL_TIMEOUT;
    n=lc_port_recv(c->fd,buffer,length);
    if(c->handshaking && (uint32_t)(lc_port_time()-c->started)>10000)return MBEDTLS_ERR_SSL_TIMEOUT;
    return n<0?MBEDTLS_ERR_SSL_INTERNAL_ERROR:n;
}
static int tls_send(void *ctx,const unsigned char *buffer,size_t length)
{
    int n=lc_port_send(((tls_client*)ctx)->fd,buffer,length);
    return n<=0?MBEDTLS_ERR_SSL_INTERNAL_ERROR:n;
}
int localcam_tls_init(void)
{
    int r,stored;
    const unsigned char personalization[]="A800-Local-TLS-0.2";
    if(ready)return 0;
    mbedtls_platform_set_calloc_free(lc_port_calloc,lc_port_free);
    mbedtls_ctr_drbg_init(&drbg);mbedtls_x509_crt_init(&cert);mbedtls_pk_init(&key);
    mbedtls_pk_init(&ca_key);mbedtls_x509_crt_init(&ca_cert);mbedtls_ssl_config_init(&config);
    r=mbedtls_ctr_drbg_seed(&drbg,lc_port_entropy,NULL,personalization,sizeof(personalization));
    if(r)goto fail;
    stored=localcam_store_load(&identity);
    if(stored<0){r=-1;goto fail;}
    if(!stored){r=create_identity();if(r)goto fail;}
    else{
        r=mbedtls_pk_parse_key(&ca_key,identity.ca_key_der,identity.ca_key_length,NULL,0,mbedtls_ctr_drbg_random,&drbg);
        if(!r)r=mbedtls_pk_parse_key(&key,identity.server_key_der,identity.server_key_length,NULL,0,mbedtls_ctr_drbg_random,&drbg);
        if(r)goto fail;
    }
    r=mbedtls_x509_crt_parse_der(&ca_cert,identity.ca_der,identity.ca_length);
    if(!r)r=issue_server_certificate("");
    if(!r)r=mbedtls_ssl_config_defaults(&config,MBEDTLS_SSL_IS_SERVER,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT);
    if(!r){
        mbedtls_ssl_conf_rng(&config,mbedtls_ctr_drbg_random,&drbg);
        mbedtls_ssl_conf_authmode(&config,MBEDTLS_SSL_VERIFY_NONE); /* Passwords and sessions authenticate clients. */
        r=mbedtls_ssl_conf_own_cert(&config,&cert,&key);
    }
    if(!r){ready=1;last_error=0;return 0;}
fail:
    last_error=r;mbedtls_ssl_config_free(&config);mbedtls_pk_free(&key);mbedtls_pk_free(&ca_key);
    mbedtls_x509_crt_free(&cert);mbedtls_x509_crt_free(&ca_cert);mbedtls_ctr_drbg_free(&drbg);
    mbedtls_platform_zeroize(&identity,sizeof(identity));return r;
}
int localcam_tls_accept(int fd)
{
    unsigned i;
    int r;
    tls_client *c=NULL;
    if(!ready)return -1;
    for(i=0;i<2;i++)if(!clients[i].used){c=&clients[i];break;}
    if(!c)return -1;
    c->fd=fd;c->used=1;c->handshaking=1;c->started=lc_port_time();mbedtls_ssl_init(&c->ssl);
    r=mbedtls_ssl_setup(&c->ssl,&config);
    if(!r){mbedtls_ssl_set_bio(&c->ssl,c,tls_send,tls_recv,NULL);r=mbedtls_ssl_handshake(&c->ssl);}
    last_error=r;
    if(r){localcam_tls_close(fd);return r;}
    c->handshaking=0;return 0;
}
int localcam_tls_read(int fd,void *buffer,size_t length)
{
    tls_client *c=find_client(fd);
    return c?mbedtls_ssl_read(&c->ssl,buffer,length):-1;
}
int localcam_tls_write(int fd,const void *buffer,size_t length)
{
    tls_client *c=find_client(fd);
    return c?mbedtls_ssl_write(&c->ssl,buffer,length):-1;
}
void localcam_tls_close(int fd)
{
    tls_client *c=find_client(fd);
    if(c){mbedtls_ssl_free(&c->ssl);c->used=0;}
}
int localcam_tls_last_error(void){return last_error;}

static int credential_hash(const unsigned char *credential,size_t length,unsigned char hash[32])
{
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,credential,length,
        identity.salt,sizeof(identity.salt),10000,32,hash);
}
static int password_hash(const char *username,const char *password,unsigned char hash[32])
{
    unsigned char credential[80];
    int n=snprintf((char*)credential,sizeof(credential),"%s:%s",username,password);
    int r=n<0 || n>=(int)sizeof(credential)?-1:credential_hash(credential,n,hash);
    mbedtls_platform_zeroize(credential,sizeof(credential));return r;
}
static int new_key(mbedtls_pk_context *context,unsigned char *der,unsigned capacity,uint32_t *length)
{
    int r=mbedtls_pk_setup(context,mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if(!r)r=mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,mbedtls_pk_ec(*context),mbedtls_ctr_drbg_random,&drbg);
    if(r)return r;
    r=mbedtls_pk_write_key_der(context,der,capacity);
    if(r<=0)return r?r:-1;
    memmove(der,der+capacity-r,r);*length=r;return 0;
}
static int write_certificate(int is_ca,const char *subject,const char *issuer,
    const unsigned char *san,size_t san_length,unsigned char *der,unsigned capacity)
{
    mbedtls_x509write_cert writer;
    mbedtls_mpi serial;
    unsigned char random_serial[16]={0};
    static const unsigned char server_usage[]={0x30,0x0a,0x06,0x08,0x2b,0x06,0x01,0x05,0x05,0x07,0x03,0x01};
    int r;
    mbedtls_x509write_crt_init(&writer);mbedtls_mpi_init(&serial);
    r=mbedtls_ctr_drbg_random(&drbg,random_serial,sizeof(random_serial));
    random_serial[0]=(random_serial[0]&0x7f)|1;
    if(!r)r=mbedtls_mpi_read_binary(&serial,random_serial,sizeof(random_serial));
    mbedtls_x509write_crt_set_version(&writer,MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&writer,MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&writer,is_ca?&ca_key:&key);
    mbedtls_x509write_crt_set_issuer_key(&writer,&ca_key);
    if(!r)r=mbedtls_x509write_crt_set_serial(&writer,&serial);
    if(!r)r=mbedtls_x509write_crt_set_subject_name(&writer,subject);
    if(!r)r=mbedtls_x509write_crt_set_issuer_name(&writer,issuer);
    if(!r)r=mbedtls_x509write_crt_set_validity(&writer,LC_CERT_NOT_BEFORE,LC_CERT_NOT_AFTER);
    if(!r)r=mbedtls_x509write_crt_set_basic_constraints(&writer,is_ca,is_ca?0:-1);
    if(!r)r=mbedtls_x509write_crt_set_key_usage(&writer,is_ca?
        MBEDTLS_X509_KU_KEY_CERT_SIGN|MBEDTLS_X509_KU_CRL_SIGN:MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
    if(!r)r=mbedtls_x509write_crt_set_subject_key_identifier(&writer);
    if(!r)r=mbedtls_x509write_crt_set_authority_key_identifier(&writer);
    if(!r && !is_ca)r=mbedtls_x509write_crt_set_extension(&writer,MBEDTLS_OID_EXTENDED_KEY_USAGE,
        MBEDTLS_OID_SIZE(MBEDTLS_OID_EXTENDED_KEY_USAGE),0,server_usage,sizeof(server_usage));
    if(!r && san_length)r=mbedtls_x509write_crt_set_extension(&writer,MBEDTLS_OID_SUBJECT_ALT_NAME,
        MBEDTLS_OID_SIZE(MBEDTLS_OID_SUBJECT_ALT_NAME),0,san,san_length);
    if(!r)r=mbedtls_x509write_crt_der(&writer,der,capacity,mbedtls_ctr_drbg_random,&drbg);
    if(r>0)memmove(der,der+capacity-r,r);
    mbedtls_mpi_free(&serial);mbedtls_x509write_crt_free(&writer);
    mbedtls_platform_zeroize(random_serial,sizeof(random_serial));return r;
}
static int create_identity(void)
{
    char ca_name[80];
    int r;
    memset(&identity,0,sizeof(identity));
    r=mbedtls_ctr_drbg_random(&drbg,identity.salt,sizeof(identity.salt));
    if(!r)r=new_key(&ca_key,identity.ca_key_der,sizeof(identity.ca_key_der),&identity.ca_key_length);
    if(!r)r=new_key(&key,identity.server_key_der,sizeof(identity.server_key_der),&identity.server_key_length);
    if(!r)r=password_hash("admin","admin",identity.admin_hash);
    snprintf(ca_name,sizeof(ca_name),"CN=A800 Local CA %02x%02x%02x%02x",identity.salt[0],identity.salt[1],identity.salt[2],identity.salt[3]);
    if(!r){r=write_certificate(1,ca_name,ca_name,NULL,0,identity.ca_der,sizeof(identity.ca_der));
        if(r>0){identity.ca_length=r;r=0;}}
    if(!r)r=localcam_store_save(&identity);
    return r;
}
static int parse_ip(const char *text,unsigned char out[4])
{
    unsigned part=0,digits=0,index=0;
    const unsigned char *p=(const unsigned char*)text;
    if(!text || !*text)return -1;
    for(;;p++){
        if(*p>='0' && *p<='9'){
            part=part*10+*p-'0';if(part>255 || ++digits>3)return -1;
        }else if(*p=='.' || !*p){
            if(!digits || index>=4)return -1;
            out[index++]=part;part=0;digits=0;
            if(!*p)break;
        }else return -1;
    }
    return index==4?0:-1;
}
static int issue_server_certificate(const char *station_ip)
{
    unsigned char san[128]={0x30,0},der[1024],ip[4];
    unsigned at=2,n=strlen(LC_HOSTNAME);
    char subject[80],issuer[160];
    mbedtls_x509_crt candidate;
    int length,r;
    san[at++]=0x82;san[at++]=n;memcpy(san+at,LC_HOSTNAME,n);at+=n;
    san[at++]=0x82;san[at++]=n+6;memcpy(san+at,LC_HOSTNAME,n);at+=n;memcpy(san+at,".local",6);at+=6;
    san[at++]=0x87;san[at++]=4;san[at++]=192;san[at++]=168;san[at++]=4;san[at++]=1;
    if(station_ip && *station_ip){
        if(parse_ip(station_ip,ip))return -1;
        san[at++]=0x87;san[at++]=4;memcpy(san+at,ip,4);at+=4;
    }
    san[1]=at-2;
    snprintf(subject,sizeof(subject),"CN=%s",LC_HOSTNAME);
    r=mbedtls_x509_dn_gets(issuer,sizeof(issuer),&ca_cert.subject);
    if(r<=0 || r>=(int)sizeof(issuer))return -1;
    length=write_certificate(0,subject,issuer,san,at,der,sizeof(der));
    if(length<=0)return length?length:-1;
    mbedtls_x509_crt_init(&candidate);
    r=mbedtls_x509_crt_parse_der(&candidate,der,length);
    if(r){mbedtls_x509_crt_free(&candidate);return r;}
    mbedtls_x509_crt_free(&cert);cert=candidate;
    snprintf(certified_ip,sizeof(certified_ip),"%s",station_ip?station_ip:"");return 0;
}
int localcam_security_refresh_certificate(const char *station_ip)
{
    unsigned i;
    if(!ready)return -1;
    if(!station_ip)station_ip="";
    if(!strcmp(station_ip,certified_ip))return 0;
    for(i=0;i<2;i++)if(clients[i].used)return 1;
    last_error=issue_server_certificate(station_ip);return last_error;
}
int localcam_security_ca_pem(unsigned char *out,size_t capacity)
{
    size_t length=0;
    if(!ready)return -1;
    if(mbedtls_pem_write_buffer("-----BEGIN CERTIFICATE-----\n","-----END CERTIFICATE-----\n",
        identity.ca_der,identity.ca_length,out,capacity,&length))return -1;
    return length && length<=capacity?(int)length-1:-1;
}
int localcam_security_ca_fingerprint(char out[65])
{
    static const char hex[]="0123456789abcdef";
    unsigned char hash[32];
    unsigned i;
    if(!ready || mbedtls_sha256(identity.ca_der,identity.ca_length,hash,0))return -1;
    for(i=0;i<32;i++){out[i*2]=hex[hash[i]>>4];out[i*2+1]=hex[hash[i]&15];}
    out[64]=0;return 0;
}
int localcam_security_configured(void){return ready && identity.configured;}
unsigned localcam_security_generation(void){return identity.generation;}
static int valid_password(const char *password)
{
    size_t n=strlen(password),i;
    if(n<12 || n>64)return 0;
    for(i=0;i<n;i++)if((unsigned char)password[i]<32 || (unsigned char)password[i]>126)return 0;
    return strcmp(password,"admin")!=0;
}
int localcam_security_set_passwords(const char *admin,const char *viewer)
{
    localcam_identity candidate=identity;
    int r=0;
    if(!ready || (!*admin && !*viewer) || (!identity.configured && (!*admin || !*viewer)) ||
       (*admin && !valid_password(admin)) || (*viewer && !valid_password(viewer)) ||
       (*admin && *viewer && !strcmp(admin,viewer)))return -1;
    if(*admin)r=password_hash("admin",admin,candidate.admin_hash);
    if(!r && *viewer)r=password_hash("ha",viewer,candidate.ha_hash);
    candidate.configured=1;
    if(!r)r=localcam_store_save(&candidate);
    if(!r){identity=candidate;localcam_security_session_clear();}
    mbedtls_platform_zeroize(&candidate,sizeof(candidate));return r?-2:0;
}
int localcam_auth_role(const char *authorization,unsigned length)
{
    unsigned char decoded[128],hash[32];
    size_t n=0;
    unsigned i,a=0,b=0;
    int role=0;
    if(!ready || !authorization || length<10 || length>178 || memcmp(authorization,"Basic ",6) || (length-6)%4)return 0;
    for(i=6;i<length;i++){
        unsigned char c=authorization[i];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='/'||
            (c=='='&&i>=length-2)))return 0;
    }
    memset(decoded,0,sizeof(decoded));
    if(mbedtls_base64_decode(decoded,sizeof(decoded),&n,(const unsigned char*)authorization+6,length-6))goto done;
    if(credential_hash(decoded,n,hash))goto done;
    for(i=0;i<32;i++){a|=hash[i]^identity.admin_hash[i];b|=hash[i]^identity.ha_hash[i];}
    role=a==0?2:(identity.configured && b==0)?1:0;
done:
    mbedtls_platform_zeroize(decoded,sizeof(decoded));mbedtls_platform_zeroize(hash,sizeof(hash));return role;
}
void localcam_security_session_clear(void)
{
    session_valid=0;mbedtls_platform_zeroize(session_token,sizeof(session_token));
}
int localcam_security_session_create(char out[65])
{
    static const char hex[]="0123456789abcdef";
    unsigned i;
    session_valid=0;
    if(!ready || mbedtls_ctr_drbg_random(&drbg,session_token,sizeof(session_token)))return -1;
    for(i=0;i<32;i++){out[i*2]=hex[session_token[i]>>4];out[i*2+1]=hex[session_token[i]&15];}
    out[64]=0;session_started=lc_port_time();session_valid=1;return 0;
}
int localcam_security_session_role(const char *token,unsigned length,int setup_interface)
{
    static const char hex[]="0123456789abcdef";
    unsigned i,different=0;
    if(!ready || !session_valid || length!=64 || (!identity.configured && !setup_interface) ||
       (uint32_t)(lc_port_time()-session_started)>=900000u)return 0;
    for(i=0;i<32;i++){
        different|=(unsigned char)token[i*2]^hex[session_token[i]>>4];
        different|=(unsigned char)token[i*2+1]^hex[session_token[i]&15];
    }
    return different?0:2;
}

