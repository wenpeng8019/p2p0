/*
 * IPv6 direct link — socket open / close / send / recv
 *
 * 与 IPv4 的 p2p_udp.c 对等，提供独立的 IPv6 UDP 套接字。
 * 仅单个 socket（inst->sock6），不使用 socks[] 数组。
 */

#include "p2p_internal.h"

ret_t p2p_ipv6_open(struct p2p_instance *inst, uint16_t port) {

    inst->sock6 = socket(AF_INET6, SOCK_DGRAM, 0);
    if (inst->sock6 == P_INVALID_SOCKET) return E_UNKNOWN;

    if (P_sock_nonblock(inst->sock6, true) != E_NONE) {
        P_sock_close(inst->sock6);
        inst->sock6 = P_INVALID_SOCKET;
        return E_UNKNOWN;
    }

    /* IPV6_V6ONLY = 1: 只接收 IPv6，不接收 IPv4-mapped */
    int v6only = 1;
    setsockopt(inst->sock6, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&v6only, sizeof(v6only));

    int opt = 1;
    setsockopt(inst->sock6, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(inst->sock6, SOL_SOCKET, SO_REUSEPORT, (const char *)&opt, sizeof(opt));
#endif

    struct sockaddr_in6 bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin6_family = AF_INET6;
    bind_addr.sin6_addr   = in6addr_any;
    bind_addr.sin6_port   = htons(port);

    if (bind(inst->sock6, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        P_sock_close(inst->sock6);
        inst->sock6 = P_INVALID_SOCKET;
        return E_UNKNOWN;
    }

    socklen_t len = sizeof(inst->sock6_addr);
    if (getsockname(inst->sock6, (struct sockaddr *)&inst->sock6_addr, &len) != 0) {
        memset(&inst->sock6_addr, 0, sizeof(inst->sock6_addr));
    }

    return E_NONE;
}

void p2p_ipv6_close(struct p2p_instance *inst) {
    if (!inst || inst->sock6 == P_INVALID_SOCKET) return;
    P_sock_close(inst->sock6);
    inst->sock6 = P_INVALID_SOCKET;
}

ret_t p2p_ipv6_send_packet(struct p2p_instance *inst, const struct sockaddr_in6 *addr,
                            uint8_t type, uint8_t flags, uint16_t seq,
                            const void *payload, int payload_len)
{
    if (inst->sock6 == P_INVALID_SOCKET) return E_INVALID;
    if (P2P_HDR_SIZE + payload_len > P2P_MTU) return E_INVALID;

    uint8_t hdr[4];
    p2p_pkt_hdr_encode(hdr, type, flags, seq);

    sock_msg_t msgs[2]; int n = 1;
    P_msg_set(&msgs[0], hdr, sizeof(hdr));
    if (payload_len > 0 && payload)
        P_msg_set(&msgs[n++], payload, payload_len);

    /* sendmsg with sockaddr_in6 */
    struct msghdr mh = {0};
    mh.msg_name    = (void *)addr;
    mh.msg_namelen = sizeof(struct sockaddr_in6);
    mh.msg_iov     = (struct iovec *)msgs;
    mh.msg_iovlen  = n;

    ssize_t sent = sendmsg(inst->sock6, &mh, 0);
    return sent >= 0 ? (ret_t)sent : E_EXTERNAL(P_sock_errno());
}

ret_t p2p_ipv6_recv_from(struct p2p_instance *inst, sockAddr_t *from,
                          void *buf, int buf_size)
{
    if (!inst || inst->sock6 == P_INVALID_SOCKET) return E_INVALID;

    socklen_t addr_len = sizeof(from->addr.v6);
    ssize_t n = recvfrom(inst->sock6, (char *)buf, buf_size, 0,
                         (struct sockaddr *)&from->addr.v6, &addr_len);
    if (n < 0) {
        if (P_sock_is_wouldblock()) return E_BUSY;
        return E_EXTERNAL(P_sock_errno());
    }
    from->family = AF_INET6;
    return (ret_t)n;
}
