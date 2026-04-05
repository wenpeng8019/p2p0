/*
 * COMPACT 模式信令实现（UDP 无状态）
 */
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#define MOD_TAG "COMPACT"

#include "p2p_internal.h"
#include "p2p_probe.h"

#define ONLINE_INTERVAL_MS              1000    /* ONLINE/SYNC0 重发间隔 */
#define SYNC_INTERVAL_MS                500     /* SYNC 重发间隔 */
#define MAX_SIG_ATTEMPTS                10      /* 最大 ONLINE/SYNC0 重发次数 */
#define REGISTER_KEEPALIVE_INTERVAL_MS  20000   /* ONLINE 状态保活重注册间隔（防服务器超时清除槽位） */
#define TRICKLE_BATCH_MS                1000    /* TURN trickle 攒批窗口（多个 TURN 响应在此窗口内合并为一个包） */
#define MAX_CANDS_PER_PACKET            10      /* 每个 SYNC 包最大候选数 */
#define NAT_PROBE_MAX_RETRIES           3       /* NAT_PROBE 最大发送次数 */
#define NAT_PROBE_INTERVAL_MS           1000    /* NAT_PROBE 重发间隔 */
#define SYNCING_TIMEOUT_MS              30000   /* SYNCING 状态超时（30秒），防止永久停留 */

#define MSG_REQ_INTERVAL_MS             500     /* MSG_REQ 重发间隔 */
#define MSG_REQ_MAX_RETRIES             5       /* MSG_REQ 最大重发次数，超出后报超时失败 */

///////////////////////////////////////////////////////////////////////////////

#define TASK_ONLINE                     "ONLINE"
#define TASK_SYNCING                    "SYNCING"
#define TASK_SYNC_REMOTE                "SYNC REMOTE"
#define TASK_NAT_PROBE                  "NAT PROBE"
#define TASK_RPC                        "RPC"

/*
 * 解析 SYNC 负载，追加到 session 的 remote_cands[]
 * 注意：这里对方的候选列表顺序并未按对方原始顺序排序，而是 FIFO 追加到 remote_cands[] 中
 *
 * 格式: [session_id(P2P_SESS_ID_PSZ)][base_index(1)][candidate_count(1)][candidates(N*23)]
 */
static void unpack_remote_candidates(struct p2p_session *s, const uint8_t *payload, int cand_cnt) {

    assert(cand_cnt && s->remote_cand_cnt + cand_cnt <= s->remote_cand_cap);

    int offset = P2P_SESS_ID_PSZ + 2;  // 第一个 candidates 列表的起始位置
    p2p_remote_candidate_entry_t*c;

    // 对于 sync0, 即由 server 维护的双方握手包
    if (payload[P2P_SESS_ID_PSZ]/* base_index */ == 0) {

        unpack_candidate(c = &s->remote_cands[0], payload + offset);
        if (s->remote_cand_cnt == 0) s->remote_cand_cnt = 1;

        // 其首个地址候选（idx=0）肯定有效，即肯定是（服务器观察到的）对方公网地址
        if ((p2p_cand_type_t)c->type != P2P_CAND_SRFLX) {

            print("W:", LA_F("%s: unexpected non-srflx cand in sync0, treating as srflx\n", LA_F192, 192),
                  TASK_SYNC_REMOTE);

            c->type = P2P_CAND_SRFLX;
        }

        print("I:", LA_F("%s: sync0 srflx cand[%d]<%s:%d>%s\n", LA_F136, 136),
                        TASK_SYNC_REMOTE, 0, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port),
                        s->inst->cfg.test_ice_srflx_off ? " (disabled)" : "");

        if (s->inst->cfg.test_ice_srflx_off) --s->remote_cand_cnt;
        else { s->remote_srflx_cnt++;

            // 这里启动打洞需要依赖于信令服务器的 REGISTER_ACK 包中携带的 feature_relay 标志
            // + 该标志用于决定所使用的冷打洞机制，如果冷打洞不依赖服务器中转，则打洞可以不依赖 REGISTER_ACK 包
            if (s->inst->sig_compact_ctx.state >= SIGNAL_COMPACT_WAIT_SYNC0_ACK && nat_punch(s, 0) != E_NONE) {
                print("W:", LA_F("%s: punch remote cand[%d]<%s:%d> failed\n", LA_F137, 137),
                    TASK_SYNC_REMOTE, 0, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
            }
        }

        --cand_cnt; offset += (int)sizeof(p2p_candidate_t);
    }
    // 如果 sync0 以外的其他 info 包先到达，则需要保留 cand[0] 给对方（在 sync0 中的）的公网地址
    else if (s->remote_cand_cnt == 0 && !s->inst->cfg.test_ice_srflx_off) {
        memset(&s->remote_cands[0], 0, sizeof(p2p_remote_candidate_entry_t));
        s->remote_cand_cnt = 1;
    }

    for (int i = 0, idx; i < cand_cnt; i++, offset += (int)sizeof(p2p_candidate_t)) {

        unpack_candidate(c = &s->remote_cands[idx = s->remote_cand_cnt], payload + offset);
        int dup_idx = p2p_find_remote_candidate_by_addr(s, &c->addr);
        if (dup_idx >= 0) {
            if (s->remote_cands[dup_idx].type == P2P_CAND_PRFLX && c->type != P2P_CAND_PRFLX) {
                s->remote_cands[dup_idx].type = c->type;
                s->remote_cands[dup_idx].priority = c->priority;
                print("I:", LA_F("%s: promoted prflx cand[%d]<%s:%d> → %s\n", LA_F601, 601),
                      TASK_SYNC_REMOTE, dup_idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port),
                      p2p_candidate_type_str(c->type));
            } else {
                print("V:", LA_F("%s: duplicate remote cand<%s:%d> from signaling, skipped\n", LA_F106, 106),
                      TASK_SYNC_REMOTE, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
            }
            continue;
        }

        const char* type_str; uint16_t* cand_cnt_ptr; bool opt_off = false;
        if (c->type == P2P_CAND_HOST) { type_str = "host"; cand_cnt_ptr = &s->remote_host_cnt; opt_off = s->inst->cfg.test_ice_host_off; }
        else if (c->type == P2P_CAND_SRFLX) { type_str = "srflx"; cand_cnt_ptr = &s->remote_srflx_cnt; opt_off = s->inst->cfg.test_ice_srflx_off; }
        else if (c->type == P2P_CAND_RELAY) { type_str = "relay"; cand_cnt_ptr = &s->remote_relay_cnt; opt_off = s->inst->cfg.test_ice_relay_off; }
        else { --s->remote_cand_cnt;
            print("E:", LA_F("%s: unexpected remote cand type %d, skipped\n", LA_F193, 193),
                  TASK_SYNC_REMOTE, c->type);
            continue;
        }

        if (opt_off) {
            print("I:", LA_F("%s: remote %s cand[%d]<%s:%d> (disabled)\n", LA_F423, 423),
                  TASK_SYNC_REMOTE, type_str, idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
            continue;
        }

        ++s->remote_cand_cnt; ++*cand_cnt_ptr;

        print("I:", LA_F("%s: remote %s cand[%d]<%s:%d> accepted\n", LA_F153, 153),
              TASK_SYNC_REMOTE, type_str, idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));

        if (s->inst->sig_compact_ctx.state < SIGNAL_COMPACT_WAIT_SYNC0_ACK) continue;

        if (nat_punch(s, idx) != E_NONE)
            print("E:", LA_F("%s: punch remote cand[%d]<%s:%d> failed\n", LA_F137, 137),
                  TASK_SYNC_REMOTE, idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
    }
}

/* 一个 SYNC 包所承载的候选数量（单位）
 * + 这里 10（字节）表示 SYNC 负载头：[session_id(P2P_SESS_ID_PSZ)][base_index(1)][candidate_count(1)] = 10 字节
 *   负载头后面的剩余空间就是候选列表，通过预定义、和 MTU 上限共同限制计算得出该单位大小
 */
#define SYNC_CAND_UNIT \
    (((P2P_MAX_PAYLOAD - P2P_SESS_ID_PSZ - 2) / (int)sizeof(p2p_candidate_t)) < MAX_CANDS_PER_PACKET \
     ? ((P2P_MAX_PAYLOAD - P2P_SESS_ID_PSZ - 2) / (int)sizeof(p2p_candidate_t)) \
     : MAX_CANDS_PER_PACKET)

/*
 * 构建 SYNC 的候选队列，返回 payload 总长度
 *
 * 格式: [session_id(P2P_SESS_ID_PSZ)][base_index(1)][candidate_count(1)][candidates(N*23)]
 */
static int pack_local_candidates(struct p2p_session *s, uint16_t seq, uint8_t *payload, uint8_t *r_flags) {

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    int base_index, cnt;

    // 对于 trickle 候选队列的前面部分，也就是首批一次性发送的候选队列
    // + 此时 SYNC 包的候选队列数量都是以 SYNC_CAND_UNIT 为单位的
    if (seq < ctx->trickle_seq_base) {

        const int cand_unit = SYNC_CAND_UNIT;
        base_index = ctx->candidates_cached + (int)(seq - 1) * cand_unit;
        if (base_index > s->local_cand_cnt) base_index = s->local_cand_cnt;

        int remaining = (int)ctx->trickle_idx_base - base_index;
        if (remaining > cand_unit) cnt = cand_unit;
        else cnt = remaining;
    }
    // 对于 trickle 候选队列部分
    else {
        base_index = ctx->trickle_idx_base;
        for(int i=ctx->trickle_seq_base; i<seq; i++)
            base_index += ctx->trickle_queue[i];
        cnt = ctx->trickle_queue[seq];
    }

    // 对于最后一个 SYNC 包，设置 FIN 标志
    if (seq == 16 || (!s->turn_pending && base_index + cnt >= s->local_cand_cnt)) {
        *r_flags |= SIG_SYNC_FLAG_FIN;
    }

    payload[P2P_SESS_ID_PSZ] = (uint8_t)base_index;
    payload[P2P_SESS_ID_PSZ + 1] = (uint8_t)cnt;

    int offset = P2P_SESS_ID_PSZ + 2; // 负载头：[session_id(P2P_SESS_ID_PSZ)][base_index(1)][candidate_count(1)]
    for (int i = 0; i < cnt; i++) { int idx = base_index + i;
        offset += pack_candidate(&s->local_cands[idx], payload + offset);
    }

    return offset;
}

/*
 * 向信令服务器发送 ONLINE（上线登录）请求
 *
 * 包头: [type=SIG_PKT_ONLINE | flags=0 | seq=0]
 * 负载: [local_peer_id(32)][instance_id(4)]
 *   - instance_id: 本次 connect() 的实例 ID（网络字节序，32位，必须非 0）
 * 注：候选地址通过后续 SYNC0 包单独提交
 */
static void send_online(struct p2p_session *s) {
    const char* PROTO = "ONLINE";

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;
    assert(ctx->state == SIGNAL_COMPACT_WAIT_ONLINE_ACK);

    // local_peer_id（32 字节，超过部分截断，不足部分补零）
    int n = P2P_PEER_ID_MAX;
    uint8_t payload[SIG_PKT_ONLINE_PSZ];
    memset(payload, 0, n);
    memcpy(payload, ctx->local_peer_id, strlen(ctx->local_peer_id));

    // instance_id（4 字节大端序）
    nwrite_l(payload + n, ctx->instance_id); n += 4;

    print("V:", LA_F("%s sent, inst_id=%u\n", LA_F596, 596), PROTO, ctx->instance_id);

    ret_t ret = p2p_udp_send_packet(s, &ctx->server_addr, SIG_PKT_ONLINE, 0, 0, payload, n);
    if (ret < 0)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F488, 488), 
              PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F489, 489),
               PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), n);
}

/*
 * 向信令服务器提交 SYNC0（首批候选 + 指定对端）
 *
 * 包头: [type=SIG_PKT_SYNC0 | flags=0 | seq=0]
 * 负载: [auth_key(8)][remote_peer_id(32)][candidate_count(1)][candidates(N*23)]
 *   - auth_key: 坥自 ONLINE_ACK 的客户端-服务器认证令牌
 *   - remote_peer_id: 目标对端 ID
 *   - candidate_count: 首批候选数量（最多 candidates_cached 个）
 */
static void send_compact_sync0(struct p2p_session *s) {
    const char* PROTO = "SYNC0";

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;
    assert(ctx->state == SIGNAL_COMPACT_WAIT_SYNC0_ACK);
    assert(ctx->auth_key != 0);

    uint8_t payload[P2P_MAX_PAYLOAD];
    int n = 0;

    // auth_key（8 字节大端序）
    nwrite_ll(payload, ctx->auth_key); n += 8;

    // remote_peer_id（32 字节，不足补零）
    memset(payload + n, 0, P2P_PEER_ID_MAX);
    memcpy(payload + n, ctx->remote_peer_id, strnlen(ctx->remote_peer_id, P2P_PEER_ID_MAX));
    n += P2P_PEER_ID_MAX;

    // candidate_count + candidates
    int cand_cnt = ctx->candidates_cached;
    payload[n++] = (uint8_t)cand_cnt;
    for (int i = 0; i < cand_cnt; i++) {
        n += pack_candidate(&s->local_cands[i], payload + n);
    }

    print("V:", LA_F("%s sent, auth_key=%" PRIu64 ", remote='%.32s', cands=%d\n", LA_F58, 58),
          PROTO, ctx->auth_key, ctx->remote_peer_id, cand_cnt);

    ret_t ret = p2p_udp_send_packet(s, &ctx->server_addr, SIG_PKT_SYNC0, 0, 0, payload, n);
    if (ret < 0)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F488, 488), 
              PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F489, 489),
               PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), n);
}

