
#include "p2p_internal.h"

#include <string.h>

/* ---- UDP socket ---- */

p2p_socket_t udp_create_socket(uint16_t port) {

    p2p_socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == P2P_INVALID_SOCKET) return P2P_INVALID_SOCKET;

    // 设置为非阻塞模式
    if (p2p_set_nonblock(sock) < 0) {
        p2p_close_socket(sock);
        return P2P_INVALID_SOCKET;
    }

    // 允许地址重用
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    // 绑定
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        p2p_close_socket(sock);
        return P2P_INVALID_SOCKET;
    }

    return sock;
}

int udp_send_to(p2p_socket_t sock, const struct sockaddr_in *addr,
                const void *buf, int len) {
    ssize_t n = sendto(sock, (const char *)buf, len, 0,
                       (const struct sockaddr *)addr, sizeof(*addr));
    return (int)n;
}

int udp_recv_from(p2p_socket_t sock, struct sockaddr_in *from,
                  void *buf, int max_len) {
    socklen_t fromlen = sizeof(*from);
    ssize_t n = recvfrom(sock, (char *)buf, max_len, 0,
                         (struct sockaddr *)from, &fromlen);
    if (n < 0) {
        int e = p2p_errno();
        if (e == P2P_EAGAIN) return 0;
        return -1;
    }
    return (int)n;
}

int udp_send_packet(p2p_socket_t sock, const struct sockaddr_in *addr,
                    uint8_t type, uint8_t flags, uint16_t seq,
                    const void *payload, int payload_len) {
    uint8_t buf[P2P_MTU];
    if (P2P_HDR_SIZE + payload_len > P2P_MTU) return -1;

    p2p_pkt_hdr_encode(buf, type, flags, seq);
    if (payload_len > 0 && payload)
        memcpy(buf + P2P_HDR_SIZE, payload, payload_len);

    return udp_send_to(sock, addr, buf, P2P_HDR_SIZE + payload_len);
}
