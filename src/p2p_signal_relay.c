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

#include "p2p_internal.h"

void p2p_signal_relay_init(p2p_signal_relay_ctx_t *ctx) {

    memset(ctx, 0, sizeof(p2p_signal_relay_ctx_t));
    ctx->fd = P2P_INVALID_SOCKET;
    ctx->state = SIGNAL_DISCONNECTED;
    ctx->total_candidates_sent = 0;
    ctx->total_candidates_acked = 0;
    ctx->next_candidate_index = 0;
    
    /* 异步读取状态机初始化 */
    ctx->read_state = RELAY_READ_IDLE;
    ctx->read_payload = NULL;
    ctx->read_offset = 0;
    ctx->read_expected = 0;
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
    ctx->last_connect_attempt = p2p_time_ms();

    // 建立标准 tcp 连接

    ctx->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->fd == P2P_INVALID_SOCKET) {
        ctx->state = SIGNAL_ERROR;
        return -1;
    }

    memset(&ctx->server_addr, 0, sizeof(ctx->server_addr));
    ctx->server_addr.sin_family = AF_INET;
    ctx->server_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip, &ctx->server_addr.sin_addr);

    if (connect(ctx->fd, (struct sockaddr *)&ctx->server_addr, sizeof(ctx->server_addr)) < 0) {
        p2p_close_socket(ctx->fd);
        ctx->fd = P2P_INVALID_SOCKET;
        ctx->state = SIGNAL_ERROR;
        return -1;
    }

    // 初始化自己的 name
    strncpy(ctx->my_name, my_name, P2P_PEER_ID_MAX);

    // 初始化登录数据包
    p2p_relay_hdr_t hdr = {P2P_RLY_MAGIC, P2P_RLY_LOGIN, sizeof(p2p_relay_login_t)};
    p2p_relay_login_t login; memset(&login, 0, sizeof(login));
    strncpy(login.name, my_name, P2P_PEER_ID_MAX);

    // 发送数据包 (socket is blocking here)
    send(ctx->fd, (const char *)&hdr, sizeof(hdr), 0);
    send(ctx->fd, (const char *)&login, sizeof(login), 0);

    // 等待 LOGIN_ACK (blocking recv with timeout)
    {
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(ctx->fd, &rfds);
        tv.tv_sec = P2P_RELAY_LOGIN_ACK_TIMEOUT_MS / 1000;
        tv.tv_usec = (P2P_RELAY_LOGIN_ACK_TIMEOUT_MS % 1000) * 1000;
#ifdef _WIN32
        int sel = select(0, &rfds, NULL, NULL, &tv);
#else
        int sel = select((int)ctx->fd + 1, &rfds, NULL, NULL, &tv);
#endif
        if (sel > 0) {
            p2p_relay_hdr_t ack_hdr;
            int rn = recv(ctx->fd, (char *)&ack_hdr, sizeof(ack_hdr), 0);
            if (rn == sizeof(ack_hdr) && ack_hdr.magic == P2P_RLY_MAGIC &&
                ack_hdr.type == P2P_RLY_LOGIN_ACK) {
                /* consume any LOGIN_ACK payload (length bytes) */
                if (ack_hdr.length > 0) {
                    char discard[64];
                    uint32_t to_read = ack_hdr.length;
                    while (to_read > 0) {
                        uint32_t chunk = to_read < sizeof(discard) ? to_read : sizeof(discard);
                        int rd = recv(ctx->fd, discard, chunk, 0);
                        if (rd <= 0) break;
                        to_read -= rd;
                    }
                }
            }
            /* 如果收到其他类型消息，留在缓冲区未读 — 不常见但不致命 */
        }
        /* 超时则继续执行；服务器可能异步发送 LOGIN_ACK */
    }

    // 设置为非阻塞模式（消费 LOGIN_ACK 后）
    p2p_set_nonblock(ctx->fd);

    // 标记为已连接
    ctx->state = SIGNAL_CONNECTED;

    P2P_LOG_INFO("RELAY", "%s %s:%d %s '%s'", LA_W("Connected to server", LA_W23, 24), server_ip, port, LA_W("as", LA_W8, 9), my_name);
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
    if (ctx->fd != P2P_INVALID_SOCKET) {
        p2p_close_socket(ctx->fd);
        ctx->fd = P2P_INVALID_SOCKET;
    }
    
    /* 释放读取缓冲区 */
    if (ctx->read_payload) {
        free(ctx->read_payload);
        ctx->read_payload = NULL;
    }
    
    ctx->state = SIGNAL_DISCONNECTED;
    ctx->read_state = RELAY_READ_IDLE;
    ctx->read_offset = 0;
    ctx->read_expected = 0;
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
    if (ctx->fd == P2P_INVALID_SOCKET) return -1;

    // 构造连接请求数据包
    // + 该数据包发给信令服务器，并由信令服务器中继转发给目标方。
    //   这也意味着负载的数据结构由对等双方约定，和服务器无关
    p2p_relay_hdr_t hdr = {P2P_RLY_MAGIC, P2P_RLY_CONNECT, (uint32_t)(P2P_PEER_ID_MAX + len)};
    char target[P2P_PEER_ID_MAX] = {0};
    strncpy(target, target_name, P2P_PEER_ID_MAX);

    if (send(ctx->fd, (const char *)&hdr, sizeof(hdr), 0) != sizeof(hdr)) {
        P2P_LOG_ERROR("RELAY", "%s", LA_S("Failed to send header", LA_S25, 177));
        return -1;
    }
    if (send(ctx->fd, target, P2P_PEER_ID_MAX, 0) != P2P_PEER_ID_MAX) {
        P2P_LOG_ERROR("RELAY", "%s", LA_S("Failed to send target name", LA_S27, 179));
        return -1;
    }
    if (send(ctx->fd, (const char *)data, len, 0) != len) {
        P2P_LOG_ERROR("RELAY", "%s", LA_S("Failed to send payload", LA_S26, 178));
        return -1;
    }
    
    P2P_LOG_INFO("RELAY", "%s %s '%s' (%d %s)", LA_W("Sent connect", LA_W116, 117), LA_S("request to", LA_S47, 200), target_name, len, LA_W("bytes", LA_W17, 18));
    
    /* 发送成功，ACK 将在状态机中异步接收 */
    return 0;
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
    if (ctx->fd == P2P_INVALID_SOCKET) return -1;

    // 构造 answer 数据包 (P2P_RLY_ANSWER)
    // 服务器会将其转换为 P2P_RLY_FORWARD 并转发给目标方
    p2p_relay_hdr_t hdr = {P2P_RLY_MAGIC, P2P_RLY_ANSWER, (uint32_t)(P2P_PEER_ID_MAX + len)};
    char target[P2P_PEER_ID_MAX] = {0};
    strncpy(target, target_name, P2P_PEER_ID_MAX);

    send(ctx->fd, (const char *)&hdr, sizeof(hdr), 0);
    send(ctx->fd, target, P2P_PEER_ID_MAX, 0);
    send(ctx->fd, (const char *)data, len, 0);
    
    P2P_LOG_INFO("RELAY", "%s '%s' (%d %s)", LA_W("Sent answer to", LA_W113, 114), target_name, len, LA_W("bytes", LA_W17, 18));
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

