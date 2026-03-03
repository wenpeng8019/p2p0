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

#define MOD_TAG "COMPACT"

#include "p2p_internal.h"

#define REGISTER_INTERVAL_MS            1000    /* 注册重发间隔 */
#define PEER_INFO_INTERVAL_MS           500     /* PEER_INFO 重发间隔 */
#define MAX_REGISTER_ATTEMPTS           10      /* 最大 REGISTER 重发次数 */
#define REGISTER_KEEPALIVE_INTERVAL_MS  20000   /* REGISTERED 状态保活重注册间隔（防服务器超时清除槽位） */
#define MAX_CANDS_PER_PACKET            10      /* 每个 PEER_INFO 包最大候选数 */
#define NAT_PROBE_MAX_RETRIES           3       /* NAT_PROBE 最大发送次数 */
#define NAT_PROBE_INTERVAL_MS           1000    /* NAT_PROBE 重发间隔 */

#define MSG_REQ_INTERVAL_MS             500     /* MSG_REQ 重发间隔 */
#define MSG_REQ_MAX_RETRIES             5       /* MSG_REQ 最大重发次数，超出后报超时失败 */

///////////////////////////////////////////////////////////////////////////////

/*
 * 向信令服务器请求 REGISTER 操作
 *
 * 协议：SIG_PKT_REGISTER (0x80)
 * 包头: [type=0x80 | flags=0 | seq=0]
 * 负载: [local_peer_id(32)][remote_peer_id(32)][instance_id(4)][candidate_count(1)][candidates(N*7)]
 */
static void send_register(p2p_session_t *s) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    assert(ctx->state == SIGNAL_COMPACT_REGISTERING);

    uint8_t payload[P2P_MAX_PAYLOAD];

    int n = P2P_MAX_PAYLOAD - (P2P_PEER_ID_MAX * 2 + 4 + 1);
    int cand_cnt = n / (int)sizeof(p2p_compact_candidate_t)/* 7 */;
    if (cand_cnt > s->local_cand_cnt) {
        cand_cnt = s->local_cand_cnt;
    }
    ctx->candidates_cached = cand_cnt;

    memset(payload, 0, P2P_PEER_ID_MAX * 2);
    memcpy(payload, ctx->local_peer_id, strlen(ctx->local_peer_id));
    memcpy(payload + P2P_PEER_ID_MAX, ctx->remote_peer_id, strlen(ctx->remote_peer_id));
    n = P2P_PEER_ID_MAX * 2;

    /* instance_id（4 字节大端序）*/
    nwrite_l(payload + n, ctx->instance_id);
    n += 4;

    /* candidate_count */
    payload[n++] = (uint8_t)cand_cnt;
    
    /* candidates (每个 7 字节: type + ip + port) */
    for (int i = 0; i < cand_cnt; i++) {
        payload[n] = (uint8_t)s->local_cands[i].type;
        memcpy(payload + n + 1, &s->local_cands[i].addr.sin_addr.s_addr, 4);
        memcpy(payload + n + 5, &s->local_cands[i].addr.sin_port, 2);
        n += 7;
    }

    // 调试打印协议包信息
    printf(LA_F("Send REGISTER pkt to %s:%d, seq=0, flags=0, len=%d, instance_id=%u, cands=%d", 0, 0),
           inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
           n, ctx->instance_id, cand_cnt);

    udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_REGISTER, 0, 0, payload, n);

    print("I:", LA_F("REGISTERING: %s #%d (%d %s)...", LA_F107, 340),
                 LA_W("Attempt", LA_W8, 10), ctx->register_attempts, s->local_cand_cnt, LA_W("candidate pairs", LA_W13, 20));
}

/*
 * 解析 PEER_INFO 负载，追加到 session 的 remote_cands[]
 *
 * 格式: [session_id(8)][base_index(1)][candidate_count(1)][candidates(N*7)]
 *
 * 注意：这里对方的候选列表顺序并未按对方原始顺序排序，而是 FIFO 追加到 remote_cands[] 中
 */
static void parse_peer_info(p2p_session_t *s, const uint8_t *payload, int cand_cnt) {

    int offset = sizeof(uint64_t) + 2; // 负载头：[session_id(8)][base_index(1)][candidate_count(1)]
    for (int i = 0; i < cand_cnt; i++) {
        int idx = p2p_cand_push_remote(s);
        if (idx < 0) break;  /* OOM */
        p2p_remote_candidate_entry_t *c = &s->remote_cands[idx];

        c->cand.type = (p2p_compact_cand_type_t)payload[offset];
        c->cand.priority = 0;                            // COMPACT 模式不使用优先级
        memset(&c->cand.addr, 0, sizeof(c->cand.addr));
        c->cand.addr.sin_family = AF_INET;
        memcpy(&c->cand.addr.sin_addr.s_addr, payload + offset + 1, 4);
        memcpy(&c->cand.addr.sin_port, payload + offset + 5, 2);
        c->last_punch_send_ms = 0;
        offset += (int)sizeof(p2p_compact_candidate_t);

        print("I:", LA_F("[Trickle] Immediately probing new candidate %s:%d", LA_F169, 363),
                     inet_ntoa(c->cand.addr.sin_addr), ntohs(c->cand.addr.sin_port));

        nat_punch(s, idx);
    }
}

// 8 位循环序比较：判断 seq 是否比 prev 更新（1..255 循环）
static bool seq8_is_newer(uint8_t seq, uint8_t prev) {

    uint8_t diff = (uint8_t)(seq - prev);
    return diff != 0 && diff < 128;
}

// 应用地址变更通知中的单个公网候选地址（覆盖 remote_cands[0]）
static int apply_addr_update_candidate(p2p_session_t *s, const uint8_t *payload) {

    if (p2p_remote_cands_reserve(s, 1) != 0) {
        return -1;
    }

    p2p_remote_candidate_entry_t *c = &s->remote_cands[0];
    c->cand.type = (p2p_compact_cand_type_t)payload[sizeof(uint64_t)+2];
    c->cand.priority = 0;
    sockaddr_init_with_net(&c->cand.addr, (uint32_t *) (payload + sizeof(uint64_t) + 3),
                           (uint16_t *) (payload + sizeof(uint64_t) + 7));
    c->last_punch_send_ms = 0;
    if (s->remote_cand_cnt == 0) {
        s->remote_cand_cnt = 1;
    }

    // Trickle ICE：NAT 打洞已启动时，立即探测最新地址
    if (s->nat.state == NAT_PUNCHING || s->nat.state == NAT_RELAY) {

        print("I:", LA_F("[Trickle] Probing updated candidate %s:%d", LA_F170, 364),
                     inet_ntoa(c->cand.addr.sin_addr), ntohs(c->cand.addr.sin_port));

        nat_punch(s, 0);
    }

    return 0;
}

/* 一个 PEER_INFO 包所承载的候选数量（单位）
 * + 这里 10（字节）表示 PEER_INFO 负载头：[session_id(8)][base_index(1)][candidate_count(1)] = 10 字节
 *   负载头后面的剩余空间就是候选列表，通过预定义、和 MTU 上限共同限制计算得出该单位大小
 */
