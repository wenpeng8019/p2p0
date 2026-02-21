
#include "p2p_internal.h"

/* 
 * 尝试 TCP 同时发起 (Simultaneous Open)
 * 这是一个复杂的流程，通常需要两端在几乎同一时间发起 connect()。
 */
int p2p_tcp_punch_connect(p2p_session_t *s, const struct sockaddr_in *remote) {
    if (!s->cfg.enable_tcp) return -1;

    p2p_socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == P2P_INVALID_SOCKET) return -1;

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
        P2P_LOG_DEBUG("TCP", "%s %s %d, %s", MSG(MSG_ERROR_BIND),
                      MSG(MSG_STUN_TO), s->cfg.tcp_port, MSG(MSG_TCP_FALLBACK_PORT));
        loc.sin_port = 0;
        if (bind(sock, (struct sockaddr *)&loc, sizeof(loc)) < 0) {
            P2P_LOG_ERROR("TCP", "%s", MSG(MSG_ERROR_BIND));
             p2p_close_socket(sock);
             return -1;
        }
    }
    {
        struct sockaddr_in bound;
        socklen_t blen = sizeof(bound);
        getsockname(sock, (struct sockaddr *)&bound, &blen);
        P2P_LOG_DEBUG("TCP", "%s :%d", MSG(MSG_TCP_BOUND_TO), ntohs(bound.sin_port));
    }

    /* 设置为非阻塞 */
    p2p_set_nonblock(sock);

    /* 进行三次握手的"同时发起"尝试 */
    P2P_LOG_INFO("TCP", "%s %s:%d", MSG(MSG_TCP_SIMULTANEOUS_OPEN),
                 inet_ntoa(remote->sin_addr), ntohs(remote->sin_port));
    
    int ret = connect(sock, (struct sockaddr *)remote, sizeof(*remote));
    if (ret < 0 && p2p_errno() != P2P_EINPROGRESS) {
        p2p_close_socket(sock);
        return -1;
    }

    s->tcp_sock = sock;
    return 0;
}
