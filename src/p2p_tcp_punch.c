
#define MOD_TAG "TCP"

#include "p2p_internal.h"

/* 
 * 尝试 TCP 同时发起 (Simultaneous Open)
 * 这是一个复杂的流程，通常需要两端在几乎同一时间发起 connect()。
 */
int p2p_tcp_punch_connect(struct p2p_session *s, const struct sockaddr_in *remote) {
    if (!s->inst->cfg.enable_tcp) return -1;

    sock_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == P_INVALID_SOCKET) return -1;

    /* 必须设置 SO_REUSEADDR and SO_REUSEPORT (如果支持) */
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    /* 绑定到与 UDP 相同的端口 */
    struct sockaddr_in loc;
    memset(&loc, 0, sizeof(loc));
    loc.sin_family = AF_INET;
    loc.sin_addr.s_addr = INADDR_ANY;
    loc.sin_port = htons(s->inst->cfg.tcp_port);
    if (bind(sock, (struct sockaddr *)&loc, sizeof(loc)) < 0) {
        /* 如果端口被占用，尝试随机端口并更新配置 */
        printf(LA_F("Bind failed to %d, port busy, trying random port", LA_F264, 264), 
                      s->inst->cfg.tcp_port);
        loc.sin_port = 0;
        if (bind(sock, (struct sockaddr *)&loc, sizeof(loc)) < 0) {
            print("E:", LA_F("Bind failed", LA_F263, 263));
             P_sock_close(sock);
             return -1;
        }
    }
    {
        struct sockaddr_in bound;
        socklen_t blen = sizeof(bound);
        getsockname(sock, (struct sockaddr *)&bound, &blen);
        printf(LA_F("Bound to :%d", LA_F265, 265), ntohs(bound.sin_port));
    }

    /* 设置为非阻塞 */
    P_sock_nonblock(sock, true);

    /* 进行三次握手的"同时发起"尝试 */
    print("I:", LA_F("Attempting Simultaneous Open to %s:%d", LA_F259, 259),
                 inet_ntoa(remote->sin_addr), ntohs(remote->sin_port));
    
    int ret = connect(sock, (struct sockaddr *)remote, sizeof(*remote));
    if (ret < 0 && !P_sock_is_inprogress()) {
        P_sock_close(sock);
        return -1;
    }

    s->inst->tcp_sock = sock;
    return 0;
}