#define PEER_INFO_CAND_UNIT \
    (((P2P_MAX_PAYLOAD - sizeof(uint64_t) - 2) / (int)sizeof(p2p_compact_candidate_t)) < MAX_CANDS_PER_PACKET \
     ? ((P2P_MAX_PAYLOAD - sizeof(uint64_t) - 2) / (int)sizeof(p2p_compact_candidate_t)) \
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

    payload[sizeof(uint64_t)] = (uint8_t)start;
    payload[sizeof(uint64_t) + 1] = (uint8_t)cnt;

    int offset = sizeof(uint64_t) + 2; // 负载头：[session_id(8)][base_index(1)][candidate_count(1)]
    for (int i = 0; i < cnt; i++) {
        int idx = start + i;
        payload[offset] = (uint8_t)s->local_cands[idx].type;
        memcpy(payload + offset + 1, &s->local_cands[idx].addr.sin_addr.s_addr, 4);
        memcpy(payload + offset + 5, &s->local_cands[idx].addr.sin_port, 2);
        offset += (int)sizeof(p2p_compact_candidate_t);
    }

    return offset;
}

/*
 * 在首次收到 PEER_INFO 包，且已经收到 REGISTER_ACK 的情况下，发送剩余候选队列和 FIN 包给对方
 * 注意：首次收到的 PEER_INFO 包，可能是服务器下发的 seq=0 的 PEER_INFO 包；
 *       也可能是对方发送的 seq!=1 的 PEER_INFO 包（在并发网络状况下，对方的 PEER_INFO 包可能先到达）
 *
 * 协议：SIG_PKT_PEER_INFO (0x03)
 * 包头: [type=0x03 | flags=见下 | seq=1-16]
 * 负载: [session_id(8)][base_index(1)][candidate_count(1)][candidates(N*7)]
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - base_index: 本批候选的起始索引
 *   - candidate_count: 本批候选数量
 *   - candidates: 每个候选 7 字节 [type(1)+ip(4)+port(2)]
 *   - flags: SIG_PKT_FLAG_MORE_CAND (0x02) 表示后续还有包
 *           SIG_PKT_FLAG_FIN (0x01) 表示发送完毕
 */
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
    memcpy(payload, &sid_net, sizeof(uint64_t));

    for (uint16_t seq = 1; seq <= (uint16_t)pkt_cnt; seq++) { uint8_t flags = 0;
        int payload_len = build_peer_info_candidates(s, seq, payload, &flags);

        // 调试打印协议包信息
        printf(LA_F("Send PEER_INFO pkt to %s:%d, seq=%u, flags=0x%02x, len=%d, session_id=%" PRIu64, 0, 0),
               inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
               seq, flags, payload_len, ctx->session_id);
        
        udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_PEER_INFO, flags, seq, payload, payload_len);
    }
}

/*
 * 周期将未确认的 PEER_INFO 包重发给对方
 *
 * 协议：SIG_PKT_PEER_INFO (0x03) - 重传机制
 * 包头: [type=0x03 | flags=见下 | seq=1-16]
 * 负载: [session_id(8)][base_index(1)][candidate_count(1)][candidates(N*7)]
 * 说明: 重发所有未收到 ACK 的 PEER_INFO 包
 */
static void resend_rest_candidates_and_fin(p2p_session_t *s) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    assert(ctx->state == SIGNAL_COMPACT_ICE);
    assert((ctx->candidates_acked & ctx->candidates_mask) != ctx->candidates_mask);

    // session_id 所有包相同，只写一次
    uint8_t payload[P2P_MAX_PAYLOAD];
    uint64_t sid_net = htonll(ctx->session_id);
    memcpy(payload, &sid_net, sizeof(uint64_t));

    for (uint16_t seq = 1; seq <= 16; seq++) {
        uint16_t bit = (uint16_t)(1u << (seq - 1));
        if ((ctx->candidates_mask & bit) == 0) break;                                   // 遇到第一个 0 就可以停止循环（mask 是低位连续段，高位全为 0）
        if ((ctx->candidates_acked & bit) != 0) continue;

        uint8_t flags = 0;
        int payload_len = build_peer_info_candidates(s, seq, payload, &flags);

        // 调试打印协议包信息
        printf(LA_F("Resend PEER_INFO pkt to %s:%d, seq=%u, flags=0x%02x, len=%d", 0, 0),
               inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
               seq, flags, payload_len);
        
        udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_PEER_INFO, flags, seq, payload, payload_len);
    }
}

/*
 * 发送一次 MSG_REQ（A→Server）
 *
 * 协议：SIG_PKT_MSG_REQ (0x20)
 * 包头: [type=0x20 | flags=0 | seq=0]
 * 负载: [target_peer_id(32)][sid(2)][msg(1)][data(N)]
 *   - target_peer_id: 目标对端的 peer_id（32字节，0填充）
 *   - sid: 序列号（2字节，网络字节序），用于匹配响应
 *   - msg: 消息 ID（1字节，用户自定义）
 *   - data: 消息数据（可选）
 * 说明: A端向服务器发送消息请求，服务器缓存并转给B端
 */
static void send_msg_req(struct p2p_session *s) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    uint8_t payload[P2P_PEER_ID_MAX + 3 + P2P_MSG_DATA_MAX];
    int n = 0;

    memcpy(payload + n, ctx->remote_peer_id, P2P_PEER_ID_MAX); n += P2P_PEER_ID_MAX;
    nwrite_s(payload + n, ctx->msg_sid); n += 2;
    payload[n++] = ctx->msg;
    if (ctx->msg_data_len > 0) {
        memcpy(payload + n, ctx->msg_data, (size_t)ctx->msg_data_len);
        n += ctx->msg_data_len;
    }

    // 调试打印协议包信息
    printf(LA_F("Send MSG_REQ pkt to %s:%d, seq=0, flags=0, len=%d, sid=%u, msg=%u", 0, 0),
           inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
           n, ctx->msg_sid, ctx->msg);

    udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_MSG_REQ, 0, 0, payload, n);

    P_clock _clk; P_clock_now(&_clk);
    ctx->msg_send_time = clock_ms(_clk);
    ctx->msg_retries++;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 初始化信令上下文
 */
void p2p_signal_compact_init(p2p_signal_compact_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = SIGNAL_COMPACT_INIT;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 开始信令交换（发送 REGISTER）
 */
ret_t p2p_signal_compact_connect(struct p2p_session *s, const char *local_peer_id, const char *remote_peer_id,
                                 const struct sockaddr_in *server) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    P_check(ctx->state == SIGNAL_COMPACT_INIT, return E_NONE_CONTEXT;)

    ctx->server_addr = *server;

    ctx->state = SIGNAL_COMPACT_REGISTERING;
    P_clock _clk; P_clock_now(&_clk);
    ctx->last_send_time = clock_ms(_clk);
    ctx->register_attempts = 0;

    /* 每次 connect() 生成新的实例 ID（加密安全随机数），用于服务器区分重启 vs 重传 */
    ctx->instance_id = P_rand32();

    memset(ctx->local_peer_id, 0, sizeof(ctx->local_peer_id));
    memset(ctx->remote_peer_id, 0, sizeof(ctx->remote_peer_id));
    strncpy(ctx->local_peer_id, local_peer_id, P2P_PEER_ID_MAX - 1);
    strncpy(ctx->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX - 1);
    ctx->local_peer_id[P2P_PEER_ID_MAX - 1] = '\0';
    ctx->remote_peer_id[P2P_PEER_ID_MAX - 1] = '\0';
    ctx->peer_online = false;

    printf(LA_F("START: %s '%s' -> '%s' %s %s:%d (%d %s)", LA_F139, 354),
                 LA_W("Registering", LA_W74, 98), local_peer_id, remote_peer_id,
                 LA_W("with server", LA_W111, 149), inet_ntoa(server->sin_addr), ntohs(server->sin_port),
                 s->local_cand_cnt, LA_W("candidate pairs", LA_W13, 20));

    print("V: %s", LA_S("Local candidates:", LA_S39, 248));

    // 构造并发送带候选列表的注册包
    send_register(s);

    return E_NONE;
}

