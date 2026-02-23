/*
 * COMPACT 模式信令实现（UDP 无状态）
 *
 * 从 p2p_nat.c 中提取的信令相关代码。
 * 负责 REGISTER/REGISTER_ACK/PEER_INFO/PEER_INFO_ACK 协议。
 *
 * 状态机：
 *   IDLE → REGISTERING → REGISTERED → READY
 *                 ↓                       
 *                 └──────────────────────┘ (收到 PEER_INFO seq=0)
 *
 * 候选列表统一存储在 p2p_session 中，本模块只负责序列化和发送。
 *
 * 序列化同步机制：
 *   - REGISTER 仅在 REGISTERING 阶段发送，收到 ACK 后停止
 *   - PEER_INFO(seq=0) 由服务器发送，包含缓存的对端候选
 *   - PEER_INFO(seq>0) 由客户端发送，继续同步剩余候选
 *   - 每个 PEER_INFO 需要 PEER_INFO_ACK 确认，未确认则重发
 */
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#include "p2p_internal.h"
#ifndef _WIN32
#include <arpa/inet.h>
#endif

#define REGISTER_INTERVAL_MS            1000    /* 注册重发间隔 */
#define PEER_INFO_INTERVAL_MS           500     /* PEER_INFO 重发间隔 */
#define MAX_REGISTER_ATTEMPTS           10      /* 最大 REGISTER 重发次数 */
#define REGISTER_KEEPALIVE_INTERVAL_MS  20000   /* REGISTERED 状态保活重注册间隔（防服务器超时清除槽位） */
#define MAX_CANDS_PER_PACKET            10      /* 每个 PEER_INFO 包最大候选数 */
#define NAT_PROBE_MAX_RETRIES           3       /* NAT_PROBE 最大发送次数 */
#define NAT_PROBE_INTERVAL_MS           1000    /* NAT_PROBE 重发间隔 */

/*
 * 构建 REGISTER 负载
 *
 * 格式: [local_peer_id(32)][remote_peer_id(32)][candidate_count(1)][candidates(N*7)]
 *
 * 从 session 的 local_cands[] 中读取候选列表
 */
static void send_register(p2p_session_t *s) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    assert(ctx->state == SIGNAL_COMPACT_REGISTERING);

    uint8_t payload[P2P_MAX_PAYLOAD];

    int n = P2P_MAX_PAYLOAD - (P2P_PEER_ID_MAX * 2 + 1);
    int cand_cnt = n / (int)sizeof(p2p_compact_candidate_t)/* 7 */;
    if (cand_cnt > s->local_cand_cnt) {
        cand_cnt = s->local_cand_cnt;
    }
    ctx->candidates_cached = cand_cnt;

    memset(payload, 0, P2P_PEER_ID_MAX * 2);
    memcpy(payload, ctx->local_peer_id, strlen(ctx->local_peer_id));
    memcpy(payload + P2P_PEER_ID_MAX, ctx->remote_peer_id, strlen(ctx->remote_peer_id));
    n = P2P_PEER_ID_MAX * 2;
    
    /* candidate_count */
    payload[n++] = (uint8_t)cand_cnt;
    
    /* candidates (每个 7 字节: type + ip + port) */
    for (int i = 0; i < cand_cnt; i++) {
        payload[n] = (uint8_t)s->local_cands[i].type;
        memcpy(payload + n + 1, &s->local_cands[i].addr.sin_addr.s_addr, 4);
        memcpy(payload + n + 5, &s->local_cands[i].addr.sin_port, 2);
        n += 7;
    }

    udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_REGISTER, 0, 0, payload, n);


    if (ctx->verbose) {
        P2P_LOG_INFO("COMPACT", "REGISTERING: %s #%d (%d %s)...",
                     MSG(MSG_COMPACT_ATTEMPT), ctx->register_attempts, s->local_cand_cnt, MSG(MSG_ICE_CANDIDATE_PAIRS));
    }
}

/*
 * 解析 PEER_INFO 负载，追加到 session 的 remote_cands[]
 *
 * 格式: [session_id(8)][base_index(1)][candidate_count(1)][candidates(N*7)]
 *
 * 注意：这里对方的候选列表顺序并未按对方原始顺序排序，而是 FIFO 追加到 remote_cands[] 中
 */
