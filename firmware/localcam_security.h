#ifndef LOCALCAM_SECURITY_H
#define LOCALCAM_SECURITY_H
#include <stdint.h>
#include <stddef.h>
#define LC_SECURITY_MAGIC 0x32434c53u
#define LC_SECURITY_A 0x1e3000u
#define LC_SECURITY_B 0x1e4000u
typedef struct {
    uint32_t magic,version,generation,checksum,configured;
    uint32_t ca_length,ca_key_length,server_key_length;
    unsigned char salt[32],admin_hash[32],ha_hash[32];
    unsigned char ca_der[768],ca_key_der[256],server_key_der[256];
} localcam_identity;
/* 1: valid, 0: erased, -1: unknown or corrupt. Never overwrite unknown data. */
int localcam_store_load(localcam_identity *out);
int localcam_store_save(localcam_identity *identity);
int localcam_security_configured(void);
unsigned localcam_security_generation(void);
int localcam_security_set_passwords(const char *admin,const char *viewer);
int localcam_security_ca_pem(unsigned char *out,size_t capacity);
int localcam_security_ca_fingerprint(char out[65]);
int localcam_security_session_create(char out[65]);
int localcam_security_session_role(const char *token,unsigned length,int setup_interface);
void localcam_security_session_clear(void);
int localcam_security_refresh_certificate(const char *station_ip);
#endif