/*
 * 在首次收到 SYNC 包，且已经收到 REGISTER_ACK 的情况下，发送剩余候选队列和 FIN 包给对方
 * 注意：首次收到的 SYNC 包，可能是服务器下发的 seq=0 的 SYNC 包；
 *       也可能是对方发送的 seq!=1 的 SYNC 包（在并发网络状况下，对方的 SYNC 包可能先到达）
 *
 * 包头: [type=SIG_PKT_SYNC | flags=见下 | seq=1-16]
 * 负载: [session_id(P2P_SESS_ID_PSZ)][base_index(1)][candidate_count(1)][candidates(N*7)]
 *   - session_id: 会话 ID（网络字节序）
 *   - base_index: 本批候选的起始索引
 *   - candidate_count: 本批候选数量
 *   - candidates: 每个候选 7 字节 [type(1)+ip(4)+port(2)]
 *   - flags: SIG_PKT_FLAG_MORE_CAND (0x02) 表示后续还有包
 *           SIG_PKT_FLAG_FIN (0x01) 表示发送完毕
 */
static void send_rest_candidates_and_fin(struct p2p_session *s) {
    const char* PROTO = "SYNC";

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;
    assert(ctx->state == SIGNAL_COMPACT_SYNCING && ctx->session_id);

    // 计算剩余候选数量
    // + 注意，此时的剩余数量，是此刻的剩余数量；因为候选队列可能是动态增加的（如 TURN Allocate 后追加的 Relay 候选）
    int cand_remain = s->local_cand_cnt - ctx->candidates_cached;
    if (cand_remain < 0) cand_remain = 0;

    // 至少发送一个包（即使没有剩余候选），以确保对方收到 FIN 信号
    const int cand_unit = SYNC_CAND_UNIT;
    int pkt_cnt = cand_remain == 0 ? 1 : (cand_remain + cand_unit - 1) / cand_unit; // 单位 ceil 取整
    if (pkt_cnt >= 16) { pkt_cnt = 16;                                              // 目前协议设计最多支持 16 个包（seq=1-16）
        ctx->candidates_mask = 0xFFFFu;
        ctx->trickle_seq_base = 17;                                                 // 无 trickle 空间（seq 溢出区）
    } else {
        ctx->trickle_seq_base = (uint8_t)(pkt_cnt + 1);                             // trickle_seq_base 从下一个 seq 开始
        ctx->candidates_mask = (uint16_t)((1u << pkt_cnt) - 1u);                    // 计算候选确认窗口的 mask
    }
    ctx->trickle_seq_next = ctx->trickle_seq_base;                                   // 初始 trickle_seq_next = trickle_seq_base

    // 记录首批候选的分界点（trickle 候选从此索引开始）
    ctx->trickle_idx_base = (uint16_t)s->local_cand_cnt;
    memset(ctx->trickle_queue, 0, sizeof(ctx->trickle_queue));

    // 初始重置确认状态
    ctx->candidates_acked = 0;

    // session_id 所有包相同，只写一次
    uint8_t payload[P2P_MAX_PAYLOAD];
    nwrite_l(payload, ctx->session_id);

    for (uint16_t seq = 1; seq <= (uint16_t)pkt_cnt; seq++) {

        uint8_t flags = 0; int payload_len = pack_local_candidates(s, seq, payload, &flags);

        ret_t ret = p2p_udp_send_packet(s, &ctx->server_addr, SIG_PKT_SYNC, flags, seq, payload, payload_len);
        if (ret < 0)
            print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F488, 488), 
                  PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
        else
            printf(LA_F("[UDP] %s send to %s:%d, seq=%u, flags=0x%02x, len=%d\n", LA_F396, 396),
                   PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
                   seq, flags, payload_len);
    }

    // 首批发完后，如果还有 TURN 异步收集未完成，启动攒批计时器
    if (s->turn_pending > 0)
        ctx->trickle_last_pack_time = P_tick_ms();

    print("V:", LA_F("%s sent, total=%d (ses_id=%" PRIu32 ")\n", LA_F62, 62), PROTO, pkt_cnt, ctx->session_id);
}

/*
 * 向对方发送新收集到的中继候选（Trickle candidates）
 *
 * 行为：
 *   1. 在现有候选窗口后追加一个 SYNC 包（seq = 已发包数 + 1）
 *   2. 扩展 candidates_mask，将新包纳入确认窗口
 *   3. 当所有 TURN 候选收集完毕（turn_pending==0）且候选已全部打包时，pack 自动附带 FIN
 *   4. 如果当前状态已到 READY（前序包均已确认），回退到 SYNCING 以等待新包确认
 *   5. 支持攒批：多个 TURN 响应在 TRICKLE_BATCH_MS 窗口内合并为一个包
 */

/* 将攒批累积的 trickle 候选打包发送（trickle_turn 和 tick flush 共用） */
static void send_trickle_candidates(struct p2p_session *s) {
    const char* PROTO = "SYNC(trickle)";

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;
    uint16_t seq = ctx->trickle_seq_next;
    assert(seq <= 16 && ctx->trickle_queue[seq] > 0);

    // 扩展确认窗口
    ctx->candidates_mask |= (uint16_t)(1u << (seq - 1));
    ctx->trickle_seq_next++;
    if (ctx->state == SIGNAL_COMPACT_READY) ctx->state = SIGNAL_COMPACT_SYNCING;

    int new_cands = ctx->trickle_queue[seq];

    uint8_t payload[P2P_MAX_PAYLOAD];
    nwrite_l(payload, ctx->session_id);

    uint8_t flags = 0;
    int payload_len = pack_local_candidates(s, seq, payload, &flags);

    print("I:", LA_F("%s: trickled %d cand(s), seq=%u (ses_id=%" PRIu32 ")\n", LA_F187, 187),
          PROTO, new_cands, seq, ctx->session_id);

    ret_t ret = p2p_udp_send_packet(s, &ctx->server_addr, SIG_PKT_SYNC, flags, seq, payload, payload_len);
    if (ret < 0)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F488, 488), 
              PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=%u, flags=0x%02x, len=%d\n", LA_F396, 396),
               PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
               seq, flags, payload_len);

    ctx->last_send_time = P_tick_ms();
    ctx->trickle_last_pack_time = s->turn_pending > 0 ? ctx->last_send_time : 0;
}

static bool compact_wait_stun_candidates(struct p2p_session *s) {
    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;
    return ctx->state == SIGNAL_COMPACT_SYNCING && ctx->candidates_mask == 0 && s->stun_pending > 0;
}

/*
 * 周期将未确认的 SYNC 包重发给对方
 *
 * 包头: [type=SIG_PKT_SYNC | flags=见下 | seq=1-16]
 * 负载: [session_id(P2P_SESS_ID_PSZ)][base_index(1)][candidate_count(1)][candidates(N*7)]
 * 说明: 重发所有未收到 ACK 的 SYNC 包
 */
static void resend_rest_candidates_and_fin(struct p2p_session *s) {
    const char* PROTO = "SYNC";

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;
    assert(ctx->state == SIGNAL_COMPACT_SYNCING && ctx->session_id);
    assert((ctx->candidates_acked & ctx->candidates_mask) != ctx->candidates_mask);

    // session_id 所有包相同，只写一次
    uint8_t payload[P2P_MAX_PAYLOAD];
    nwrite_l(payload, ctx->session_id);

    uint16_t seq = 1; int pkt_cnt = 0;
    for (; seq <= 16; seq++) {
        uint16_t bit = (uint16_t)(1u << (seq - 1));
        if ((ctx->candidates_mask & bit) == 0) break;           // 遇到第一个 0 就可以停止循环（mask 是低位连续段，高位全为 0）
        if ((ctx->candidates_acked & bit) != 0) continue;

        uint8_t flags = 0;
        int payload_len = pack_local_candidates(s, seq, payload, &flags);

        ret_t ret = p2p_udp_send_packet(s, &ctx->server_addr, SIG_PKT_SYNC, flags, seq, payload, payload_len);
        if (ret < 0)
            print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F488, 488), 
                  PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
        else
            printf(LA_F("[UDP] Resend %s to %s:%d, seq=%u, flags=0x%02x, len=%d\n", LA_F401, 401),
                   PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
                   seq, flags, payload_len);
        pkt_cnt++;
    }

    if (pkt_cnt) print("V:", LA_F("%s resent, %d/%d\n", LA_F51, 51), PROTO, pkt_cnt, seq - 1);
}

/*
 * 通过服务器向对端发送 RPC 消息请求
 *
 * 包头: [type=SIG_PKT_MSG_REQ | flags=0 | seq=0]
 * 负载: [session_id(P2P_SESS_ID_PSZ)][sid(2)][msg(1)][data(N)]
 *   - session_id: 本端会话 ID（来自 REGISTER_ACK）
 *   - sid: 序列号（2字节，网络字节序），用于匹配响应
 *   - msg: 消息 ID（1字节，用户自定义）
 *   - data: 消息数据（可选）
 */
static void send_rpc_req(struct p2p_session *s) {
    const char* PROTO = "MSG_REQ";

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    uint8_t payload[P2P_SESS_ID_PSZ + 3 + P2P_MSG_DATA_MAX]; int n = 0;
    nwrite_l(payload + n, ctx->session_id); n += P2P_SESS_ID_PSZ;
    nwrite_s(payload + n, ctx->req_sid); n += 2;
    payload[n++] = ctx->req_msg;
    if (ctx->req_data_len > 0) {
        memcpy(payload + n, ctx->req_data, (size_t)ctx->req_data_len);
        n += ctx->req_data_len;
    }

    print("V:", LA_F("%s sent, sid=%u, msg=%u, size=%d\n", LA_F450, 450),
          PROTO, ctx->req_sid, ctx->req_msg, ctx->req_data_len);

    ret_t ret = p2p_udp_send_packet(s, &ctx->server_addr, SIG_PKT_MSG_REQ, 0, 0, payload, n);
    if (ret < 0)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F488, 488), 
              PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F489, 489),
               PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), n);
}

/*
 * （对端）通过服务器向源端回复 RPC 消息响应
 *
 * 包头: [type=SIG_PKT_MSG_RESP | flags=0 | seq=0]
 * 负载: [session_id(P2P_SESS_ID_PSZ)][sid(2)][code(1)][data(N)]
 *   - session_id: A端的会话 ID（网络字节序）
 *   - sid: 序列号，必顾与 MSG_REQ 中的 sid 一致
 *   - code: 响应码
 *   - data: 响应数据
 */
static void send_rpc_resp(struct p2p_session *s) {
    const char* PROTO = "MSG_RESP";

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    uint8_t payload[P2P_SESS_ID_PSZ + 2 + 1 + P2P_MSG_DATA_MAX]; int n = 0;
    nwrite_l(payload, ctx->resp_session_id); n = P2P_SESS_ID_PSZ;
    nwrite_s(payload + n, ctx->resp_sid); n += 2;
    payload[n++] = ctx->resp_code;
    if (ctx->resp_data_len > 0) {
        memcpy(payload + n, ctx->resp_data, (size_t)ctx->resp_data_len);
        n += ctx->resp_data_len;
    }

    print("V:", LA_F("%s: sent, sid=%u, code=%u, size=%d\n", LA_F165, 165),
          PROTO, ctx->resp_sid, ctx->resp_code, ctx->resp_data_len);

    ret_t ret = p2p_udp_send_packet(s, &ctx->server_addr, SIG_PKT_MSG_RESP, 0, 0, payload, n);
    if (ret < 0)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F488, 488), 
              PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F489, 489),
               PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), n);
}

/*
 * 发送 NAT_PROBE 探测包
 * 即向服务器探测端口发送空包，服务器观察源地址来判断 NAT 类型
 *
 * 包头: [type=SIG_PKT_NAT_PROBE | flags=0 | seq=探测重试次数]
 * 负载: 空
 */
static void send_nat_probe(struct p2p_session *s) {
    const char* PROTO = "NAT_PROBE";

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    struct sockaddr_in probe_addr = ctx->server_addr;
    probe_addr.sin_port = htons(ctx->probe_port);

    print("V:", LA_F("%s sent, seq=%u\n", LA_F60, 60), PROTO, ctx->nat_probe_retries);

    ret_t ret = p2p_udp_send_packet(s, &probe_addr, SIG_PKT_NAT_PROBE, 0, ctx->nat_probe_retries, NULL, 0);
    if (ret < 0)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F488, 488), 
              PROTO, inet_ntoa(probe_addr.sin_addr), ctx->probe_port, E_EXT_CODE(ret));
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=%u, flags=0, len=0\n", LA_F395, 395),
               PROTO, inet_ntoa(probe_addr.sin_addr), ctx->probe_port, ctx->nat_probe_retries);
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 初始化信令上下文
 */
void p2p_signal_compact_init(p2p_signal_compact_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = SIGNAL_COMPACT_INIT;
}

