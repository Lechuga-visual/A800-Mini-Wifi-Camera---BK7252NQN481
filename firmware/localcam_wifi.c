#include "include.h"
#include "rtos_pub.h"
#include "wlan_ui_pub.h"
#include "BkDriverFlash.h"
#include "uart_pub.h"
#include "mem_pub.h"
#include "arm_arch.h"
#include "localcam_wifi.h"
#include "localcam_security.h"
#include "localcam_device.h"
#include <stdint.h>
#include <string.h>

/* Verified erased in this unit's original dump. Outside OTA/RF/vendor config. */
#define CONFIG_ADDR 0x1e2000u
#define CONFIG_MAGIC 0x31434c57u
typedef struct { uint32_t magic, checksum; char ssid[33], password[65]; unsigned char pad[2]; } wifi_config;
static wifi_config pending_config;
static volatile int pending;
static localcam_wifi_status wifi_status;
static beken_thread_t network_thread;

static uint32_t checksum(const wifi_config *cfg)
{
    const unsigned char *p=(const unsigned char*)cfg->ssid;
    uint32_t c=2166136261u;
    unsigned i;
    for(i=0;i<sizeof(*cfg)-8;i++)c=(c^p[i])*16777619u;
    return c;
}

static int valid_config(const wifi_config *cfg)
{
    return cfg->magic==CONFIG_MAGIC && cfg->checksum==checksum(cfg) &&
        cfg->ssid[0] && memchr(cfg->ssid,0,sizeof(cfg->ssid)) &&
        memchr(cfg->password,0,sizeof(cfg->password));
}

static int hex_digit(char c)
{
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}

static int decode_field(const char *src,unsigned length,char *dst,unsigned capacity)
{
    unsigned at=0,i;
    for(i=0;i<length;i++){
        unsigned char c=(unsigned char)src[i];
        if(c=='+')c=' ';
        else if(c=='%'){
            int a,b;
            if(i+2>=length||(a=hex_digit(src[i+1]))<0||(b=hex_digit(src[i+2]))<0)return -1;
            c=(unsigned char)(a*16+b);i+=2;
        }
        if(c<32||c==127||at+1>=capacity)return -1;
        dst[at++]=(char)c;
    }
    dst[at]=0;return (int)at;
}

static int parse_config(const char *body,unsigned length,wifi_config *cfg)
{
    unsigned pos=0,seen=0;
    memset(cfg,0,sizeof(*cfg));
    if(!length||length>512)return -1;
    while(pos<length){
        unsigned end=pos,equal;
        int decoded;
        while(end<length&&body[end]!='&')end++;
        equal=pos;while(equal<end&&body[equal]!='=')equal++;
        if(equal==end)return -1;
        if(equal-pos==4&&!memcmp(body+pos,"ssid",4)&&!(seen&1)){
            decoded=decode_field(body+equal+1,end-equal-1,cfg->ssid,sizeof(cfg->ssid));
            if(decoded<=0)return -1;
            seen|=1;
        }else if(equal-pos==8&&!memcmp(body+pos,"password",8)&&!(seen&2)){
            decoded=decode_field(body+equal+1,end-equal-1,cfg->password,sizeof(cfg->password));
            if(decoded<0||(decoded>0&&decoded<8))return -1;
            if(decoded==64){unsigned k;for(k=0;k<64;k++)if(hex_digit(cfg->password[k])<0)return -1;}
            seen|=2;
        }else return -1;
        pos=end+1;
    }
    if(seen!=3)return -1;
    cfg->magic=CONFIG_MAGIC;cfg->checksum=checksum(cfg);return 0;
}

int localcam_wifi_submit(const char *body,unsigned length)
{
    wifi_config cfg,verify;
    unsigned char *sector;
    unsigned i;
    uint32_t magic;
    GLOBAL_INT_DECLARATION();
    if(parse_config(body,length,&cfg))return -1;
    sector=os_malloc(4096);
    if(!sector)return -2;
    if(bk_flash_abs_addr_read(CONFIG_ADDR,sector,4096)){os_free(sector);return -2;}
    memcpy(&magic,sector,4);
    /* Refuse to erase any unknown data; only erased space or our own format. */
    if(magic!=CONFIG_MAGIC){
        for(i=0;i<4096;i++)if(sector[i]!=255){os_free(sector);return -2;}
    }
    os_free(sector);
    if(bk_flash_abs_addr_write(CONFIG_ADDR,(const unsigned char*)&cfg,sizeof(cfg),1) ||
        bk_flash_abs_addr_read(CONFIG_ADDR,(unsigned char*)&verify,sizeof(verify)) ||
        memcmp(&cfg,&verify,sizeof(cfg)))return -2;
    GLOBAL_INT_DISABLE();
    pending_config=cfg;pending=1;
    GLOBAL_INT_RESTORE();
    return 0;
}

