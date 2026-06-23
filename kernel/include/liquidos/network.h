#ifndef LIQUIDOS_NETWORK_H
#define LIQUIDOS_NETWORK_H

#include <liquidos/types.h>

#define NET_URL_LENGTH 192
#define NET_HOST_LENGTH 40

typedef struct NetInfo {
    bool link_up;
    bool dhcp_configured;
    bool dns_configured;
    const char *driver;
    const char *ipv4;
    const char *gateway;
    const char *dns;
} NetInfo;

typedef struct NetResponse {
    bool ok;
    u16 status;
    const char *mime;
    const char *body;
    size_t size;
    char message[64];
} NetResponse;

void network_init(void);
const NetInfo *network_info(void);
bool network_ping(const char *host);
NetResponse network_fetch(const char *url);
bool network_download_to_file(const char *url, const char *path);
const char *network_package_url(const char *name);

#endif