static void reset_peer(p2p_signal_compact_ctx_t *ctx) {

    ctx->peer_online = false;

    // 清理发给对端的候选及其确认状态
    ctx->candidates_mask = 0;
    ctx->candidates_acked = 0;
    ctx->trickle_seq_base = 1;
    ctx->trickle_seq_next = 1;
    ctx->trickle_idx_base = 0;
    memset(ctx->trickle_queue, 0, sizeof(ctx->trickle_queue));
    ctx->trickle_last_pack_time = 0;

    // 清理从对端收到的候选状态
    ctx->remote_candidates_0 = false;
    ctx->remote_candidates_mask = 0;
    ctx->remote_candidates_done = 0;
    ctx->remote_addr_notify_seq = 0;

    // 清理 MSG RPC 状态
    ctx->rpc_last_sid = 0;
    ctx->req_sid = 0;
    ctx->req_state = 0;
    ctx->resp_sid = 0;
    ctx->resp_state = 0;
    ctx->resp_session_id = 0;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 客户端上线（发送 ONLINE，建立 client↔server 关系，获取 auth_key）
 *
 * 包头: [type=SIG_PKT_ONLINE | flags=0 | seq=0]
 * 负载: [local_peer_id(32)][instance_id(4)]
 *   - local_peer_id: 本端 ID（32 字节，不足补零）
 *   - instance_id:   本次上线实例 ID（非零，用于服务器区分重启会话）
 */
ret_t p2p_signal_compact_online(struct p2p_session *s, const char *local_peer_id,
                                const struct sockaddr_in *server) {

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;
    P_check(ctx->state == SIGNAL_COMPACT_INIT, return E_NONE_CONTEXT;)

    ctx->server_addr = *server;

    // 每次 online() 生成新的实例 ID（加密安全随机数），用于服务器区分重新连接会话
    uint32_t rid = ctx->instance_id;
    while (rid == ctx->instance_id) rid = P_rand32();
    ctx->instance_id = rid;

    memset(ctx->local_peer_id, 0, sizeof(ctx->local_peer_id));
    strncpy(ctx->local_peer_id, local_peer_id, P2P_PEER_ID_MAX - 1);
    ctx->local_peer_id[P2P_PEER_ID_MAX - 1] = '\0';

    ctx->state = SIGNAL_COMPACT_WAIT_ONLINE_ACK;
    send_online(s);
    ctx->sig_attempts = 1;
    ctx->last_send_time = P_tick_ms();

    return E_NONE;
}

/*
 * 客户端下线（发送 OFFLINE，清理服务器上的全部状态，回到 INIT）
 *
 * 包头: [type=SIG_PKT_OFFLINE | flags=0 | seq=0]
 * 负载: [auth_key(8)]
 */
ret_t p2p_signal_compact_offline(struct p2p_session *s) {
    const char* PROTO = "OFFLINE";

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;
    if (ctx->state == SIGNAL_COMPACT_INIT) return E_NONE;

    uint8_t payload[SIG_PKT_OFFLINE_PSZ];
    nwrite_ll(payload, ctx->auth_key);

    print("V:", LA_F("%s sent, inst_id=%u\n", LA_F596, 596), PROTO, ctx->instance_id);

    ret_t ret = p2p_udp_send_packet(s, &ctx->server_addr, SIG_PKT_OFFLINE, 0, 0, payload, (int) sizeof(payload));
    if (ret < 0)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F488, 488),
              PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
    else {
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F489, 489),
               PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
               (int)sizeof(payload));

        for (int n = 2; n--;) { // 最多重试 2 次，确保服务器收到注销请求
            P_usleep(50 * 1000);
            p2p_udp_send_packet(s, &ctx->server_addr, SIG_PKT_OFFLINE, 0, 0, payload, (int) sizeof(payload));
        }
    }

    p2p_signal_compact_init(ctx);
    return E_NONE;
}

/*
 * 建立与对端的会话（发送 SYNC0，建立 client↔peer 关系，获取 session_id）
 *
 * 包头: [type=SIG_PKT_SYNC0 | flags=0 | seq=0]
 * 负载: [auth_key(8)][remote_peer_id(32)][candidate_count(1)][candidates(N*23)]
 *   - auth_key:        ONLINE_ACK 中分配的客户端令牌
 *   - remote_peer_id:  目标对端 ID（32 字节，不足补零）
 *   - candidates:      首批本地候选（最多 candidates_cached 个）
 * 注：若状态为 WAIT_ONLINE_ACK，仅存储 remote_peer_id，SYNC0 在收到 ONLINE_ACK 后自动触发
 */
ret_t p2p_signal_compact_connect(struct p2p_session *s, const char *remote_peer_id) {

    P_check(remote_peer_id && remote_peer_id[0], return E_INVALID;)

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;
    if (ctx->state == SIGNAL_COMPACT_INIT) {
        return E_NONE_CONTEXT;  // 还没调用 online()
    }

    // 幂等：相同 remote 则成功，不同则忙
    if (ctx->remote_peer_id[0]) {
        return strncmp(ctx->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX) == 0 ? E_NONE : E_BUSY;
    }

    strncpy(ctx->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX - 1);
    ctx->remote_peer_id[P2P_PEER_ID_MAX - 1] = '\0';

    // ONLINE_ACK 已收到，立即发 SYNC0
    if (ctx->state == SIGNAL_COMPACT_ONLINE) {
        ctx->state = SIGNAL_COMPACT_WAIT_SYNC0_ACK;
        send_compact_sync0(s);
        ctx->sig_attempts = 1;
        ctx->last_send_time = P_tick_ms();
    }

    return E_NONE;
}

/*
 * 断开与对端的会话（发送 OFFLINE，清理 peer 会话状态，回到 ONLINE）
 *
 * 包头: [type=SIG_PKT_OFFLINE | flags=0 | seq=0]
 * 负载: [auth_key(8)]
 *   - auth_key: ONLINE_ACK 中分配的客户端令牌，服务器据此查找并释放配对槽位
 * 注：若尚在 WAIT_ONLINE_ACK 状态（auth_key 未分配），仅清除 remote_peer_id，不发包
 */
ret_t p2p_signal_compact_disconnect(struct p2p_session *s) {
    const char* PROTO = "OFFLINE";

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    if (!ctx->remote_peer_id[0]) return E_NONE;  // 没有建立过配对

    // 尚未完成配对（WAIT_SYNC0_ACK 之前）：仅清除 remote，无需通知服务器
    if (ctx->state <= SIGNAL_COMPACT_WAIT_ONLINE_ACK) {
        memset(ctx->remote_peer_id, 0, sizeof(ctx->remote_peer_id));
        return E_NONE;
    }

    uint8_t payload[SIG_PKT_OFFLINE_PSZ];
    nwrite_ll(payload, ctx->auth_key);

    print("V:", LA_F("%s sent, inst_id=%u\n", LA_F596, 596), PROTO, ctx->instance_id);

    ret_t ret = p2p_udp_send_packet(s, &ctx->server_addr, SIG_PKT_OFFLINE, 0, 0, payload,
                                    (int) sizeof(payload));
    if (ret < 0)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F488, 488),
              PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
    else {
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F489, 489),
               PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
               (int)sizeof(payload));
    }

    // 清理 peer 会话状态，回到 ONLINE（保留 auth_key，等待下次 connect()）
    memset(ctx->remote_peer_id, 0, sizeof(ctx->remote_peer_id));
    ctx->session_id = 0;
    ctx->peer_online = false;
    ctx->state = SIGNAL_COMPACT_ONLINE;
    reset_peer(ctx);

    return E_NONE;
}

/*
 * 本地候选异步补发（Trickle candidates，发送 SYNC 包追加 STUN/TURN 候选）
 *
 * 包头: [type=SIG_PKT_SYNC | flags=SIG_SYNC_FLAG_FIN | seq=1-16]
 * 负载: [session_id(P2P_SESS_ID_PSZ)][base_index(1)][candidate_count(1)][candidates(N*23)]
 *   - session_id:    会话 ID（SYNC0_ACK 中分配）
 *   - base_index:   本批候选在本端候选列表中的起始索引
 *   - candidates:   本批候选（最多 SYNC_CAND_UNIT 个）
 *   - FIN flag:     末包置位，通知对端本端候选列表已全部发送完毕
 * 注：支持 TRICKLE_BATCH_MS 窗口攒批，多个 STUN/TURN 响应合并为一个包后发送
 */
void p2p_signal_compact_trickle_candidate(struct p2p_session *s) {
    
    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;
    if (ctx->state != SIGNAL_COMPACT_SYNCING && ctx->state != SIGNAL_COMPACT_READY) return;
    if (!ctx->session_id) return;

    if (ctx->state == SIGNAL_COMPACT_SYNCING && ctx->candidates_mask == 0) {
        if (s->stun_pending > 0) {
            print("V:", LA_F("%s: waiting for STUN candidates, stun_pending=%d\n", LA_F569, 569),
                  TASK_SYNCING, s->stun_pending);
            return;
        }

        send_rest_candidates_and_fin(s);
        ctx->last_send_time = P_tick_ms();
        return;
    }

    uint16_t seq = ctx->trickle_seq_next;
    if (seq > 16) {
        print("W:", LA_F("SYNC(trickle): seq overflow, cannot trickle more\n", LA_F279, 279));
        return;
    }

    // O(1) 累加：每次本地异步候选（STUN/TURN）带来 1 个新候选
    ctx->trickle_queue[seq]++;

    // 攒批间隔控制（固定窗口策略）
    uint64_t now = P_tick_ms();
    if (ctx->trickle_last_pack_time && tick_diff(now, ctx->trickle_last_pack_time) < TRICKLE_BATCH_MS) {
        print("V:", LA_F("SYNC(trickle): batching, queued %d cand(s) for seq=%u\n", LA_F278, 278),
              ctx->trickle_queue[seq], seq);
        return;
    }

    send_trickle_candidates(s);
}

/*
 * 通过 COMPACT 信令中转发送数据包（通用接口）
 *
 * 包头：[type=P2P_PKT_DATA/ACK/CRYPTO/REACH/CONN/CONN_ACK | flags|=P2P_RELAY_FLAG_SESSION | seq]
 * 负载：[session_id(P2P_SESS_ID_PSZ)][原始 payload]
 */
ret_t p2p_signal_compact_relay(struct p2p_session *s,
                               uint8_t type, uint8_t flags, uint16_t seq,
                               const void *payload, uint16_t payload_len) {

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;
    P_check(!payload_len || payload, return E_INVALID;)
    P_check(ctx->feature_relay, return E_NO_SUPPORT;)
    P_check(ctx->session_id, return E_NONE_CONTEXT;)

    // 构造完整负载：[session_id(P2P_SESS_ID_PSZ)][原始 payload]
    uint8_t relay_payload[P2P_MAX_PAYLOAD];
    if (payload_len + (int)P2P_SESS_ID_PSZ > P2P_MAX_PAYLOAD) {
        print("E:", LA_F("COMPACT relay payload too large: %d", LA_F212, 212), payload_len);
        return E_OUT_OF_CAPACITY;
    }
    
    nwrite_l(relay_payload, ctx->session_id);
    if (payload_len > 0 && payload)
        memcpy(relay_payload + P2P_SESS_ID_PSZ, payload, payload_len);

    // 发送包，自动添加 P2P_RELAY_FLAG_SESSION 标志
    ret_t ret = p2p_udp_send_packet(s, &ctx->server_addr, type,
                                    flags | P2P_RELAY_FLAG_SESSION, seq,
                                    relay_payload, (int)P2P_SESS_ID_PSZ + payload_len);
    
    if (ret >= 0) {
        print("V:", LA_F("COMPACT relay: type=0x%02x, seq=%u (session_id=%" PRIu32 ")", LA_F214, 214),
              type, seq, ctx->session_id);
    } else {
        print("E:", LA_F("COMPACT relay send failed: type=0x%02x, ret=%d", LA_F213, 213),
              type, E_EXT_CODE(ret));
    }
    
    return ret >= 0 ? E_NONE : ret;
}

/* 
 * COMPACT 信令会话隔离验证（防止旧会话重传包污染新会话）
 */
bool p2p_signal_compact_relay_validation(struct p2p_session *s,
                                         const uint8_t **payload, int *len,
                                         const char *proto_name) {
    // 至少需要 session_id(P2P_SESS_ID_PSZ)
    if (*len < (int)P2P_SESS_ID_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%d, need >=8)\n", LA_F101, 101), proto_name, *len);
        return false;
    }

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;
    uint32_t session_id = nget_l(*payload);

    // 验证 session_id 匹配
    if (session_id != ctx->session_id) {
        print("W:", LA_F("%s: session mismatch(local=%" PRIu32 ", pkt=%" PRIu32 ")\n", LA_F602, 602),
              proto_name, ctx->session_id, session_id);
        return false;
    }

    print("V:", LA_F("%s: session validated, len=%d (ses_id=%" PRIu32 ")\n", LA_F169, 169),
          proto_name, *len, session_id);

    // 跳过 session_id 头部
    *payload += P2P_SESS_ID_PSZ;
    *len -= (int)P2P_SESS_ID_PSZ;
    return true;
}

/*
 * 通过信令服务器向对端发起 RPC 消息请求（A 端）
 *
 * 包头: [type=SIG_PKT_MSG_REQ | flags=0 | seq=0]
 * 负载: [session_id(P2P_SESS_ID_PSZ)][sid(2)][msg(1)][data(N)]
 *   - session_id: 本端会话 ID（SYNC0_ACK 中分配，用于服务器路由到对端）
 *   - sid:        RPC 序列号（非零，循环自增，用于匹配响应）
 *   - msg:        应用层消息类型（1 字节，应用自定义）
 *   - data:       消息数据（可选，最多 P2P_MSG_DATA_MAX 字节）
 */
ret_t p2p_signal_compact_request(struct p2p_session *s,
                                 uint8_t msg, const void *data, int len) {

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    P_check(len == 0 || data, return E_INVALID;)
    P_check(len >= 0 && len <= P2P_MSG_DATA_MAX, return E_INVALID;)
    P_check(ctx->state >= SIGNAL_COMPACT_WAIT_SYNC0_ACK, return E_NONE_CONTEXT;)    // 未上线
    if (!ctx->feature_msg) {
        print("E:", LA_F("MSG RPC not supported by server\n", LA_F447, 447));
        return E_NO_SUPPORT;
    }
    
    if (ctx->req_state != 0) return E_BUSY;                                     // 已有挂起请求

    // 生成非零（循环）序列号
    static uint16_t _sid_bse = 0;
    uint16_t sid = ++_sid_bse;
    if (sid == 0) sid = ++_sid_bse;

    ctx->req_sid       = sid;
    ctx->req_msg           = msg;
    ctx->req_data_len  = len;
    if (len > 0) memcpy(ctx->req_data, data, (size_t)len);
    ctx->req_state     = 1/* waiting REQ_ACK */;

    send_rpc_req(s);
    ctx->req_retries = 0;
    ctx->req_send_time = P_tick_ms();
    return E_NONE;
}