void localcam_wifi_status_get(localcam_wifi_status *out)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();*out=wifi_status;GLOBAL_INT_RESTORE();
}

static void start_setup_ap(void)
{
    network_InitTypeDef_st net;
    if(wifi_status.setup_ap)return;
    memset(&net,0,sizeof(net));
    strcpy(net.wifi_ssid,LC_SETUP_SSID);strcpy(net.wifi_key,LC_SETUP_PASSWORD);
    net.wifi_mode=BK_SOFT_AP;net.dhcp_mode=DHCP_SERVER;net.wifi_retry_interval=100;
    strcpy(net.local_ip_addr,"192.168.4.1");strcpy(net.net_mask,"255.255.255.0");
    strcpy(net.gateway_ip_addr,"192.168.4.1");strcpy(net.dns_server_ip_addr,"192.168.4.1");
    if(bk_wlan_start(&net)==0)wifi_status.setup_ap=1;
}

static void start_station(const wifi_config *cfg)
{
    network_InitTypeDef_st net;
    start_setup_ap();
    bk_wlan_stop(BK_STATION);
    memset(&net,0,sizeof(net));
    memcpy(net.wifi_ssid,cfg->ssid,sizeof(cfg->ssid));
    memcpy(net.wifi_key,cfg->password,sizeof(cfg->password));
    net.wifi_mode=BK_STATION;net.dhcp_mode=DHCP_CLIENT;net.wifi_retry_interval=100;
    wifi_status.configured=1;wifi_status.connected=0;wifi_status.ip[0]=0;
    bk_wlan_start(&net);
}

static void network_main(beken_thread_arg_t arg)
{
    wifi_config cfg;
    unsigned online_seconds=0,offline_seconds=0;
    GLOBAL_INT_DECLARATION();
    (void)arg;
    memset(&cfg,0,sizeof(cfg));
    if(!bk_flash_abs_addr_read(CONFIG_ADDR,(unsigned char*)&cfg,sizeof(cfg))&&valid_config(&cfg))start_station(&cfg);
    else start_setup_ap();
    for(;;){
        IPStatusTypedef ip;
        LinkStatusTypeDef link;
        int connected;
        if(pending){
            GLOBAL_INT_DISABLE();cfg=pending_config;pending=0;GLOBAL_INT_RESTORE();
            start_station(&cfg);online_seconds=offline_seconds=0;
        }
        memset(&ip,0,sizeof(ip));memset(&link,0,sizeof(link));
        connected=wifi_status.configured && !bk_wlan_get_link_status(&link) && link.conn_state &&
            !bk_wlan_get_ip_status(&ip,BK_STATION) && ip.ip[0] && strcmp(ip.ip,"0.0.0.0");
        if(connected){
            if(!wifi_status.connected)os_printf("LOCALCAM: station IP %s\r\n",ip.ip);
            GLOBAL_INT_DISABLE();wifi_status.connected=1;memcpy(wifi_status.ip,ip.ip,16);wifi_status.ip[15]=0;GLOBAL_INT_RESTORE();
            offline_seconds=0;
            if(localcam_security_configured())online_seconds++;else online_seconds=0;
            /* Keep setup available briefly so the phone can show the LAN address. */
            if(online_seconds>=60&&wifi_status.setup_ap){bk_wlan_stop(BK_SOFT_AP);wifi_status.setup_ap=0;}
        }else{
            wifi_status.connected=0;online_seconds=0;offline_seconds++;
            if(offline_seconds>=30)start_setup_ap();
        }
        rtos_delay_milliseconds(1000);
    }
}

void localcam_wifi_init(void)
{
    rtos_create_thread(&network_thread,4,"localwifi",network_main,4096,0);
}

