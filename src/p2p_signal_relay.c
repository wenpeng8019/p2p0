/*
 * Relay 信令客户端实现
 *
 * ============================================================================
 * 概述
 * ============================================================================
 *
 * 本模块实现基于中央服务器的 P2P 信令交换机制。
 * 客户端通过 TCP 长连接与信令服务器通信，服务器负责转发信令消息。
 *
 * 工作原理：
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │                         信令服务器 (p2p_server)                         │
 * │                                                                         │
 * │    ┌───────────────────────────────────────────────────────────────┐   │
 * │    │                     已登录客户端列表                           │   │
 * │    │   [alice] ─────── fd:5                                        │   │
 * │    │   [bob]   ─────── fd:6                                        │   │
 * │    │   [carol] ─────── fd:7                                        │   │
 * │    └───────────────────────────────────────────────────────────────┘   │
 * └─────────────────────────────────────────────────────────────────────────┘
 *          ▲                                              ▲
 *          │ TCP 长连接                                   │ TCP 长连接
 *          │                                              │
 *   ┌──────┴──────┐                                ┌──────┴──────┐
 *   │    Alice    │ ─────── MSG_CONNECT ──────────▶│     Bob     │
 *   │  (主动方)   │ ◀───── MSG_SIGNAL_RELAY ───────│   (被动方)  │
 *   └─────────────┘                                └─────────────┘
 *
 * ============================================================================
 * 通信协议
 * ============================================================================
 *
 * 消息格式（9 字节头 + 变长负载）：
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  Magic (4B)  │  Type (1B)  │  Length (4B)  │  Payload (N bytes)        │
 * │  "P2P0"      │  MSG_xxx    │  负载长度     │  [target_name + data]     │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * 消息类型：
 *   MSG_LOGIN        (1) → 客户端登录，携带 peer_name
 *   MSG_LOGIN_ACK    (2) → 服务器确认登录
 *   MSG_LIST         (3) → 请求在线用户列表
 *   MSG_LIST_RES     (4) → 返回在线用户列表
 *   MSG_CONNECT      (5) → 向目标方发起连接请求（服务器转为 MSG_SIGNAL 转发）
 *   MSG_SIGNAL       (6) → 服务器转发的连接请求（来自主动方）
 *   MSG_SIGNAL_ANS   (7) → 被动方应答（服务器转为 MSG_SIGNAL_RELAY 转发）
 *   MSG_SIGNAL_RELAY (8) → 服务器转发的应答（来自被动方）
 *   MSG_HEARTBEAT    (9) → 心跳包，保持 TCP 连接和 NAT 映射
 *
 * ============================================================================
 * 连接流程
 * ============================================================================
 *
 *   Alice (主动方)                 Server                  Bob (被动方)
 *      │                             │                          │
 *      │── MSG_LOGIN ───────────────▶│                          │
 *      │◀─ MSG_LOGIN_ACK ────────────│                          │
 *      │                             │                          │
 *      │                             │◀──────── MSG_LOGIN ──────│
 *      │                             │───── MSG_LOGIN_ACK ─────▶│
 *      │                             │                          │
 *      │── MSG_CONNECT(bob,offer) ──▶│                          │
 *      │                             │── MSG_SIGNAL(alice) ────▶│
 *      │                             │                          │
 *      │                             │◀─ MSG_SIGNAL_ANS(answer)─│
 *      │◀─ MSG_SIGNAL_RELAY(bob) ────│                          │
 *      │                             │                          │
 *      ▼                             ▼                          ▼
 *   ICE 连接检查开始（使用交换的候选地址进行 UDP 打洞）
 *
 * ============================================================================
 * 状态机
 * ============================================================================
 *
 *   ┌─────────────┐     connect()     ┌─────────────┐
 *   │ DISCONNECTED│ ─────────────────▶│ CONNECTING  │
 *   └─────────────┘                   └─────────────┘
 *          ▲                                 │
 *          │ close()                         │ 连接成功
 *          │                                 ▼
 *   ┌─────────────┐                   ┌─────────────┐
 *   │    ERROR    │ ◀───────────────  │  CONNECTED  │
 *   └─────────────┘   连接失败/超时   └─────────────┘
 *
 * ============================================================================
 * 使用示例
 * ============================================================================
 *
 *   // 初始化
 *   p2p_signal_relay_ctx_t ctx = {0};
 *
 *   // 连接信令服务器
 *   p2p_signal_relay_connect(&ctx, "192.168.1.100", 8888, "alice");
 *
 *   // 发送连接请求给 bob
 *   p2p_signal_relay_send_connect(&ctx, "bob", payload_data, payload_len);
 *
 *   // 主循环中处理接收
 *   p2p_signal_relay_tick(&ctx, session);
 *
 *   // 断开连接
 *   p2p_signal_relay_close(&ctx);
 */