/*
 * 通过信令服务器向请求端回复 RPC 消息响应（B 端）
 *
 * 包头: [type=SIG_PKT_MSG_RESP | flags=0 | seq=0]
 * 负载: [session_id(P2P_SESS_ID_PSZ)][sid(2)][code(1)][data(N)]
 *   - session_id: A 端会话 ID（来自 MSG_REQ 的 relay 包，用于服务器路由回 A 端）
 *   - sid:        RPC 序列号，必须与对应 MSG_REQ 的 sid 一致
 *   - code:       响应码（1 字节，应用自定义）
 *   - data:       响应数据（可选，最多 P2P_MSG_DATA_MAX 字节）
 */
ret_t p2p_signal_compact_response(struct p2p_session *s,
                                  uint8_t code, const void *data, int len) {
    const char* PROTO = "MSG_RESP";

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    P_check(len >= 0 && len <= P2P_MSG_DATA_MAX, return E_INVALID;)
    P_check(len == 0 || data, return E_INVALID;)
    if (!ctx->resp_sid) {
         print("E:", LA_F("%s: no rpc request\n", LA_F446, 446), PROTO);
        return E_INVALID;
    }

    // 缓存响应数据用于重发
    ctx->resp_code      = code;
    ctx->resp_data_len  = len;
    if (len > 0) memcpy(ctx->resp_data, data, (size_t)len);
    ctx->resp_state = 1/* waiting RESP_ACK */;

    send_rpc_resp(s);
    ctx->resp_retries = 0;
    ctx->resp_send_time = P_tick_ms();
    return E_NONE;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 处理 ONLINE_ACK，服务器上线确认
 *
 * 包头: [type=SIG_PKT_ONLINE_ACK | flags=见下 | seq=0]
 * 负载: [auth_key(8) | instance_id(4) | max_candidates(1) | public_ip(4) | public_port(2) | probe_port(2)]
 *   - auth_key: 客户端-服务器认证令牌（0=服务器拒绝登录，无可用槽位）
 *   - max_candidates: 服务器缓存的最大候选数量（0=不支持缓存）
 *   - public_ip/port: 客户端的公网地址（服务器观察到的 UDP 源地址）
 *   - probe_port: NAT 探测端口（0=不支持探测）
 *   - flags: SIG_ONACK_FLAG_RELAY (0x01) 表示服务器支持中继
 */
void compact_on_online_ack(struct p2p_session *s, uint16_t seq, uint8_t flags,
                              const uint8_t *payload, int len,
                              const struct sockaddr_in *from) {
    const char* PROTO = "ONLINE_ACK";

    printf(LA_F("[UDP] %s recv from %s:%d, seq=%u, flags=0x%02x, len=%d\n", LA_F391, 391),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), seq, flags, len);

    if (len < (int)SIG_PKT_ONLINE_ACK_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    // instance_id 先于 auth_key，可在验证state前快速过滤过期 ACK
    uint32_t ack_instance_id = 0;
    nread_l(&ack_instance_id, payload + 0);

    uint64_t ack_auth_key = nget_ll(payload + 4);
    if (ack_auth_key == 0) {
        print("E:", LA_F("%s: server rejected (no slot)\n", LA_F463, 463), PROTO);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    if (ctx->state != SIGNAL_COMPACT_WAIT_ONLINE_ACK) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)ctx->state);
        return;
    }

    // 验证 echo instance_id（确保 ACK 对应当前上线请求）
    if (ack_instance_id != ctx->instance_id) {
        print("V:", LA_F("%s: stale ACK(ack_inst=%u local_inst=%u), ignored\n", LA_F172, 172),
              PROTO, ack_instance_id, ctx->instance_id);
        return;
    }

    ctx->feature_relay = (flags & SIG_ONACK_FLAG_RELAY) != 0;      // 服务器是否支持数据中继转发
    ctx->feature_msg   = (flags & SIG_ONACK_FLAG_MSG) != 0;        // 服务器是否支持 MSG RPC
    uint8_t max_candidates = payload[12];  // payload: [instance_id(4)][auth_key(8)][max(1)][ip(4)][port(2)][probe(2)]

    // 根据服务器通告的 max_candidates 和 MTU 计算首批候选数量（SYNC0 使用）
    // SYNC0 负载头: [auth_key(8)][remote_peer_id(32)][candidate_count(1)] = 41 字节
    const int sync0_header = (int)SIG_AUTH_KEY_PSZ + P2P_PEER_ID_MAX + 1;
    int n = P2P_MAX_PAYLOAD - sync0_header; if (n < 0) n = 0;
    int cand_cnt = n / (int)sizeof(p2p_candidate_t);
    if (cand_cnt > max_candidates) cand_cnt = max_candidates;
    if (cand_cnt > s->local_cand_cnt) cand_cnt = s->local_cand_cnt;
    ctx->candidates_cached = cand_cnt;

    // 根据服务器能力设置探测状态
    if (ctx->feature_msg) {
        s->probe_ctx.state = P2P_PROBE_STATE_READY;
    } else {
        s->probe_ctx.state = P2P_PROBE_STATE_NO_SUPPORT;
    }

    // 解析自己的公网地址（服务器主端口探测到的 UDP 源地址）
    memset(&ctx->public_addr, 0, sizeof(ctx->public_addr));
    ctx->public_addr.sin_family = AF_INET;
    memcpy(&ctx->public_addr.sin_addr.s_addr, payload + 13, 4);
    memcpy(&ctx->public_addr.sin_port, payload + 17, 2);

    // 解析服务器提供的 NAT 探测端口，0 表示服务器不支持
    nread_s(&ctx->probe_port, payload + 19);

    // ONLINE_ACK 下发 auth_key（客户端-服务器认证令牌）
    if (!ctx->auth_key) ctx->auth_key = ack_auth_key;
    else if (ctx->auth_key != ack_auth_key) {

        print("E:", LA_F("%s: auth_key mismatch(local=%" PRIu64 " ack=%" PRIu64 ")\n", LA_F166, 166),
              PROTO, ctx->auth_key, ack_auth_key);

        // 之前"提前"收到的 SYNC 包都是无效的，以 ONLINE_ACK 的 auth_key 为准
        ctx->auth_key = ack_auth_key;
        ctx->session_id = 0;
        ctx->peer_online = false;

        ctx->remote_candidates_mask = 0;
        ctx->remote_candidates_done = 0;
        ctx->remote_candidates_0 = false;

        p2p_session_reset(s, false);
    }

    print("V:", LA_F("%s: accepted, public=%s:%d auth_key=%" PRIu64 " max_cands=%d probe_port=%d relay=%s msg=%s\n", LA_F453, 453),
          PROTO, inet_ntoa(ctx->public_addr.sin_addr), ntohs(ctx->public_addr.sin_port),
          ctx->auth_key, max_candidates, ctx->probe_port,
          ctx->feature_relay ? "yes" : "no",
          ctx->feature_msg ? "yes" : "no");

    // ONLINE 阶段不知道对端状态（对端在线状态将在 SYNC0_ACK 中告知）

    if (s->state == P2P_STATE_REGISTERING) s->state = P2P_STATE_ONLINE;

    // 如果 connect() 已被调用（remote_peer_id 已设置），立即发送 SYNC0；否则进入 ONLINE 状态等待 connect()
    if (ctx->remote_peer_id[0]) {

        ctx->state = SIGNAL_COMPACT_WAIT_SYNC0_ACK;
        send_compact_sync0(s);
        ctx->sig_attempts = 1;
        ctx->last_send_time = P_tick_ms();
        print("I:", LA_F("ONLINE: auth_key acquired, auto SYNC0 sent\n", LA_F475, 475));
    }
    else {
        ctx->state = SIGNAL_COMPACT_ONLINE;
        print("I:", LA_F("ONLINE: auth_key acquired, waiting connect()\n", LA_F476, 476));
    }

    // 如果服务器支持 NAT 探测端口，则启动 NAT_PROBE 探测流程
    if (ctx->probe_port > 0) {

        // 标记进入 NAT_PROBE 探测中状态，发送第一轮探测包
        s->inst->nat_type = P2P_NAT_DETECTING;
        print("I:", LA_F("%s: started, sending first probe\n", LA_F175, 175), TASK_NAT_PROBE);
        ctx->nat_probe_retries = 0/* 初始化启动探测 */;
        send_nat_probe(s);
        ctx->nat_probe_send_time = P_tick_ms();
    }
    else s->inst->nat_type = P2P_NAT_UNDETECTABLE;

    // 如果服务器支持数据中继
    if (ctx->feature_relay) {

        // 启动数据中继功能
        assert(!s->inst->signaling_relay_fn);
        s->inst->signaling_relay_fn = p2p_signal_compact_relay;

        // 将 SIGNALING 路径添加到路径管理器（作为 fallback）
        path_manager_enable_signaling(s, &ctx->server_addr);
        print("I:", LA_F("SIGNALING path enabled (server supports relay)\n", LA_F320, 320));
    }
}

/*
 * 处理 ALIVE_ACK，服务器保活确认
 *
 * 包头: [type=SIG_PKT_ALIVE_ACK | flags=0 | seq=0]
 * 负载: 无
 */
void compact_on_alive_ack(struct p2p_session *s, const struct sockaddr_in *from) {
    const char* PROTO = "ALIVE_ACK";

    printf(LA_F("[UDP] %s recv from %s:%d\n", LA_F487, 487),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    if (ctx->state < SIGNAL_COMPACT_ONLINE) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)ctx->state);
        return;
    }

    print("V:", LA_F("%s: accepted\n", LA_F597, 597), PROTO);

    // 确认服务器未掉线
    uint64_t now = P_tick_ms();
    ctx->last_recv_time = now;
    
    // 通知路径管理器：ALIVE_ACK 确认（seq=0），完成 RoundTrip 测量
    // 仅当 SIGNALING 作为 relay 路径被启用时才统计 RTT
    if (s->inst->signaling.active) {
        path_manager_on_packet_recv(s, PATH_IDX_SIGNALING, now, 0, true, 0);
    }
}

/*
 * 处理 SYNC0_ACK（即连接应答）, session_id 和首批候选同步提交确认
 *
 * 包头: [type=SIG_PKT_SYNC0_ACK | flags=0 | seq=0]
 * 负载: [session_id(P2P_SESS_ID_PSZ)][online(1)]
 *   - session_id: 对端配对会话 ID（由服务器在 SYNC0 时分配，标识 client↔peer 会话）
 *   - online: 1=对端已上线（已有配对），0=对端尚未上线
 */
void compact_on_sync0_ack(struct p2p_session *s,
                              const uint8_t *payload, int len,
                              const struct sockaddr_in *from) {
    const char* PROTO = "SYNC0_ACK";

    printf(LA_F("[UDP] %s recv from %s:%d, len=%d\n", LA_F390, 390),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), len);

    if (len < (int)SIG_PKT_SYNC0_ACK_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;
    if (ctx->state < SIGNAL_COMPACT_WAIT_SYNC0_ACK) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)ctx->state);
        return;
    }

    uint32_t session_id = nget_l(payload);
    if (!session_id) {
        print("W:", LA_F("%s: invalid session_id=0\n", LA_F168, 168), PROTO);
        return;
    }

    // 存储对端配对会话 ID（与 auth_key 语义不同：auth_key=client↔server, session_id=client↔peer）
    if (ctx->session_id && ctx->session_id != session_id) {

        print("I:", LA_F("%s: session_id changed (old=%" PRIu32 " new=%" PRIu32 "), resetting peer state\n", LA_F603, 603),
              PROTO, ctx->session_id, session_id);

        ctx->remote_candidates_mask = 0;
        ctx->remote_candidates_done = 0;
        ctx->remote_candidates_0 = false;
        p2p_session_reset(s, false);
    }
    ctx->session_id = session_id;

    uint8_t online = payload[P2P_SESS_ID_PSZ];
    print("V:", LA_F("%s: accepted, ses_id=%" PRIu32 ", peer=%s\n", LA_F97, 97),
          PROTO, session_id, online ? "online" : "offline");

    ctx->last_recv_time = P_tick_ms();

    if (ctx->state == SIGNAL_COMPACT_WAIT_SYNC0_ACK) 
        ctx->state = SIGNAL_COMPACT_WAIT_PEER;

    if (online && !ctx->peer_online) {
        ctx->peer_online = true;
    }

    // 进入 SYNCING 有两个入口，谁先发生谁先执行：
    // 1. 本处（SYNC0_ACK online=1）：对端已在线，直接进入 SYNCING，立即发送本端剩余候选
    // 2. compact_on_server_sync0 收到服务器下发的 SYNC0：服务器下发对端候选，同时触发 SYNCING
    // 两者均在 WAIT_PEER 状态下判断，状态机本身保证幂等（已 SYNCING 则跳过）
    if (online && ctx->state == SIGNAL_COMPACT_WAIT_PEER) {

        ctx->state = SIGNAL_COMPACT_SYNCING;
        print("I:", LA_F("%s: entered, peer online in SYNC0_ACK\n", LA_F618, 618), TASK_SYNCING);

        if (compact_wait_stun_candidates(s)) {
            print("I:", LA_F("%s: waiting for initial STUN candidates before sending local queue\n", LA_F570, 570), TASK_SYNCING);
        } else {
            send_rest_candidates_and_fin(s);
            ctx->last_send_time = P_tick_ms();
        }
    }
}