static void parse_peer_info(p2p_session_t *s, const uint8_t *payload, int cand_cnt) {

    int offset = 10; // 负载头：[session_id(8)][base_index(1)][candidate_count(1)]
    for (int i = 0; i < cand_cnt; i++) {
        p2p_candidate_entry_t *c = &s->remote_cands[s->remote_cand_cnt++];

        c->type = (p2p_cand_type_t)payload[offset];
        c->priority = 0;                            // COMPACT 模式不使用优先级
        memset(&c->addr, 0, sizeof(c->addr));
        c->addr.sin_family = AF_INET;
        memcpy(&c->addr.sin_addr.s_addr, payload + offset + 1, 4);
        memcpy(&c->addr.sin_port, payload + offset + 5, 2);
        offset += (int)sizeof(p2p_compact_candidate_t);

        // Trickle ICE：如果 NAT 打洞已启动，立即向新候选发送探测包
        if (s->nat.state == NAT_PUNCHING || s->nat.state == NAT_RELAY) {

            udp_send_packet(s->sock, &c->addr, P2P_PKT_PUNCH, 0, 0, NULL, 0);

            P2P_LOG_DEBUG("COMPACT", "[Trickle] Immediately probing new candidate %s:%d",
                          inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
        }
    }
}

/* 一个 PEER_INFO 包所承载的候选数量（单位）
 * + 这里 10（字节）表示 PEER_INFO 负载头：[session_id(8)][base_index(1)][candidate_count(1)] = 10 字节
 *   负载头后面的剩余空间就是候选列表，通过预定义、和 MTU 上限共同限制计算得出该单位大小
 */
#define PEER_INFO_CAND_UNIT \
    (((P2P_MAX_PAYLOAD - 10) / (int)sizeof(p2p_compact_candidate_t)) < MAX_CANDS_PER_PACKET \
     ? ((P2P_MAX_PAYLOAD - 10) / (int)sizeof(p2p_compact_candidate_t)) \
     : MAX_CANDS_PER_PACKET)

// 构建 PEER_INFO 的候选队列，返回 payload 总长度
static int build_peer_info_candidates(p2p_session_t *s, uint16_t seq, uint8_t *payload, uint8_t *r_flags) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    const int cand_unit = PEER_INFO_CAND_UNIT;
    int start = ctx->candidates_cached + (int)(seq - 1) * cand_unit;
    if (start > s->local_cand_cnt) start = s->local_cand_cnt;

    int remaining = s->local_cand_cnt - start, cnt;
    if (remaining > cand_unit) cnt = cand_unit;
    else { cnt = remaining;
        *r_flags |= SIG_PEER_INFO_FIN;
    }

    payload[8] = (uint8_t)start;
    payload[9] = (uint8_t)cnt;

    int offset = 10; // 负载头：[session_id(8)][base_index(1)][candidate_count(1)]
    for (int i = 0; i < cnt; i++) {
        int idx = start + i;
        payload[offset] = (uint8_t)s->local_cands[idx].type;
        memcpy(payload + offset + 1, &s->local_cands[idx].addr.sin_addr.s_addr, 4);
        memcpy(payload + offset + 5, &s->local_cands[idx].addr.sin_port, 2);
        offset += (int)sizeof(p2p_compact_candidate_t);
    }

    return offset;
}

// 在首次收到 PEER_INFO 包，且已经收到 REGISTER_ACK 的情况下，发送剩余候选队列和 FIN 包给对方
// + 注意，首次收到的 PEER_INFO 包，可能是服务器下发的 seq=0 的 PEER_INFO 包;
//   也可能是对方发送的 seq!=1 的 PEER_INFO 包（在并发网络状况下，对方的 PEER_INFO 包可能先到达）
static void send_rest_candidates_and_fin(p2p_session_t *s) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    assert(ctx->state == SIGNAL_COMPACT_ICE);

    // 计算剩余候选数量
    int cand_remain = s->local_cand_cnt - ctx->candidates_cached;
    if (cand_remain < 0) cand_remain = 0;

    // 至少发送一个包（即使没有剩余候选），以确保对方收到 FIN 信号
    const int cand_unit = PEER_INFO_CAND_UNIT;
    int pkt_cnt = cand_remain == 0 ? 1 : (cand_remain + cand_unit - 1) / cand_unit;     // 单位 ceil 取整
    if (pkt_cnt >= 16) { pkt_cnt = 16;                                                  // 目前协议设计最多支持 16 个包（seq=1-16）
        ctx->candidates_mask = 0xFFFFu;
    } else ctx->candidates_mask = (uint16_t)((1u << pkt_cnt) - 1u);                     // 计算候选确认窗口的 mask
    
    // 初始重置确认状态
    ctx->candidates_acked = 0;

    // session_id 所有包相同，只写一次
    uint8_t payload[P2P_MAX_PAYLOAD];
    uint64_t sid_net = htonll(ctx->session_id);
    memcpy(payload, &sid_net, 8);

    for (uint16_t seq = 1; seq <= (uint16_t)pkt_cnt; seq++) { uint8_t flags = 0;
        int payload_len = build_peer_info_candidates(s, seq, payload, &flags);
        udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_PEER_INFO, flags, seq, payload, payload_len);
    }
}

