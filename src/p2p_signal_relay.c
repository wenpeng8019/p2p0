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
 *   │    Alice    │ ─────── P2P_RLY_CONNECT ──────────▶│     Bob     │
 *   │  (主动方)   │ ◀───── P2P_RLY_FORWARD ───────│   (被动方)  │
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
 *   P2P_RLY_LOGIN        (1) → 客户端登录，携带 peer_name
 *   P2P_RLY_LOGIN_ACK    (2) → 服务器确认登录
 *   P2P_RLY_LIST         (3) → 请求在线用户列表
 *   P2P_RLY_LIST_RES     (4) → 返回在线用户列表
 *   P2P_RLY_CONNECT      (5) → 向目标方发起连接请求（服务器转为 P2P_RLY_OFFER 转发）
 *   P2P_RLY_OFFER        (6) → 服务器转发的连接请求（来自主动方）
 *   P2P_RLY_ANSWER       (7) → 被动方应答（服务器转为 P2P_RLY_FORWARD 转发）
 *   P2P_RLY_FORWARD      (8) → 服务器转发的应答（来自被动方）
 *   P2P_RLY_HEARTBEAT    (9) → 心跳包，保持 TCP 连接和 NAT 映射
 *
 * ============================================================================
 * 连接流程
 * ============================================================================
 *
 *   Alice (主动方)                 Server                  Bob (被动方)
 *      │                             │                          │
 *      │── P2P_RLY_LOGIN ───────────────▶│                          │
 *      │◀─ P2P_RLY_LOGIN_ACK ────────────│                          │
 *      │                             │                          │
 *      │                             │◀──────── P2P_RLY_LOGIN ──────│
 *      │                             │───── P2P_RLY_LOGIN_ACK ─────▶│
 *      │                             │                          │
 *      │── P2P_RLY_CONNECT(bob,offer) ──▶│                          │
 *      │                             │── P2P_RLY_OFFER(alice) ────▶│
 *      │                             │                          │
 *      │                             │◀─ P2P_RLY_ANSWER(answer)─│
 *      │◀─ P2P_RLY_FORWARD(bob) ────│                          │
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
 *   p2p_signal_relay_login(&ctx, "192.168.1.100", 8888, "alice");
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
#include "p2p_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p2p_platform.h"