/*
 * 断开连接，通知服务器注销配对
 *
 * 协议：SIG_PKT_UNREGISTER (0x06)
 * 包头: [type=0x06 | flags=0 | seq=0]
 * 负载: [local_peer_id(32)][remote_peer_id(32)]
 *   - local_peer_id: 本地 peer_id（32字节，0填充）
 *   - remote_peer_id: 远端 peer_id（32字节，0填充）
 * 说明: 客户端通知服务器注销配对，清理服务器上的会话状态
 */
ret_t p2p_signal_compact_disconnect(struct p2p_session *s) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    P_check(ctx->state != SIGNAL_COMPACT_INIT, return E_NONE_CONTEXT;)

    uint8_t payload[P2P_PEER_ID_MAX * 2];
    memset(payload, 0, sizeof(payload));
    memcpy(payload, ctx->local_peer_id, strnlen(ctx->local_peer_id, P2P_PEER_ID_MAX));
    memcpy(payload + P2P_PEER_ID_MAX, ctx->remote_peer_id, strnlen(ctx->remote_peer_id, P2P_PEER_ID_MAX));

    // 调试打印协议包信息
    printf(LA_F("Send UNREGISTER pkt to %s:%d, seq=0, flags=0, len=%d", 0, 0),
           inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
           (int)sizeof(payload));

    udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_UNREGISTER, 0, 0, payload, (int)sizeof(payload));

    ctx->state = SIGNAL_COMPACT_INIT;
    return E_NONE;
}

/*
 * 通过服务器中转发送数据包（RELAY 机制）
 *
 * 协议：P2P_PKT_RELAY_DATA (0x10)
 * 包头: [type=0x10 | flags=0 | seq=0]
 * 负载: [session_id(8)][data(N)]
 *   - session_id: 会话 ID（8字节，网络字节序）
 *   - data: 用户数据
 * 说明: 当直连失败时，通过服务器中转数据包
 */
ret_t p2p_signal_compact_relay_send(struct p2p_session *s, void* data, uint32_t size) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    P_check(data && size, return E_INVALID;)
    P_check(ctx->relay_support, return E_NO_SUPPORT;)
    P_check(ctx->session_id, return E_NONE_CONTEXT;)

    if (size > (uint32_t)(P2P_MAX_PAYLOAD - sizeof(uint64_t))) {
        size = (uint32_t)(P2P_MAX_PAYLOAD - sizeof(uint64_t));
    }

    uint8_t payload[P2P_MAX_PAYLOAD];
    uint64_t sid_net = htonll(ctx->session_id);
    memcpy(payload, &sid_net, sizeof(uint64_t));
    memcpy(payload + sizeof(uint64_t), data, size);

    // 调试打印协议包信息
    printf(LA_F("Send RELAY_DATA pkt to %s:%d, seq=0, flags=0, len=%d, session_id=%" PRIu64, 0, 0),
           inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
           (int)(sizeof(uint64_t) + size), ctx->session_id);

    udp_send_packet(s->sock, &ctx->server_addr, P2P_PKT_RELAY_DATA, 0, 0, payload, (int)(sizeof(uint64_t) + size));


    return E_NONE;
}

/*
 * A 端：发起 MSG 请求
 * 生成序列号，保存 payload，立即发送第一个 MSG_REQ，进入 state=1 等待 REQ_ACK。
 */
ret_t p2p_signal_compact_request(struct p2p_session *s,
                                 uint8_t msg, const void *data, int len) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    P_check(len >= 0 && len <= P2P_MSG_DATA_MAX, return E_INVALID;)
    P_check(len == 0 || data, return E_INVALID;)
    P_check(ctx->msg_support, return E_NO_SUPPORT;)                    /* 服务器不支持 MSG */
    P_check(ctx->state >= SIGNAL_COMPACT_REGISTERED, return E_NONE_CONTEXT;) /* 未注册 */
    P_check(ctx->msg_state == 0, return E_BUSY;)                       /* 已有挂起请求 */

    /* 生成非零序列号（简单递增，不依赖随机源）*/
    static uint16_t _sid_counter = 0;
    uint16_t sid = ++_sid_counter;
    if (sid == 0) sid = ++_sid_counter;

    ctx->msg_sid      = sid;
    ctx->msg          = msg;
    ctx->msg_data_len = len;
    if (len > 0) memcpy(ctx->msg_data, data, (size_t)len);
    ctx->msg_state    = 1;   /* 等待 REQ_ACK */
    ctx->msg_retries  = 0;
    ctx->msg_send_time = 0;  /* 强制 tick 中立即发出第一包 */

    send_msg_req(s);
    return E_NONE;
}

/*
 * B 端：回复 MSG 请求（用户在 on_msg_req 回调中或异步调用）
 * 向服务器发送 MSG_RESP，服务器负责转给 A 并重传直至 A 回 MSG_RESP_ACK。
 *
 * 协议：SIG_PKT_MSG_RESP (0x22)
 * 包头: [type=0x22 | flags=0 | seq=0]
 * 负载: [session_id(8)][sid(2)][msg(1)][data(N)]
 *   - session_id: A端的会话 ID（8字节，网络字节序）
 *   - sid: 序列号，必顾与 MSG_REQ 中的 sid 一致
 *   - msg: 响应消息类型
 *   - data: 响应数据
 * 说明: B端向服务器发送消息响应，服务器转给A端
 */
ret_t p2p_signal_compact_response(struct p2p_session *s, uint16_t sid,
                                  uint8_t msg, const void *data, int len) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    P_check(len >= 0 && len <= P2P_MSG_DATA_MAX, return E_INVALID;)
    P_check(len == 0 || data, return E_INVALID;)
    P_check(ctx->msg_relay_sid == sid, return E_INVALID;)

    uint8_t payload[8 + 2 + 1 + P2P_MSG_DATA_MAX];
    int n = 0;

    uint64_t sid_net = htonll(ctx->msg_relay_session_id);
    memcpy(payload + n, &sid_net, 8); n += 8;
    nwrite_s(payload + n, sid); n += 2;
    payload[n++] = msg;
    if (len > 0) { memcpy(payload + n, data, (size_t)len); n += len; }

    // 调试打印协议包信息
    printf(LA_F("Send MSG_RESP pkt to %s:%d, seq=0, flags=0, len=%d, sid=%u, msg=%u", 0, 0),
           inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
           n, sid, msg);

    udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_MSG_RESP, 0, 0, payload, n);

    ctx->msg_relay_sid = 0;   /* 清除挂起状态 */
    return E_NONE;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 协议：SIG_PKT_REGISTER_ACK (0x81)
 * 包头: [type=0x81 | flags=见下 | seq=0]
 * 负载: [status(1) | max_candidates(1) | public_ip(4) | public_port(2) | probe_port(2)]
 *   - status: 0=对端离线, 1=对端在线, >=2=错误码
 *   - max_candidates: 服务器缓存的最大候选数量（0=不支持缓存）
 *   - public_ip/port: 客户端的公网地址（服务器观察到的 UDP 源地址）
 *   - probe_port: NAT 探测端口（0=不支持探测）
 *   - flags: SIG_REGACK_FLAG_RELAY (0x01) 表示服务器支持中继
 * 
 * 处理 REGISTER_ACK（服务器注册确认）
 */