#include "p2p_signal_relay.h"
#include "p2p_signal_protocol.h"
#include "p2p_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

/* ============================================================================
 * 信令客户端状态机周期维护
 * ============================================================================
 *
 * 在主循环中调用，处理从信令服务器接收的消息。
 * 非阻塞模式，无数据时立即返回。
 *
 * 处理的消息类型：
 *   - MSG_SIGNAL:       来自主动方的连接请求（服务器转发）
 *   - MSG_SIGNAL_RELAY: 来自被动方的应答（服务器转发）
 *
 * @param ctx  信令上下文
 * @param s    P2P 会话
 */
void p2p_signal_relay_tick(p2p_signal_relay_ctx_t *ctx, struct p2p_session *s) {
    if (ctx->fd < 0) return;

    // 接收信令服务数据包
    p2p_msg_hdr_t hdr;
    int n = recv(ctx->fd, &hdr, sizeof(hdr), 0);
    if (n == (int)sizeof(hdr)) {
        if (hdr.magic != P2P_SIGNAL_MAGIC) return;

        // 信令服务器（中继）转发的，来自对方的请求
        if (hdr.type == MSG_SIGNAL || hdr.type == MSG_SIGNAL_RELAY) {

            // 读取对方 name，即来自谁的连接请求
            char sender_name[P2P_MAX_NAME];
            recv(ctx->fd, sender_name, P2P_MAX_NAME, 0);
            
            // 保存发送者名称（用于后续发送 answer）
            if (hdr.type == MSG_SIGNAL) {
                strncpy(ctx->incoming_peer_name, sender_name, P2P_MAX_NAME);
            }
            
            // 接收后面的负载数据
            // + 信令服务器只中继转发请求数据，所以数据结构是由对等双方自行约定
            uint32_t payload_len = hdr.length - P2P_MAX_NAME;
            uint8_t *payload = malloc(payload_len);
            recv(ctx->fd, payload, payload_len, 0);

            printf("Signaling: Received signal from '%s' (%u bytes)\n", sender_name, payload_len);
            fflush(stdout);
            
            // 解析信令数据并注入 ICE 状态机
            p2p_signaling_payload_t p;
            if (p2p_signal_unpack(&p, payload, payload_len) == 0) {
                p2p_ice_handle_signaling_payload(s, &p);
            } else {
                /* 解包失败，尝试作为旧版 trickle 候选处理 */
                p2p_ice_on_remote_candidates(s, payload, payload_len);
            }
            
            free(payload);
        }
    }
}

/* ============================================================================
 * 连接信令服务器
 * ============================================================================
 *
 * 建立与信令服务器的 TCP 连接，并发送登录请求。
 * 使用单例模式：如果已连接，直接返回成功。
 *
 * 连接流程：
 *   1. 创建 TCP socket
 *   2. 连接到服务器
 *   3. 设置为非阻塞模式
 *   4. 发送 MSG_LOGIN 包
 *   5. 状态转为 SIGNAL_CONNECTED
 *
 * @param ctx        信令上下文
 * @param server_ip  服务器 IP 地址
 * @param port       服务器端口（默认 8888）
 * @param my_name    本地 peer 名称（用于标识自己）
 * @return           0 成功，-1 失败
 */
int p2p_signal_relay_connect(p2p_signal_relay_ctx_t *ctx, const char *server_ip, int port, const char *my_name) {

    /* 单例模式：如果已经连接，直接返回成功 */
    if (ctx->state == SIGNAL_CONNECTED) {
        return 0;
    }

    // 如果正在连接中，跳过（避免重复连接）
    if (ctx->state == SIGNAL_CONNECTING) {
        return 0;
    }

    // 标记为连接中
    ctx->state = SIGNAL_CONNECTING;
    ctx->last_connect_attempt = time_ms();

    // 建立标准 tcp 连接

    ctx->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->fd < 0) {
        ctx->state = SIGNAL_ERROR;
        return -1;
    }

    memset(&ctx->server_addr, 0, sizeof(ctx->server_addr));
    ctx->server_addr.sin_family = AF_INET;
    ctx->server_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip, &ctx->server_addr.sin_addr);

    if (connect(ctx->fd, (struct sockaddr *)&ctx->server_addr, sizeof(ctx->server_addr)) < 0) {
        close(ctx->fd);
        ctx->fd = -1;
        ctx->state = SIGNAL_ERROR;
        return -1;
    }

    // 设置为非阻塞模式
    int flags = fcntl(ctx->fd, F_GETFL, 0);
    fcntl(ctx->fd, F_SETFL, flags | O_NONBLOCK);

    // 初始化自己的 name
    strncpy(ctx->my_name, my_name, P2P_MAX_NAME);

    // 初始化登录数据包
    p2p_msg_hdr_t hdr = {P2P_SIGNAL_MAGIC, MSG_LOGIN, sizeof(p2p_msg_login_t)};
    p2p_msg_login_t login;
    memset(&login, 0, sizeof(login));
    strncpy(login.name, my_name, P2P_MAX_NAME);

    // 发送数据包
    send(ctx->fd, &hdr, sizeof(hdr), 0);
    send(ctx->fd, &login, sizeof(login), 0);

    // 标记为已连接
    ctx->state = SIGNAL_CONNECTED;

    printf("Signaling: Connected to server %s:%d as '%s'\n", server_ip, port, my_name);
    fflush(stdout);
    return 0;
}