/* ============================================================================
 * 信令客户端状态机周期维护（异步 I/O 实现）
 * ============================================================================
 *
 * 在主循环中调用，处理从信令服务器接收的消息。
 * 使用状态机 + 单次 recv() 实现真正的异步读取，避免循环阻塞。
 *
 * 设计原则：
 *   1. 每次 tick 只调用一次 recv()，不循环死读
 *   2. 使用 select 检查数据可读性（非阻塞，0 超时）
 *   3. 维护读取状态机，分段读取消息（header → sender → payload）
 *   4. 读取完成后才执行业务逻辑
 *
 * 状态机转换：
 *   IDLE → HEADER（开始读取 9 字节消息头）
 *   HEADER → SENDER（OFFER/FORWARD 消息需读取 32 字节发送者名称）
 *   HEADER → PAYLOAD（其他消息直接读取 payload）
 *   HEADER → IDLE（无 payload 的消息）
 *   SENDER → PAYLOAD（读完 sender_name 后读取 payload）
 *   PAYLOAD → IDLE（读完 payload，处理消息）
 *   DISCARD → IDLE（丢弃未处理的消息）
 *
 * 处理的消息类型：
 *   - P2P_RLY_OFFER:   来自主动方的连接请求（服务器转发）
 *   - P2P_RLY_FORWARD: 来自被动方的应答（服务器转发）
 *   - 其他类型：读取并丢弃（避免数据流错位）
 *
 * @param ctx  信令上下文
 * @param s    P2P 会话
 */
