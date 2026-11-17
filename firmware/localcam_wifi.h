#ifndef LOCALCAM_WIFI_H
#define LOCALCAM_WIFI_H
typedef struct { int configured, connected, setup_ap; char ip[16]; } localcam_wifi_status;
void localcam_wifi_init(void);
void localcam_wifi_status_get(localcam_wifi_status *out);
/* URL-encoded form body. Returns 0 on success, -1 invalid input, -2 flash error. */
int localcam_wifi_submit(const char *body, unsigned length);
#endif
