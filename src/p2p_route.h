
#ifndef P2P_ROUTE_H
#define P2P_ROUTE_H

#include <stdint.h>
#include <stdbool.h>
#include "p2p_platform.h"   /* cross-platform socket headers */

typedef struct {
    struct sockaddr_in local_addrs[8];
    uint32_t           local_masks[8];
    int                addr_count;
    struct sockaddr_in lan_peer_addr;   /* confirmed LAN addr */
    int                lan_confirmed;
    uint64_t           probe_time;
} route_ctx_t;

void route_init(route_ctx_t *rt);

int  route_detect_local(route_ctx_t *rt);

bool  route_check_same_subnet(route_ctx_t *rt, const struct sockaddr_in *peer_priv);

int  route_send_probe(route_ctx_t *rt, p2p_socket_t sock, const struct sockaddr_in *peer_priv, uint16_t local_port);
int  route_on_probe(route_ctx_t *rt, const struct sockaddr_in *from, p2p_socket_t sock);
int  route_on_probe_ack(route_ctx_t *rt, const struct sockaddr_in *from);

#endif /* P2P_ROUTE_H */
