
#include "p2p_internal.h"

ret_t p2p_udp_open(struct p2p_instance *inst, const struct sockaddr_in *bind_ip, uint16_t port) {

    P_check(inst && inst->socks && inst->sock_cnt < inst->sock_cap, return E_INVALID;)

    p2p_sock_t *ps = &inst->socks[inst->sock_cnt];

    ps->sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ps->sock == P_INVALID_SOCKET) return P_INVALID_SOCKET;

    if (P_sock_nonblock(ps->sock, true) != E_NONE) {
        P_sock_close(ps->sock);
        ps->sock = P_INVALID_SOCKET;
        return P_INVALID_SOCKET;
    }

    int opt = 1;
    setsockopt(ps->sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(ps->sock, SOL_SOCKET, SO_REUSEPORT, (const char *)&opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = bind_ip ? bind_ip->sin_addr.s_addr : INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(ps->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        P_sock_close(ps->sock);
        ps->sock = P_INVALID_SOCKET;
        return P_INVALID_SOCKET;
    }

    socklen_t len = sizeof(ps->local_addr);
    if (getsockname(ps->sock, (struct sockaddr *)&ps->local_addr, &len) != 0) {
        memset(&ps->local_addr, 0, sizeof(ps->local_addr));
    }

    memset(&ps->mapped_addr, 0, sizeof(ps->mapped_addr));
    ps->state = 1/*bound*/;
    inst->sock_cnt++;
    return E_NONE;
}

void p2p_udp_close(struct p2p_instance *inst, int sock_idx) {
    P_check(inst && inst->socks && sock_idx >= 0 && sock_idx < inst->sock_cnt, return;)

    if (inst->socks[sock_idx].sock != P_INVALID_SOCKET) {
        P_sock_close(inst->socks[sock_idx].sock);
    }

    if (sock_idx + 1 < inst->sock_cnt) {
        memmove(&inst->socks[sock_idx], &inst->socks[sock_idx + 1],
                (size_t)(inst->sock_cnt - sock_idx - 1) * sizeof(*inst->socks));
    }

    inst->sock_cnt--;
    memset(&inst->socks[inst->sock_cnt], 0, sizeof(*inst->socks));
    inst->socks[inst->sock_cnt].sock = P_INVALID_SOCKET;
    inst->socks[inst->sock_cnt].state = -1/*invalid*/;
}

void p2p_udp_close_all(struct p2p_instance *inst) {
    if (!inst || !inst->socks) return;

    while (inst->sock_cnt > 0) {
        p2p_udp_close(inst, inst->sock_cnt - 1);
    }

    free(inst->socks);
    inst->socks = NULL;
    inst->sock_cap = 0;
}

ret_t p2p_udp_send_to_sock(struct p2p_instance *inst, int sock_idx,
                           const struct sockaddr_in *addr,
                           const void *data, int len) {

    P_check(inst && inst->socks && sock_idx >= 0 && sock_idx < inst->sock_cnt, return E_INVALID;)
    sock_t fd = inst->socks[sock_idx].sock;
    if (fd == P_INVALID_SOCKET) return E_INVALID;

    ssize_t n = sendto(fd, (const char *)data, len, 0,
                       (const struct sockaddr *)addr, sizeof(*addr));
    if (n != len) {
        int e = P_sock_errno();
        return e ? E_EXTERNAL(e) : E_UNKNOWN;
    }
    return (int)n;
}

ret_t p2p_udp_send_to(struct p2p_instance *inst, const struct sockaddr_in *addr,
                      const void *data, int len) {
    return p2p_udp_send_to_sock(inst, 0, addr, data, len);
}

ret_t p2p_udp_recv_from(struct p2p_instance *inst, struct sockaddr_in *from,
                        void *buf, int buf_size, int *recv_sock_idx) {

    P_check(inst && inst->socks, return E_INVALID;)

    for (int i = 0; i < inst->sock_cnt; i++) {
        if (inst->socks[i].sock == P_INVALID_SOCKET) continue;

        socklen_t sock_len = sizeof(*from);
        ssize_t n = recvfrom(inst->socks[i].sock, (char *)buf, buf_size, 0,
                             (struct sockaddr *)from, &sock_len);
        if (n < 0) {
            if (P_sock_is_wouldblock()) continue;
            int e = P_sock_errno();
            return e ? E_EXTERNAL(e) : E_UNKNOWN;
        }

        if (recv_sock_idx) *recv_sock_idx = i;
        return (int)n;
    }

    return E_BUSY;
}