void compact_on_register_ack(struct p2p_session *s, uint16_t seq, uint8_t flags,
                              const uint8_t *payload, int len,
                              const struct sockaddr_in *from) {
    (void)seq;

    printf(LA_F("Received REGISTER_ACK pkt from %s:%d, seq=%u, flags=0x%02x, len=%d", LA_F125, 143),
        inet_ntoa(from->sin_addr), ntohs(from->sin_port), seq, flags, len);

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    if (ctx->state != SIGNAL_COMPACT_REGISTERING) {
        print("W:", LA_F("Ignore REGISTER_ACK in state=%d", LA_F74, 323), (int)ctx->state);
        return;
    }

    if (len < 10) {
        print("E:", LA_F("REGISTER_ACK payload too short: %d", LA_F109, 342), len);
        return;
    }

    uint8_t status = payload[0];
    if (status >= 2) {
        print("E:", LA_F("REGISTER_ACK error: %s (status=%d)", LA_F108, 341), LA_W("Server error", LA_W89, 119), status);
        return;
    }

    ctx->relay_support = (flags & SIG_REGACK_FLAG_RELAY) != 0;      // 服务器是否支持数据中继转发
    ctx->msg_support   = (flags & SIG_REGACK_FLAG_MSG)   != 0;      // 服务器是否支持 MSG RPC
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

    print("V:", LA_F("REGISTER_ACK: peer_online=%d, max_cands=%d (%s=%s), %s=%s, public_addr=%s:%d, probe_port=%d", LA_F110, 343),
                    ctx->peer_online, payload[1], LA_W("cache", LA_W12, 19), payload[1] > 0 ? LA_W("yes", LA_W112, 150) : LA_W("no (cached)", LA_W50, 63),
                    LA_S("relay", LA_S70, 197), ctx->relay_support ? LA_W("yes", LA_W112, 150) : LA_W("no (cached)", LA_W50, 63),
                    inet_ntoa(ctx->public_addr.sin_addr), ntohs(ctx->public_addr.sin_port),
                    ctx->probe_port);

    // 标记进入 REGISTERED 状态（该状态将停止周期发送 REGISTER）
    ctx->state = SIGNAL_COMPACT_REGISTERED;

    print("I:", LA_F("%s: %s", LA_F50, 311), LA_W("Entered REGISTERED state", LA_W22, 30),
                    ctx->peer_online ? LA_W("Peer online, waiting for PEER_INFO(seq=1)", LA_W63, 78) : LA_W("Peer offline, waiting for peer to come online", LA_W61, 76));

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

        print("I: %s", LA_S("Received REGISTER_ACK with session_id already set, directly enter ICE phase", LA_S69, 252));

        send_rest_candidates_and_fin(s);
        P_clock _clk; P_clock_now(&_clk);
        ctx->last_send_time = clock_ms(_clk);
    }

    // 如果服务器支持 NAT 探测端口，则启动 NAT_PROBE 探测流程
    if (ctx->probe_port > 0) {

        if (ctx->nat_probe_retries) {
            print("W:", LA_F("NAT_PROBE already started (retries=%d)", LA_F90, 334), (int)ctx->nat_probe_retries);
        }

        // 对于 lan_punch 模式：本地直接打洞，无需探测，NAT 类型直接标记为 OPEN
        if (s->cfg.lan_punch) {

            s->nat_type = P2P_NAT_OPEN;
            ctx->nat_probe_retries = -1/* 探测完成 */;

            print("I: %s", LA_S("[lan_punch] 跳过 NAT_PROBE，直接标记 NAT=OPEN", LA_S1, 246));
        }
        else {

            // 标记进入 NAT_PROBE 探测中状态，发送第一轮探测包
            s->nat_type = P2P_NAT_DETECTING;
            ctx->nat_probe_retries = 1;
            P_clock _clk; P_clock_now(&_clk);
            ctx->nat_probe_send_time = clock_ms(_clk);

            /*
             * 发送 NAT_PROBE 探测包
             *
             * 协议：SIG_PKT_NAT_PROBE (0x07)
             * 包头: [type=0x07 | flags=0 | seq=探测重试次数]
             * 负载: 空
             *   - seq: 探测重试次数（从1开始）
             * 说明: 向服务器探测端口发送空包，服务器观察源地址来判断 NAT 类型
             */
            struct sockaddr_in probe_addr = ctx->server_addr;
            probe_addr.sin_port = htons(ctx->probe_port);

            // 调试打印协议包信息
            printf(LA_F("Send NAT_PROBE pkt to %s:%d, seq=%u, flags=0, len=0", 0, 0),
                   inet_ntoa(probe_addr.sin_addr), ntohs(probe_addr.sin_port),
                   ctx->nat_probe_retries);
            
            udp_send_packet(s->sock, &probe_addr, SIG_PKT_NAT_PROBE, 0, ctx->nat_probe_retries, NULL, 0);

            print("I:", LA_F("NAT_PROBE: %s %s:%d (1/%d)", LA_F93, 337),
                            LA_W("NAT probe sent to", LA_W45, 58),
                            inet_ntoa(probe_addr.sin_addr), ctx->probe_port,
                            NAT_PROBE_MAX_RETRIES);
        }
    }
    else s->nat_type = P2P_NAT_UNSUPPORTED;
}

/*
 * 协议：SIG_PKT_ALIVE_ACK (0x87)
 * 包头: [type=0x87 | flags=0 | seq=0]
 * 负载: 无
 * 
 * 处理 ALIVE_ACK（保活确认）
 */