/*
 * 处理服务器下发的 SYNC0（首次对端候选推送）
 *
 * 包头: [type=SIG_PKT_SYNC0 | flags=0 | seq=0]  （server→client 方向）
 * 负载: [session_id(P2P_SESS_ID_PSZ)][0x00(1)][candidate_count(1)][candidates(N*23)]
 *   - session_id: 服务器分配的对端配对会话 ID（来自对端 SYNC0 触发的配对）
 *   - 0x00: 保留字节（固定为 0，供 unpack_remote_candidates 识别为初始推送）
 *   - candidate_count: 对端候选数量（首个为服务器观察到的公网地址 srflx，必须 >= 1）
 *   客户端收到后以 SIG_PKT_SYNC_ACK(seq=0) 确认
 */
void compact_on_server_sync0(struct p2p_session *s,
                             const uint8_t *payload, int len,
                             const struct sockaddr_in *from) {
    const char* PROTO = "SYNC0";

    printf(LA_F("[UDP] %s recv from %s:%d, len=%d\n", LA_F390, 390),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), len);

    if (len < (int)SIG_PKT_SYNC0_S2C_PSZ(0)) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    int cand_cnt = payload[P2P_SESS_ID_PSZ + 1];
    if (len < (int)SIG_PKT_SYNC0_S2C_PSZ(cand_cnt)) {
        print("E:", LA_F("%s: bad payload(len=%d cand_cnt=%d)\n", LA_F561, 561), PROTO, len, cand_cnt);
        return;
    }

    if (!cand_cnt) {
        print("E:", LA_F("%s: invalid cand_cnt=0\n", LA_F64, 64), PROTO);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    if (ctx->state < SIGNAL_COMPACT_WAIT_ONLINE_ACK) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)ctx->state);
        return;
    }

    uint32_t session_id = nget_l(payload);
    if (session_id == 0) {
        print("E:", LA_F("%s: invalid session_id=0\n", LA_F168, 168), PROTO);
        return;
    }

    // 并行网络中，SYNC0 可能先于 SYNC0_ACK 到达，此时 session_id 可能还未设置
    if (!ctx->session_id) ctx->session_id = session_id;

    // 如果 session_id 不一致，说明对端可能重新发起连接（崩溃重启）
    // ! 根据协议，对方将自己的 session 重置，并不会影响自己和信令服务之间的连接和数据状态
    else if (ctx->session_id != session_id) {

        // 此时本端只能被迫强制重连
        print("W:", LA_F("%s: renew session due to session_id changed (local=%" PRIu32 " pkt=%" PRIu32 ")\n", LA_F156, 156),
              PROTO, ctx->session_id, session_id);

        // 如果 p2p 之前已经连接成功过，即业务层可能已经完成部分通讯
        // ! 对于业务层来说，session 被强制重置 = 连接断开，需要重新开始
        if (s->state >= P2P_STATE_LOST) {
            if (s->inst->cfg.on_state) s->inst->cfg.on_state((p2p_session_t)s, s->state, P2P_STATE_CLOSED, s->inst->cfg.userdata);
        }

        reset_peer(ctx);
        p2p_session_reset(s, false);

        ctx->session_id = session_id;

        if (ctx->state == SIGNAL_COMPACT_WAIT_ONLINE_ACK) s->state = P2P_STATE_REGISTERING;
        else { ctx->state = SIGNAL_COMPACT_WAIT_SYNC0_ACK;
            s->state = P2P_STATE_ONLINE;
        }
    }

    // 进入 SYNCING 有两个入口，谁先发生谁先执行：
    // 1. compact_on_sync0_ack（SYNC0_ACK online=1）：对端已在线，直接进入 SYNCING
    // 2. 本处（收到服务器下发的 SYNC0）：服务器下发对端候选，同时触发 SYNCING
    // 两者均在 WAIT_PEER 状态下判断，状态机本身保证幂等（已 SYNCING 则跳过）
    if (ctx->state == SIGNAL_COMPACT_WAIT_SYNC0_ACK || ctx->state == SIGNAL_COMPACT_WAIT_PEER) {

        ctx->state = SIGNAL_COMPACT_SYNCING;
        print("I:", LA_F("%s: entered, %s arrived\n", LA_F619, 619), TASK_SYNCING, PROTO);

        if (compact_wait_stun_candidates(s)) {
            print("I:", LA_F("%s: waiting for initial STUN candidates before sending local queue\n", LA_F570, 570), TASK_SYNCING);
        } else {
            send_rest_candidates_and_fin(s);
            ctx->last_send_time = P_tick_ms();
        }
    }

    bool new_seq = false;

    // 排重
    if (!ctx->remote_candidates_0) {

        if (p2p_remote_cands_reserve(s, cand_cnt) != E_NONE) {
            print("E:", LA_F("Failed to reserve remote candidates (cnt=%d)\n", LA_F245, 245), cand_cnt);
            return;
        }

        unpack_remote_candidates(s, payload, cand_cnt);

        ctx->remote_candidates_0 = new_seq = true;

        print("V:", LA_F("%s: accepted cand_cnt=%d\n", LA_F63, 63), PROTO, cand_cnt);
    }

    // 收到该消息说明对方肯定已上线
    if (!ctx->peer_online) ctx->peer_online = true;

    // 无论 peer_online 之前是否已设置，只要 NAT 打洞还未启动就启动
    // 注：Bob 在 SYNC0_ACK(online=1) 时已设置 peer_online，但 punch 尚未启动，需在此触发
    if (s->nat.state < NAT_PUNCHING) {
        print("I:", LA_F("%s: peer online, starting NAT punch\n", LA_F424, 424), PROTO);
        nat_punch(s, -1/* all candidates */);
    }

    if (new_seq) {

        // 如果对方所有的候选队列都已经接收完成
        if (ctx->remote_candidates_0 && ctx->remote_candidates_mask &&
            (ctx->remote_candidates_done & ctx->remote_candidates_mask) == ctx->remote_candidates_mask) {

            s->remote_cand_done = true;

            print("I:", LA_F("%s: sync complete (ses_id=%" PRIu32 ", mask=0x%04x)\n", LA_F178, 178),
                  TASK_SYNC_REMOTE, ctx->session_id, (unsigned)ctx->remote_candidates_mask);
        }
    }

    // 发送 SYNC0_ACK（client→server 方向）确认收到服务器 SYNC0
    {
        uint8_t ack_payload[SIG_PKT_SYNC0_ACK_C2S_PSZ];
        nwrite_l(ack_payload, ctx->session_id);

        print("V:", LA_F("SYNC0_ACK sent (ses_id=%" PRIu32 ")\n", LA_F621, 621), ctx->session_id);

        ret_t ret = p2p_udp_send_packet(s, &ctx->server_addr, SIG_PKT_SYNC0_ACK, 0, 0, ack_payload, sizeof(ack_payload));
        if (ret < 0)
            print("E:", LA_F("[UDP] SYNC0_ACK send to %s:%d failed(%d)\n", LA_F622, 622),
                  inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
        else
            printf(LA_F("[UDP] SYNC0_ACK send to %s:%d, seq=0, flags=0, len=%d\n", LA_F623, 623),
                   inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
                   (int)sizeof(ack_payload));
    }
}


/*
 * 包头: [type=SIG_PKT_SYNC | flags=见下 | seq=序列号]
 * 负载: [session_id(P2P_SESS_ID_PSZ) | base_index(1) | candidate_count(1) | candidates(N*7)]
 *   - session_id: 会话 ID（网络字节序）
 *   - base_index: 本批候选的起始索引（0-based）
 *   - candidate_count: 本批候选数量，0 表示结束标识
 *   - seq=0: 服务器发送，base_index!=0，对端公网地址变更通知（首次推送已改用 SIG_PKT_SYNC0）
 *   - seq>0: 客户端发送，继续同步剩余候选
 *   - flags: SIG_SYNC_FLAG_FIN (0x01) 表示候选列表发送完毕
 */
void compact_on_sync(struct p2p_session *s, uint16_t seq, uint8_t flags,
                          const uint8_t *payload, int len,
                          const struct sockaddr_in *from) {
    const char* PROTO = "SYNC";

    printf(LA_F("[UDP] %s recv from %s:%d, seq=%u, flags=0x%02x, len=%d\n", LA_F391, 391),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), seq, flags, len);

    if (seq > 16) {
        print("E:", LA_F("%s: invalid seq=%u\n", LA_F122, 122), PROTO, seq);
        return;
    }

    if (len < (int)SIG_PKT_SYNC_PSZ(0)) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    int cand_cnt = payload[P2P_SESS_ID_PSZ + 1];
    if (len < (int)SIG_PKT_SYNC_PSZ(cand_cnt)) {
        print("E:", LA_F("%s: bad payload(len=%d cand_cnt=%d)\n", LA_F561, 561), PROTO, len, cand_cnt);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    if (ctx->state < SIGNAL_COMPACT_WAIT_ONLINE_ACK) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)ctx->state);
        return;
    }

    // 获取 session_id（对端配对会话 ID，由服务器在配对成功时下发）
    uint32_t session_id = nget_l(payload);
    if (session_id == 0) {
        print("E:", LA_F("%s: invalid session_id=0\n", LA_F168, 168), PROTO);
        return;
    }

    uint8_t base_index = payload[P2P_SESS_ID_PSZ];
    // seq>0 时 base_index 为候选起始索引；seq=0 时 base_index 为循环通知序号（1..255）

    // 并行网络中，乱序包（SYNC seq>0 或地址变更通知 seq=0）可能先于 SYNC0_ACK/SYNC0 到达，此时 session_id 可能还未设置
    if (!ctx->session_id) ctx->session_id = session_id;

    // 如果 session_id 不一致（session 重置逻辑在 compact_on_server_sync0，此处不处理）
    else if (ctx->session_id != session_id) {
        print("E:", LA_F("%s: session mismatch(local=%" PRIu32 " pkt=%" PRIu32 ")\n", LA_F167, 167), PROTO, ctx->session_id, session_id);
        return;
    }

    // SYNCING 通常由 compact_on_sync0_ack 或 compact_on_server_sync0 触发；
    // 此处为兜底，处理乱序先到（SYNC seq>0 或 seq=0 地址变更通知先于 SYNC0_ACK/SYNC0 到达）
    if (ctx->state == SIGNAL_COMPACT_WAIT_SYNC0_ACK || ctx->state == SIGNAL_COMPACT_WAIT_PEER) {

        ctx->state = SIGNAL_COMPACT_SYNCING;
        print("I:", LA_F("%s: entered early, %s arrived before SYNC0\n", LA_F109, 109), TASK_SYNCING, PROTO);

        if (compact_wait_stun_candidates(s)) {
            print("I:", LA_F("%s: waiting for initial STUN candidates before sending local queue\n", LA_F570, 570), TASK_SYNCING);
        } else {
            send_rest_candidates_and_fin(s);
            ctx->last_send_time = P_tick_ms();
        }
    }

    bool new_seq = false;

    // seq!=0 说明是对端发来的后续 SYNC 包
    if (seq != 0) {

        print("V:", LA_F("%s: accepted seq=%u cand_cnt=%d flags=0x%02x\n", LA_F92, 92), PROTO, seq, cand_cnt, flags);

        // 排重
        if ((new_seq = (ctx->remote_candidates_done & (1u << (seq - 1))) == 0)) {

            // 对于 FIN 包，计算对方候选地址集合序列掩码（即计算全集区间）
            if ((flags & SIG_SYNC_FLAG_FIN) || !cand_cnt) {

                ctx->remote_candidates_mask = (1u << seq) - 1u;
            }

            // 维护分配远端候选列表的空间
            // + 这里 payload[P2P_SESS_ID_PSZ](base_index) + cand_cnt，表示该包至少需要的远端候选数量; 1 为至少包含一个对方的公网地址
            if (p2p_remote_cands_reserve(s, 1 + payload[P2P_SESS_ID_PSZ] + cand_cnt) != E_NONE) {
                print("E:", LA_F("Failed to reserve remote candidates (base=%u cnt=%d)\n", LA_F244, 244), payload[P2P_SESS_ID_PSZ], cand_cnt);
                return;
            }

            if (cand_cnt) 
                unpack_remote_candidates(s, payload, cand_cnt);

            ctx->remote_candidates_done |= 1u << (seq - 1);
        }
    }
    // seq=0: 对端公网地址变更通知，必须只携带一个候选地址（变更后的公网地址），且不带 FIN 标识
    // base_index（循环通知序号 1..255，服务器侧保证跳过 0）用于接收端排重，确保只处理最新通知
    else if (cand_cnt != 1 || (flags & SIG_SYNC_FLAG_FIN)) {

        print("E:", LA_F("%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)\n", LA_F48, 48),
              PROTO, base_index, cand_cnt, flags);
        return;
    }
    // 确保地址变更通知是最新的
    else if (ctx->remote_addr_notify_seq == 0 || uint8_circle_newer(base_index, ctx->remote_addr_notify_seq)) {

        print("V:", LA_F("%s NOTIFY: accepted\n", LA_F45, 45), PROTO);

        if (p2p_remote_cands_reserve(s, 1) != E_NONE) {
            print("E:", LA_F("Failed to reserve remote candidates (cnt=1)\n", LA_F246, 246));
            return;
        }

        if (s->inst->cfg.test_ice_srflx_off) {
            print("I:", LA_F("%s NOTIFY: srflx addr update (disabled)\n", LA_F47, 47), PROTO);
            return;
        }

        p2p_remote_candidate_entry_t *c = &s->remote_cands[0];
        c->type = (p2p_cand_type_t)payload[P2P_SESS_ID_PSZ+2];
        c->priority = 0;
        sockaddr_init_with_net(&c->addr, (uint32_t *) (payload + P2P_SESS_ID_PSZ + 3),
                               (uint16_t *) (payload + P2P_SESS_ID_PSZ + 7));
        c->last_punch_send_ms = 0;
        if (s->remote_cand_cnt == 0) s->remote_cand_cnt = 1;

        // Trickle candidates：NAT 打洞已启动时，立即探测最新地址
        if (s->nat.state == NAT_PUNCHING || s->nat.state == NAT_RELAY) {

            print("I:", LA_F("%s: Peer addr changed -> %s:%d, retrying punch\n", LA_F76, 76),
                  TASK_SYNC_REMOTE, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));

            // 标记旧的活跃路径为失效（地址已变更）
            // fixme: 这个逻辑好像有问题
            // if (s->active_path >= 0 && s->active_path < s->remote_cand_cnt) {
            //     path_manager_set_path_state(s, s->active_path, PATH_STATE_FAILED);
            //     print("V:", LA_F("Marked old path (idx=%d) as FAILED due to addr change\n", LA_F268, 268),
            //            s->active_path);
            // }

            if (ctx->state >= SIGNAL_COMPACT_WAIT_SYNC0_ACK) {

                if (nat_punch(s, 0) != E_NONE) {
                    print("E:", LA_F("Failed to send punch packet for new peer addr\n", LA_F251, 251));
                }
            }
        }
        else {
            print("I:", LA_F("%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)\n", LA_F75, 75),
                  TASK_SYNC_REMOTE, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port), (int)s->nat.state);
        }

        ctx->remote_addr_notify_seq = base_index;
    }
    else print("V:", LA_F("%s NOTIFY: ignored old notify base=%u (current=%u)\n", LA_F46, 46),
               PROTO, base_index, ctx->remote_addr_notify_seq);

    // 收到该消息说明对方肯定已上线
    if (!ctx->peer_online) ctx->peer_online = true;

    // 无论 peer_online 之前是否已设置，只要 NAT 打洞还未启动就启动
    if (s->nat.state < NAT_PUNCHING) {
        print("I:", LA_F("%s: peer online, starting NAT punch\n", LA_F424, 424), PROTO);
        nat_punch(s, -1/* all candidates */);
    }

    if (new_seq) {

        // 如果对方所有的候选队列都已经接收完成
        // 注：此状态用于 NAT 打洞超时判断，只有候选同步完成后才会触发打洞超时
        if (ctx->remote_candidates_0 && ctx->remote_candidates_mask &&
            (ctx->remote_candidates_done & ctx->remote_candidates_mask) == ctx->remote_candidates_mask) {

            // 标记远程候选交换完成（供 NAT 层判断打洞超时使用）
            s->remote_cand_done = true;

            print("I:", LA_F("%s: sync complete (ses_id=%" PRIu32 ", mask=0x%04x)\n", LA_F178, 178),
                  TASK_SYNC_REMOTE, ctx->session_id, (unsigned)ctx->remote_candidates_mask);
        }
    }

    /*
     * 发送 SYNC_ACK 确认包
     * 说明: 确认收到对方的候选地址包
     *
     * 包头: [type=SIG_PKT_SYNC_ACK | flags=0 | seq=被确认的SYNC包序号]
     * 负载: [session_id(P2P_SESS_ID_PSZ)]
     *   - session_id: 会话 ID（网络字节序）
     *   - seq: 被确认的 SYNC 包的序列号
     */
    {
        uint8_t ack_payload[P2P_SESS_ID_PSZ];
        nwrite_l(ack_payload, ctx->session_id);

        print("V:", LA_F("%s_ACK sent, seq=%u (ses_id=%" PRIu32 ")\n", LA_F196, 196), PROTO, seq, ctx->session_id);

        ret_t ret = p2p_udp_send_packet(s, &ctx->server_addr, SIG_PKT_SYNC_ACK, 0, seq, ack_payload, sizeof(ack_payload));
        if (ret < 0)
            print("E:", LA_F("[UDP] %s_ACK send to %s:%d failed(%d)\n", LA_F398, 398), 
                  PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
        else
            printf(LA_F("[UDP] %s_ACK send to %s:%d, seq=%u, flags=0, len=%d\n", LA_F399, 399),
                   PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
                   seq, (int)sizeof(ack_payload));
    }
}