// 周期将未确认的 PEER_INFO 包重发给对方
static void resend_rest_candidates_and_fin(p2p_session_t *s) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    assert(ctx->state == SIGNAL_COMPACT_ICE);
    assert((ctx->candidates_acked & ctx->candidates_mask) != ctx->candidates_mask);

    // session_id 所有包相同，只写一次
    uint8_t payload[P2P_MAX_PAYLOAD];
    uint64_t sid_net = htonll(ctx->session_id);
    memcpy(payload, &sid_net, 8);   

    for (uint16_t seq = 1; seq <= 16; seq++) {
        uint16_t bit = (uint16_t)(1u << (seq - 1));
        if ((ctx->candidates_mask & bit) == 0) break;                                   // 遇到第一个 0 就可以停止循环（mask 是低位连续段，高位全为 0）
        if ((ctx->candidates_acked & bit) != 0) continue;

        uint8_t flags = 0;
        int payload_len = build_peer_info_candidates(s, seq, payload, &flags);
        udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_PEER_INFO, flags, seq, payload, payload_len);
    }
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 初始化信令上下文
 */
void p2p_signal_compact_init(p2p_signal_compact_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = SIGNAL_COMPACT_INIT;
}

/*
 * 开始信令交换（发送 REGISTER）
 */
int p2p_signal_compact_connect(struct p2p_session *s, const char *local_peer_id,
                               const char *remote_peer_id,
                               const struct sockaddr_in *server, int verbose) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    if (ctx->state != SIGNAL_COMPACT_INIT) return -1;

    ctx->server_addr = *server;
    ctx->verbose = verbose;

    ctx->state = SIGNAL_COMPACT_REGISTERING;
    ctx->last_send_time = p2p_time_ms();
    ctx->register_attempts = 0;

    memset(ctx->local_peer_id, 0, sizeof(ctx->local_peer_id));
    memset(ctx->remote_peer_id, 0, sizeof(ctx->remote_peer_id));
    strncpy(ctx->local_peer_id, local_peer_id, P2P_PEER_ID_MAX - 1);
    strncpy(ctx->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX - 1);
    ctx->local_peer_id[P2P_PEER_ID_MAX - 1] = '\0';
    ctx->remote_peer_id[P2P_PEER_ID_MAX - 1] = '\0';
    ctx->peer_online = false;

    if (ctx->verbose) {
        P2P_LOG_INFO("COMPACT", "START: %s '%s' -> '%s' %s %s:%d (%d %s)",
               MSG(MSG_COMPACT_REGISTERING), local_peer_id, remote_peer_id,
               MSG(MSG_COMPACT_WITH_SERVER), inet_ntoa(server->sin_addr), ntohs(server->sin_port),
               s->local_cand_cnt, MSG(MSG_ICE_CANDIDATE_PAIRS));
    }

    // 构造并发送带候选列表的注册包
    send_register(s);

    return 0;
}

int p2p_signal_compact_disconnect(struct p2p_session *s) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    if (ctx->state == SIGNAL_COMPACT_INIT) return 1;

    uint8_t payload[P2P_PEER_ID_MAX * 2];
    memset(payload, 0, sizeof(payload));
    memcpy(payload, ctx->local_peer_id, strnlen(ctx->local_peer_id, P2P_PEER_ID_MAX));
    memcpy(payload + P2P_PEER_ID_MAX, ctx->remote_peer_id, strnlen(ctx->remote_peer_id, P2P_PEER_ID_MAX));

    udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_UNREGISTER, 0, 0, payload, (int)sizeof(payload));

    ctx->state = SIGNAL_COMPACT_INIT;
    return 0;
}