void compact_on_alive_ack(struct p2p_session *s, const struct sockaddr_in *from) {
    printf(LA_F("Received ALIVE_ACK pkt from %s:%d", LA_F113, 143),
        inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    if (ctx->state <= SIGNAL_COMPACT_REGISTERING) {
        print("W:", LA_F("Ignore ALIVE_ACK in state=%d", LA_F71, 320), (int)ctx->state);
        return;
    }

    print("V: %s", LA_S("Received ALIVE_ACK from server", LA_S66, 250));

    // 确认服务器未掉线
    P_clock _clk; P_clock_now(&_clk);
    ctx->last_recv_time = clock_ms(_clk);
}

/*
 * 协议：SIG_PKT_PEER_INFO (0x83)
 * 包头: [type=0x83 | flags=见下 | seq=序列号]
 * 负载: [session_id(8) | base_index(1) | candidate_count(1) | candidates(N*7)]
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - base_index: 本批候选的起始索引（0-based）
 *   - candidate_count: 本批候选数量，0 表示结束标识
 *   - seq=0: 服务器发送，包含缓存的对端候选，首次分配 session_id
 *   - seq>0: 客户端发送，继续同步剩余候选
 *   - flags: SIG_PEER_INFO_FIN (0x01) 表示候选列表发送完毕
 * 
 * 处理 PEER_INFO（对端候选信息）
 */
void compact_on_peer_info(struct p2p_session *s, uint16_t seq, uint8_t flags,
                          const uint8_t *payload, int len,
                          const struct sockaddr_in *from) {
    printf(LA_F("Received PEER_INFO pkt from %s:%d, seq=%u, flags=0x%02x, len=%d", LA_F118, 143),
        inet_ntoa(from->sin_addr), ntohs(from->sin_port), seq, flags, len);

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    if (ctx->state < SIGNAL_COMPACT_REGISTERING) {
        print("W:", LA_F("Invalid PEER_INFO: state=%d len=%d", LA_F83, 330), (int)ctx->state, len);
        return;
    }

    if (len < (int)sizeof(uint64_t) + 2 || seq > 16) {
        print("E:", LA_F("Invalid PEER_INFO seq=%u", LA_F81, 328), seq);
        return;
    }

    int cand_cnt = payload[sizeof(uint64_t) + 1];
    if (len < (int)sizeof(uint64_t) + 2 + (int)sizeof(p2p_compact_candidate_t) * cand_cnt) {
        print("E:", LA_F("Invalid PEER_INFO payload: len=%d cand_cnt=%d", LA_F80, 327), len, cand_cnt);
        return;
    }

    // 服务器发送的第一个 PEER_INFO，至少有一个对方公网的候选地址，且肯定不带 FIN 标识
    if (seq == 0 && (!cand_cnt || (flags & SIG_PEER_INFO_FIN))) {
        print("E:", LA_F("Invalid PEER_INFO seq=0: cand_cnt=%d flags=0x%02x", LA_F82, 329), cand_cnt, flags);
        return;
    }

    // 初始化获取、或验证 session_id，作为双方连接的唯一标识（后续双方基于连接的通讯以此作为标识）
    uint64_t sid;
    memcpy(&sid, payload, sizeof(uint64_t));
    sid = ntohll(sid);
    if (!ctx->session_id) {

        ctx->session_id = sid;

        s->remote_cand_cnt = 0;     // 初始化清空对端候选列表

        // 如果之前已经收到过 REGISTER_ACK，则启动 ICE 阶段，向对方发送后续候选队列和 FIN 包
        // + ICE 阶段同时依赖 SIG_PKT_REGISTER_ACK 和 SIG_PKT_PEER_INFO 包：
        //   SIG_PKT_REGISTER_ACK 提供后续候选队列基准; SIG_PKT_PEER_INFO 提供 session_id 作为双方连接的唯一标识
        if (ctx->state == SIGNAL_COMPACT_REGISTERED) {
            ctx->state = SIGNAL_COMPACT_ICE;

            print("I: %s", LA_S("Received first PEER_INFO with session_id, enter ICE phase", LA_S68, 251));

            send_rest_candidates_and_fin(s);
            P_clock _clk; P_clock_now(&_clk);
            ctx->last_send_time = clock_ms(_clk);
        }
    }
    else if (ctx->session_id != sid) {

        print("E:", LA_F("Session mismatch in PEER_INFO: local=%" PRIu64 " pkt=%" PRIu64, 0, 0),
                        ctx->session_id, sid);
        return;
    }

    print("V:", LA_F("Received PEER_INFO(seq=%u, cand_cnt=%d, flags=0x%02x)", LA_F119, 346), seq, cand_cnt, flags);

    bool new_seq = false;

    // seq=0: 服务器维护的首个 PEER_INFO 包，或地址变更通知
    if (seq == 0) {

        uint8_t base_index = payload[sizeof(uint64_t)];

        // base_index!=0 表示地址变更通知，candidate_count 必须为 1
        if (base_index != 0) {

            if (cand_cnt != 1 || (flags & SIG_PEER_INFO_FIN)) {
                print("E:", LA_F("Invalid PEER_INFO notify: base=%u cand_cnt=%d flags=0x%02x", LA_F79, 326),
                                base_index, cand_cnt, flags);
                return;
            }

            if (ctx->remote_addr_notify_seq == 0 || seq8_is_newer(base_index, ctx->remote_addr_notify_seq)) {

                if (apply_addr_update_candidate(s, payload) != 0) {
                    print("E: %s", LA_S("Failed to apply addr update candidate", LA_S27, 247));
                    return;
                }

                ctx->remote_addr_notify_seq = base_index;
                new_seq = true;
            }
        }
        else if (!ctx->remote_candidates_0) {

            // 维护分配远端候选列表的空间（作为首个 PEER_INFO 包，候选队列基准 base_index 肯定是 0）
            // + 注意，seq=0 的 PEER_INFO 包的 base_index 字段值可以不为 0（协议上 base_index !=0 说明是对方公网地址发生变更的通知）
            if (p2p_remote_cands_reserve(s, cand_cnt) != 0) {
                print("E:", LA_F("Failed to reserve remote candidates (cnt=%d)", LA_F68, 318), cand_cnt);
                return;
            }

            parse_peer_info(s, payload, cand_cnt);

            ctx->remote_candidates_0 = new_seq = true;
        }
    }
    // seq!=0 说明是对方发来的 PEER_INFO 包
    else {

        if ((new_seq = (ctx->remote_candidates_done & (1u << (seq - 1))) == 0)) {

            // 对于 FIN 包，计算对方候选地址集合序列掩码（即计算全集区间）
            if ((flags & SIG_PEER_INFO_FIN) || !cand_cnt) {

                ctx->remote_candidates_mask = (1u << seq) - 1u;
            }

            // 维护分配远端候选列表的空间
            // + 这里 payload[8](base_index) + cand_cnt，表示该包至少需要的远端候选数量; 1 为至少包含一个对方的公网地址
            if (p2p_remote_cands_reserve(s, 1 + payload[sizeof(uint64_t)] + cand_cnt) != 0) {
                print("E:", LA_F("Failed to reserve remote candidates (base=%u cnt=%d)", LA_F67, 317), payload[sizeof(uint64_t)], cand_cnt);
                return;
            }

            parse_peer_info(s, payload, cand_cnt);

            ctx->remote_candidates_done |= 1u << (seq - 1);
        }
    }

    if (new_seq) {

        // 收到该消息说明对方肯定已上线
        ctx->peer_online = true;

        // 如果对方所有的候选队列都已经接收完成（todo 打洞超时失败，应该和这个状态有关）
        if (ctx->remote_candidates_0 && ctx->remote_candidates_mask &&
            (ctx->remote_candidates_done & ctx->remote_candidates_mask) == ctx->remote_candidates_mask) {

            print("I:", LA_F("Remote candidate sync complete (mask=0x%04x)", LA_F136, 351),
                            (unsigned)ctx->remote_candidates_mask);
        }
    }

    /*
     * 发送 PEER_INFO_ACK 确认包
     *
     * 协议：SIG_PKT_PEER_INFO_ACK (0x04)
     * 包头: [type=0x04 | flags=0 | seq=被确认的PEER_INFO包序号]
     * 负载: [session_id(8)]
     *   - session_id: 会话 ID（网络字节序，64位）
     *   - seq: 被确认的 PEER_INFO 包的序列号
     * 说明: 确认收到对方的候选地址包
     */
    {
        print("V:", LA_F("%s PEER_INFO_ACK(seq=%u)", LA_F35, 296), LA_W("Sent ANSWER", LA_W84, 113), seq);

        uint8_t ack_payload[sizeof(uint64_t)];
        uint64_t sid_net = htonll(ctx->session_id);
        memcpy(ack_payload, &sid_net, sizeof(uint64_t));

        // 调试打印协议包信息
        printf(LA_F("Send PEER_INFO_ACK pkt to %s:%d, seq=%u, flags=0, len=%d, session_id=%" PRIu64, 0, 0),
               inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
               seq, (int)sizeof(ack_payload), ctx->session_id);
        
        udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_PEER_INFO_ACK, 0, seq, ack_payload, sizeof(ack_payload));
    }
}

/*
 * 协议：SIG_PKT_PEER_INFO_ACK (0x84)
 * 包头: [type=0x84 | flags=0 | seq=确认的 PEER_INFO 序列号]
 * 负载: [session_id(8)]
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - seq: 确认的 PEER_INFO 序列号（0 表示确认服务器下发的 PEER_INFO(seq=0)）
 * 
 * 处理 PEER_INFO_ACK（对端候选确认）
 */