/*
 * 处理 SYNC_ACK，对端确认已收到自己发过去的候选地址信息
 *
 * 包头: [type=SIG_PKT_SYNC_ACK | flags=0 | seq=确认的 SYNC 序列号]
 * 负载: [session_id(P2P_SESS_ID_PSZ)]
 *   - session_id: 会话 ID（网络字节序）
 *   - seq: 确认的 SYNC 序列号（0 表示确认服务器下发的 SYNC(seq=0)）
 */
void compact_on_sync_ack(struct p2p_session *s, uint16_t seq,
                               const uint8_t *payload, int len,
                               const struct sockaddr_in *from) {
    const char* PROTO = "SYNC_ACK";

    printf(LA_F("[UDP] %s recv from %s:%d, seq=%u, len=%d\n", LA_F392, 392),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), seq, len);

    if (len < (int)SIG_PKT_SYNC_ACK_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    if (seq == 0 || seq > 16) {
        print("E:", LA_F("%s: invalid ack_seq=%u\n", LA_F118, 118), PROTO, seq);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    uint32_t session_id = nget_l(payload);
    if (session_id != ctx->session_id) {
        print("W:", LA_F("%s: ignored for ses_id=%" PRIu32 " (local ses_id=%" PRIu32 ")\n", LA_F114, 114),
              PROTO, session_id, ctx->session_id);
        return;
    }

    uint16_t bit = (uint16_t)(1u << (seq - 1));
    if ((ctx->candidates_mask & bit) == 0) {
        print("E:", LA_F("%s: unexpected ack_seq=%u mask=0x%04x\n", LA_F191, 191),
              PROTO, seq, (unsigned)ctx->candidates_mask);
        return;
    }

    if ((ctx->candidates_acked & bit)) {
        print("V:", LA_F("%s: ignored for duplicated seq=%u, already acked\n", LA_F112, 112), PROTO, seq);
        return;
    }

    print("V:", LA_F("%s: accepted for ack_seq=%u\n", LA_F90, 90), PROTO, seq);

    ctx->candidates_acked |= bit;

    // 如果对方所有的候选队列都已经接收完成
    if ((ctx->candidates_acked & ctx->candidates_mask) == ctx->candidates_mask) {

        ctx->state = SIGNAL_COMPACT_READY;
        print("I:", LA_F("%s: sync complete (ses_id=%" PRIu32 ")\n", LA_F177, 177), TASK_SYNCING, ctx->session_id);
    }
}

/*
 * 处理 FIN，对端离线通知
 *
 * 包头: [type=SIG_PKT_FIN | flags=0 | seq=0]
 * 负载: [session_id(P2P_SESS_ID_PSZ)]
 *   - session_id: 已断开的会话 ID（网络字节序）
 */
void compact_on_fin(struct p2p_session *s, const uint8_t *payload, int len,
                    const struct sockaddr_in *from) {
    const char* PROTO = "FIN";

    printf(LA_F("[UDP] %s recv from %s:%d, len=%d\n", LA_F390, 390),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), len);

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    if (len < (int)SIG_PKT_FIN_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    uint32_t session_id = nget_l(payload);
    if (!session_id || ctx->session_id != session_id) {
        print("W:", LA_F("%s: session mismatch(local=%" PRIu32 ", pkt=%" PRIu32 ")\n", LA_F602, 602), PROTO, ctx->session_id, session_id);
        return;
    }

    print("V:", LA_F("%s: accepted (ses_id=%" PRIu32 ")\n", LA_F88, 88), PROTO, session_id);

    // 重置到 WAIT_SYNC0_ACK 状态，等待对端重新上线
    // ! 这里可以明确知道是对方断开了连接，所以自己和信令服务器之间的连接和数据状态都是正常的，不需要重置
    //   auth_key 不需要重置，因为它是 client↔server 的认证令牌，对端断开不影响与服务器的关系
    //   session_id 对应 client↔peer 会话，对端断开后将在下一轮 SYNC0_ACK 中重新获得
    ctx->session_id = 0;
    ctx->state = SIGNAL_COMPACT_WAIT_SYNC0_ACK;

    // 清除双方协商信息
    reset_peer(ctx);

    print("I:", LA_F("%s: peer disconnected (ses_id=%" PRIu32 "), reset to WAIT_SYNC0_ACK\n", LA_F551, 551), PROTO, session_id);

    // 标记 NAT 为已关闭
    // + 这里将信令层的 peer close 转换为 NAT 层的 closed 状态，主循环会统一以 NAT 层的 NAT_CLOSED 状态机变更为准
    //   并统一调用 p2p_session_reset
    s->nat.state = NAT_CLOSED;
}

/*
 * 处理 MSG_REQ，（服务器代理转发的）源端消息请求
 * 说明: B端收到服务器转发的消息请求，A端发出的原始请求(flags=0)不会到达客户端
 *
 * 包头: [type=SIG_PKT_MSG_REQ | flags=SIG_MSG_FLAG_RELAY | seq=0]
 * 负载: [session_id(P2P_SESS_ID_PSZ)][sid(2)][msg(1)][data(N)]
 *   - session_id: A端的会话 ID（网络字节序，64位）
 *   - sid: 序列号（2字节，网络字节序）
 *   - msg: 消息 ID（1字节）
 *   - data: 消息数据
 *   - flags: SIG_MSG_FLAG_RELAY (0x01) 表示服务器转发
 */
void compact_on_request(struct p2p_session *s, uint8_t flags,
                        const uint8_t *payload, int len,
                        const struct sockaddr_in *from) {
    const char* PROTO = "MSG_REQ";

    printf(LA_F("[UDP] %s recv from %s:%d, flags=0x%02x, len=%d\n", LA_F389, 389),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), flags, len);

    // 客户端收到 req 肯定都是 Server 转发过来，而不是对方直接发来的原始请求
    if (!(flags & SIG_MSG_FLAG_RELAY)) {
        print("E:", LA_F("%s: invalid for non-relay req\n", LA_F120, 120), PROTO);
        return;
    }

    // 最小长度：session_id(P2P_SESS_ID_PSZ) + sid(2) + msg(1) = 11
    if (len < (int)SIG_PKT_MSG_REQ_MIN_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    uint32_t session_id = nget_l(payload);
    uint16_t sid = nget_s(payload + P2P_SESS_ID_PSZ);
    uint8_t msg = payload[P2P_SESS_ID_PSZ + 2];
    const uint8_t *req_data = payload + P2P_SESS_ID_PSZ + 3;
    int req_len = len - (int)SIG_PKT_MSG_REQ_MIN_PSZ;

    /* 判断是否是新请求（使用循环序列号比较）：
     * 1. 如果正在处理请求且 sid 相同 → 忽略重复包
     * 2. 如果 sid <= last_sid（旧请求）→ 忽略
     * 3. 如果 sid > last_sid（新请求）→ 处理（可能覆盖正在处理的旧请求）*/
    if (ctx->resp_state == 1 && ctx->resp_sid == sid) {
        print("V:", LA_F("%s: duplicate request ignored (sid=%u, already processing)\n", LA_F598, 598), PROTO, sid);
        return;
    }

    // 忽略旧请求（sid <= last_sid）
    if (ctx->rpc_last_sid != 0 && !uint16_circle_newer(sid, ctx->rpc_last_sid)) {
        print("V:", LA_F("%s: old request ignored (sid=%u <= last_sid=%u)\n", LA_F131, 131),
              PROTO, sid, ctx->rpc_last_sid);
        return;
    }

    // 如果正在处理旧请求但收到新请求（sid > last_sid），则覆盖旧请求
    if (ctx->resp_state == 1) {
        print("W:", LA_F("%s: new request (sid=%u) overrides pending request (sid=%u)\n", LA_F125, 125), 
              PROTO, sid, ctx->resp_sid);
    }
    
    ctx->resp_sid         = sid;
    ctx->resp_session_id  = session_id;

    // msg=0: 默认自动 echo 回复（无需应用层介入）
    if (msg == 0) {
        print("V:", LA_F("%s msg=0: accepted, echo reply (sid=%u, len=%d)\n", LA_F49, 49), PROTO, sid, req_len);
        p2p_signal_compact_response(s, 0, req_data, req_len);
        return;
    }

    print("V:", LA_F("%s: accepted sid=%u, msg=%u\n", LA_F93, 93), PROTO, sid, msg);

    // 触发用户回调
    if (s->inst->cfg.on_request)
        s->inst->cfg.on_request((p2p_session_t)s, sid, msg, req_data, req_len, s->inst->cfg.userdata);
}

/*
 * 处理 MSG_REQ_ACK，服务器对自己发起的请求的确认
 * 说明: 该请求已经被服务器代理接管，并确保完成向对端的转发
 * 所以收到该消息后，自己就可以停止重发请求了，接下来只需要等待 MSG_RESP 即可知道请求的最终结果
 *
 * 包头: [type=SIG_PKT_MSG_REQ_ACK | flags=0 | seq=0]
 * 负载: [session_id(P2P_SESS_ID_PSZ)][sid(2)][status(1)]
 *   - session_id: A端的会话 ID（用于验证响应合法性）
 *   - sid: 序列号，与 MSG_REQ 中的 sid 对应
 *   - status: 0=已缓存开始中转, 1=B不在线（失败）
 */
void compact_on_request_ack(struct p2p_session *s,
                             const uint8_t *payload, int len,
                             const struct sockaddr_in *from) {
    const char* PROTO = "MSG_REQ_ACK";

    printf(LA_F("[UDP] %s recv from %s:%d, len=%d\n", LA_F390, 390),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), len);

    if (len < (int)SIG_PKT_MSG_REQ_ACK_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    uint32_t session_id = nget_l(payload);
    uint16_t sid = nget_s(payload + P2P_SESS_ID_PSZ);
    uint8_t status = payload[P2P_SESS_ID_PSZ + 2];

    // 验证 session_id 是否匹配
    if (session_id != ctx->session_id) {
        print("W:", LA_F("%s: session_id mismatch (recv=%" PRIu32 ", expect=%" PRIu32 ")\n", LA_F170, 170),
              PROTO, session_id, ctx->session_id);
        return;
    }

    if (ctx->req_sid != sid) {
        print("V:", LA_F("%s: ignored for sid=%u (current sid=%u)\n", LA_F115, 115), PROTO, sid, ctx->req_sid);
        return;
    }
    if (ctx->req_state != 1/* waiting REQ_ACK */) {
        print("V:", LA_F("%s: ignored in invalid state=%d\n", LA_F116, 116), PROTO, (int)ctx->req_state);
        return;
    }

    // msg=0 data=0 是 probe 保留的空 RPC，短路处理，不走应用层
    if (ctx->req_msg == 0 && ctx->req_data_len == 0) {
        probe_compact_on_req_ack(s, sid, status);
        if (status == 0)
            ctx->req_state = 2/* waiting RESP */;
        else {
            ctx->req_state = 0;
            ctx->req_sid   = 0;
        }
        return;
    }

    // 成功：服务器已收到请求并开始向对端中转，停止重发，等待 MSG_RESP
    if (status == 0) {
        ctx->req_state = 2/* waiting RESP */;
        print("V:", LA_F("%s: accepted, waiting for response (sid=%u)\n", LA_F96, 96), PROTO, ctx->req_sid);
    }
    // 对端不在线：请求失败，通知上层
    else {

        uint16_t saved_id  = ctx->req_sid;
        uint8_t  saved_msg = ctx->req_msg;
        ctx->req_state  = 0;
        ctx->req_sid = 0;

        print("W:", LA_F("%s: RPC fail due to peer offline (sid=%u)\n", LA_F79, 79), PROTO, saved_id);

        if (s->inst->cfg.on_response)
            s->inst->cfg.on_response((p2p_session_t)s, saved_id, saved_msg, NULL, -1, s->inst->cfg.userdata);
    }
}

/*
 * 处理 MSG_RESP，服务器代理转发的，对端（对自己向对端请求的）消息响应
 *
 * 包头: [type=SIG_PKT_MSG_RESP | flags=见下 | seq=0]
 * 负载:
 * > [session_id(P2P_SESS_ID_PSZ)][sid(2)][code(1)][data(N)] （正常响应）
 * > [session_id(P2P_SESS_ID_PSZ)][sid(2)]（错误响应）
 *   - session_id: A端的会话 ID（用于验证响应合法性）
 *   - sid: 序列号，与 MSG_REQ 中的 sid 对应
 *   - code: 响应消息 ID（正常响应时）
 *   - data: 响应数据（正常响应时）
 *   - flags: 0=正常响应（B端返回的数据）
 *            SIG_MSG_FLAG_PEER_OFFLINE (0x02)=B端在 REQ_ACK 之后离线
 *            SIG_MSG_FLAG_TIMEOUT (0x04)=服务器向B端转发请求超时
 */
void compact_on_response(struct p2p_session *s, uint8_t flags,
                        const uint8_t *payload, int len,
                        const struct sockaddr_in *from) {
    const char* PROTO = "MSG_RESP";

    printf(LA_F("[UDP] %s recv from %s:%d, flags=0x%02x, len=%d\n", LA_F389, 389),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), flags, len);

    // 检查最小负载长度：session_id(P2P_SESS_ID_PSZ) + sid(2)
    if (len < (int)(P2P_SESS_ID_PSZ + 2u)) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    uint32_t session_id = nget_l(payload);
    uint16_t sid = nget_s(payload + P2P_SESS_ID_PSZ);

    // 验证 session_id 是否匹配
    if (session_id != ctx->session_id) {
        print("W:", LA_F("%s: session_id mismatch (recv=%" PRIu32 ", expect=%" PRIu32 ")\n", LA_F170, 170),
              PROTO, session_id, ctx->session_id);
        return;
    }

    /*
     * 发送 MSG_RESP_ACK 确认包
     * 服务器收到该确认后，将会停止重发该响应到本地，即结束整个请求-响应流程
     *
     * 包头: [type=SIG_PKT_MSG_RESP_ACK | flags=0 | seq=0]
     * 负载: [session_id(P2P_SESS_ID_PSZ)][sid(2)]
     *   - session_id: A端的会话 ID（用于 O(1) 哈希查找）
     *   - sid: 序列号，与 MSG_RESP 中的 sid 一致
     * 说明: A端确认收到B端的响应，幂等操作，即使已处理过也补发
     */
    {
        uint8_t ack[SIG_PKT_MSG_RESP_ACK_PSZ]; int n = 0;
        nwrite_l(ack + n, ctx->session_id); n += P2P_SESS_ID_PSZ;
        nwrite_s(ack + n, sid); n += 2;

        print("V:", LA_F("%s_ACK sent, sid=%u\n", LA_F197, 197), PROTO, sid);

        ret_t ret = p2p_udp_send_packet(s, &ctx->server_addr, SIG_PKT_MSG_RESP_ACK, 0, 0, ack, n);
        if (ret < 0)
            print("E:", LA_F("[UDP] %s_ACK send to %s:%d failed(%d)\n", LA_F398, 398), 
                  PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
        else
            printf(LA_F("[UDP] %s_ACK send to %s:%d, seq=0, flags=0, len=%d\n", LA_F400, 400),
                   PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), n);
    }

    // 仅命中当前挂起请求时，才需要继续解析响应内容
    if (!(ctx->req_state == 2/* waiting RESP */ && ctx->req_sid == sid)) {
        print("V:", LA_F("%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)\n", LA_F108, 108),
              PROTO, sid, ctx->req_sid, (int)ctx->req_state);
        return;
    }

    // msg=0 data=0 是 probe 保留的空 RPC，短路处理，不走应用层
    if (ctx->req_msg == 0 && ctx->req_data_len == 0) {
        probe_compact_on_response(s, sid);
        ctx->req_state = 0;
        ctx->req_sid   = 0;
        return;
    }

    uint8_t res_code = 0;
    const uint8_t *res_data = NULL;
    int res_size = -1; // -1 表示错误

    if (flags & (SIG_MSG_FLAG_PEER_OFFLINE | SIG_MSG_FLAG_TIMEOUT)) {

        res_code = (flags & SIG_MSG_FLAG_PEER_OFFLINE) ? P2P_MSG_ERR_PEER_OFFLINE : P2P_MSG_ERR_TIMEOUT;
    }
    else {

        // 正常响应：需要包含 code 和可选的 data
        // 最小长度：session_id(P2P_SESS_ID_PSZ) + sid(2) + code(1) = 11
        if (len < (int)SIG_PKT_MSG_RESP_MIN_PSZ) {
            print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
            return;
        }
        res_code = payload[P2P_SESS_ID_PSZ + 2];
        res_data = payload + P2P_SESS_ID_PSZ + 3;
        res_size = len - (int)SIG_PKT_MSG_RESP_MIN_PSZ;
    }

    print("V:", LA_F("%s: accepted (sid=%u)\n", LA_F89, 89), PROTO, sid);

    ctx->req_state  = 0;
    ctx->req_sid = 0;

    /* 根据 flags 输出不同的日志 */
    if (flags & SIG_MSG_FLAG_PEER_OFFLINE) {
        print("W:", LA_F("%s: RPC fail due to peer offline (sid=%u)\n", LA_F79, 79), PROTO, sid);
    }
    else if (flags & SIG_MSG_FLAG_TIMEOUT) {
        print("W:", LA_F("%s: RPC fail due to relay timeout (sid=%u)\n", LA_F80, 80), PROTO, sid);
    }
    else {
        print("I:", LA_F("%s: RPC complete (sid=%u)\n", LA_F78, 78), PROTO, sid);
    }

    if (s->inst->cfg.on_response)
        s->inst->cfg.on_response((p2p_session_t)s, sid, res_code, res_data, res_size, s->inst->cfg.userdata);
}