int p2p_signal_compact_relay_send(struct p2p_session *s, void* data, uint32_t size) {
    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    if (!ctx->relay_support || !ctx->session_id || !data || size == 0) return -1;

    if (size > (uint32_t)(P2P_MAX_PAYLOAD - 8 - 2)) {
        size = (uint32_t)(P2P_MAX_PAYLOAD - 8 - 2);
    }

    uint8_t payload[P2P_MAX_PAYLOAD];
    uint64_t sid_net = htonll(ctx->session_id);
    memcpy(payload, &sid_net, 8);
    uint16_t nsize = htons((uint16_t)size);
    memcpy(payload + 8, &nsize, 2);
    memcpy(payload + 10, data, size);

    udp_send_packet(s->sock, &ctx->server_addr, P2P_PKT_RELAY_DATA, 0, 0, payload, (int)(10 + size));
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 处理收到的信令包
 *
 * 支持的包类型：
 * - REGISTER_ACK: 服务器确认，提取对端状态
 * - PEER_INFO: 对端候选列表（序列化）
 * - PEER_INFO_ACK: 对端确认
 */
int p2p_signal_compact_on_packet(struct p2p_session *s, uint8_t type, uint16_t seq, uint8_t flags,
                                 const uint8_t *payload, int len,
                                 const struct sockaddr_in *from) { (void)from;

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    
    switch (type) {

    /* 解析 REGISTER_ACK: [status(1)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)] */
    case SIG_PKT_REGISTER_ACK: {

        if (len < 10) {
            P2P_LOG_ERROR("COMPACT", "REGISTER_ACK payload too short: %d", len);
            return -1;
        }

        if (ctx->state != SIGNAL_COMPACT_REGISTERING) {
            if (ctx->verbose) {
                P2P_LOG_WARN("COMPACT", "Ignore REGISTER_ACK in state=%d", (int)ctx->state);
            }
            return -1;
        }

        uint8_t status = payload[0];
        if (status >= 2) {
            if (ctx->verbose) {
                P2P_LOG_WARN("COMPACT", "REGISTER_ACK error: %s (status=%d)", MSG(MSG_COMPACT_SERVER_ERROR), status);
            }
            return -1;
        }

        ctx->relay_support = (flags & SIG_REGACK_FLAG_RELAY) != 0;      // 服务器是否支持数据中继转发
        if (ctx->candidates_cached > payload[1/*max_candidates*/])      // 计算服务器实际缓存的候选数量，作为后续发送 PEER_INFO 包的基准
            ctx->candidates_cached = payload[1/*max_candidates*/];

        // 解析自己的公网地址（服务器主端口探测到的 UDP 源地址）
        memset(&ctx->public_addr, 0, sizeof(ctx->public_addr));
        ctx->public_addr.sin_family = AF_INET;
        memcpy(&ctx->public_addr.sin_addr.s_addr, payload + 2, 4);
        memcpy(&ctx->public_addr.sin_port, payload + 6, 2);

        // 解析服务器提供的 NAT 探测端口，0 表示服务器不支持
        memcpy(&ctx->probe_port, payload + 8, 2);
        ctx->probe_port = ntohs(ctx->probe_port);

        if (ctx->verbose) {
            P2P_LOG_INFO("COMPACT", "REGISTER_ACK: peer_online=%d, max_cands=%d (%s=%s), %s=%s, public_addr=%s:%d, probe_port=%d",
                   ctx->peer_online, payload[1], MSG(MSG_COMPACT_CACHE), payload[1] > 0 ? MSG(MSG_ICE_YES) : MSG(MSG_ICE_NO_CACHED),
                   MSG(MSG_COMPACT_RELAY), ctx->relay_support ? MSG(MSG_ICE_YES) : MSG(MSG_ICE_NO_CACHED),
                   inet_ntoa(ctx->public_addr.sin_addr), ntohs(ctx->public_addr.sin_port),
                   ctx->probe_port);
        }

        // 标记进入 REGISTERED 状态（该状态将停止周期发送 REGISTER）
        ctx->state = SIGNAL_COMPACT_REGISTERED;

        P2P_LOG_INFO("COMPACT", "%s: %s", MSG(MSG_COMPACT_ENTERED_REGISTERED),
                     ctx->peer_online ? MSG(MSG_COMPACT_PEER_ONLINE) : MSG(MSG_COMPACT_PEER_OFFLINE));

        // 如果对方在线
        // + 注意，此时对方可能已经是在线状态，也就是 SIG_PKT_PEER_INFO 可能先于 SIG_PKT_REGISTER_ACK 到达
        if (status == SIG_REGACK_PEER_ONLINE)
            ctx->peer_online = true;

        // 如果已获得和对方建立的 session id（也就是 SIG_PKT_PEER_INFO 先到达）
        // + 进入 ICE 阶段，开始向对端发送后续候选队列和 FIN 包
        // + ICE 阶段同时依赖 SIG_PKT_REGISTER_ACK 和 SIG_PKT_PEER_INFO 包：
        //   SIG_PKT_REGISTER_ACK 提供后续候选队列基准; SIG_PKT_PEER_INFO 提供 session_id 作为双方连接的唯一标识
        if (ctx->session_id) {

            ctx->state = SIGNAL_COMPACT_ICE;
            send_rest_candidates_and_fin(s);
            ctx->last_send_time = p2p_time_ms();
        }

        // 如果服务器支持 NAT 探测端口，则启动 NAT_PROBE 探测流程
        if (ctx->probe_port > 0) {

            if (ctx->nat_probe_retries) {
                P2P_LOG_WARN("COMPACT", "NAT_PROBE already started (retries=%d)", (int)ctx->nat_probe_retries);
            }

            // 对于 lan_punch 模式：本地直接打洞，无需探测，NAT 类型直接标记为 OPEN
            if (s->cfg.lan_punch) {

                s->nat_type = P2P_NAT_OPEN;
                ctx->nat_probe_retries = -1/* 探测完成 */;
                if (ctx->verbose) {
                    P2P_LOG_INFO("COMPACT", "[lan_punch] 跳过 NAT_PROBE，直接标记 NAT=OPEN");
                }
            }
            else {

                // 标记进入 NAT_PROBE 探测中状态，发送第一轮探测包
                s->nat_type = P2P_NAT_DETECTING;
                ctx->nat_probe_retries = 1;
                ctx->nat_probe_send_time = p2p_time_ms();

                // 构造并发送 NAT_PROBE 包（协议：空包，服务器通过观察源地址和 probe_port 来探测 NAT 映射）
                struct sockaddr_in probe_addr = ctx->server_addr;
                probe_addr.sin_port = htons(ctx->probe_port);
                udp_send_packet(s->sock, &probe_addr, SIG_PKT_NAT_PROBE, 0, ctx->nat_probe_retries, NULL, 0);

                if (ctx->verbose) {
                    P2P_LOG_INFO("COMPACT", "NAT_PROBE: %s %s:%d (1/%d)",
                                 MSG(MSG_COMPACT_NAT_PROBE_SENT),
                                 inet_ntoa(probe_addr.sin_addr), ctx->probe_port,
                                 NAT_PROBE_MAX_RETRIES);
                }
            }
        }

    } break;

    /* 服务器通知：收到 alive 包, 协议：空包 */
    case SIG_PKT_ALIVE_ACK: {

        if (ctx->state <= SIGNAL_COMPACT_REGISTERING) {
            P2P_LOG_WARN("COMPACT", "Ignore ALIVE_ACK in state=%d", (int)ctx->state);
            return -1;
        }

        // 确认服务器未掉线
        ctx->last_recv_time = p2p_time_ms();

    } break;

    /* 解析 [session_id(8)][base_index(1)][candidate_count(1)][candidates(N*7)] */
    case SIG_PKT_PEER_INFO: {

        if (ctx->state < SIGNAL_COMPACT_REGISTERING || len < 10) {
            P2P_LOG_WARN("COMPACT", "Invalid PEER_INFO: state=%d len=%d", (int)ctx->state, len);
            return -1;
        }

        int cand_cnt = payload[9];
        if (len < 10 + (int)sizeof(p2p_compact_candidate_t) * cand_cnt) {
            P2P_LOG_WARN("COMPACT", "Invalid PEER_INFO payload: len=%d cand_cnt=%d", len, cand_cnt);
            return -1;
        }

        // 服务器发送的第一个 PEER_INFO，至少有一个对方公网的候选地址，且肯定不带 FIN 标识
        if (seq == 0 && (!cand_cnt || (flags & SIG_PEER_INFO_FIN))) {
            P2P_LOG_WARN("COMPACT", "Invalid PEER_INFO seq=0: cand_cnt=%d flags=0x%02x", cand_cnt, flags);
            return -1;
        }

        // 初始化获取、或验证 session_id，作为双方连接的唯一标识（后续双方基于连接的通讯以此作为标识）
        uint64_t sid;
        memcpy(&sid, payload, 8);
        if (!ctx->session_id) {

            ctx->session_id = ntohll(sid);

            s->remote_cand_cnt = 0;     // 初始化清空对端候选列表

            // 如果之前已经收到过 REGISTER_ACK，则启动 ICE 阶段，向对方发送后续候选队列和 FIN 包
            // + ICE 阶段同时依赖 SIG_PKT_REGISTER_ACK 和 SIG_PKT_PEER_INFO 包：
            //   SIG_PKT_REGISTER_ACK 提供后续候选队列基准; SIG_PKT_PEER_INFO 提供 session_id 作为双方连接的唯一标识
            if (ctx->state == SIGNAL_COMPACT_REGISTERED) {

                ctx->state = SIGNAL_COMPACT_ICE;
                send_rest_candidates_and_fin(s);
                ctx->last_send_time = p2p_time_ms();
            }
        }
        else if (ctx->session_id != ntohll(sid)) {

            P2P_LOG_WARN("COMPACT", "Session mismatch in PEER_INFO: local=%" PRIu64 " pkt=%" PRIu64,
                         ctx->session_id, ntohll(sid));
            return -1;
        }

        bool new_seq = false;

        // seq=0: 服务器维护的首个 PEER_INFO 包
        if (seq == 0) {

            if (!ctx->remote_candidates_0) {

                // 维护分配远端候选列表的空间（作为首个 PEER_INFO 包，候选队列基准 base_index 肯定是 0）
                // + 注意，seq=0 的 PEER_INFO 包的 base_index 字段值可以不为 0（协议上 base_index !=0 说明是对方公网地址发生变更的通知）
                if (p2p_remote_cands_reserve(s, cand_cnt) != 0) {
                    P2P_LOG_ERROR("COMPACT", "Failed to reserve remote candidates (cnt=%d)", cand_cnt);
                    return -1;
                }

                parse_peer_info(s, payload, cand_cnt);

                ctx->remote_candidates_0 = new_seq = true;
            }
        }
        // seq!=0 说明是对方发来的 PEER_INFO 包
        else {

            if ((new_seq = (ctx->remote_candidates_done & (1 << (seq - 1))) == 0)) {

                // 对于 FIN 包，计算对方候选地址集合序列掩码（即计算全集区间）
                if ((flags & SIG_PEER_INFO_FIN) || !cand_cnt) {

                    ctx->remote_candidates_mask = (1 << seq) - 1;
                }

                // 维护分配远端候选列表的空间
                // + 这里 payload[8](base_index) + cand_cnt，表示该包至少需要的远端候选数量; 1 为至少包含一个对方的公网地址
                if (p2p_remote_cands_reserve(s, 1 + payload[8] + cand_cnt) != 0) {
                    P2P_LOG_ERROR("COMPACT", "Failed to reserve remote candidates (base=%u cnt=%d)", payload[8], cand_cnt);
                    return -1;
                }

                parse_peer_info(s, payload, cand_cnt);

                ctx->remote_candidates_done |= 1 << (seq - 1);
            }
        }

        if (new_seq) {

            // 收到该消息说明对方肯定已上线
            ctx->peer_online = true;

            // 如果对方所有的候选队列都已经接收完成（todo 打洞超时失败，应该和这个状态有关）
            if (ctx->remote_candidates_0 && ctx->remote_candidates_mask &&
                (ctx->remote_candidates_done & ctx->remote_candidates_mask) == ctx->remote_candidates_mask) {
                if (ctx->verbose) {
                    P2P_LOG_INFO("COMPACT", "Remote candidate sync complete (mask=0x%04x)",
                                 (unsigned)ctx->remote_candidates_mask);
                }
            }
        }

        /* 发送 PEER_INFO_ACK: [session_id(8)]，确认序号在包头 seq */
        {
            uint8_t ack_payload[8];
            uint64_t sid_net = htonll(ctx->session_id);
            memcpy(ack_payload, &sid_net, 8);
            udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_PEER_INFO_ACK, 0, seq, ack_payload, sizeof(ack_payload));
        }

        if (ctx->verbose) {
            P2P_LOG_INFO("COMPACT", "%s PEER_INFO_ACK(seq=%u)", MSG(MSG_RELAY_ANSWER_SENT), seq);
        }

    } break;

    /* 解析 PEER_INFO_ACK: [session_id(8)]，确认序号在包头 seq */
    case SIG_PKT_PEER_INFO_ACK: {

        if (len < 8) {
            P2P_LOG_WARN("COMPACT", "Invalid PEER_INFO_ACK len=%d", len);
            return -1;
        }

        uint64_t sid;
        memcpy(&sid, payload, 8);
        sid = ntohll(sid);
        if (sid != ctx->session_id) {
            if (ctx->verbose) {
                P2P_LOG_WARN("COMPACT", "Ignore PEER_INFO_ACK for sid=%" PRIu64 " (local sid=%" PRIu64 ")",
                             sid, ctx->session_id);
            }
            return -1;
        }

        uint16_t ack_seq = seq;
        if (ack_seq == 0 || ack_seq > 16) {
            P2P_LOG_WARN("COMPACT", "Invalid PEER_INFO_ACK ack_seq=%u", ack_seq);
            return -1;
        }

        uint16_t bit = (uint16_t)(1u << (ack_seq - 1));
        if ((ctx->candidates_mask & bit) == 0) {

            if (ctx->verbose) {
                P2P_LOG_WARN("COMPACT", "Unexpected PEER_INFO_ACK ack_seq=%u mask=0x%04x",
                             ack_seq, (unsigned)ctx->candidates_mask);
            }
            return -1;
        }

        if ((ctx->candidates_acked & bit) == 0) {

            ctx->candidates_acked |= bit;

            // 如果对方所有的候选队列都已经接收完成
            if ((ctx->candidates_acked & ctx->candidates_mask) == ctx->candidates_mask) {

                ctx->state = SIGNAL_COMPACT_READY;

                if (ctx->verbose) {
                    P2P_LOG_INFO("COMPACT", "%s (sid=%" PRIu64 ")", MSG(MSG_COMPACT_ENTERED_READY), ctx->session_id);
                }
            }
        }

    } break;

    /* 服务器通知：对端已离线。格式: [session_id(8)] */
    case SIG_PKT_PEER_OFF: {

        if (len < 8) {
            P2P_LOG_WARN("COMPACT", "Invalid PEER_OFF len=%d", len);
            return -1;
        }

        uint64_t off_sid;
        memcpy(&off_sid, payload, 8);
        off_sid = ntohll(off_sid);

        if (ctx->session_id != 0 && ctx->session_id == off_sid) {

            // 重置到 REGISTERED 状态，等待对端重新注册
            ctx->state = SIGNAL_COMPACT_REGISTERED;
            ctx->peer_online = false;
            ctx->session_id = 0;

            ctx->candidates_mask = 0;
            ctx->candidates_acked = 0;
            ctx->remote_candidates_mask = 0;
            ctx->remote_candidates_done = 0;
            ctx->remote_candidates_0 = false;

            s->remote_cand_cnt = 0;

            if (ctx->verbose) {
                P2P_LOG_WARN("COMPACT", "PEER_OFF: sid=%" PRIu64 " peer disconnected, reset to REGISTERED", off_sid);
            }
        }

    } break;

    /* 服务器中转：[session_id(8)][data_len(2)][data(N)] / [session_id(8)] */
    case P2P_PKT_RELAY_DATA:
    case P2P_PKT_RELAY_ACK: {

        if (!ctx->relay_support) {
            P2P_LOG_WARN("COMPACT", "Relay packet received but relay not enabled");
            return -1;
        }

        if (len < 8) {
            return -1;
        }

        uint64_t off_sid;
        memcpy(&off_sid, payload, 8);
        off_sid = ntohll(off_sid);

        if (off_sid != ctx->session_id) {
            P2P_LOG_WARN("COMPACT", "Relay sid mismatch: local=%" PRIu64 " pkt=%" PRIu64,
                         ctx->session_id, off_sid);
            return -1;
        }

    } break;

    /* 解析 NAT_PROBE_ACK: [probe_ip(4)][probe_port(2)] 共6字节，使用包头 seq 匹配请求 */
    case SIG_PKT_NAT_PROBE_ACK: {

        if (len < 6) return -1;
        if (seq != ctx->nat_probe_retries) {
            if (ctx->verbose) {
                P2P_LOG_DEBUG("COMPACT", "Ignore NAT_PROBE_ACK seq=%u (expect=%d)", seq, (int)ctx->nat_probe_retries);
            }
            return 1;  // 忽略非本次请求的响应
        }
        
        struct sockaddr_in probe_mapped;
        memset(&probe_mapped, 0, sizeof(probe_mapped));
        probe_mapped.sin_family = AF_INET;
        memcpy(&probe_mapped.sin_addr.s_addr, payload, 4);
        memcpy(&probe_mapped.sin_port,        payload + 4, 2);
        
        // 端口一致性：主端口映射端口 == 探测端口映射端口 → 锥形，否则 → 对称
        ctx->nat_is_port_consistent =
            (probe_mapped.sin_port == ctx->public_addr.sin_port) ? 1 : 0;
        
        // 检测 OPEN：公网地址 IP 与任意本地地址相同（无 NAT）
        int is_open = 0;
        for (int i = 0; i < s->route.addr_count; i++) {
            if (ctx->public_addr.sin_addr.s_addr == s->route.local_addrs[i].sin_addr.s_addr) {
                is_open = 1;
                break;
            }
        }
        
        if (is_open) s->nat_type = P2P_NAT_OPEN;
        else if (ctx->nat_is_port_consistent)
            s->nat_type = P2P_NAT_FULL_CONE; // 满足端口一致性 → Cone NAT（无法区分 Full/Restricted/Port-Restricted，取最乐观估计）
        else s->nat_type = P2P_NAT_SYMMETRIC;
        ctx->nat_probe_retries = -1/* 探测完成 */;
        
        if (ctx->verbose) {
            const char *result_str = p2p_nat_type_str(s->nat_type, s->cfg.language);
            P2P_LOG_INFO("COMPACT", "%s %s %s:%d probe=%s:%d -> %s",
                   MSG(MSG_NAT_DETECTION_COMPLETED),
                   MSG(MSG_STUN_MAPPED_ADDRESS),
                   inet_ntoa(ctx->public_addr.sin_addr), ntohs(ctx->public_addr.sin_port),
                   inet_ntoa(probe_mapped.sin_addr),     ntohs(probe_mapped.sin_port),
                   result_str);
        }

    } break;

    default:
        return 1;  /* 未处理 */
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 周期调用，处理 REGISTER 重发和 PEER_INFO 序列化发送
 *
 * REGISTERING 状态：快速重发（1秒），等待 ACK 确认，有超时限制
 * READY 状态：序列化发送剩余候选（2秒重传间隔），确认后停止
 */
int p2p_signal_compact_tick(struct p2p_session *s) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    uint64_t now = p2p_time_ms();

    // 进入 REGISTERED 之前，定期重发 REGISTER
    if (ctx->state == SIGNAL_COMPACT_REGISTERING) {

        if (now - ctx->last_send_time < REGISTER_INTERVAL_MS) return 0;
        
        // 超时检查
        if (++ctx->register_attempts > MAX_REGISTER_ATTEMPTS) {
            if (ctx->verbose) {
                P2P_LOG_ERROR("COMPACT", "TIMEOUT: %s (%d)",
                       MSG(MSG_COMPACT_MAX_ATTEMPTS), MAX_REGISTER_ATTEMPTS);
            }
            return -1;
        }
        
        /* 构建并发送 REGISTER 包 */
        send_register(s);

        ctx->last_send_time = now;
        return 0;
    }
    // 进入 READY 之前，定期向对方重发剩余候选、以及 FIN
    else if (ctx->state == SIGNAL_COMPACT_ICE) {

        if (now - ctx->last_send_time < PEER_INFO_INTERVAL_MS) return 0;

        resend_rest_candidates_and_fin(s);

        ctx->last_send_time = now;
    }
    // 完成注册且在对方上线（并开始向对方同步后续候选队列）之前；或完成 FIN 确认后，定期向服务器发送保活包
    else if (ctx->state == SIGNAL_COMPACT_REGISTERED || ctx->state == SIGNAL_COMPACT_READY) {

        if (now - ctx->last_send_time < REGISTER_KEEPALIVE_INTERVAL_MS) return 0;

        uint8_t payload[P2P_PEER_ID_MAX * 2];
        memset(payload, 0, sizeof(payload));
        memcpy(payload, ctx->local_peer_id, strnlen(ctx->local_peer_id, P2P_PEER_ID_MAX));
        memcpy(payload + P2P_PEER_ID_MAX, ctx->remote_peer_id, strnlen(ctx->remote_peer_id, P2P_PEER_ID_MAX));

        // 发送 keep-alive 包
        udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_ALIVE, 0, 0, payload, (int)sizeof(payload));

        if (ctx->verbose) {
            P2P_LOG_INFO("COMPACT", "REGISTERED: keepalive ALIVE sent to %s:%d",
                         inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port));
        }

        ctx->last_send_time = now;
    }

    return 0;
}

