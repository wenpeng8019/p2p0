
#define MOD_TAG "TCP"

#include "p2p_internal.h"

/* 
 * 尝试 TCP 同时发起 (Simultaneous Open)
 * 这是一个复杂的流程，通常需要两端在几乎同一时间发起 connect()。
 */
int p2p_tcp_punch_connect(p2p_session_t *s, const struct sockaddr_in *remote) {
    if (!s->cfg.enable_tcp) return -1;

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
    loc.sin_port = htons(s->cfg.tcp_port);
    if (bind(sock, (struct sockaddr *)&loc, sizeof(loc)) < 0) {
        /* 如果端口被占用，尝试随机端口并更新配置 */
        printf("D:", LA_F("%s %s %d, %s", LA_F9, 264), LA_W("Bind failed", LA_W12, 16),
                      LA_S("to", LA_S87, 209), s->cfg.tcp_port, LA_W("port busy, trying random port", LA_W71, 79));
        loc.sin_port = 0;
        if (bind(sock, (struct sockaddr *)&loc, sizeof(loc)) < 0) {
            printf("E: %s", LA_S("Bind failed", LA_S9, 160));
             P_sock_close(sock);
             return -1;
        }
    }
    {
        struct sockaddr_in bound;
        socklen_t blen = sizeof(bound);
        getsockname(sock, (struct sockaddr *)&bound, &blen);
        printf("D:", LA_F("%s :%d", LA_F40, 295), LA_W("Bound to", LA_W13, 17), ntohs(bound.sin_port));
    }

    /* 设置为非阻塞 */
    P_sock_nonblock(sock, true);

    /* 进行三次握手的"同时发起"尝试 */
    printf("I:", LA_F("Attempting Simultaneous Open to %s:%d", LA_F61, 315),
                 inet_ntoa(remote->sin_addr), ntohs(remote->sin_port));
    
    int ret = connect(sock, (struct sockaddr *)remote, sizeof(*remote));
    if (ret < 0 && !P_sock_is_inprogress()) {
        P_sock_close(sock);
        return -1;
    }

    s->tcp_sock = sock;
    return 0;
}