/*
 * 处理 MSG_RESP_ACK，服务器对自己（经由服务器代理）向源方发出的请求的响应的确认
 * 收到该确认后，自己就不会再重发 response 了，且可以记录该 sid 已完成
 *
 * 包头: [type=SIG_PKT_MSG_RESP_ACK | flags=0 | seq=0]
 * 负载: [session_id(P2P_SESS_ID_PSZ)][sid(2)]
 *   - session_id: B端的会话 ID（用于 O(1) 哈希查找）
 *   - sid: 序列号，与 MSG_RESP 中的 sid 对应
 */
void compact_on_response_ack(struct p2p_session *s,
                             const uint8_t *payload, int len,
                             const struct sockaddr_in *from) {
    const char* PROTO = "MSG_RESP_ACK";

    printf(LA_F("[UDP] %s recv from %s:%d, len=%d\n", LA_F390, 390),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), len);

    if (len < (int)SIG_PKT_MSG_RESP_ACK_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    uint32_t session_id = nget_l(payload);
    uint16_t sid = nget_s(payload + P2P_SESS_ID_PSZ);

    // 验证 session_id 是否匹配（可选，增强安全性）
    if (session_id != ctx->session_id) {
        print("W:", LA_F("%s: session_id mismatch (recv=%" PRIu32 ", expect=%" PRIu32 ")\n", LA_F170, 170),
              PROTO, session_id, ctx->session_id);
        return;
    }
    if (ctx->resp_sid != sid) {
        print("V:", LA_F("%s: ignored for sid=%u (current sid=%u)\n", LA_F115, 115), PROTO, sid, ctx->resp_sid);
        return;
    }
    if (ctx->resp_state != 1/* waiting RESP_ACK */) {
        print("V:", LA_F("%s: ignored in invalid state=%d\n", LA_F116, 116), PROTO, (int)ctx->resp_state);
        return;
    }

    // 记录最后完成的 sid
    print("V:", LA_F("%s: accepted (sid=%u)\n", LA_F89, 89), PROTO, sid);

    ctx->rpc_last_sid = ctx->resp_sid;

    // 成功：Server 已收到 B 的 MSG_RESP，结束 RESP 重发
    ctx->resp_sid = 0;
    ctx->resp_state = 0;
    ctx->resp_session_id = 0;

    print("I:", LA_F("%s: RPC finished (sid=%u)\n", LA_F81, 81), PROTO, sid);
}

/*
 * 处理 NAT_PROBE_ACK，NAT 探测响应
 *
 * 包头: [type=SIG_PKT_NAT_PROBE_ACK | flags=0 | seq=对应的 NAT_PROBE 请求 seq]
 * 负载: [probe_ip(4) | probe_port(2)]
 *   - probe_ip/port: 服务器在探测端口观察到的客户端源地址（第二次映射）
 *   - seq: 复制请求包的 seq，用于客户端匹配响应
 */
