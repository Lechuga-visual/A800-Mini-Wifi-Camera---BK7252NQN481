#ifndef LOCALCAM_TLS_H
#define LOCALCAM_TLS_H
#include <stddef.h>
int localcam_tls_init(void);
int localcam_tls_accept(int fd);
int localcam_tls_read(int fd, void *buffer, size_t length);
int localcam_tls_write(int fd, const void *buffer, size_t length);
void localcam_tls_close(int fd);
/* 0: denied, 1: Home Assistant/viewer, 2: administrator. */
int localcam_auth_role(const char *authorization, unsigned length);
int localcam_tls_last_error(void);
unsigned localcam_tls_allocated(void);
unsigned localcam_tls_peak(void);
unsigned localcam_heap_free(void);
int localcam_is_setup_interface(int fd);
#endif
