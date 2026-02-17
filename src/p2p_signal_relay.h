/*
 * RELAY 模块信令（TCP，实现登录与消息转发）
 *
 * ============================================================================
 * 模块概述
 * ============================================================================
 *
 * 基于中央服务器的信令交换模块。客户端通过 TCP 长连接与信令服务器通信，
 * 服务器负责用户管理和消息转发。
 *
 * 特点：
 *   - TCP 长连接，可靠传输
 *   - 服务器维护在线用户列表
 *   - 消息透明转发，负载由对等方约定
 *   - 支持心跳保活
 *
 * 与 PubSub 模式对比：
 *   ┌──────────────┬──────────────────────────┬──────────────────────────┐
 *   │   特性       │   Relay 模式             │   PubSub 模式            │
 *   ├──────────────┼──────────────────────────┼──────────────────────────┤
 *   │ 传输层       │ TCP 长连接               │ HTTPS (GitHub API)       │
 *   │ 服务器       │ 自建 p2p_server          │ GitHub Gist              │
 *   │ 实时性       │ 高（推送模式）           │ 低（轮询模式）           │
 *   │ 部署复杂度   │ 需要自建服务器           │ 无需服务器               │
 *   │ 加密         │ 可选（TLS 或应用层）     │ 内置 DES 加密            │
 *   └──────────────┴──────────────────────────┴──────────────────────────┘
 */

#ifndef P2P_SIGNAL_RELAY_H
#define P2P_SIGNAL_RELAY_H

#include <stdint.h>
#include <netinet/in.h>
#include <p2pp.h>  /* RELAY 模式协议定义 */

/* ============================================================================
 * 信令上下文
 * ============================================================================ */

struct p2p_session;

/* 信令连接状态 */
typedef enum {
    SIGNAL_DISCONNECTED = 0,  /* 未连接 */
    SIGNAL_CONNECTING,        /* 连接中 */
    SIGNAL_CONNECTED,         /* 已连接 */
    SIGNAL_ERROR              /* 错误状态 */
} p2p_signal_relay_state_t;

/*
 * 信令上下文结构
 *
 * 保存与信令服务器的连接状态和相关信息。
 */
typedef struct {
    int fd;                                      /* TCP socket 描述符 */
    char my_name[P2P_PEER_ID_MAX];                  /* 本地 peer 名称 */
    char incoming_peer_name[P2P_PEER_ID_MAX];       /* 收到请求时的对端名称 */
    struct sockaddr_in server_addr;              /* 服务器地址 */
    p2p_signal_relay_state_t state;              /* 连接状态 */
    uint64_t last_connect_attempt;               /* 最后连接尝试时间（毫秒） */
} p2p_signal_relay_ctx_t;

/* ============================================================================
 * API 函数
 * ============================================================================ */

int  p2p_signal_relay_connect(p2p_signal_relay_ctx_t *ctx, const char *server_ip, int port, const char *my_name);
void p2p_signal_relay_tick(p2p_signal_relay_ctx_t *ctx, struct p2p_session *s);
int  p2p_signal_relay_send_connect(p2p_signal_relay_ctx_t *ctx, const char *target_name, const void *data, int len);
int  p2p_signal_relay_reply_connect(p2p_signal_relay_ctx_t *ctx, const char *target_name, const void *data, int len);
void p2p_signal_relay_close(p2p_signal_relay_ctx_t *ctx);

#endif /* P2P_SIGNAL_RELAY_H */