/*
 * 根据 COMPACT 信令/探测状态推导并写入当前 NAT 检测结果到 s->nat_type。
 * 由 p2p.c 在每次 update tick 中调用。
 */
void p2p_signal_compact_nat_detect_tick(struct p2p_session *s) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    // 探测端口未知
    if (ctx->state == SIGNAL_COMPACT_INIT || ctx->state == SIGNAL_COMPACT_REGISTERING) {
        return;
    }
    // 不支持探测
    if (!ctx->probe_port) {
        assert(s->nat_type == P2P_NAT_UNSUPPORTED);
        return;
    }
    // 已经探测完成、或超时
    if (ctx->nat_probe_retries < 0) {
        return;
    }

    // 间隔等待
    uint64_t now = p2p_time_ms();
    if ((now - ctx->nat_probe_send_time) < NAT_PROBE_INTERVAL_MS) return;

    if (ctx->nat_probe_retries < NAT_PROBE_MAX_RETRIES) {

        ctx->nat_probe_send_time = now;

        struct sockaddr_in probe_addr = ctx->server_addr;
        probe_addr.sin_port = htons(ctx->probe_port);
        udp_send_packet(s->sock, &probe_addr, SIG_PKT_NAT_PROBE, 0, ++ctx->nat_probe_retries, NULL, 0);

        if (ctx->verbose) {
            P2P_LOG_INFO("COMPACT", "NAT_PROBE: %s %d/%d %s %s:%d",
                         MSG(MSG_COMPACT_NAT_PROBE_RETRY),
                         (int)ctx->nat_probe_retries, NAT_PROBE_MAX_RETRIES,
                         MSG(MSG_STUN_TO),
                         inet_ntoa(probe_addr.sin_addr), ctx->probe_port);
        }
    }
        // 最大重试失败，探测端口无应答，无法确定 NAT 类型
    else {

        ctx->nat_probe_retries = -2/* 探测超时 */;
        s->nat_type = P2P_NAT_TIMEOUT;

        if (ctx->verbose) {
            P2P_LOG_WARN("COMPACT", "NAT_PROBE: %s", MSG(MSG_COMPACT_NAT_PROBE_TIMEOUT));
        }
    }
}

#pragma clang diagnostic pop