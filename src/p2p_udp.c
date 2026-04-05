
#include "p2p_internal.h"

ret_t p2p_udp_open(struct p2p_instance *inst, uint16_t port) {

    inst->sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (inst->sock == P_INVALID_SOCKET) return P_INVALID_SOCKET;

    // 设置为非阻塞模式
    if (P_sock_nonblock(inst->sock, true) != E_NONE) {
        P_sock_close(inst->sock);
        return P_INVALID_SOCKET;
    }

    // 允许地址重用
    int opt = 1;
    setsockopt(inst->sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    // 绑定
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(inst->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        P_sock_close(inst->sock);
        return P_INVALID_SOCKET;
    }

    return E_NONE;
}

void p2p_udp_close(struct p2p_instance *inst) {
    if (inst->sock != P_INVALID_SOCKET) {
        P_sock_close(inst->sock);
        inst->sock = P_INVALID_SOCKET;
    }
}

ret_t p2p_udp_send_to(struct p2p_instance *inst, const struct sockaddr_in *addr,
                      const void *data, int len) {

    ssize_t n = sendto(inst->sock, (const char *)data, len, 0,
                       (const struct sockaddr *)addr, sizeof(*addr));
    if (n != len) {
        int e = P_sock_errno();
        return e ? E_EXTERNAL(e) : E_UNKNOWN;
    }
    return (int)n;
}

ret_t p2p_udp_recv_from(struct p2p_instance *inst, struct sockaddr_in *from,
                        void *buf, int buf_size) {

    socklen_t sock_len = sizeof(*from);

    ssize_t n = recvfrom(inst->sock, (char *)buf, buf_size, 0, (struct sockaddr *)from, &sock_len);
    if (n < 0) {
        if (P_sock_is_wouldblock()) return E_BUSY;
        int e = P_sock_errno();
        return e ? E_EXTERNAL(e) : E_UNKNOWN;
    }
    return (int)n;
}