void compact_on_peer_info_ack(struct p2p_session *s, uint16_t seq,
                               const uint8_t *payload, int len,
                               const struct sockaddr_in *from) {
    printf(LA_F("Received PEER_INFO_ACK pkt from %s:%d, seq=%u, len=%d", LA_F121, 143),
        inet_ntoa(from->sin_addr), ntohs(from->sin_port), seq, len);

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    if (len < (int)sizeof(uint64_t)) {
        print("E:", LA_F("Invalid PEER_INFO_ACK len=%d", LA_F85, 332), len);
        return;
    }

    uint64_t sid;
    memcpy(&sid, payload, sizeof(uint64_t));
    sid = ntohll(sid);
    if (sid != ctx->session_id) {
        print("E:", LA_F("Ignore PEER_INFO_ACK for sid=%" PRIu64 " (local sid=%" PRIu64 ")", 0, 0),
                        sid, ctx->session_id);
        return;
    }

    uint16_t ack_seq = seq;
    if (ack_seq == 0 || ack_seq > 16) {
        print("E:", LA_F("Invalid PEER_INFO_ACK ack_seq=%u", LA_F84, 331), ack_seq);
        return;
    }

    uint16_t bit = (uint16_t)(1u << (ack_seq - 1));
    if ((ctx->candidates_mask & bit) == 0) {
        print("E:", LA_F("Unexpected PEER_INFO_ACK ack_seq=%u mask=0x%04x", LA_F159, 356),
                        ack_seq, (unsigned)ctx->candidates_mask);
        return;
    }

    if ((ctx->candidates_acked & bit) == 0) {

        print("V:", LA_F("Received PEER_INFO_ACK for seq=%u", LA_F120, 347), ack_seq);

        ctx->candidates_acked |= bit;

        // 如果对方所有的候选队列都已经接收完成
        if ((ctx->candidates_acked & ctx->candidates_mask) == ctx->candidates_mask) {

            ctx->state = SIGNAL_COMPACT_READY;

            print("I:", LA_F("%s (sid=%" PRIu64 ")", 0, 0), LA_W("Entered READY state, starting NAT punch and candidate sync", LA_W21, 29), ctx->session_id);
        }
    }
    else print("W:", LA_F("Received PEER_INFO_ACK for seq=%u", LA_F120, 347), ack_seq);
}

/*
 * 协议：SIG_PKT_PEER_OFF (0x85)
 * 包头: [type=0x85 | flags=0 | seq=0]
 * 负载: [session_id(8)]
 *   - session_id: 已断开的会话 ID（网络字节序，64位）
 * 
 * 处理 PEER_OFF（对端离线通知）
 */
void compact_on_peer_off(struct p2p_session *s, const uint8_t *payload, int len,
                         const struct sockaddr_in *from) {
    printf(LA_F("Received PEER_OFF pkt from %s:%d, len=%d", LA_F123, 143),
        inet_ntoa(from->sin_addr), ntohs(from->sin_port), len);

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    if (len < (int)sizeof(uint64_t)) {
        print("E:", LA_F("Invalid PEER_OFF len=%d", LA_F86, 333), len);
        return;
    }

    uint64_t off_sid;
    memcpy(&off_sid, payload, sizeof(uint64_t));
    off_sid = ntohll(off_sid);

    if (ctx->session_id != 0 && ctx->session_id == off_sid) {

        print("V:", LA_F("Received PEER_OFF for sid=%" PRIu64, 0, 0), off_sid);

        // 重置到 REGISTERED 状态，等待对端重新注册
        ctx->state = SIGNAL_COMPACT_REGISTERED;
        ctx->peer_online = false;
        ctx->session_id = 0;

        ctx->candidates_mask = 0;
        ctx->candidates_acked = 0;
        ctx->remote_candidates_mask = 0;
        ctx->remote_candidates_done = 0;
        ctx->remote_candidates_0 = false;
        ctx->remote_addr_notify_seq = 0;

        s->remote_cand_cnt = 0;

        print("W:", LA_F("PEER_OFF: sid=%" PRIu64 " peer disconnected, reset to REGISTERED", 0, 0), off_sid);
    }
    else print("W:", LA_F("Received PEER_OFF for sid=%" PRIu64, 0, 0), off_sid);
}

/*
 * 协议：P2P_PKT_RELAY_DATA (0xA0) / P2P_PKT_RELAY_ACK (0xA1)
 * 包头: [type=0xA0/0xA1 | flags=0 | seq=数据序列号]
 * 负载: [session_id(8)][data(N)]  (对于 RELAY_DATA)
 *       [session_id(8)][ack_seq(2)][sack(4)]  (对于 RELAY_ACK)
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - data: 中继数据（对于 RELAY_DATA）
 *   - ack_seq: 确认序列号（对于 RELAY_ACK）
 *   - sack: 选择性确认（对于 RELAY_ACK）
 * 说明: P2P 打洞失败后，通过服务器中继转发数据
 *
 * 处理 RELAY_DATA / RELAY_ACK（中继数据）
 * 验证 COMPACT 层的 session_id，并调整 payload/len 跳过该头部
 * @return true=验证成功（payload/len 已调整），false=验证失败
 */
bool compact_on_relay_packet(struct p2p_session *s, uint8_t type,
                             const uint8_t **payload, int *len,
                             const struct sockaddr_in *from) {
    printf(LA_F("Received RELAY pkt from %s:%d, type=0x%02X, len=%d", LA_F127, 143),
        inet_ntoa(from->sin_addr), ntohs(from->sin_port), type, *len);

    // RELAY 包只能在 COMPACT 模式下使用
    if (s->signaling_mode != P2P_SIGNALING_MODE_COMPACT) {
        print("W:", LA_F("Received RELAY packet (type=0x%02X) in non-COMPACT mode", LA_F126, 232), type);
        return false;
    }

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    if (!ctx->relay_support) {
        print("E: %s", LA_S("Relay packet received but relay not enabled", LA_S72, 253));
        return false;
    }

    if (*len < (int)sizeof(uint64_t)) {
        return false;
    }

    uint64_t off_sid;
    memcpy(&off_sid, *payload, sizeof(uint64_t));
    off_sid = ntohll(off_sid);

    if (off_sid != ctx->session_id) {
        print("E:", LA_F("Relay sid mismatch: local=%" PRIu64 " pkt=%" PRIu64, 0, 0),
                     ctx->session_id, off_sid);
        return false;
    }

    print("V:", LA_F("Received %s for sid=%" PRIu64 ", len=%d", 0, 0),
                    type == P2P_PKT_RELAY_DATA ? "RELAY_DATA" : "RELAY_ACK", off_sid, *len);
    
    // 跳过 COMPACT 层的 session_id 头部
    *payload += sizeof(uint64_t);
    *len -= (int)sizeof(uint64_t);
    return true;
}

/*
 * 协议：SIG_PKT_NAT_PROBE_ACK (0x8D)
 * 包头: [type=0x8D | flags=0 | seq=对应的 NAT_PROBE 请求 seq]
 * 负载: [probe_ip(4) | probe_port(2)]
 *   - probe_ip/port: 服务器在探测端口观察到的客户端源地址（第二次映射）
 *   - seq: 复制请求包的 seq，用于客户端匹配响应
 * 
 * 处理 NAT_PROBE_ACK（NAT 探测响应）
 */