void compact_on_nat_probe_ack(struct p2p_session *s, uint16_t seq,
                              const uint8_t *payload, int len,
                              const struct sockaddr_in *from) {
    const char* PROTO = "NAT_PROBE_ACK";

    printf(LA_F("[UDP] %s recv from %s:%d, seq=%u, len=%d\n", LA_F392, 392),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), seq, len);

    if (len < (int)SIG_PKT_NAT_PROBE_ACK_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    if (seq != ctx->nat_probe_retries) {
        print("V:", LA_F("%s: ignored for seq=%u (expect=%d)\n", LA_F113, 113),
              PROTO, seq, (int)ctx->nat_probe_retries);
        return;
    }

    struct sockaddr_in probe_mapped;
    memset(&probe_mapped, 0, sizeof(probe_mapped));
    probe_mapped.sin_family = AF_INET;
    memcpy(&probe_mapped.sin_addr.s_addr, payload, 4);
    memcpy(&probe_mapped.sin_port,        payload + 4, 2);

    print("V:", LA_F("%s: accepted, probe_mapped=%s:%d\n", LA_F94, 94),
          PROTO, inet_ntoa(probe_mapped.sin_addr), ntohs(probe_mapped.sin_port));

    // 端口一致性：主端口映射端口 == 探测端口映射端口 → 锥形，否则 → 对称
    ctx->nat_is_port_consistent = (probe_mapped.sin_port == ctx->public_addr.sin_port) ? 1 : 0;

    // 检测 OPEN：公网地址 IP 与任意本地地址相同（无 NAT）
    int is_open = 0;
    const route_ctx_t *rt = route_shared_get();
    for (int i = 0; rt && i < rt->addr_count; i++) {
        if (ctx->public_addr.sin_addr.s_addr == rt->local_addrs[i].sin_addr.s_addr) {
            is_open = 1;
            break;
        }
    }

    if (is_open) s->inst->nat_type = P2P_NAT_OPEN;
    else if (ctx->nat_is_port_consistent)
        s->inst->nat_type = P2P_NAT_FULL_CONE; // 满足端口一致性 → Cone NAT（无法区分 Full/Restricted/Port-Restricted，取最乐观估计）
    else s->inst->nat_type = P2P_NAT_SYMMETRIC;
    ctx->nat_probe_retries = -1/* 探测完成 */;

    print("I:", LA_F("%s: completed, mapped=%s:%d probe=%s:%d -> %s\n", LA_F104, 104),
          TASK_NAT_PROBE,
          inet_ntoa(ctx->public_addr.sin_addr), ntohs(ctx->public_addr.sin_port),
          inet_ntoa(probe_mapped.sin_addr),     ntohs(probe_mapped.sin_port),
          p2p_nat_type_str(s->inst->nat_type));
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 信令服务周期维护（拉取阶段）— 注册重试、保活
 * 
 * 处理向服务器发送的维护包：
 * - REGISTERING：重发 ONLINE（获取配对信息）
 * - WAIT_SYNC0_ACK：重发 SYNC0（等待服务器缓存首批候选）
 * - ONLINE/READY：发送 keepalive（保持槽位）
 */

void p2p_signal_compact_tick_recv(struct p2p_session *s) {

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;
    uint64_t now = P_tick_ms();

    // REGISTERING 状态：定期重发 ONLINE
    if (ctx->state == SIGNAL_COMPACT_WAIT_ONLINE_ACK) {

        if (tick_diff(now, ctx->last_send_time) >= ONLINE_INTERVAL_MS) {

            // 超时检查
            if (ctx->sig_attempts++ < MAX_SIG_ATTEMPTS) {

                print("I:", LA_F("%s: retry, (attempt %d/%d)\n", LA_F161, 161),
                      TASK_ONLINE, ctx->sig_attempts, MAX_SIG_ATTEMPTS);

                send_online(s);
                ctx->last_send_time = now;
            }
            else {

                print("W:", LA_F("%s: timeout, max(%d) attempts reached, reset to INIT\n", LA_F184, 184),
                      TASK_ONLINE, MAX_SIG_ATTEMPTS);

                ctx->state = SIGNAL_COMPACT_INIT;
            }
        }
    }
    // WAIT_SYNC0_ACK 状态：SYNC0 重传
    else if (ctx->state == SIGNAL_COMPACT_WAIT_SYNC0_ACK) {

        // SYNC0 重传（等待服务器缓存首批候选）
        if (tick_diff(now, ctx->last_send_time) >= ONLINE_INTERVAL_MS) {

            if (ctx->sig_attempts++ < MAX_SIG_ATTEMPTS) {

                print("I:", LA_F("SYNC0: retry, (attempt %d/%d)\n", LA_F611, 611),
                      ctx->sig_attempts, MAX_SIG_ATTEMPTS);

                send_compact_sync0(s);
                ctx->last_send_time = now;
            }
            else {

                print("W:", LA_F("SYNC0: timeout, max(%d) attempts reached, reset to INIT\n", LA_F612, 612),
                      MAX_SIG_ATTEMPTS);

                ctx->state = SIGNAL_COMPACT_INIT;
            }
        }
    }
    // ONLINE/WAIT_PEER/SYNCING/READY 状态：keepalive（保持服务器槽位活跃）
    else if (ctx->state == SIGNAL_COMPACT_ONLINE ||
             ctx->state == SIGNAL_COMPACT_WAIT_PEER ||
             ctx->state == SIGNAL_COMPACT_SYNCING ||
             ctx->state == SIGNAL_COMPACT_READY) {

        if (tick_diff(now, ctx->last_send_time) >= REGISTER_KEEPALIVE_INTERVAL_MS) {

            /*
             * 发送 ALIVE 保活包
             *
             * 包头: [type=SIG_PKT_ALIVE | flags=0 | seq=0]
             * 负载: [auth_key(8)]
             *   - auth_key: 客户端-服务器认证令牌（来自 ONLINE_ACK）
             * 说明: 保活包，维持服务器上的注册状态
             */
            {   const char* PROTO = "ALIVE";

                if (ctx->auth_key) {

                    uint8_t payload[SIG_PKT_ALIVE_PSZ];
                    nwrite_ll(payload, ctx->auth_key);

                    print("V:", LA_F("%s, sent on %s\n", LA_F67, 67),
                          PROTO, ctx->state == SIGNAL_COMPACT_WAIT_PEER ? "WAIT_PEER" : "READY");

                    ret_t ret = p2p_udp_send_packet(s, &ctx->server_addr, SIG_PKT_ALIVE, 0, 0, payload,
                                                    (int) sizeof(payload));
                    ctx->last_send_time = now;
                    if (ret < 0)
                        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F488, 488), 
                              PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
                    else {
                        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F489, 489),
                               PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
                               (int)sizeof(payload));
                        
                        // 通知路径管理器：ALIVE 是唯一需要 per-packet RTT 的信令包，rt_track=true
                        // 仅当 SIGNALING 作为 relay 路径被启用时才统计 RTT
                        if (s->inst->signaling.active) {
                            path_manager_on_packet_send(s, PATH_IDX_SIGNALING, 0, now, 0, true);
                        }
                    }
                }
                else print("W:", LA_F("%s skipped: auth_key=0\n", LA_F65, 65), PROTO); 
            }
        }
    }

    // 当前（请求端）处于等待（服务器返回的）REQ_ACK 的阶段
    if (ctx->req_state == 1/* waiting REQ_ACK */) {

        if (tick_diff(now, ctx->req_send_time) >= MSG_REQ_INTERVAL_MS) {

            /* 超时失败 */
            if (ctx->req_retries++ < MSG_REQ_MAX_RETRIES) {

                print("I:", LA_F("%s: retry(%d/%d) req (sid=%u)\n", LA_F159, 159),
                      TASK_RPC, ctx->req_retries, MSG_REQ_MAX_RETRIES, ctx->req_sid);

                send_rpc_req(s);
                ctx->req_send_time = now;

                if (ctx->state == SIGNAL_COMPACT_WAIT_SYNC0_ACK ||
                    ctx->state == SIGNAL_COMPACT_WAIT_PEER ||
                    ctx->state == SIGNAL_COMPACT_READY)
                    ctx->last_send_time = now;
            }
            else {

                uint16_t sid = ctx->req_sid;
                uint8_t  msg = ctx->req_msg;
                ctx->req_sid = 0;
                ctx->req_state = 0;

                print("W:", LA_F("%s: %s timeout after %d retries (sid=%u)\n", LA_F68, 68),
                      TASK_RPC, "req", MSG_REQ_MAX_RETRIES, sid);

                if (s->inst->cfg.on_response)
                    s->inst->cfg.on_response((p2p_session_t)s, sid, msg, NULL, -1, s->inst->cfg.userdata);
            }
        }
    }
    
    // 当前（响应端）处于等待（服务器返回的）RESP_ACK 的阶段
    if (ctx->resp_state == 1/* waiting RESP_ACK */) {

        if (tick_diff(now, ctx->resp_send_time) >= MSG_REQ_INTERVAL_MS) {

            /* 超时失败（与 A 端对称，使用相同的超时配置） */
            if (ctx->resp_retries++ < MSG_REQ_MAX_RETRIES) {

                print("I:", LA_F("%s: retry(%d/%d) resp (sid=%u)\n", LA_F160, 160),
                      TASK_RPC, ctx->resp_retries, MSG_REQ_MAX_RETRIES, ctx->resp_sid);

                send_rpc_resp(s);
                ctx->resp_send_time = now;

                if (ctx->state == SIGNAL_COMPACT_WAIT_SYNC0_ACK ||
                    ctx->state == SIGNAL_COMPACT_WAIT_PEER ||
                    ctx->state == SIGNAL_COMPACT_READY)
                    ctx->last_send_time = now;
            }
            else {

                uint16_t sid = ctx->resp_sid;
                ctx->resp_sid = 0;
                ctx->resp_state = 0;
                ctx->resp_session_id = 0;

                print("W:", LA_F("%s: %s timeout after %d retries (sid=%u)\n", LA_F68, 68),
                      TASK_RPC, "resp", MSG_REQ_MAX_RETRIES, sid);
            }
        }
    }
}

/*
 * 信令输出（推送阶段）— 向对端发送候选地址
 * 
 * 处理向对端发送的候选包：
 * - SYNCING：重发剩余候选和 FIN
 * - READY：发送新收集到的候选（如果有）
 */
void p2p_signal_compact_tick_send(struct p2p_session *s) {

    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;
    uint64_t now = P_tick_ms();

    // SYNCING 阶段：定期重发剩余候选和 FIN，直到收到对端确认（进入 READY）
    if (ctx->state == SIGNAL_COMPACT_SYNCING) {

        // SYNCING 超时：直接执行 OFFLINE + RE-REGISTER 即重新初始连接
        // + 由于候选同步是服务器中转实现，所以此时有可能是信令服务器的问题、也可能是对端网络问题，当然也可能是自己网络问题
        //   由于无法确认问题来自哪里，所以不能回退到 REGISTERED，因为三方如果都误认为是别人的问题，那就会一直卡在 SYNCING 阶段无法恢复了
        // ! 注意：如果 p2p 已经是连接过的状态
        //   由于候选支持 trickle 模式，所以 p2p 可能在 SYNCING 阶段变为已连接状态，即可能已经部分打洞连接成功
        //   此时如果 p2p 是可连接状态，那么问题应该出现在信令服务器或和服务器之间的网络，如果问题出现在之间的网络，那么是可能恢复的
        //   即使信号已经 lost，连接过的网络，也应该由应用来决定是否断开或重连
        if (s->state < P2P_STATE_LOST && tick_diff(now, ctx->last_send_time) >= SYNCING_TIMEOUT_MS) {

            char local_peer_id[P2P_PEER_ID_MAX];
            char remote_peer_id[P2P_PEER_ID_MAX];
            memcpy(local_peer_id, ctx->local_peer_id, sizeof(local_peer_id));
            memcpy(remote_peer_id, ctx->remote_peer_id, sizeof(remote_peer_id));
            local_peer_id[P2P_PEER_ID_MAX - 1] = '\0';
            remote_peer_id[P2P_PEER_ID_MAX - 1] = '\0';

            print("W:", LA_F("%s: timeout after %d ms, restarting signaling (OFFLINE + RE-REGISTER)\n", LA_F180, 180),
                  TASK_SYNCING, SYNCING_TIMEOUT_MS);

            // 重置双方协商信息
            reset_peer(ctx);

            // 重置 p2p 会话
            p2p_session_reset(s, false);

            // auth_key 和 session_id 将在服务器重新注册时重新分配
            ctx->auth_key = 0;
            ctx->session_id = 0;

            // 重新 ONLINE
            // + 这里无需向服务器发送 OFFLINE 包，因为协议上要求新的 rid 会确保服务器上的旧注册失效
            ctx->state = SIGNAL_COMPACT_WAIT_ONLINE_ACK;

            // 生成新的注册 rid
            uint32_t rid = ctx->instance_id;
            while (rid == ctx->instance_id) rid = P_rand32();
            ctx->instance_id = rid;

            // 发送 ONLINE 包
            send_online(s);
            ctx->sig_attempts = 1;
            ctx->last_send_time = P_tick_ms();

            // 重新发起 TURN Allocate（上一轮分配已随 SYNCING 重置失效）
            // todo ? turn 为啥 SYNCING 重置会失效，另外 turn 的生命周期管理设计？
            // turn 的公网地址应该会添加到本地候选队列，不应该失效呀
//            if (!instrument_option(P2P_INST_OPT_RELAY_OFF) && s->inst->cfg.turn_server) {
//                if (p2p_turn_allocate(s) == 0) {
//                    print("I:", LA_F("Requested Relay Candidate from TURN %s", LA_F286, 286), s->inst->cfg.turn_server);
//                }
//            }

            return;
        }

        if (ctx->candidates_mask == 0) {
            if (compact_wait_stun_candidates(s)) return;

            send_rest_candidates_and_fin(s);
            ctx->last_send_time = now;
            return;
        }

        // 如果有待发送的 trickle 候选，且超过了攒批间隔时间窗口
        if (ctx->trickle_last_pack_time && tick_diff(now, ctx->trickle_last_pack_time) >= TRICKLE_BATCH_MS
            && ctx->trickle_seq_next <= 16 && ctx->trickle_queue[ctx->trickle_seq_next] > 0) {
            send_trickle_candidates(s);
        }

        if (tick_diff(now, ctx->last_send_time) < SYNC_INTERVAL_MS) return;

        print("V:", LA_F("%s, retry remaining candidates and FIN to peer\n", LA_F66, 66), TASK_SYNCING);

        resend_rest_candidates_and_fin(s);
        ctx->last_send_time = now;
    }
    // READY 状态：检查是否有新候选需要发送（暂时为空，未来可扩展）
    else if (ctx->state == SIGNAL_COMPACT_READY) {

        // 延迟的本地候选（如 STUN/TURN）通过异步入口即时触发，这里无需额外处理
    }
}

/*
 * 根据 COMPACT 信令/探测状态推导并写入当前 NAT 检测结果到 s->inst->nat_type。
 * 由 p2p.c 在每次 update tick 中调用。
 */
void p2p_signal_compact_nat_detect_tick(struct p2p_session *s) {

    assert(s->inst->signaling_mode == P2P_SIGNALING_MODE_COMPACT);
    p2p_signal_compact_ctx_t *ctx = &s->inst->sig_compact_ctx;

    // 探测端口未知
    if (ctx->state == SIGNAL_COMPACT_INIT || ctx->state == SIGNAL_COMPACT_WAIT_ONLINE_ACK) {
        return;
    }
    // 不支持探测
    if (!ctx->probe_port) {
        // 服务器不支持 NAT probe，设置为 UNDETECTABLE
        if (s->inst->nat_type != P2P_NAT_UNDETECTABLE) {
            s->inst->nat_type = P2P_NAT_UNDETECTABLE;
        }
        return;
    }
    // 已经探测完成、或超时
    if (ctx->nat_probe_retries < 0) {
        return;
    }

    // 间隔等待
    uint64_t now = P_tick_ms();
    if (tick_diff(now, ctx->nat_probe_send_time) < NAT_PROBE_INTERVAL_MS) return;

    if (ctx->nat_probe_retries++ < NAT_PROBE_MAX_RETRIES) {

        print("V:", LA_F("%s: retry(%d/%d) probe\n", LA_F158, 158),
              TASK_NAT_PROBE, ctx->resp_retries, MSG_REQ_MAX_RETRIES);
        send_nat_probe(s);
        ctx->nat_probe_send_time = now;
    }
    else {

        s->inst->nat_type = P2P_NAT_TIMEOUT;
        ctx->nat_probe_retries = -2/* 探测超时 */;

        print("W:", LA_F("%s: timeout after %d retries , type unknown\n", LA_F181, 181), 
              MSG_REQ_MAX_RETRIES, TASK_NAT_PROBE);
    }
}

#pragma clang diagnostic pop