/* ============================================================================
 * 断开与信令服务器的连接
 * ============================================================================
 *
 * 关闭 TCP socket，释放资源，状态转为 SIGNAL_DISCONNECTED。
 *
 * @param ctx  信令上下文
 */
void p2p_signal_relay_close(p2p_signal_relay_ctx_t *ctx) {
    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
    ctx->state = SIGNAL_DISCONNECTED;
}

/* ============================================================================
 * 向目标对端发起连接请求
 * ============================================================================
 *
 * 发送 MSG_CONNECT 消息到信令服务器，服务器将其转为 MSG_SIGNAL 转发给目标方。
 *
 * 消息格式：
 *   [HDR: 9B] [target_name: 32B] [payload: N bytes]
 *
 * 负载数据由对等双方约定格式（使用 p2p_signal_protocol 模块序列化），
 * 信令服务器仅作中继转发，不解析负载内容。
 *
 * @param ctx         信令上下文
 * @param target_name 目标方 peer 名称
 * @param data        负载数据（ICE 候选等）
 * @param len         负载长度
 * @return            0 成功，-1 失败
 */
int p2p_signal_relay_send_connect(p2p_signal_relay_ctx_t *ctx, const char *target_name, const void *data, int len) {
    if (ctx->fd < 0) return -1;

    // 构造连接请求数据包
    // + 该数据包发给信令服务器，并由信令服务器中继转发给目标方。
    //   这也意味着负载的数据结构由对等双方约定，和服务器无关
    p2p_msg_hdr_t hdr = {P2P_SIGNAL_MAGIC, MSG_CONNECT, (uint32_t)(P2P_MAX_NAME + len)};
    char target[P2P_MAX_NAME] = {0};
    strncpy(target, target_name, P2P_MAX_NAME);

    send(ctx->fd, &hdr, sizeof(hdr), 0);
    send(ctx->fd, target, P2P_MAX_NAME, 0);

    // 发送自定义负载数据
    send(ctx->fd, data, len, 0);
    
    printf("Signaling [PUB]: Sent connect request to '%s' (%d bytes)\n", target_name, len);
    fflush(stdout);
    return 0;
}

/* ============================================================================
 * 回复连接请求（发送 answer）
 * ============================================================================
 *
 * 被动方收到 MSG_SIGNAL 后，使用此函数发送 answer。
 * 发送 MSG_SIGNAL_ANS 消息，服务器将其转为 MSG_SIGNAL_RELAY 转发给主动方。
 *
 * @param ctx         信令上下文
 * @param target_name 主动方 peer 名称（通常使用 ctx->incoming_peer_name）
 * @param data        应答负载数据（ICE 候选等）
 * @param len         负载长度
 * @return            0 成功，-1 失败
 */
int p2p_signal_relay_reply_connect(p2p_signal_relay_ctx_t *ctx, const char *target_name, const void *data, int len) {
    if (ctx->fd < 0) return -1;

    // 构造 answer 数据包 (MSG_SIGNAL_ANS)
    // 服务器会将其转换为 MSG_SIGNAL_RELAY 并转发给目标方
    p2p_msg_hdr_t hdr = {P2P_SIGNAL_MAGIC, MSG_SIGNAL_ANS, (uint32_t)(P2P_MAX_NAME + len)};
    char target[P2P_MAX_NAME] = {0};
    strncpy(target, target_name, P2P_MAX_NAME);

    send(ctx->fd, &hdr, sizeof(hdr), 0);
    send(ctx->fd, target, P2P_MAX_NAME, 0);
    send(ctx->fd, data, len, 0);
    
    printf("Signaling [PUB]: Sent answer to '%s' (%d bytes)\n", target_name, len);
    fflush(stdout);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
