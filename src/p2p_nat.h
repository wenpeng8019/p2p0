/*
 * NAT 穿透
 */

#ifndef P2P_NAT_H
#define P2P_NAT_H

#include <p2p.h>
#include <netinet/in.h>

enum {
    NAT_IDLE = 0,
    NAT_REGISTERING,
    NAT_PUNCHING,
    NAT_CONNECTED,
    NAT_RELAY
};

typedef struct {
    int              state;
    struct sockaddr_in server_addr;                     // 信令服务器套接口地址
    struct sockaddr_in peer_pub_addr;                   // 外网地址套接口地址
    struct sockaddr_in peer_priv_addr;                  // 内网地址套接口地址 (用于内网直接) */
    char             local_peer_id[P2P_PEER_ID_MAX];    // 本端 ID
    char             remote_peer_id[P2P_PEER_ID_MAX];   // 对端 ID
    uint64_t         last_send_time;
    uint64_t         last_recv_time;
    int              punch_attempts;
    uint64_t         punch_start;
    int              verbose;                           // 是否输出详细日志
} nat_ctx_t;

void nat_init(nat_ctx_t *n);
int  nat_start(nat_ctx_t *n, const char *local_peer_id, const char *remote_peer_id,
               int sock, const struct sockaddr_in *server, int verbose);
int  nat_on_packet(nat_ctx_t *n, uint8_t type, const uint8_t *payload, int len,
                   const struct sockaddr_in *from, int sock);
int  nat_tick(nat_ctx_t *n, int sock);

#endif /* P2P_NAT_H */