void compact_on_nat_probe_ack(struct p2p_session *s, uint16_t seq,
                               const uint8_t *payload, int len,
                               const struct sockaddr_in *from) {
    printf(LA_F("Received NAT_PROBE_ACK pkt from %s:%d, seq=%u, len=%d", LA_F116, 143),
        inet_ntoa(from->sin_addr), ntohs(from->sin_port), seq, len);

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    if (len < 6) {
        print("E:", LA_F("Invalid NAT_PROBE_ACK len=%d", LA_F78, 325), len);
        return;
    }
    if (seq != ctx->nat_probe_retries) {
        print("W:", LA_F("Ignore NAT_PROBE_ACK seq=%u (expect=%d)", LA_F72, 321), seq, (int)ctx->nat_probe_retries);
        return;
    }
    
    struct sockaddr_in probe_mapped;
    memset(&probe_mapped, 0, sizeof(probe_mapped));
    probe_mapped.sin_family = AF_INET;
    memcpy(&probe_mapped.sin_addr.s_addr, payload, 4);
    memcpy(&probe_mapped.sin_port,        payload + 4, 2);

    print("V:", LA_F("Received NAT_PROBE_ACK: probe_mapped=%s:%d", LA_F117, 345), inet_ntoa(probe_mapped.sin_addr), ntohs(probe_mapped.sin_port));
    
    // 端口一致性：主端口映射端口 == 探测端口映射端口 → 锥形，否则 → 对称
    ctx->nat_is_port_consistent = (probe_mapped.sin_port == ctx->public_addr.sin_port) ? 1 : 0;
    
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
    
    print("I:", LA_F("%s %s %s:%d probe=%s:%d -> %s", LA_F16, 273),
                    LA_W("Detection completed", LA_W19, 27),
                    LA_S("Mapped address", LA_S40, 186),
                    inet_ntoa(ctx->public_addr.sin_addr), ntohs(ctx->public_addr.sin_port),
                    inet_ntoa(probe_mapped.sin_addr),     ntohs(probe_mapped.sin_port),
                    p2p_nat_type_str(s->nat_type));
}

/*
 * 协议：SIG_PKT_MSG_REQ (0x20)
 * 包头: [type=0x20 | flags=SIG_MSG_FLAG_RELAY | seq=0]
 * 负载: [session_id(8)][sid(2)][msg(1)][data(N)]
 *   - session_id: A端的会话 ID（网络字节序，64位）
 *   - sid: 序列号（2字节，网络字节序）
 *   - msg: 消息 ID（1字节）
 *   - data: 消息数据
 *   - flags: SIG_MSG_FLAG_RELAY (0x01) 表示服务器转发
 * 说明: B端收到服务器转发的消息请求，A端发出的原始请求(flags=0)不会到达客户端
 *
 * 处理 MSG_REQ（服务器转发的消息请求）
 */
void compact_on_request(struct p2p_session *s, uint8_t flags,
                        const uint8_t *payload, int len,
                        const struct sockaddr_in *from) {

    if (!(flags & SIG_MSG_FLAG_RELAY)) return;  /* A→Server 不应由客户端收到 */
    if (len < 11) return;                       /* session_id(8)+sid(2)+msg(1) */

    printf(LA_F("Received MSG_REQ pkt from %s:%d, flags=0x%02x, len=%d", 0, 0),
           inet_ntoa(from->sin_addr), ntohs(from->sin_port), flags, len);

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    uint64_t session_id = 0;
    nread_ll(&session_id, payload);
    uint16_t sid = 0;
    nread_s(&sid, payload + 8);
    uint8_t msg = payload[10];
    const uint8_t *req_data = payload + 11;
    int req_len = len - 11;

    /* 保存 B 端待回复状态 */
    ctx->msg_relay_sid         = sid;
    ctx->msg_relay_session_id  = session_id;

    /* msg=0: 自动 echo 回复（无需应用层介入） */
    if (msg == 0) {
        print("V:", LA_F("MSG_REQ msg=0, auto echo reply: sid=%u, len=%d", 0, 0), sid, req_len);
        p2p_signal_compact_response(s, sid, 0, req_data, req_len);
        return;
    }

    /* 触发用户回调 */
    if (s->cfg.on_msg_req)
        s->cfg.on_msg_req((p2p_handle_t)s, sid, msg,
                          req_data, req_len, s->cfg.userdata);
}

/*
 * 协议：SIG_PKT_MSG_REQ_ACK (0x21)
 * 包头: [type=0x21 | flags=0 | seq=0]
 * 负载: [sid(2)][status(1)]
 *   - sid: 序列号，与 MSG_REQ 中的 sid 对应
 *   - status: 0=已缓存开始中转, 1=B不在线（失败）
 * 说明: A端收到服务器对消息请求的确认
 *
 * 处理 MSG_REQ_ACK（服务器对请求的确认）
 */
void compact_on_request_ack(struct p2p_session *s,
                             const uint8_t *payload, int len,
                             const struct sockaddr_in *from) {

    if (len < 3) return;

    printf(LA_F("Received MSG_REQ_ACK pkt from %s:%d, len=%d", 0, 0),
           inet_ntoa(from->sin_addr), ntohs(from->sin_port), len);

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    uint16_t sid = 0;
    nread_s(&sid, payload);
    uint8_t status = payload[2];

    if (ctx->msg_state != 1 || ctx->msg_sid != sid) return; /* 过期/无关 */

    if (status == 0) {
        /* 成功：服务器已收到请求并开始向 B 中转，A 停止重发，等待 MSG_RESP */
        ctx->msg_state = 2;
        print("I:", LA_S("MSG_REQ_ACK: request accepted, waiting for response", LA_S44, 752));
    } else {
        /* B 不在线：请求失败，通知上层 */
        uint16_t saved_id  = ctx->msg_sid;
        uint8_t  saved_msg = ctx->msg;
        ctx->msg_state  = 0;
        ctx->msg_sid = 0;
        print("W:", LA_S("MSG_REQ_ACK: peer offline, request failed", LA_S43, 751));
        if (s->cfg.on_msg_res)
            s->cfg.on_msg_res((p2p_handle_t)s, saved_id, saved_msg,
                              NULL, -1, s->cfg.userdata);
    }
}

/*
 * 协议：SIG_PKT_MSG_RESP (0x22)
 * 包头: [type=0x22 | flags=0 | seq=0]
 * 负载: [sid(2)][msg(1)][data(N)]
 *   - sid: 序列号，与 MSG_REQ 中的 sid 对应
 *   - msg: 响应消息 ID
 *   - data: 响应数据
 * 说明: A端收到服务器转发的B端响应，自动回复 MSG_RESP_ACK 给服务器
 *
 * 处理 MSG_RESP（服务器转发的B端响应）
 */
void compact_on_response(struct p2p_session *s,
                        const uint8_t *payload, int len,
                        const struct sockaddr_in *from) {

    if (len < 3) return;

    printf(LA_F("Received MSG_RESP pkt from %s:%d, len=%d", 0, 0),
           inet_ntoa(from->sin_addr), ntohs(from->sin_port), len);

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    uint16_t sid = 0;
    nread_s(&sid, payload);
    uint8_t res_type = payload[2];
    const uint8_t *res_data = payload + 3;
    int res_len = len - 3;

    /*
     * 发送 MSG_RESP_ACK 确认包
     *
     * 协议：SIG_PKT_MSG_RESP_ACK (0x23)
     * 包头: [type=0x23 | flags=0 | seq=0]
     * 负载: [sid(2)]
     *   - sid: 序列号，与 MSG_RESP 中的 sid 一致
     * 说明: A端确认收到B端的响应，幂等操作，即使已处理过也补发
     */
    uint8_t ack[2];
    nwrite_s(ack, sid);

    // 调试打印协议包信息
    printf(LA_F("Send MSG_RESP_ACK pkt to %s:%d, seq=0, flags=0, len=2, sid=%u", 0, 0),
           inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
           sid);
    
    udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_MSG_RESP_ACK, 0, 0, ack, 2);

    /* 仅当序列号匹配且仍在等待应答时触发回调 */
    if (ctx->msg_state == 2 && ctx->msg_sid == sid) {
        ctx->msg_state  = 0;
        ctx->msg_sid = 0;
        print("I:", LA_S("MSG_RESP received, RPC complete", LA_S45, 753));
        if (s->cfg.on_msg_res)
            s->cfg.on_msg_res((p2p_handle_t)s, sid, res_type,
                              res_data, res_len, s->cfg.userdata);
    }
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 信令服务周期维护（拉取阶段）— 注册重试、保活
 * 
 * 处理向服务器发送的维护包：
 * - REGISTERING：重发 REGISTER（获取配对信息）
 * - REGISTERED/READY：发送 keepalive（保持槽位）
 */