void p2p_signal_relay_init(p2p_signal_relay_ctx_t *ctx) {

    memset(ctx, 0, sizeof(p2p_signal_relay_ctx_t));
    ctx->fd = -1;
    ctx->state = SIGNAL_DISCONNECTED;
    ctx->total_candidates_sent = 0;
    ctx->total_candidates_acked = 0;
    ctx->next_candidate_index = 0;
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
 *   4. 发送 P2P_RLY_LOGIN 包
 *   5. 状态转为 SIGNAL_CONNECTED
 *
 * @param ctx        信令上下文
 * @param server_ip  服务器 IP 地址
 * @param port       服务器端口（默认 8888）
 * @param my_name    本地 peer 名称（用于标识自己）
 * @return           0 成功，-1 失败
 */
int p2p_signal_relay_login(p2p_signal_relay_ctx_t *ctx, const char *server_ip, int port, const char *my_name) {

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
    p2p_set_nonblock(ctx->fd);

    // 初始化自己的 name
    strncpy(ctx->my_name, my_name, P2P_PEER_ID_MAX);

    // 初始化登录数据包
    p2p_relay_hdr_t hdr = {P2P_RLY_MAGIC, P2P_RLY_LOGIN, sizeof(p2p_relay_login_t)};
    p2p_relay_login_t login; memset(&login, 0, sizeof(login));
    strncpy(login.name, my_name, P2P_PEER_ID_MAX);

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
 * 发送 P2P_RLY_CONNECT 消息到信令服务器，服务器将其转为 P2P_RLY_OFFER 转发给目标方。
 * 服务器处理后返回 P2P_RLY_CONNECT_ACK 确认。
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
 * @return            >0 成功转发/存储的候选数量
 *                    0  目标不在线（已存储等待转发）
 *                    -1 TCP 发送失败或超时
 *                    -2 服务器存储失败（容量不足）
 *                    -3 服务器错误
 */
int p2p_signal_relay_send_connect(p2p_signal_relay_ctx_t *ctx, const char *target_name, const void *data, int len) {
    if (ctx->fd < 0) return -1;

    // 构造连接请求数据包
    // + 该数据包发给信令服务器，并由信令服务器中继转发给目标方。
    //   这也意味着负载的数据结构由对等双方约定，和服务器无关
    p2p_relay_hdr_t hdr = {P2P_RLY_MAGIC, P2P_RLY_CONNECT, (uint32_t)(P2P_PEER_ID_MAX + len)};
    char target[P2P_PEER_ID_MAX] = {0};
    strncpy(target, target_name, P2P_PEER_ID_MAX);

    if (send(ctx->fd, &hdr, sizeof(hdr), 0) != sizeof(hdr)) {
        printf("Signaling [PUB]: Failed to send header\n");
        return -1;
    }
    if (send(ctx->fd, target, P2P_PEER_ID_MAX, 0) != P2P_PEER_ID_MAX) {
        printf("Signaling [PUB]: Failed to send target name\n");
        return -1;
    }
    if (send(ctx->fd, data, len, 0) != len) {
        printf("Signaling [PUB]: Failed to send payload\n");
        return -1;
    }
    
    printf("Signaling [PUB]: Sent connect request to '%s' (%d bytes), waiting for ACK...\n", target_name, len);
    fflush(stdout);

    // 从负载中提取候选数量（用于状态追踪）
    // 负载格式（p2p_signaling_payload_t 序列化后）：
    //   [sender: 32B][target: 32B][timestamp: 4B][delay_trigger: 4B][candidate_count: 4B][...]
    // candidate_count 位于偏移量 32+32+4+4 = 72
    uint32_t candidates_in_payload = 0;
    if (len >= 76) {  /* 至少包含到 candidate_count */
        memcpy(&candidates_in_payload, (const uint8_t*)data + 72, 4);
    }

    // 等待服务器 ACK（超时 2 秒）
    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(ctx->fd, &readfds);
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    int ret = select(ctx->fd + 1, &readfds, NULL, NULL, &tv);
    if (ret <= 0) {
        printf("Signaling [PUB]: ACK timeout or select error\n");
        return -1;  // 超时或错误
    }

    // 接收 ACK 头部
    p2p_relay_hdr_t ack_hdr;
    ssize_t n = recv(ctx->fd, &ack_hdr, sizeof(ack_hdr), 0);
    if (n != sizeof(ack_hdr) || ack_hdr.magic != P2P_RLY_MAGIC || ack_hdr.type != P2P_RLY_CONNECT_ACK) {
        printf("Signaling [PUB]: Invalid ACK header (n=%zd, type=%d)\n", n, ack_hdr.type);
        return -1;
    }

    // 接收 ACK 负载
    p2p_relay_connect_ack_t ack_payload;
    n = recv(ctx->fd, &ack_payload, sizeof(ack_payload), 0);
    if (n != sizeof(ack_payload)) {
        printf("Signaling [PUB]: Invalid ACK payload\n");
        return -1;
    }

    printf("Signaling [PUB]: Received ACK (status=%d, candidates_acked=%d)\n", 
           ack_payload.status, ack_payload.candidates_acked);

    // 更新状态追踪
    ctx->total_candidates_sent += candidates_in_payload;
    ctx->total_candidates_acked += ack_payload.candidates_acked;
    ctx->next_candidate_index += ack_payload.candidates_acked;

    // 根据状态返回
    switch (ack_payload.status) {
        case 0:  // 对端在线，成功转发
            printf("Signaling [PUB]: Peer online, forwarded %d candidates\n", ack_payload.candidates_acked);
            ctx->waiting_for_peer = false;  // 对端在线，无需等待
            return ack_payload.candidates_acked > 0 ? ack_payload.candidates_acked : 1;
            
        case 1:  // 对端离线，已缓存且有剩余空间
            printf("Signaling [PUB]: Peer offline, cached %d candidates (storage has remaining space)\n", 
                   ack_payload.candidates_acked);
            ctx->waiting_for_peer = false;  // 还有空间，继续 Trickle ICE
            return 0;  // 返回 0 表示已缓存（目标离线）
            
        case 2:  // 缓存已满（可能缓存了部分候选后满，也可能之前就满了）
            printf("Signaling [PUB]: Storage full (cached %d candidates), waiting for peer to come online\n",
                   ack_payload.candidates_acked);
            // 进入等待状态（等待对端上线后收到 FORWARD）
            ctx->waiting_for_peer = true;
            strncpy(ctx->waiting_target, target_name, P2P_PEER_ID_MAX);
            ctx->waiting_start_time = time_ms();
            return -2;  // 返回 -2 表示缓存已满，停止发送
            
        default: // 服务器错误
            printf("Signaling [PUB]: Unknown status %d\n", ack_payload.status);
            return -3;
    }
}

/* ============================================================================
 * 回复连接请求（发送 answer）
 * ============================================================================
 *
 * 被动方收到 P2P_RLY_OFFER 后，使用此函数发送 answer。
 * 发送 P2P_RLY_ANSWER 消息，服务器将其转为 P2P_RLY_FORWARD 转发给主动方。
 *
 * @param ctx         信令上下文
 * @param target_name 主动方 peer 名称（通常使用 ctx->incoming_peer_name）
 * @param data        应答负载数据（ICE 候选等）
 * @param len         负载长度
 * @return            0 成功，-1 失败
 */
int p2p_signal_relay_reply_connect(p2p_signal_relay_ctx_t *ctx, const char *target_name, const void *data, int len) {
    if (ctx->fd < 0) return -1;

    // 构造 answer 数据包 (P2P_RLY_ANSWER)
    // 服务器会将其转换为 P2P_RLY_FORWARD 并转发给目标方
    p2p_relay_hdr_t hdr = {P2P_RLY_MAGIC, P2P_RLY_ANSWER, (uint32_t)(P2P_PEER_ID_MAX + len)};
    char target[P2P_PEER_ID_MAX] = {0};
    strncpy(target, target_name, P2P_PEER_ID_MAX);

    send(ctx->fd, &hdr, sizeof(hdr), 0);
    send(ctx->fd, target, P2P_PEER_ID_MAX, 0);
    send(ctx->fd, data, len, 0);
    
    printf("Signaling [PUB]: Sent answer to '%s' (%d bytes)\n", target_name, len);
    fflush(stdout);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

/* ============================================================================
 * 信令客户端状态机周期维护
 * ============================================================================
 *
 * 在主循环中调用，处理从信令服务器接收的消息。
 * 非阻塞模式，无数据时立即返回。
 *
 * 处理的消息类型：
 *   - P2P_RLY_OFFER:       来自主动方的连接请求（服务器转发）
 *   - P2P_RLY_FORWARD: 来自被动方的应答（服务器转发）
 *
 * @param ctx  信令上下文
 * @param s    P2P 会话
 */
void p2p_signal_relay_tick(p2p_signal_relay_ctx_t *ctx, struct p2p_session *s) {
    if (ctx->fd < 0) return;

    // 检查等待超时（60 秒）
    if (ctx->waiting_for_peer) {
        uint64_t elapsed = time_ms() - ctx->waiting_start_time;
        if (elapsed > 60000) {  // 60 秒超时
            printf("Signaling: Waiting for peer '%s' timed out (60s), giving up\n", ctx->waiting_target);
            ctx->waiting_for_peer = false;
            ctx->waiting_target[0] = '\0';
            // 可选：断开连接或重置状态
            return;
        }
    }

    // 接收信令服务数据包
    p2p_relay_hdr_t hdr;
    int n = recv(ctx->fd, &hdr, sizeof(hdr), 0);
    if (n == (int)sizeof(hdr)) {
        if (hdr.magic != P2P_RLY_MAGIC) return;

        // 信令服务器（中继）转发的，来自对方的请求
        if (hdr.type == P2P_RLY_OFFER || hdr.type == P2P_RLY_FORWARD) {

            // 读取对方 name，即来自谁的连接请求
            char sender_name[P2P_PEER_ID_MAX];
            recv(ctx->fd, sender_name, P2P_PEER_ID_MAX, 0);

            // 保存发送者名称（用于后续发送 answer）
            if (hdr.type == P2P_RLY_OFFER) {
                strncpy(ctx->incoming_peer_name, sender_name, P2P_PEER_ID_MAX);
            }
            
            // 收到 FORWARD：对端已上线，清除等待状态
            if (hdr.type == P2P_RLY_FORWARD && ctx->waiting_for_peer && 
                strcmp(ctx->waiting_target, sender_name) == 0) {
                printf("Signaling: Peer '%s' is now online (received FORWARD), resuming...\n", sender_name);
                ctx->waiting_for_peer = false;
                ctx->waiting_target[0] = '\0';
                // 注：后续应该继续发送剩余候选，由上层 p2p_update() 检查 next_candidate_index
            }

            // 接收后面的负载数据
            // + 信令服务器只中继转发请求数据，所以数据结构是由对等双方自行约定
            uint32_t payload_len = hdr.length - P2P_PEER_ID_MAX;
            uint8_t *payload = malloc(payload_len);
            recv(ctx->fd, payload, payload_len, 0);

            printf("Signaling: Received signal from '%s' (%u bytes)\n", sender_name, payload_len);
            fflush(stdout);

            // 解析信令数据并注入 ICE 状态机
            p2p_signaling_payload_hdr_t p;
            if (payload_len >= 76 && unpack_signaling_payload_hdr(&p, payload) == 0 &&
                payload_len >= (size_t)(76 + p.candidate_count * 32)) {
                
                /* 添加远端 ICE 候选 */
                for (int i = 0; i < p.candidate_count; i++) {
                    p2p_candidate_t c;
                    unpack_candidate(&c, payload + sizeof(p2p_signaling_payload_hdr_t) + i * sizeof(p2p_candidate_t));
                    
                    /* 排重检查 */
                    int exists = 0;
                    for (int j = 0; j < s->remote_cand_cnt; j++) {
                        if (s->remote_cands[j].addr.sin_addr.s_addr == c.addr.sin_addr.s_addr &&
                            s->remote_cands[j].addr.sin_port == c.addr.sin_port) {
                            exists = 1;
                            break;
                        }
                    }
                    
                    if (!exists && s->remote_cand_cnt < P2P_MAX_CANDIDATES) {
                        s->remote_cands[s->remote_cand_cnt++] = c;
                        printf("[ICE] Added Remote Candidate: %d -> %s:%d\n",
                               c.type, inet_ntoa(c.addr.sin_addr), ntohs(c.addr.sin_port));
                    }
                }
            } else {
                /* 解包失败，尝试作为旧版 trickle 候选处理 */
                p2p_ice_on_remote_candidates(s, payload, payload_len);
            }

            free(payload);
        }
    }
}