void p2p_signal_relay_tick(p2p_signal_relay_ctx_t *ctx, struct p2p_session *s) {
    if (ctx->fd == P2P_INVALID_SOCKET) return;

    /* P2P 连接已建立（直连或 TURN 中继），信令服务器使命完成，关闭 TCP 连接释放服务器资源 */
    if (s->state == P2P_STATE_CONNECTED || s->state == P2P_STATE_RELAY) {
        P2P_LOG_INFO("RELAY", "P2P connected, closing signaling TCP connection");
        p2p_signal_relay_close(ctx);
        return;
    }

    /* 发送心跳，刷新服务器的 last_active，防止超时踢下线（仅连接建立前） */
    {
        uint64_t now_ms = p2p_time_ms();
        if (ctx->last_heartbeat_ms == 0 ||
            now_ms - ctx->last_heartbeat_ms >= P2P_RELAY_HEARTBEAT_INTERVAL_MS) {
            p2p_relay_hdr_t hb;
            hb.magic  = P2P_RLY_MAGIC;
            hb.type   = P2P_RLY_HEARTBEAT;
            hb.length = 0;
            send(ctx->fd, (const char *)&hb, sizeof(hb), 0);
            ctx->last_heartbeat_ms = now_ms;
        }
    }

    /* 检查等待超时 */
    if (ctx->waiting_for_peer) {
        uint64_t elapsed = p2p_time_ms() - ctx->waiting_start_time;
        if (elapsed > P2P_RELAY_PEER_WAIT_TIMEOUT_MS) {
            P2P_LOG_WARN("RELAY", "%s '%s' %s (%dms), %s", 
                   LA_W("Waiting for peer", LA_W142, 143), ctx->waiting_target, LA_W("timed out", LA_W129, 130),
                   P2P_RELAY_PEER_WAIT_TIMEOUT_MS, LA_W("giving up", LA_W44, 45));
            ctx->waiting_for_peer = false;
            ctx->waiting_target[0] = '\0';
            return;
        }
    }

    /* 状态机：循环读取，直到没有数据（EAGAIN）或完成消息处理 
     * 关键：每次 recv() 如果返回 EAGAIN，说明 TCP 缓冲区空了，立即返回
     * 这样可以一次 tick 处理完缓冲区的所有数据，又不会阻塞 */
    for(;;) {
        switch (ctx->read_state) {
        
        case RELAY_READ_IDLE: {
            /* 空闲状态：开始读取新消息的头部 */
            ctx->read_offset = 0;
            ctx->read_expected = sizeof(p2p_relay_hdr_t);
            ctx->read_state = RELAY_READ_HEADER;
            /* fall through - 不 break，继续读取 */
        }
        
        case RELAY_READ_HEADER: {
            /* 读取消息头（9 字节）*/
            int remaining = ctx->read_expected - ctx->read_offset;
            char *buf = ((char*)&ctx->read_hdr) + ctx->read_offset;
            
            int n = recv(ctx->fd, buf, remaining, 0);
            
            if (n == 0) {
                /* 连接关闭 */
                P2P_LOG_WARN("RELAY", "%s", LA_S("Connection closed by server", LA_S13, 164));
                p2p_signal_relay_close(ctx);
                return;
            }
            
            if (n < 0) {
                int err = p2p_errno();
                if (err == P2P_EAGAIN || err == P2P_EINPROGRESS) {
                    /* 缓冲区空了，等待下次 tick */
                    return;
                }
                /* 真正的错误 */
                P2P_LOG_ERROR("RELAY", "%s %d", LA_W("recv error", LA_W96, 97), err);
                p2p_signal_relay_close(ctx);
                return;
            }
            
            ctx->read_offset += n;
            
            /* 检查是否读完 header */
            if (ctx->read_offset >= ctx->read_expected) {
                /* 验证 magic */
                if (ctx->read_hdr.magic != P2P_RLY_MAGIC) {
                    P2P_LOG_WARN("RELAY", "%s 0x%x (%s 0x%x), %s",
                           LA_W("Invalid magic", LA_W46, 47), ctx->read_hdr.magic,
                           LA_W("expected", LA_W30, 31), P2P_RLY_MAGIC, LA_W("resetting", LA_W103, 104));
                    ctx->read_state = RELAY_READ_IDLE;
                    return;
                }
                
                P2P_LOG_DEBUG("RELAY", "[DEBUG] relay_tick: recv header complete, magic=0x%x, type=%d, length=%u",
                       ctx->read_hdr.magic, ctx->read_hdr.type, ctx->read_hdr.length);
                
                /* 根据消息类型决定下一步 */
                if (ctx->read_hdr.type == P2P_RLY_OFFER || ctx->read_hdr.type == P2P_RLY_FORWARD) {
                    /* 需要读取 sender_name（32 字节） */
                    ctx->read_offset = 0;
                    ctx->read_expected = P2P_PEER_ID_MAX;
                    ctx->read_state = RELAY_READ_SENDER;
                } else if (ctx->read_hdr.type == P2P_RLY_CONNECT_ACK) {
                    /* 处理 CONNECT_ACK（8 字节 payload） */
                    if (ctx->read_hdr.length > 0) {
                        ctx->read_payload = (uint8_t*)malloc(ctx->read_hdr.length);
                        if (!ctx->read_payload) {
                            P2P_LOG_ERROR("RELAY", "%s", LA_S("Failed to allocate ACK payload buffer", LA_S21, 173));
                            ctx->read_state = RELAY_READ_IDLE;
                            return;
                        }
                        ctx->read_offset = 0;
                        ctx->read_expected = ctx->read_hdr.length;
                        ctx->read_state = RELAY_READ_PAYLOAD;
                    } else {
                        ctx->read_state = RELAY_READ_IDLE;
                    }
                } else if (ctx->read_hdr.length > 0) {
                    /* 其他消息类型，读取并丢弃 payload */
                    ctx->read_offset = 0;
                    ctx->read_expected = ctx->read_hdr.length;
                    ctx->read_state = RELAY_READ_DISCARD;
                } else {
                    /* 无 payload 的消息，直接完成 */
                    ctx->read_state = RELAY_READ_IDLE;
                    /* 继续循环，处理下一个消息（如果有） */
                }
            }
            /* 继续循环 */
            break;
        }
        
        case RELAY_READ_SENDER: {
            /* 读取 sender_name（32 字节） */
            int remaining = ctx->read_expected - ctx->read_offset;
            char *buf = ctx->read_sender + ctx->read_offset;
            
            int n = recv(ctx->fd, buf, remaining, 0);
            
            if (n == 0) {
                P2P_LOG_WARN("RELAY", "%s", LA_S("Connection closed while reading sender", LA_S16, 167));
                p2p_signal_relay_close(ctx);
                return;
            }
            
            if (n < 0) {
                int err = p2p_errno();
                if (err == P2P_EAGAIN || err == P2P_EINPROGRESS) {
                    /* 缓冲区空了，等待下次 tick */
                    return;
                }
                P2P_LOG_ERROR("RELAY", "%s %d %s", LA_W("recv error", LA_W96, 97), err, LA_W("while reading sender", LA_W145, 146));
                p2p_signal_relay_close(ctx);
                return;
            }
            
            ctx->read_offset += n;
            
            /* 检查是否读完 sender_name */
            if (ctx->read_offset >= ctx->read_expected) {
                /* 计算 payload 长度（总长度 - sender_name） */
                uint32_t payload_len = ctx->read_hdr.length - P2P_PEER_ID_MAX;
                
                if (payload_len > 0) {
                    /* 分配 payload 缓冲区 */
                    ctx->read_payload = (uint8_t*)malloc(payload_len);
                    if (!ctx->read_payload) {
                        P2P_LOG_ERROR("RELAY", "%s %u %s", LA_W("Failed to allocate", LA_W31, 32), payload_len, LA_W("bytes", LA_W17, 18));
                        ctx->read_state = RELAY_READ_IDLE;
                        return;
                    }
                    
                    ctx->read_offset = 0;
                    ctx->read_expected = payload_len;
                    ctx->read_state = RELAY_READ_PAYLOAD;
                } else {
                    /* 无 payload（不应该，但防御性处理） */
                    ctx->read_state = RELAY_READ_IDLE;
                }
            }
            /* 继续循环 */
            break;
        }
        
        case RELAY_READ_PAYLOAD: {
            /* 读取 payload（变长） */
            int remaining = ctx->read_expected - ctx->read_offset;
            char *buf = (char*)ctx->read_payload + ctx->read_offset;
            
            int n = recv(ctx->fd, buf, remaining, 0);
            
            if (n == 0) {
                P2P_LOG_WARN("RELAY", "%s", LA_S("Connection closed while reading payload", LA_S15, 166));
                p2p_signal_relay_close(ctx);
                return;
            }
            
            if (n < 0) {
                int err = p2p_errno();
                if (err == P2P_EAGAIN || err == P2P_EINPROGRESS) {
                    /* 缓冲区空了，等待下次 tick */
                    return;
                }
                P2P_LOG_ERROR("RELAY", "%s %d %s", LA_W("recv error", LA_W96, 97), err, LA_W("while reading payload", LA_W144, 145));
                p2p_signal_relay_close(ctx);
                return;
            }
            
            ctx->read_offset += n;
            
            /* 检查是否读完 payload */
            if (ctx->read_offset >= ctx->read_expected) {
                /* 读取完成，处理消息 */
                uint32_t payload_len = ctx->read_expected;
                
                /* 处理 CONNECT_ACK */
                if (ctx->read_hdr.type == P2P_RLY_CONNECT_ACK) {
                    if (payload_len >= sizeof(p2p_relay_connect_ack_t)) {
                        p2p_relay_connect_ack_t *ack = (p2p_relay_connect_ack_t *)ctx->read_payload;
                        P2P_LOG_INFO("RELAY", "%s (status=%d, candidates_acked=%d)",
                               LA_W("Received ACK", LA_W89, 90), ack->status, ack->candidates_acked);
                        
                        /* 更新候选索引（避免重复发送） */
                        ctx->next_candidate_index += ack->candidates_acked;
                        
                        /* 根据状态处理 */
                        switch (ack->status) {
                            case 0:  // 对端在线
                                P2P_LOG_INFO("RELAY", "%s, %s %d %s",
                                       LA_W("Peer online", LA_W76, 77), LA_S("forwarded", LA_S28, 180), ack->candidates_acked, LA_W("candidates", LA_W20, 21));
                                ctx->waiting_for_peer = false;
                                break;
                            case 1:  // 对端离线，已缓存
                                P2P_LOG_INFO("RELAY", "%s, %s %d %s",
                                       LA_W("Peer offline", LA_W74, 75), LA_S("cached", LA_S10, 161), ack->candidates_acked, LA_W("candidates", LA_W20, 21));
                                ctx->waiting_for_peer = false;
                                break;
                            case 2:  // 缓存已满
                                P2P_LOG_INFO("RELAY", "%s, %s",
                                       LA_W("Storage full", LA_W122, 123), LA_S("waiting for peer to come online", LA_S62, 215));
                                ctx->waiting_for_peer = true;
                                ctx->waiting_start_time = p2p_time_ms();
                                break;
                            default:
                                P2P_LOG_WARN("RELAY", "%s %d", LA_W("Unknown ACK status", LA_W138, 139), ack->status);
                                break;
                        }
                    }
                    
                    /* 释放 payload 并重置 */
                    free(ctx->read_payload);
                    ctx->read_payload = NULL;
                    ctx->read_state = RELAY_READ_IDLE;
                    break;
                }
                
                /* 保存发送者名称（用于后续发送 answer） */
                if (ctx->read_hdr.type == P2P_RLY_OFFER) {
                    strncpy(ctx->incoming_peer_name, ctx->read_sender, P2P_PEER_ID_MAX);
                    
                    /* 被动方（无 --to）从 OFFER 中学习 remote_peer_id */
                    if (s->remote_peer_id[0] == '\0') {
                        strncpy(s->remote_peer_id, ctx->read_sender, P2P_PEER_ID_MAX - 1);
                        s->remote_peer_id[P2P_PEER_ID_MAX - 1] = '\0';
                        
                        /* 重置 Trickle ICE 计数器 */
                        ctx->total_candidates_sent  = 0;
                        ctx->total_candidates_acked = 0;
                        ctx->next_candidate_index   = 0;
                        ctx->waiting_for_peer       = false;
                        s->signal_sent              = false;
                        s->last_cand_cnt_sent       = 0;
                        
                        P2P_LOG_INFO("RELAY", "%s '%s' %s OFFER",
                               LA_W("Passive peer learned remote ID", LA_W69, 70), ctx->read_sender, LA_W("from", LA_W39, 40));
                    }
                }
                
                /* 收到 FORWARD：对端已上线，清除等待状态 */
                if (ctx->read_hdr.type == P2P_RLY_FORWARD && ctx->waiting_for_peer &&
                    strcmp(ctx->waiting_target, ctx->read_sender) == 0) {
                    P2P_LOG_INFO("RELAY", "%s '%s' %s (%s FORWARD), %s",
                           LA_W("Peer", LA_W71, 72), ctx->read_sender, LA_W("is now online", LA_W49, 50),
                           LA_S("received", LA_S43, 196), LA_S("resuming", LA_S48, 201));
                    ctx->waiting_for_peer = false;
                    ctx->waiting_target[0] = '\0';
                }
                
                P2P_LOG_INFO("RELAY", "%s '%s' (%u %s)", LA_W("Received signal from", LA_W93, 94), ctx->read_sender, payload_len, LA_W("bytes", LA_W17, 18));
                
                /* OFFER 表示新连接，FORWARD 如果 ICE 已 FAILED 则重置让其恢复 */
                if (ctx->read_hdr.type == P2P_RLY_OFFER || ctx->read_hdr.type == P2P_RLY_FORWARD) {
                    /* OFFER：总是重置（新连接）
                     * FORWARD：仅当 ICE 已 FAILED 时重置（避免在 CHECKING 时反复重置导致永远无法完成） */
                    bool should_reset = (ctx->read_hdr.type == P2P_RLY_OFFER) || 
                                       (s->ice_state == P2P_ICE_STATE_FAILED);
                    
                    if (should_reset && (s->remote_cand_cnt > 0 || s->ice_state != P2P_ICE_STATE_INIT)) {
                        P2P_LOG_DEBUG("RELAY", "[DEBUG] %s received (ice_state=%d), resetting ICE and clearing %d stale candidates",
                               ctx->read_hdr.type == P2P_RLY_OFFER ? "OFFER" : "FORWARD", s->ice_state, s->remote_cand_cnt);
                        s->remote_cand_cnt = 0;
                        s->ice_state = P2P_ICE_STATE_GATHERING_DONE;
                        s->ice_check_count = 0;
                        s->ice_check_last_ms = 0;
                        
                        /* 重置候选发送索引（对端已清空，需从头重发） */
                        ctx->next_candidate_index = 0;
                    }
                }
                
                /* 解析信令数据并注入 ICE 状态机 */
                p2p_signaling_payload_hdr_t p;
                if (payload_len >= 76 && unpack_signaling_payload_hdr(&p, ctx->read_payload) == 0 &&
                    payload_len >= (size_t)(76 + p.candidate_count * 32)) {
                    
                    /* 添加远端 ICE 候选（步长 = sizeof(p2p_candidate_t) = 32）*/
                    for (int i = 0; i < p.candidate_count; i++) {
                        p2p_candidate_entry_t c;
                        unpack_candidate(&c, ctx->read_payload + sizeof(p2p_signaling_payload_hdr_t) + i * sizeof(p2p_candidate_t));
                        
                        /* 排重检查 */
                        int exists = 0;
                        for (int j = 0; j < s->remote_cand_cnt; j++) {
                            if (s->remote_cands[j].cand.addr.sin_addr.s_addr == c.addr.sin_addr.s_addr &&
                                s->remote_cands[j].cand.addr.sin_port == c.addr.sin_port) {
                                exists = 1;
                                break;
                            }
                        }
                        
                        if (!exists) {
                            p2p_remote_candidate_entry_t *rc = p2p_cand_push_remote(s);
                            if (rc) {

                                rc->cand = c;  /* entry ← base entry */
                                rc->last_punch_send_ms = 0;
                                P2P_LOG_INFO("ICE", "%s: %d -> %s:%d",
                                       LA_W("Added Remote Candidate", LA_W4, 5), c.type, inet_ntoa(c.addr.sin_addr), ntohs(c.addr.sin_port));
                                
                                /* Trickle ICE：如果 ICE 已在 CHECKING 状态，立即向新候选发送探测包 */
                                if (s->ice_state == P2P_ICE_STATE_CHECKING) {

                                    P2P_LOG_INFO("ICE", "[Trickle] Immediately probing new candidate %s:%d",
                                                 inet_ntoa(rc->cand.addr.sin_addr), ntohs(rc->cand.addr.sin_port));

                                    nat_punch(s, &rc->cand.addr);
                                }
                            }
                        }
                    }
                } else {
                    /* 解包失败，尝试作为旧版 trickle 候选处理 */
                    p2p_ice_on_remote_candidates(s, ctx->read_payload, payload_len);
                }
                
                /* 释放 payload 缓冲区 */
                free(ctx->read_payload);
                ctx->read_payload = NULL;
                
                /* 重置状态机，继续处理下一个消息（如果有） */
                ctx->read_state = RELAY_READ_IDLE;
            }
            /* 继续循环 */
            break;
        }
        
        case RELAY_READ_DISCARD: {
            /* 丢弃未处理的消息类型 */
            if (!ctx->read_payload && ctx->read_expected > 0) {
                ctx->read_payload = (uint8_t*)malloc(ctx->read_expected);
                if (!ctx->read_payload) {
                    P2P_LOG_ERROR("RELAY", "%s", LA_S("Failed to allocate discard buffer, closing connection", LA_S22, 174));
                    p2p_signal_relay_close(ctx);
                    return;
                }
            }
            
            int remaining = ctx->read_expected - ctx->read_offset;
            char *buf = (char*)ctx->read_payload + ctx->read_offset;
            
            int n = recv(ctx->fd, buf, remaining, 0);
            
            if (n == 0) {
                P2P_LOG_WARN("RELAY", "%s", LA_S("Connection closed while discarding", LA_S14, 165));
                p2p_signal_relay_close(ctx);
                return;
            }
            
            if (n < 0) {
                int err = p2p_errno();
                if (err == P2P_EAGAIN || err == P2P_EINPROGRESS) {
                    /* 缓冲区空了，等待下次 tick */
                    return;
                }
                P2P_LOG_ERROR("RELAY", "%s %d %s", LA_W("recv error", LA_W96, 97), err, LA_W("while discarding", LA_W143, 144));
                p2p_signal_relay_close(ctx);
                return;
            }
            
            ctx->read_offset += n;
            
            if (ctx->read_offset >= ctx->read_expected) {
                P2P_LOG_DEBUG("RELAY", "[DEBUG] %s %d %s %s %d",
                       LA_W("Discarded", LA_W27, 28), ctx->read_expected, LA_W("bytes", LA_W17, 18),
                       LA_S("payload of message type", LA_S40, 193), ctx->read_hdr.type);
                
                /* 释放缓冲区 */
                if (ctx->read_payload) {
                    free(ctx->read_payload);
                    ctx->read_payload = NULL;
                }
                
                /* 重置状态机，继续处理下一个消息 */
                ctx->read_state = RELAY_READ_IDLE;
            }
            /* 继续循环 */
            break;
        }
        
        default:
            /* 不应该到达这里 */
            P2P_LOG_WARN("RELAY", "%s %d, %s", LA_W("Invalid read state", LA_W47, 48), ctx->read_state, LA_W("resetting", LA_W103, 104));
            ctx->read_state = RELAY_READ_IDLE;
            break;
        }
    } // for(;;)
}