void p2p_signal_compact_tick_recv(struct p2p_session *s) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    P_clock _clk; P_clock_now(&_clk);
    uint64_t now = clock_ms(_clk);

    // REGISTERING 状态：定期重发 REGISTER
    if (ctx->state == SIGNAL_COMPACT_REGISTERING) {

        if (now - ctx->last_send_time < REGISTER_INTERVAL_MS) return;
        
        // 超时检查
        if (ctx->register_attempts > MAX_REGISTER_ATTEMPTS) return;
        if (++ctx->register_attempts > MAX_REGISTER_ATTEMPTS) {
            print("W:", LA_F("TIMEOUT: Max register attempts reached (%d)", LA_F156, 55), MAX_REGISTER_ATTEMPTS);
            return;
        }
        
        print("V:", LA_F("Resend REGISTER (attempt %d)", LA_F137, 352), ctx->register_attempts);
        send_register(s);
        ctx->last_send_time = now;
    }
    // REGISTERED/READY 状态：定期向服务器发送保活包
    else if (ctx->state == SIGNAL_COMPACT_REGISTERED || ctx->state == SIGNAL_COMPACT_READY) {

        if (now - ctx->last_send_time < REGISTER_KEEPALIVE_INTERVAL_MS) return;

        uint8_t payload[P2P_PEER_ID_MAX * 2];
        memset(payload, 0, sizeof(payload));
        memcpy(payload, ctx->local_peer_id, strnlen(ctx->local_peer_id, P2P_PEER_ID_MAX));
        memcpy(payload + P2P_PEER_ID_MAX, ctx->remote_peer_id, strnlen(ctx->remote_peer_id, P2P_PEER_ID_MAX));

        /*
         * 发送 ALIVE 保活包
         *
         * 协议：SIG_PKT_ALIVE (0x0A)
         * 包头: [type=0x0A | flags=0 | seq=0]
         * 负载: [local_peer_id(32)][remote_peer_id(32)]
         *   - local_peer_id: 本地 peer_id
         *   - remote_peer_id: 远端 peer_id
         * 说明: 保活包，维持服务器上的注册状态
         */
        // 调试打印协议包信息
        printf(LA_F("Send ALIVE pkt to %s:%d, seq=0, flags=0, len=%d", 0, 0),
               inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
               (int)sizeof(payload));
        
        udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_ALIVE, 0, 0, payload, (int)sizeof(payload));
        ctx->last_send_time = now;

        print("V:", LA_F("%s: keepalive ALIVE sent to %s:%d", LA_F52, 313),
              ctx->state == SIGNAL_COMPACT_REGISTERED ? "REGISTERED" : "READY",
              inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port));
    }

    if (ctx->msg_state != 1) return; /* 只在等待 REQ_ACK 阶段重发 */

    if ((now - ctx->msg_send_time) < MSG_REQ_INTERVAL_MS) return;

    if (ctx->msg_retries > MSG_REQ_MAX_RETRIES) {
        /* 超时失败 */
        uint16_t saved_id  = ctx->msg_sid;
        uint8_t  saved_msg = ctx->msg;
        ctx->msg_state  = 0;
        ctx->msg_sid = 0;
        print("W:", LA_S("MSG_REQ timeout, no REQ_ACK received", LA_S42, 750));
        if (s->cfg.on_msg_res)
            s->cfg.on_msg_res((p2p_handle_t)s, saved_id, saved_msg,
                              NULL, -1, s->cfg.userdata);
        return;
    }

    print("V:", LA_S("MSG_REQ retransmit", LA_S41, 749));
    send_msg_req(s);
}

/*
 * 信令输出（推送阶段）— 向对端发送候选地址
 * 
 * 处理向对端发送的候选包：
 * - ICE：重发剩余候选和 FIN
 * - READY：发送新收集到的候选（如果有）
 */
void p2p_signal_compact_tick_send(struct p2p_session *s) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    P_clock _clk; P_clock_now(&_clk);
    uint64_t now = clock_ms(_clk);

    // ICE 状态：定期向对方重发剩余候选、以及 FIN
    if (ctx->state == SIGNAL_COMPACT_ICE) {

        if (now - ctx->last_send_time < PEER_INFO_INTERVAL_MS) return;

        print("V: %s", LA_S("Resend remaining candidates and FIN to peer", LA_S75, 254));

        resend_rest_candidates_and_fin(s);
        ctx->last_send_time = now;
    }
    // READY 状态：检查是否有新候选需要发送（暂时为空，未来可扩展）
    else if (ctx->state == SIGNAL_COMPACT_READY) {

        // TODO: 如果后续收集到新候选（如延迟的 STUN 响应），可在此发送
        // 目前 COMPACT 模式在进入 READY 前已发送所有候选
    }
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
    P_clock _clk; P_clock_now(&_clk);
    uint64_t now = clock_ms(_clk);
    if ((now - ctx->nat_probe_send_time) < NAT_PROBE_INTERVAL_MS) return;

    if (ctx->nat_probe_retries < NAT_PROBE_MAX_RETRIES) {

        ctx->nat_probe_send_time = now;

        /*
         * 重发 NAT_PROBE 探测包
         *
         * 协议：SIG_PKT_NAT_PROBE (0x07)
         * 包头: [type=0x07 | flags=0 | seq=重试次数]
         * 负载: 空
         *   - seq: 探测重试次数（递增）
         * 说明: 重试NAT探测，seq字段递增表示重试次数
         */
        struct sockaddr_in probe_addr = ctx->server_addr;
        probe_addr.sin_port = htons(ctx->probe_port);

        // 调试打印协议包信息
        printf(LA_F("Resend NAT_PROBE pkt to %s:%d, seq=%u, flags=0, len=0", 0, 0),
               inet_ntoa(probe_addr.sin_addr), ntohs(probe_addr.sin_port),
               ctx->nat_probe_retries + 1);
        
        udp_send_packet(s->sock, &probe_addr, SIG_PKT_NAT_PROBE, 0, ++ctx->nat_probe_retries, NULL, 0);

        print("V:", LA_F("NAT_PROBE: %s %d/%d %s %s:%d", LA_F92, 336),
                        LA_W("NAT probe retry", LA_W44, 57),
                        (int)ctx->nat_probe_retries, NAT_PROBE_MAX_RETRIES,
                        LA_W("to", LA_W99, 135),
                        inet_ntoa(probe_addr.sin_addr), ctx->probe_port);
    }
    // 最大重试失败，探测端口无应答，无法确定 NAT 类型
    else if (ctx->nat_probe_retries > NAT_PROBE_MAX_RETRIES) return;
    else { ++ctx->nat_probe_retries;

        ctx->nat_probe_retries = -2/* 探测超时 */;
        s->nat_type = P2P_NAT_TIMEOUT;

        print("W:", LA_F("NAT_PROBE: %s", LA_F91, 335), LA_W("NAT probe timeout, type unknown", LA_W46, 59));
    }
}


#pragma clang diagnostic pop
