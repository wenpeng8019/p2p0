/*
 * COMPACT 模式信令实现（UDP 无状态）
 *
 * 日志原则：
 * - printf 用于调试级别的日志输出; print() 用于正式级别的日志输出（V/I/W/E）
 * - 对于主控流程（如 xxx_tick）
 *   > 在状态变更、或执行子步骤前，输出 I 级日志说明当前步骤和状态
 *   > 操作结果出现异常时，输出 W/E 级日志说明异常情况
 * - 对于响应流程（如收到某个包）
 *   > 在收到包时，调试打印包的详细信息；
 *   > 处理协议过程中，如果出现错误，输出 W/E 级日志说明异常情况
 *   > 如果最终成功处理了协议包，最后输出 V 级日志说明处理结果（如确认收到、状态变更等）
 * - 对于发包操作
 *   > 在发送包前，调试打印包的详细信息（目的地址、包类型、序列号、负载长度等）
 *   > 如果发送操作失败，输出 W/E 级日志说明异常情况
 *   > 如果发送成功，输出 V 级日志说明发送结果，包括包的关键信息
 */
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#define MOD_TAG "COMPACT"

#include "p2p_internal.h"
#include "p2p_probe.h"

#define REGISTER_INTERVAL_MS            1000    /* 注册重发间隔 */
#define PEER_INFO_INTERVAL_MS           500     /* PEER_INFO 重发间隔 */
#define MAX_REGISTER_ATTEMPTS           10      /* 最大 REGISTER 重发次数 */
#define REGISTER_KEEPALIVE_INTERVAL_MS  20000   /* REGISTERED 状态保活重注册间隔（防服务器超时清除槽位） */
#define TRICKLE_BATCH_MS                1000    /* TURN trickle 攒批窗口（多个 TURN 响应在此窗口内合并为一个包） */
#define MAX_CANDS_PER_PACKET            10      /* 每个 PEER_INFO 包最大候选数 */
#define NAT_PROBE_MAX_RETRIES           3       /* NAT_PROBE 最大发送次数 */
#define NAT_PROBE_INTERVAL_MS           1000    /* NAT_PROBE 重发间隔 */
#define ICE_TIMEOUT_MS                  30000   /* ICE 状态超时（30秒），防止永久停留 */

#define MSG_REQ_INTERVAL_MS             500     /* MSG_REQ 重发间隔 */
#define MSG_REQ_MAX_RETRIES             5       /* MSG_REQ 最大重发次数，超出后报超时失败 */

///////////////////////////////////////////////////////////////////////////////

#define TASK_REG                        "REGISTER"
#define TASK_ICE                        "ICE"
#define TASK_ICE_REMOTE                 "ICE REMOTE"
#define TASK_NAT_PROBE                  "NAT PROBE"
#define TASK_RPC                        "RPC"

/*
 * 解析 PEER_INFO 负载，追加到 session 的 remote_cands[]
 * 注意：这里对方的候选列表顺序并未按对方原始顺序排序，而是 FIFO 追加到 remote_cands[] 中
 *
 * 格式: [session_id(8)][base_index(1)][candidate_count(1)][candidates(N*23)]
 */
static void unpack_remote_candidates(p2p_session_t *s, const uint8_t *payload, int cand_cnt) {

    assert(s->remote_cand_cnt + cand_cnt <= s->remote_cand_cap);

    int offset = sizeof(uint64_t) + 2;  // 第一个 candidates 列表的起始位置
    p2p_remote_candidate_entry_t*c;

    // 对于 peer_info0, 即由 server 维护的双方握手包
    if (payload[sizeof(uint64_t)]/* base_index */ == 0) {

        unpack_candidate(c = &s->remote_cands[0], payload + offset);
        if (s->remote_cand_cnt == 0) s->remote_cand_cnt = 1;

        // 其首个地址候选（idx=0）肯定有效，即肯定是（服务器观察到的）对方公网地址
        if ((p2p_cand_type_t)c->type != P2P_CAND_SRFLX) {

            print("W:", LA_F("%s: unexpected non-srflx cand in peer_info0, treating as srflx\n", LA_F384, 384),
                  TASK_ICE_REMOTE);

            c->type = P2P_CAND_SRFLX;
        }

        print("I:", LA_F("%s: peer_info0 srflx cand[%d]<%s:%d>%s\n", LA_F133, 133),
                        TASK_ICE_REMOTE, 0, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port),
                        instrument_option(P2P_INST_OPT_ICE_SRFLX_OFF) ? ", ignored due to instrument" : "");

        if (instrument_option(P2P_INST_OPT_ICE_SRFLX_OFF)) --s->remote_cand_cnt;
        else { s->remote_srflx_cnt++;

            // 这里启动打洞需要依赖于信令服务器的 REGISTER_ACK 包中携带的 relay_support 标志
            // + 该标志用于决定所使用的冷打洞机制，如果冷打洞不依赖服务器中转，则打洞可以不依赖 REGISTER_ACK 包
            if (s->sig_compact_ctx.state >= SIGNAL_COMPACT_REGISTERED && nat_punch(s, 0) != E_NONE) {
                print("W:", LA_F("%s: punch remote cand[%d]<%s:%d> failed\n", LA_F128, 128),
                    TASK_ICE_REMOTE, 0, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
            }
        }

        --cand_cnt; offset += (int)sizeof(p2p_candidate_t);
    }
    // 如果 peer_info0 以外的其他 info 包先到达，则需要保留 cand[0] 给对方（在 info0 中的）的公网地址
    else if (s->remote_cand_cnt == 0 && !instrument_option(P2P_INST_OPT_ICE_SRFLX_OFF)) {
        memset(&s->remote_cands[0], 0, sizeof(p2p_remote_candidate_entry_t));
        s->remote_cand_cnt = 1;
    }

    for (int i = 0, idx; i < cand_cnt; i++, offset += (int)sizeof(p2p_candidate_t)) {

        unpack_candidate(c = &s->remote_cands[idx = s->remote_cand_cnt], payload + offset);
        if (p2p_find_remote_candidate_by_addr(s, &c->addr) >= 0) {
            print("W:", LA_F("%s: duplicate remote cand<%s:%d> from signaling, skipped\n", LA_F165, 165),
                  TASK_ICE_REMOTE, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
            continue;
        }
        ++s->remote_cand_cnt;

        // 对于 relay 候选类型，无需打洞
        if (c->type == P2P_CAND_RELAY) {

            print("I:", LA_F("%s: remote relay cand[%d]<%s:%d>%s\n", LA_F382, 382),
                  TASK_ICE_REMOTE, idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port),
                  instrument_option(P2P_INST_OPT_ICE_RELAY_OFF) ? ", ignored due to instrument" : "");

            if (instrument_option(P2P_INST_OPT_ICE_RELAY_OFF)) {
                --s->remote_cand_cnt;
                continue;
            }
            
            s->remote_relay_cnt++;

            // 注册到路径管理器（设置为 ACTIVE 状态）
            path_manager_set_path_state(s, idx, PATH_STATE_ACTIVE);

            continue;
        }

        const char* type_str;
        if (c->type == P2P_CAND_HOST) type_str = "host";
        else if (c->type == P2P_CAND_SRFLX) type_str = "srflx";
        else { --s->remote_cand_cnt;
            print("E:", LA_F("%s: unexpected remote cand type %d, skipped\n", LA_F385, 385),
                  TASK_ICE_REMOTE, c->type);
            continue;
        }

        if (instrument_option(c->type == P2P_CAND_SRFLX ? P2P_INST_OPT_ICE_SRFLX_OFF : P2P_INST_OPT_ICE_HOST_OFF)) {
            --s->remote_cand_cnt;
            print("I:", LA_F("%s: remote %s cand[%d]<%s:%d>, ignored due to instrument\n", LA_F381, 381),
                  TASK_ICE_REMOTE, type_str, idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
            continue;
        }

        print("I:", LA_F("%s: remote %s cand[%d]<%s:%d> accepted\n", LA_F383, 383),
              TASK_ICE_REMOTE, type_str, idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));

        s->remote_srflx_cnt++;

        if (s->sig_compact_ctx.state < SIGNAL_COMPACT_REGISTERED) continue;

        if (nat_punch(s, idx) != E_NONE)
            print("E:", LA_F("%s: punch remote cand[%d]<%s:%d> failed\n", LA_F128, 128),
                  TASK_ICE_REMOTE, idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
    }
}

/* 一个 PEER_INFO 包所承载的候选数量（单位）
 * + 这里 10（字节）表示 PEER_INFO 负载头：[session_id(8)][base_index(1)][candidate_count(1)] = 10 字节
 *   负载头后面的剩余空间就是候选列表，通过预定义、和 MTU 上限共同限制计算得出该单位大小
 */
#define PEER_INFO_CAND_UNIT \
    (((P2P_MAX_PAYLOAD - sizeof(uint64_t) - 2) / (int)sizeof(p2p_candidate_t)) < MAX_CANDS_PER_PACKET \
     ? ((P2P_MAX_PAYLOAD - sizeof(uint64_t) - 2) / (int)sizeof(p2p_candidate_t)) \
     : MAX_CANDS_PER_PACKET)

/*
 * 构建 PEER_INFO 的候选队列，返回 payload 总长度
 *
 * 格式: [session_id(8)][base_index(1)][candidate_count(1)][candidates(N*23)]
 */
static int pack_local_candidates(p2p_session_t *s, uint16_t seq, uint8_t *payload, uint8_t *r_flags) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    int base_index, cnt;

    // 对于 trickle 候选队列的前面部分，也就是首批一次性发送的候选队列
    // + 此时 ICE 包的候选队列数量都是以 PEER_INFO_CAND_UNIT 为单位的
    if (seq < ctx->trickle_seq_base) {

        const int cand_unit = PEER_INFO_CAND_UNIT;
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

    // 对于最后一个 PEER_INFO 包，设置 FIN 标志
    if (seq == 16 || (!s->turn_pending && base_index + cnt >= s->local_cand_cnt)) {
        *r_flags |= SIG_PEER_INFO_FIN;
    }

    payload[sizeof(uint64_t)] = (uint8_t)base_index;
    payload[sizeof(uint64_t) + 1] = (uint8_t)cnt;

    int offset = sizeof(uint64_t) + 2; // 负载头：[session_id(8)][base_index(1)][candidate_count(1)]
    for (int i = 0; i < cnt; i++) { int idx = base_index + i;
        offset += pack_candidate(&s->local_cands[idx], payload + offset);
    }

    return offset;
}

/*
 * 向信令服务器请求 REGISTER 操作
 *
 * 协议：SIG_PKT_REGISTER (0x80)
 * 包头: [type=0x80 | flags=0 | seq=0]
 * 负载: [local_peer_id(32)][remote_peer_id(32)][instance_id(4)][candidate_count(1)][candidates(N*7)]
 */
static void send_register(p2p_session_t *s) {
    const char* PROTO = "REGISTER";

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    assert(ctx->state == SIGNAL_COMPACT_REGISTERING);

    // 默认计算 REGISTER:MTU 包中最多能承载的候选数量
    // + 该值随后会根据 SIG_PKT_REGISTER_ACK 的 max_candidates 值进行修正
    const int header_size = P2P_PEER_ID_MAX * 2 + 4 + 1;
    assert(P2P_MAX_PAYLOAD >= header_size);
    int n = P2P_MAX_PAYLOAD - header_size; if (n < 0) n = 0;
    int cand_cnt = n / (int)sizeof(p2p_candidate_t)/* 23 */;
    if (cand_cnt > s->local_cand_cnt) cand_cnt = s->local_cand_cnt;
    ctx->candidates_cached = cand_cnt;

    // local_peer_id / remote_peer_id（各 32 字节，超过部分截断，不足部分补零）
    uint8_t payload[P2P_MAX_PAYLOAD];
    memset(payload, 0, n = P2P_PEER_ID_MAX * 2);
    memcpy(payload, ctx->local_peer_id, strlen(ctx->local_peer_id));
    memcpy(payload + P2P_PEER_ID_MAX, ctx->remote_peer_id, strlen(ctx->remote_peer_id));

    // instance_id（4 字节大端序）
    nwrite_l(payload + n, ctx->instance_id); n += 4;

    // candidates (每个 23 字节: type + addr + priority)
    payload[n++] = (uint8_t)cand_cnt;
    for (int i = 0; i < cand_cnt; i++) {
        n += pack_candidate(&s->local_cands[i], payload + n);
    }

    print("V:", LA_F("%s sent, inst_id=%u, cands=%d\n", LA_F58, 58), PROTO, ctx->instance_id, cand_cnt);

    ret_t ret = udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_REGISTER, 0, 0, payload, n);
    if (ret < 0)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F355, 355), 
              PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F358, 358),
               PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), n);
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
    const char* PROTO = "PEER_INFO";

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    assert(ctx->state == SIGNAL_COMPACT_ICE && ctx->session_id);

    // 计算剩余候选数量
    // + 注意，此时的剩余数量，是此刻的剩余数量；因为候选队列可能是动态增加的（如 TURN Allocate 后追加的 Relay 候选）
    int cand_remain = s->local_cand_cnt - ctx->candidates_cached;
    if (cand_remain < 0) cand_remain = 0;

    // 至少发送一个包（即使没有剩余候选），以确保对方收到 FIN 信号
    const int cand_unit = PEER_INFO_CAND_UNIT;
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
    nwrite_ll(payload, ctx->session_id);

    for (uint16_t seq = 1; seq <= (uint16_t)pkt_cnt; seq++) {

        uint8_t flags = 0; int payload_len = pack_local_candidates(s, seq, payload, &flags);

        ret_t ret = udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_PEER_INFO, flags, seq, payload, payload_len);
        if (ret < 0)
            print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F355, 355), 
                  PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
        else
            printf(LA_F("[UDP] %s send to %s:%d, seq=%u, flags=0x%02x, len=%d\n", LA_F357, 357),
                   PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
                   seq, flags, payload_len);
    }

    // 首批发完后，如果还有 TURN 异步收集未完成，启动攒批计时器
    if (s->turn_pending > 0)
        ctx->trickle_last_pack_time = P_tick_ms();

    print("V:", LA_F("%s sent, total=%d (ses_id=%" PRIu64 ")\n", LA_F63, 63), PROTO, pkt_cnt, ctx->session_id);
}

/*
 * 向对方发送新收集到的中继候选（Trickle ICE）
 *
 * 行为：
 *   1. 在现有候选窗口后追加一个 PEER_INFO 包（seq = 已发包数 + 1）
 *   2. 扩展 candidates_mask，将新包纳入确认窗口
 *   3. 当所有 TURN 收集完毕（turn_pending==0）且候选已全部打包时，pack 自动附带 FIN
 *   4. 如果当前状态已到 READY（前序包均已确认），回退到 ICE 以等待新包确认
 *   5. 支持攒批：多个 TURN 响应在 TRICKLE_BATCH_MS 窗口内合并为一个包
 */

/* 将攒批累积的 trickle 候选打包发送（trickle_turn 和 tick flush 共用） */
static void send_trickle_candidates(p2p_session_t *s) {
    const char* PROTO = "PEER_INFO(trickle)";

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    uint16_t seq = ctx->trickle_seq_next;
    assert(seq <= 16 && ctx->trickle_queue[seq] > 0);

    // 扩展确认窗口
    ctx->candidates_mask |= (uint16_t)(1u << (seq - 1));
    ctx->trickle_seq_next++;
    if (ctx->state == SIGNAL_COMPACT_READY) ctx->state = SIGNAL_COMPACT_ICE;

    int new_cands = ctx->trickle_queue[seq];

    uint8_t payload[P2P_MAX_PAYLOAD];
    nwrite_ll(payload, ctx->session_id);

    uint8_t flags = 0;
    int payload_len = pack_local_candidates(s, seq, payload, &flags);

    print("I:", LA_F("%s: trickled %d cand(s), seq=%u (ses_id=%" PRIu64 ")\n", LA_F161, 161),
          PROTO, new_cands, seq, ctx->session_id);

    ret_t ret = udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_PEER_INFO, flags, seq, payload, payload_len);
    if (ret < 0)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F355, 355), 
              PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=%u, flags=0x%02x, len=%d\n", LA_F357, 357),
               PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
               seq, flags, payload_len);

    ctx->last_send_time = P_tick_ms();
    ctx->trickle_last_pack_time = s->turn_pending > 0 ? ctx->last_send_time : 0;
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
    const char* PROTO = "PEER_INFO";

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    assert(ctx->state == SIGNAL_COMPACT_ICE && ctx->session_id);
    assert((ctx->candidates_acked & ctx->candidates_mask) != ctx->candidates_mask);

    // session_id 所有包相同，只写一次
    uint8_t payload[P2P_MAX_PAYLOAD];
    nwrite_ll(payload, ctx->session_id);

    uint16_t seq = 1; int pkt_cnt = 0;
    for (; seq <= 16; seq++) {
        uint16_t bit = (uint16_t)(1u << (seq - 1));
        if ((ctx->candidates_mask & bit) == 0) break;           // 遇到第一个 0 就可以停止循环（mask 是低位连续段，高位全为 0）
        if ((ctx->candidates_acked & bit) != 0) continue;

        uint8_t flags = 0;
        int payload_len = pack_local_candidates(s, seq, payload, &flags);

        ret_t ret = udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_PEER_INFO, flags, seq, payload, payload_len);
        if (ret < 0)
            print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F355, 355), 
                  PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
        else
            printf(LA_F("[UDP] Resend %s to %s:%d, seq=%u, flags=0x%02x, len=%d\n", LA_F362, 362),
                   PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
                   seq, flags, payload_len);
        pkt_cnt++;
    }

    if (pkt_cnt) print("V:", LA_F("%s resent, %d/%d\n", LA_F54, 54), PROTO, pkt_cnt, seq - 1);
}

/*
 * 通过服务器向对端发送 RPC 消息请求
 *
 * 协议：SIG_PKT_MSG_REQ (0x20)
 * 包头: [type=0x20 | flags=0 | seq=0]
 * 负载: [session_id(8)][sid(2)][msg(1)][data(N)]
 *   - session_id: 本端会话 ID（来自 REGISTER_ACK）
 *   - sid: 序列号（2字节，网络字节序），用于匹配响应
 *   - msg: 消息 ID（1字节，用户自定义）
 *   - data: 消息数据（可选）
 */
static void send_rpc_req(struct p2p_session *s) {
    const char* PROTO = "MSG_REQ";

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    uint8_t payload[8 + 3 + P2P_MSG_DATA_MAX]; int n = 0;
    nwrite_ll(payload + n, ctx->session_id); n += 8;
    nwrite_s(payload + n, ctx->req_sid); n += 2;
    payload[n++] = ctx->req_msg;
    if (ctx->req_data_len > 0) {
        memcpy(payload + n, ctx->req_data, (size_t)ctx->req_data_len);
        n += ctx->req_data_len;
    }

    print("V:", LA_F("%s sent, sid=%u, msg=%u, size=%d\n", LA_F61, 61),
          PROTO, ctx->req_sid, ctx->req_msg, ctx->req_data_len);

    ret_t ret = udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_MSG_REQ, 0, 0, payload, n);
    if (ret < 0)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F355, 355), 
              PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F358, 358),
               PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), n);
}

/*
 * （对端）通过服务器向源端回复 RPC 消息响应
 *
 * 协议：SIG_PKT_MSG_RESP (0x22)
 * 包头: [type=0x22 | flags=0 | seq=0]
 * 负载: [session_id(8)][sid(2)][code(1)][data(N)]
 *   - session_id: A端的会话 ID（8字节，网络字节序）
 *   - sid: 序列号，必顾与 MSG_REQ 中的 sid 一致
 *   - code: 响应码
 *   - data: 响应数据
 */
static void send_rpc_resp(struct p2p_session *s) {
    const char* PROTO = "MSG_RESP";

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    uint8_t payload[8 + 2 + 1 + P2P_MSG_DATA_MAX]; int n = 0;
    nwrite_ll(payload, ctx->resp_session_id); n = sizeof(uint64_t);
    nwrite_s(payload + n, ctx->resp_sid); n += 2;
    payload[n++] = ctx->resp_code;
    if (ctx->resp_data_len > 0) {
        memcpy(payload + n, ctx->resp_data, (size_t)ctx->resp_data_len);
        n += ctx->resp_data_len;
    }

    print("V:", LA_F("%s: sent, sid=%u, code=%u, size=%d\n", LA_F142, 142),
          PROTO, ctx->resp_sid, ctx->resp_code, ctx->resp_data_len);

    ret_t ret = udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_MSG_RESP, 0, 0, payload, n);
    if (ret < 0)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F355, 355), 
              PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F358, 358),
               PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), n);
}

/*
 * 发送 NAT_PROBE 探测包
 * 即向服务器探测端口发送空包，服务器观察源地址来判断 NAT 类型
 *
 * 协议：SIG_PKT_NAT_PROBE (0x07)
 * 包头: [type=0x07 | flags=0 | seq=探测重试次数]
 * 负载: 空
 */
static void send_nat_probe(struct p2p_session *s) {
    const char* PROTO = "NAT_PROBE";

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    struct sockaddr_in probe_addr = ctx->server_addr;
    probe_addr.sin_port = htons(ctx->probe_port);

    print("V:", LA_F("%s sent, seq=%u\n", LA_F60, 60), PROTO, ctx->nat_probe_retries);

    ret_t ret = udp_send_packet(s->sock, &probe_addr, SIG_PKT_NAT_PROBE, 0, ctx->nat_probe_retries, NULL, 0);
    if (ret < 0)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F355, 355), 
              PROTO, inet_ntoa(probe_addr.sin_addr), ctx->probe_port, E_EXT_CODE(ret));
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=%u, flags=0, len=0\n", LA_F356, 356),
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
 * 开始信令交换（发送 REGISTER）
 */
ret_t p2p_signal_compact_connect(struct p2p_session *s, const char *local_peer_id, const char *remote_peer_id,
                                 const struct sockaddr_in *server) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    P_check(ctx->state == SIGNAL_COMPACT_INIT, return E_NONE_CONTEXT;)

    ctx->state = SIGNAL_COMPACT_REGISTERING;
    ctx->server_addr = *server;

    // 每次 connect() 生成新的实例 ID（加密安全随机数），用于服务器区分重新连接会话
    uint32_t rid = ctx->instance_id;
    while (rid == ctx->instance_id) rid = P_rand32();
    ctx->instance_id = rid;

    // local_peer_id / remote_peer_id
    memset(ctx->local_peer_id, 0, sizeof(ctx->local_peer_id));
    memset(ctx->remote_peer_id, 0, sizeof(ctx->remote_peer_id));
    strncpy(ctx->local_peer_id, local_peer_id, P2P_PEER_ID_MAX - 1);
    strncpy(ctx->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX - 1);
    ctx->local_peer_id[P2P_PEER_ID_MAX - 1] = '\0';
    ctx->remote_peer_id[P2P_PEER_ID_MAX - 1] = '\0';

    // 构造并发送带候选列表的注册包
    send_register(s);
    ctx->register_attempts = 1;
    ctx->last_send_time = P_tick_ms();

    return E_NONE;
}

/*
 * 断开连接，通知服务器注销配对，这会清理服务器上的会话状态
 *
 * 协议：SIG_PKT_UNREGISTER (0x06)
 * 包头: [type=0x06 | flags=0 | seq=0]
 * 负载: [local_peer_id(32)][remote_peer_id(32)]
 *   - local_peer_id: 本地 peer_id（32字节，0填充）
 *   - remote_peer_id: 远端 peer_id（32字节，0填充）
 */
ret_t p2p_signal_compact_disconnect(struct p2p_session *s) {
    const char* PROTO = "UNREGISTER";

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    P_check(ctx->state != SIGNAL_COMPACT_INIT, return E_NONE_CONTEXT;)

    uint8_t payload[P2P_PEER_ID_MAX * 2];
    memset(payload, 0, sizeof(payload));
    memcpy(payload, ctx->local_peer_id, strnlen(ctx->local_peer_id, P2P_PEER_ID_MAX));
    memcpy(payload + P2P_PEER_ID_MAX, ctx->remote_peer_id, strnlen(ctx->remote_peer_id, P2P_PEER_ID_MAX));

    print("V:", LA_F("%s sent, inst_id=%u\n", LA_F59, 59), PROTO, ctx->instance_id);

    ret_t ret = udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_UNREGISTER, 0, 0, payload, (int)sizeof(payload));
    if (ret < 0)
        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F355, 355), 
              PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
    else
        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F358, 358),
               PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
               (int)sizeof(payload));

    p2p_signal_compact_init(ctx);
    return E_NONE;
}

void p2p_signal_compact_trickle_turn(p2p_session_t *s) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    if (ctx->state != SIGNAL_COMPACT_ICE && ctx->state != SIGNAL_COMPACT_READY) return;
    if (!ctx->session_id) return;

    uint16_t seq = ctx->trickle_seq_next;
    if (seq > 16) {
        print("W:", LA_F("PEER_INFO(trickle): seq overflow, cannot trickle more\n", LA_F250, 250));
        return;
    }

    // O(1) 累加：每次 TURN 响应带来 1 个新候选
    ctx->trickle_queue[seq]++;

    // 攒批间隔控制（固定窗口策略）
    uint64_t now = P_tick_ms();
    if (ctx->trickle_last_pack_time && tick_diff(now, ctx->trickle_last_pack_time) < TRICKLE_BATCH_MS) {
        print("V:", LA_F("PEER_INFO(trickle): batching, queued %d cand(s) for seq=%u\n", LA_F249, 249),
              ctx->trickle_queue[seq], seq);
        return;
    }

    send_trickle_candidates(s);
}

/*
 * 通过 COMPACT 信令中转发送数据包（通用接口）
 *
 * 协议：P2P_PKT_* + P2P_DATA_FLAG_SESSION (0x01)
 * 包头：[type | flags|P2P_DATA_FLAG_SESSION | seq]
 * 负载：[session_id(8)][原始 payload]
 *
 * 用于所有需要通过信令服务器转发的包类型（REACH/DATA/ACK/CRYPTO 等）。
 */
ret_t p2p_signal_compact_relay(struct p2p_session *s,
                               uint8_t type, uint8_t flags, uint16_t seq,
                               const void *payload, uint16_t payload_len) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    P_check(!payload_len || payload, return E_INVALID;)
    P_check(ctx->relay_support, return E_NO_SUPPORT;)
    P_check(ctx->session_id, return E_NONE_CONTEXT;)

    // 构造完整负载：[session_id(8)][原始 payload]
    uint8_t relay_payload[P2P_MAX_PAYLOAD];
    if (payload_len + 8 > P2P_MAX_PAYLOAD) {
        print("E:", LA_F("COMPACT relay payload too large: %d", LA_F433, 433), payload_len);
        return E_OUT_OF_CAPACITY;
    }
    
    nwrite_ll(relay_payload, ctx->session_id);
    if (payload_len > 0 && payload)
        memcpy(relay_payload + 8, payload, payload_len);

    // 发送包，自动添加 P2P_DATA_FLAG_SESSION 标志
    ret_t ret = udp_send_packet(s->sock, &ctx->server_addr, type,
                                flags | P2P_DATA_FLAG_SESSION, seq,
                                relay_payload, 8 + payload_len);
    
    if (ret >= 0) {
        print("V:", LA_F("COMPACT relay: type=0x%02x, seq=%u (session_id=%" PRIu64 ")", LA_F422, 422),
              type, seq, ctx->session_id);
    } else {
        print("E:", LA_F("COMPACT relay send failed: type=0x%02x, ret=%d", LA_F434, 434),
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
    // 至少需要 session_id(8)
    if (*len < (int)sizeof(uint64_t)) {
        print("E:", LA_F("%s: bad payload(len=%d, need >=8)\n", LA_F392, 392), proto_name, *len);
        return false;
    }

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    uint64_t session_id = nget_ll(*payload);

    // 验证 session_id 匹配
    if (session_id != ctx->session_id) {
        print("W:", LA_F("%s: session mismatch(local=%" PRIu64 ", pkt=%" PRIu64 ")\n", LA_F145, 145),
              proto_name, ctx->session_id, session_id);
        return false;
    }

    print("V:", LA_F("%s: session validated, len=%d (ses_id=%" PRIu64 ")\n", LA_F88, 88),
          proto_name, *len, session_id);

    // 跳过 session_id 头部
    *payload += sizeof(uint64_t);
    *len -= (int)sizeof(uint64_t);
    return true;
}

/*
 * （通过服务器）向对端发起 RPC 消息请求
 */
ret_t p2p_signal_compact_request(struct p2p_session *s,
                                 uint8_t msg, const void *data, int len) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    P_check(len == 0 || data, return E_INVALID;)
    P_check(len >= 0 && len <= P2P_MSG_DATA_MAX, return E_INVALID;)
    P_check(ctx->state >= SIGNAL_COMPACT_REGISTERED, return E_NONE_CONTEXT;)    // 未注册
    P_check(ctx->msg_support, return E_NO_SUPPORT;)                             // 服务器不支持 MSG
    
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
 * （对端）通过服务器向源端回复 RPC 消息响应
 */
ret_t p2p_signal_compact_response(struct p2p_session *s,
                                  uint8_t code, const void *data, int len) {
    const char* PROTO = "MSG_RESP";

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    P_check(len >= 0 && len <= P2P_MSG_DATA_MAX, return E_INVALID;)
    P_check(len == 0 || data, return E_INVALID;)
    P_check(ctx->resp_sid != 0, return E_INVALID;)

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
 * 处理 REGISTER_ACK，服务器注册确认
 *
 * 协议：SIG_PKT_REGISTER_ACK (0x81)
 * 包头: [type=0x81 | flags=见下 | seq=0]
 * 负载: [status(1) | session_id(8) | instance_id(4) | max_candidates(1) | public_ip(4) | public_port(2) | probe_port(2)]
 *   - status: 0=对端离线, 1=对端在线, >=2=错误码
 *   - session_id: 本端会话 ID（网络字节序，64位）
 *   - max_candidates: 服务器缓存的最大候选数量（0=不支持缓存）
 *   - public_ip/port: 客户端的公网地址（服务器观察到的 UDP 源地址）
 *   - probe_port: NAT 探测端口（0=不支持探测）
 *   - flags: SIG_REGACK_FLAG_RELAY (0x01) 表示服务器支持中继
 */
void compact_on_register_ack(struct p2p_session *s, uint16_t seq, uint8_t flags,
                              const uint8_t *payload, int len,
                              const struct sockaddr_in *from) {
    const char* PROTO = "REGISTER_ACK";

    printf(LA_F("[UDP] %s recv from %s:%d, seq=%u, flags=0x%02x, len=%d\n", LA_F352, 352),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), seq, flags, len);

    if (len < 22) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F95, 95), PROTO, len);
        return;
    }

    uint8_t status = payload[0];
    if (status >= 2) {
        print("E:", LA_F("%s: status error(%d)\n", LA_F150, 150), PROTO, status);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    if (ctx->state != SIGNAL_COMPACT_REGISTERING) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F113, 113), PROTO, (int)ctx->state);
        return;
    }

    // 验证 echo instance_id（确保 ACK 对应当前注册请求）
    uint32_t ack_instance_id = 0;
    nread_l(&ack_instance_id, payload + 9);
    if (ack_instance_id != ctx->instance_id) {
        print("V:", LA_F("%s: stale ACK(ack_inst=%u local_inst=%u), ignored\n", LA_F147, 147),
              PROTO, ack_instance_id, ctx->instance_id);
        return;
    }

    ctx->relay_support = (flags & SIG_REGACK_FLAG_RELAY) != 0;      // 服务器是否支持数据中继转发
    ctx->msg_support   = (flags & SIG_REGACK_FLAG_MSG)   != 0;      // 服务器是否支持 MSG RPC
    uint64_t ack_session_id = nget_ll(payload + 1);
    uint8_t max_candidates = payload[13];

    if (ctx->candidates_cached > max_candidates)      // 计算服务器实际缓存的候选数量，作为后续发送 PEER_INFO 包的基准
        ctx->candidates_cached = max_candidates;

    // 根据服务器能力设置探测状态
    if (ctx->msg_support) {
        s->probe_ctx.state = P2P_PROBE_STATE_READY;
    } else {
        s->probe_ctx.state = P2P_PROBE_STATE_NO_SUPPORT;
    }

    // 解析自己的公网地址（服务器主端口探测到的 UDP 源地址）
    memset(&ctx->public_addr, 0, sizeof(ctx->public_addr));
    ctx->public_addr.sin_family = AF_INET;
    memcpy(&ctx->public_addr.sin_addr.s_addr, payload + 14, 4);
    memcpy(&ctx->public_addr.sin_port, payload + 18, 2);

    // 解析服务器提供的 NAT 探测端口，0 表示服务器不支持
    nread_s(&ctx->probe_port, payload + 20);

    // REGISTER_ACK 直接下发本端 session_id
    if (!ctx->session_id) ctx->session_id = ack_session_id;
    else if (ctx->session_id != ack_session_id) {

        print("E:", LA_F("%s: session mismatch(local=%" PRIu64 " ack=%" PRIu64 ")\n", LA_F143, 143),
              PROTO, ctx->session_id, ack_session_id);

        // 之前"提前"收到的 PEER_INFO 包都是无效的，以 REGISTER_ACK 的 ses_id 为准
        ctx->session_id = ack_session_id;
        ctx->peer_online = false;

        ctx->remote_candidates_mask = 0;
        ctx->remote_candidates_done = 0;
        ctx->remote_candidates_0 = false;

        p2p_session_reset(s, false);
    }

    print("V:", LA_F("%s: accepted, public=%s:%d ses_id=%" PRIu64 " max_cands=%d probe_port=%d relay=%s msg=%s\n", LA_F90, 90),
          PROTO, inet_ntoa(ctx->public_addr.sin_addr), ntohs(ctx->public_addr.sin_port), 
          ctx->session_id, max_candidates, ctx->probe_port,
          ctx->relay_support ? "yes" : "no",
          ctx->msg_support   ? "yes" : "no");

    // 如果对方在线
    // + 注意，此时对方可能已经是在线状态，也就是 SIG_PKT_PEER_INFO 可能先于 SIG_PKT_REGISTER_ACK 到达
    if (status == SIG_REGACK_PEER_ONLINE) ctx->peer_online = true;

    // 标记进入 REGISTERED 状态（该状态将停止周期发送 REGISTER）
    ctx->state = SIGNAL_COMPACT_REGISTERED;
    print("I:", LA_F("REGISTERED: peer=%s\n", LA_F266, 266), ctx->peer_online ? "online" : "offline");
    if (s->state == P2P_STATE_REGISTERING) s->state = P2P_STATE_REGISTERED;

    // 如果对方在线，直接进入 ICE 阶段，发送后续候选队列和 FIN 包
    // + 这里可能存在两种情况：
    //   1. PEER_INFO 先于 REGISTER_ACK 到达，此时先到达的 PEER_INFO 会先将 peer_online 标记为 true
    //      注意，并发场景下，此时 REGISTER_ACK 可能并未携带 SIG_REGACK_PEER_ONLINE 标识
    //   2. REGISTER_ACK 先到达，且携带 SIG_REGACK_PEER_ONLINE 标识，此时直接标记 peer_online 为 true
    if (ctx->peer_online) {

        print("I:", LA_F("%s: peer online, proceeding to ICE\n", LA_F126, 126), PROTO);
        ctx->state = SIGNAL_COMPACT_ICE;
        send_rest_candidates_and_fin(s);
        ctx->last_send_time = P_tick_ms();
    }

    // 如果服务器支持 NAT 探测端口，则启动 NAT_PROBE 探测流程
    if (ctx->probe_port > 0) {

        // 标记进入 NAT_PROBE 探测中状态，发送第一轮探测包
        s->nat_type = P2P_NAT_DETECTING;
        print("I:", LA_F("%s: started, sending first probe\n", LA_F149, 149), TASK_NAT_PROBE);
        ctx->nat_probe_retries = 0/* 初始化启动探测 */;
        send_nat_probe(s);
        ctx->nat_probe_send_time = P_tick_ms();
    }
    else s->nat_type = P2P_NAT_UNDETECTABLE;

    // 如果服务器支持数据中继，提前将 SIGNALING 路径添加到路径管理器（作为 fallback）
    // 这样当 P2P 打洞失败或连接断开时，可以自动降级到信令服务器中转
    if (ctx->relay_support && !s->signaling.active) {
        path_manager_enable_signaling(s, &ctx->server_addr);
        print("I:", LA_F("SIGNALING path enabled (server supports relay)\n", LA_F398, 398));
    }

    // 启动数据中继功能（如果服务器支持）
    assert(!s->signaling_relay_fn);
    if (ctx->relay_support)
        s->signaling_relay_fn = p2p_signal_compact_relay;

    // 启动 NAT 打洞
    assert(s->nat.state < NAT_PUNCHING);
    nat_punch(s, -1/* all candidates */);
}

/*
 * 处理 ALIVE_ACK，服务器保活确认
 *
 * 协议：SIG_PKT_ALIVE_ACK (0x87)
 * 包头: [type=0x87 | flags=0 | seq=0]
 * 负载: 无
 */
void compact_on_alive_ack(struct p2p_session *s, const struct sockaddr_in *from) {
    const char* PROTO = "ALIVE_ACK";

    printf(LA_F("[UDP] %s recv from %s:%d\n", LA_F354, 354),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    if (ctx->state <= SIGNAL_COMPACT_REGISTERING) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F113, 113), PROTO, (int)ctx->state);
        return;
    }

    print("V:", LA_F("%s: accepted\n", LA_F92, 92), PROTO);

    // 确认服务器未掉线
    ctx->last_recv_time = P_tick_ms();
}

/*
 * 处理 PEER_INFO，对端发送过来的对端的候选地址信息
 *
 * 协议：SIG_PKT_PEER_INFO (0x83)
 * 包头: [type=0x83 | flags=见下 | seq=序列号]
 * 负载: [session_id(8) | base_index(1) | candidate_count(1) | candidates(N*7)]
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - base_index: 本批候选的起始索引（0-based）
 *   - candidate_count: 本批候选数量，0 表示结束标识
 *   - seq=0: 服务器发送，包含缓存的对端候选，首次分配 session_id
 *   - seq>0: 客户端发送，继续同步剩余候选
 *   - flags: SIG_PEER_INFO_FIN (0x01) 表示候选列表发送完毕
 */
void compact_on_peer_info(struct p2p_session *s, uint16_t seq, uint8_t flags,
                          const uint8_t *payload, int len,
                          const struct sockaddr_in *from) {
    const char* PROTO = "PEER_INFO";

    printf(LA_F("[UDP] %s recv from %s:%d, seq=%u, flags=0x%02x, len=%d\n", LA_F352, 352),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), seq, flags, len);

    if (seq > 16) {
        print("E:", LA_F("%s: invalid seq=%u\n", LA_F118, 118), PROTO, seq);
        return;
    }

    if (len < (int)sizeof(uint64_t) + 2) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F95, 95), PROTO, len);
        return;
    }

    int cand_cnt = payload[sizeof(uint64_t) + 1];
    if (len < (int)sizeof(uint64_t) + 2 + (int)sizeof(p2p_candidate_t) * cand_cnt) {
        print("E:", LA_F("%s: bad payload(len=%d cand_cnt=%d)\n", LA_F94, 94), PROTO, len, cand_cnt);
        return;
    }

    // 服务器发送的第一个 PEER_INFO，至少有一个对方公网的候选地址，且肯定不带 FIN 标识
    if (seq == 0 && (!cand_cnt || (flags & SIG_PEER_INFO_FIN))) {
        print("E:", LA_F("%s seq=0: invalid(cand_cnt=%d flags=0x%02x)\n", LA_F65, 65), PROTO, cand_cnt, flags);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    if (ctx->state < SIGNAL_COMPACT_REGISTERING) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F113, 113), PROTO, (int)ctx->state);
        return;
    }

    // 获取 session_id，作为双方连接的唯一标识
    uint64_t session_id = nget_ll(payload);
    if (session_id == 0) {
        print("E:", LA_F("%s: invalid session_id=0\n", LA_F119, 119), PROTO);
        return;
    }

    uint8_t base_index = payload[sizeof(uint64_t)];

    // 并行网络中，PEER_INFO 可能先于 REGISTER_ACK 到达，所以此时 session_id 可能还未设置
    if (!ctx->session_id) ctx->session_id = session_id;

    // 如果 session_id 不一致
    else if (ctx->session_id != session_id) {

        // 对于 info0 包，意味着对方可能重新发起连接了（例如对端崩溃重启）
        // ! 根据协议，对方将自己的 session 重置，并不会影响自己和信令服务之间的连接和数据状态
        if (seq == 0 && base_index == 0) {

            // 此时本端只能被迫强制重连
            print("W:", LA_F("%s: renew session due to session_id changed by info0 (local=%" PRIu64 " pkt=%" PRIu64 ")\n", LA_F391, 391),
                  PROTO, ctx->session_id, session_id);

            // 如果 p2p 之前已经连接成功过，即业务层可能已经完成部分通讯
            // + 此时需要通知业务层，确保业务层的数据一致性
            // ! 对于业务层来说，session 被强制重置 = 连接断开，需要重新开始
            if (s->state >= P2P_STATE_LOST) {
                // 通知业务层连接断开（session 被对方重置）
                if (s->cfg.on_state) s->cfg.on_state(s, s->state, P2P_STATE_CLOSED, s->cfg.userdata);
            }

            // 清除双方协商信息
            reset_peer(ctx);

            // 重置 p2p 会话
            p2p_session_reset(s, false);

            ctx->session_id = session_id;

            if (ctx->state == SIGNAL_COMPACT_REGISTERING) s->state = P2P_STATE_REGISTERING;
            else { ctx->state = SIGNAL_COMPACT_REGISTERED;
                s->state = P2P_STATE_REGISTERED;
            }
        }
        else {
            print("E:", LA_F("%s: session mismatch(local=%" PRIu64 " pkt=%" PRIu64 ")\n", LA_F144, 144), PROTO, ctx->session_id, session_id);
            return;
        }
    }

    // 如果之前已经收到过 REGISTER_ACK，则启动 ICE 阶段，向对方发送后续候选队列和 FIN 包
    // + ICE 阶段依赖 SIG_PKT_REGISTER_ACK，它提供后续候选队列基准
    if (ctx->state == SIGNAL_COMPACT_REGISTERED) {

        ctx->state = SIGNAL_COMPACT_ICE;
        print("I:", LA_F("%s: entered, %s arrived after REGISTERED\n", LA_F101, 101), TASK_ICE, PROTO);

        send_rest_candidates_and_fin(s);
        ctx->last_send_time = P_tick_ms();
    }

    bool new_seq = false;

    // seq!=0 说明是对端发来的后续 PEER_INFO 包
    if (seq != 0) {

        print("V:", LA_F("%s: accepted seq=%u cand_cnt=%d flags=0x%02x\n", LA_F86, 86), PROTO, seq, cand_cnt, flags);

        // 排重
        if ((new_seq = (ctx->remote_candidates_done & (1u << (seq - 1))) == 0)) {

            // 对于 FIN 包，计算对方候选地址集合序列掩码（即计算全集区间）
            if ((flags & SIG_PEER_INFO_FIN) || !cand_cnt) {

                ctx->remote_candidates_mask = (1u << seq) - 1u;
            }

            // 维护分配远端候选列表的空间
            // + 这里 payload[8](base_index) + cand_cnt，表示该包至少需要的远端候选数量; 1 为至少包含一个对方的公网地址
            if (p2p_remote_cands_reserve(s, 1 + payload[sizeof(uint64_t)] + cand_cnt) != E_NONE) {
                print("E:", LA_F("Failed to reserve remote candidates (base=%u cnt=%d)\n", LA_F211, 211), payload[sizeof(uint64_t)], cand_cnt);
                return;
            }

            unpack_remote_candidates(s, payload, cand_cnt);
            ctx->remote_candidates_done |= 1u << (seq - 1);
        }
    }
    // seq=0 则是服务器维护的 PEER_INFO 包（可能是首个 info0，也可能是对端的公网地址变更通知）
    // base_index = 0 意味着首个 info0
    else if (base_index == 0) {

        print("V:", LA_F("%s seq=0: accepted cand_cnt=%d\n", LA_F64, 64), PROTO, cand_cnt);

        // 排重
        if (!ctx->remote_candidates_0) {

            // 维护分配远端候选列表的空间（作为首个 PEER_INFO 包，候选队列基准 base_index 肯定是 0）
            // + 注意，seq=0 的 PEER_INFO 包的 base_index 字段值可以不为 0（协议上 base_index !=0 说明是对方公网地址发生变更的通知）
            if (p2p_remote_cands_reserve(s, cand_cnt) != E_NONE) {
                print("E:", LA_F("Failed to reserve remote candidates (cnt=%d)\n", LA_F212, 212), cand_cnt);
                return;
            }

            unpack_remote_candidates(s, payload, cand_cnt);

            ctx->remote_candidates_0 = new_seq = true;
        }
    }
    // base_index!=0 说明是对方公网地址变更通知。此时 pkt 必须只携带一个候选地址（即变更后的公网地址），且不带 FIN 标识
    else if (cand_cnt != 1 || (flags & SIG_PEER_INFO_FIN)) {

        print("E:", LA_F("%s NOTIFY: invalid(base=%u cand_cnt=%d flags=0x%02x)\n", LA_F51, 51),
              PROTO, base_index, cand_cnt, flags);
        return;
    }
    // 确保地址变更通知是最新的
    else if (ctx->remote_addr_notify_seq == 0 || uint8_circle_newer(base_index, ctx->remote_addr_notify_seq)) {

        print("V:", LA_F("%s NOTIFY: accepted\n", LA_F49, 49), PROTO);

        if (p2p_remote_cands_reserve(s, 1) != E_NONE) {
            print("E:", LA_F("Failed to reserve remote candidates (cnt=1)\n", LA_F213, 213));
            return;
        }

        if (instrument_option(P2P_INST_OPT_ICE_SRFLX_OFF)) {
            print("I:", LA_F("%s NOTIFY: ignored srflx addr update due to instument\n", LA_F379, 379), PROTO);
            return;
        }

        p2p_remote_candidate_entry_t *c = &s->remote_cands[0];
        c->type = (p2p_cand_type_t)payload[sizeof(uint64_t)+2];
        c->priority = 0;
        sockaddr_init_with_net(&c->addr, (uint32_t *) (payload + sizeof(uint64_t) + 3),
                               (uint16_t *) (payload + sizeof(uint64_t) + 7));
        c->last_punch_send_ms = 0;
        if (s->remote_cand_cnt == 0) s->remote_cand_cnt = 1;

        // Trickle ICE：NAT 打洞已启动时，立即探测最新地址
        if (s->nat.state == NAT_PUNCHING || s->nat.state == NAT_RELAY) {

            print("I:", LA_F("%s: Peer addr changed -> %s:%d, retrying punch\n", LA_F71, 71),
                  TASK_ICE_REMOTE, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));

            // 标记旧的活跃路径为失效（地址已变更）
            if (s->active_path >= 0 && s->active_path < s->remote_cand_cnt) {
                path_manager_set_path_state(s, s->active_path, PATH_STATE_FAILED);
                print("V:", LA_F("Marked old path (idx=%d) as FAILED due to addr change\n", LA_F235, 235),
                       s->active_path);
            }

            if (ctx->state >= SIGNAL_COMPACT_REGISTERED) {
                if (nat_punch(s, 0) != E_NONE) {
                    print("E:", LA_F("Failed to send punch packet for new peer addr\n", LA_F218, 218));
                }
            }
        }
        else {
            print("I:", LA_F("%s: Peer addr changed -> %s:%d, punch deferred (NAT=%d)\n", LA_F70, 70),
                  TASK_ICE_REMOTE, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port), (int)s->nat.state);
        }

        ctx->remote_addr_notify_seq = base_index;
    }
    else print("V:", LA_F("%s NOTIFY: ignored old notify base=%u (current=%u)\n", LA_F50, 50),
               PROTO, base_index, ctx->remote_addr_notify_seq);

    if (new_seq) {

        // 收到该消息说明对方肯定已上线
        ctx->peer_online = true;

        // 如果对方所有的候选队列都已经接收完成
        // 注：此状态用于 NAT 打洞超时判断，只有 ICE 交换完成后才会触发打洞超时
        if (ctx->remote_candidates_0 && ctx->remote_candidates_mask &&
            (ctx->remote_candidates_done & ctx->remote_candidates_mask) == ctx->remote_candidates_mask) {

            // 标记远程候选交换完成（供 NAT 层判断打洞超时使用）
            s->remote_ice_done = true;

            print("I:", LA_F("%s: sync complete (ses_id=%" PRIu64 ", mask=0x%04x)\n", LA_F152, 152),
                  TASK_ICE_REMOTE, ctx->session_id, (unsigned)ctx->remote_candidates_mask);
        }
    }

    /*
     * 发送 PEER_INFO_ACK 确认包
     * 说明: 确认收到对方的候选地址包
     *
     * 协议：SIG_PKT_PEER_INFO_ACK (0x04)
     * 包头: [type=0x04 | flags=0 | seq=被确认的PEER_INFO包序号]
     * 负载: [session_id(8)]
     *   - session_id: 会话 ID（网络字节序，64位）
     *   - seq: 被确认的 PEER_INFO 包的序列号
     */
    {
        uint8_t ack_payload[sizeof(uint64_t)];
        nwrite_ll(ack_payload, ctx->session_id);

        print("V:", LA_F("%s_ACK sent, seq=%u (ses_id=%" PRIu64 ")\n", LA_F169, 169), PROTO, seq, ctx->session_id);

        ret_t ret = udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_PEER_INFO_ACK, 0, seq, ack_payload, sizeof(ack_payload));
        if (ret < 0)
            print("E:", LA_F("[UDP] %s_ACK send to %s:%d failed(%d)\n", LA_F359, 359), 
                  PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
        else
            printf(LA_F("[UDP] %s_ACK send to %s:%d, seq=%u, flags=0, len=%d\n", LA_F360, 360),
                   PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
                   seq, (int)sizeof(ack_payload));
    }
}

/*
 * 处理 PEER_INFO_ACK，对端确认已收到自己发过去的候选地址信息
 *
 * 协议：SIG_PKT_PEER_INFO_ACK (0x84)
 * 包头: [type=0x84 | flags=0 | seq=确认的 PEER_INFO 序列号]
 * 负载: [session_id(8)]
 *   - session_id: 会话 ID（网络字节序，64位）
 *   - seq: 确认的 PEER_INFO 序列号（0 表示确认服务器下发的 PEER_INFO(seq=0)）
 */
void compact_on_peer_info_ack(struct p2p_session *s, uint16_t seq,
                               const uint8_t *payload, int len,
                               const struct sockaddr_in *from) {
    const char* PROTO = "PEER_INFO_ACK";

    printf(LA_F("[UDP] %s recv from %s:%d, seq=%u, len=%d\n", LA_F353, 353),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), seq, len);

    if (len < (int)sizeof(uint64_t)) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F95, 95), PROTO, len);
        return;
    }

    if (seq == 0 || seq > 16) {
        print("E:", LA_F("%s: invalid ack_seq=%u\n", LA_F114, 114), PROTO, seq);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    uint64_t session_id = nget_ll(payload);
    if (session_id != ctx->session_id) {
        print("W:", LA_F("%s: ignored for ses_id=%" PRIu64 " (local ses_id=%" PRIu64 ")\n", LA_F109, 109),
              PROTO, session_id, ctx->session_id);
        return;
    }

    uint16_t bit = (uint16_t)(1u << (seq - 1));
    if ((ctx->candidates_mask & bit) == 0) {
        print("E:", LA_F("%s: unexpected ack_seq=%u mask=0x%04x\n", LA_F166, 166),
              PROTO, seq, (unsigned)ctx->candidates_mask);
        return;
    }

    if ((ctx->candidates_acked & bit)) {
        print("V:", LA_F("%s: ignored for duplicated seq=%u, already acked\n", LA_F107, 107), PROTO, seq);
        return;
    }

    print("V:", LA_F("%s: accepted for ack_seq=%u\n", LA_F84, 84), PROTO, seq);

    ctx->candidates_acked |= bit;

    // 如果对方所有的候选队列都已经接收完成
    if ((ctx->candidates_acked & ctx->candidates_mask) == ctx->candidates_mask) {

        ctx->state = SIGNAL_COMPACT_READY;
        print("I:", LA_F("%s: sync complete (ses_id=%" PRIu64 ")\n", LA_F151, 151), TASK_ICE, ctx->session_id);
    }
}

/*
 * 处理 PEER_OFF，对端离线通知
 *
 * 协议：SIG_PKT_PEER_OFF (0x85)
 * 包头: [type=0x85 | flags=0 | seq=0]
 * 负载: [session_id(8)]
 *   - session_id: 已断开的会话 ID（网络字节序，64位）
 */
void compact_on_peer_off(struct p2p_session *s, const uint8_t *payload, int len,
                         const struct sockaddr_in *from) {
    const char* PROTO = "PEER_OFF";

    printf(LA_F("[UDP] %s recv from %s:%d, len=%d\n", LA_F351, 351),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), len);

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    if (len < (int)sizeof(uint64_t)) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F95, 95), PROTO, len);
        return;
    }

    uint64_t session_id = nget_ll(payload);
    if (!session_id || ctx->session_id != session_id) {
        print("W:", LA_F("%s: session mismatch(local=%" PRIu64 ", pkt=%" PRIu64 ")\n", LA_F145, 145), PROTO, ctx->session_id, session_id);
        return;
    }

    print("V:", LA_F("%s: accepted (ses_id=%" PRIu64 ")\n", LA_F82, 82), PROTO, session_id);

    // 重置到 REGISTERED 状态，等待对端重新注册
    // ! 这里可以明确知道是对方断开了连接，所以自己和信令服务器之间的连接和数据状态都是正常的，不需要重置
    //   同样，session_id 也不需要重置，因为它是本端的唯一标识，是信令服务器在 REGISTER 请求时分配的
    ctx->state = SIGNAL_COMPACT_REGISTERED;

    // 清除双方协商信息
    reset_peer(ctx);

    print("I:", LA_F("%s: peer disconnected (ses_id=%" PRIu64 "), reset to REGISTERED\n", LA_F125, 125), PROTO, session_id);

    // 标记 NAT 为已关闭
    // + 这里将信令层的 peer close 转换为 NAT 层的 closed 状态，主循环会统一以 NAT 层的 NAT_CLOSED 状态机变更为准
    //   并统一调用 p2p_session_reset
    s->nat.state = NAT_CLOSED;
}

/*
 * 处理 MSG_REQ，（服务器代理转发的）源端消息请求
 * 说明: B端收到服务器转发的消息请求，A端发出的原始请求(flags=0)不会到达客户端
 *
 * 协议：SIG_PKT_MSG_REQ (0x20)
 * 包头: [type=0x20 | flags=SIG_MSG_FLAG_RELAY | seq=0]
 * 负载: [session_id(8)][sid(2)][msg(1)][data(N)]
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

    printf(LA_F("[UDP] %s recv from %s:%d, flags=0x%02x, len=%d\n", LA_F350, 350),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), flags, len);

    // 客户端收到 req 肯定都是 Server 转发过来，而不是对方直接发来的原始请求
    if (!(flags & SIG_MSG_FLAG_RELAY)) {
        print("E:", LA_F("%s: invalid for non-relay req\n", LA_F116, 116), PROTO);
        return;
    }

    // 最小长度：session_id(8) + sid(2) + msg(1) = 11
    if (len < 11) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F95, 95), PROTO, len);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    uint64_t session_id = nget_ll(payload);
    uint16_t sid = nget_s(payload + 8);
    uint8_t msg = payload[10];
    const uint8_t *req_data = payload + 11;
    int req_len = len - 11;

    /* 判断是否是新请求（使用循环序列号比较）：
     * 1. 如果正在处理请求且 sid 相同 → 忽略重复包
     * 2. 如果 sid <= last_sid（旧请求）→ 忽略
     * 3. 如果 sid > last_sid（新请求）→ 处理（可能覆盖正在处理的旧请求）*/
    if (ctx->resp_state == 1 && ctx->resp_sid == sid) {
        print("V:", LA_F("%s: duplicate request ignored (sid=%u, already processing)\n", LA_F99, 99), PROTO, sid);
        return;
    }

    // 忽略旧请求（sid <= last_sid）
    if (ctx->rpc_last_sid != 0 && !uint16_circle_newer(sid, ctx->rpc_last_sid)) {
        print("V:", LA_F("%s: old request ignored (sid=%u <= last_sid=%u)\n", LA_F124, 124),
              PROTO, sid, ctx->rpc_last_sid);
        return;
    }

    // 如果正在处理旧请求但收到新请求（sid > last_sid），则覆盖旧请求
    if (ctx->resp_state == 1) {
        print("W:", LA_F("%s: new request (sid=%u) overrides pending request (sid=%u)\n", LA_F121, 121), 
              PROTO, sid, ctx->resp_sid);
    }
    
    ctx->resp_sid         = sid;
    ctx->resp_session_id  = session_id;

    // msg=0: 默认自动 echo 回复（无需应用层介入）
    if (msg == 0) {
        print("V:", LA_F("%s msg=0: accepted, echo reply (sid=%u, len=%d)\n", LA_F52, 52), PROTO, sid, req_len);
        p2p_signal_compact_response(s, 0, req_data, req_len);
        return;
    }

    print("V:", LA_F("%s: accepted sid=%u, msg=%u\n", LA_F87, 87), PROTO, sid, msg);

    // 触发用户回调
    if (s->cfg.on_request)
        s->cfg.on_request((p2p_handle_t)s, sid, msg, req_data, req_len, s->cfg.userdata);
}

/*
 * 处理 MSG_REQ_ACK，服务器对自己发起的请求的确认
 * 说明: 该请求已经被服务器代理接管，并确保完成向对端的转发
 * 所以收到该消息后，自己就可以停止重发请求了，接下来只需要等待 MSG_RESP 即可知道请求的最终结果
 *
 * 协议：SIG_PKT_MSG_REQ_ACK (0x89)
 * 包头: [type=0x89 | flags=0 | seq=0]
 * 负载: [session_id(8)][sid(2)][status(1)]
 *   - session_id: A端的会话 ID（用于验证响应合法性）
 *   - sid: 序列号，与 MSG_REQ 中的 sid 对应
 *   - status: 0=已缓存开始中转, 1=B不在线（失败）
 */
void compact_on_request_ack(struct p2p_session *s,
                             const uint8_t *payload, int len,
                             const struct sockaddr_in *from) {
    const char* PROTO = "MSG_REQ_ACK";

    printf(LA_F("[UDP] %s recv from %s:%d, len=%d\n", LA_F351, 351),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), len);

    if (len < 11) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F95, 95), PROTO, len);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    uint64_t session_id = nget_ll(payload);
    uint16_t sid = nget_s(payload + 8);
    uint8_t status = payload[10];

    // 验证 session_id 是否匹配
    if (session_id != ctx->session_id) {
        print("W:", LA_F("%s: session_id mismatch (recv=%" PRIu64 ", expect=%" PRIu64 ")\n", LA_F146, 146),
              PROTO, session_id, ctx->session_id);
        return;
    }

    if (ctx->req_sid != sid) {
        print("V:", LA_F("%s: ignored for sid=%u (current sid=%u)\n", LA_F110, 110), PROTO, sid, ctx->req_sid);
        return;
    }
    if (ctx->req_state != 1/* waiting REQ_ACK */) {
        print("V:", LA_F("%s: ignored in invalid state=%d\n", LA_F111, 111), PROTO, (int)ctx->req_state);
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
        print("V:", LA_F("%s: accepted, waiting for response (sid=%u)\n", LA_F91, 91), PROTO, ctx->req_sid);
    }
    // 对端不在线：请求失败，通知上层
    else {

        uint16_t saved_id  = ctx->req_sid;
        uint8_t  saved_msg = ctx->req_msg;
        ctx->req_state  = 0;
        ctx->req_sid = 0;

        print("W:", LA_F("%s: RPC fail due to peer offline (sid=%u)\n", LA_F73, 73), PROTO, saved_id);

        if (s->cfg.on_response)
            s->cfg.on_response((p2p_handle_t)s, saved_id, saved_msg, NULL, -1, s->cfg.userdata);
    }
}

/*
 * 处理 MSG_RESP，服务器代理转发的，对端（对自己向对端请求的）消息响应
 *
 * 协议：SIG_PKT_MSG_RESP (0x8A)
 * 包头: [type=0x8A | flags=见下 | seq=0]
 * 负载:
 * > [session_id(8)][sid(2)][code(1)][data(N)] （正常响应）
 * > [session_id(8)][sid(2)]（错误响应）
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

    printf(LA_F("[UDP] %s recv from %s:%d, flags=0x%02x, len=%d\n", LA_F350, 350),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), flags, len);

    // 检查最小负载长度：session_id(8) + sid(2) = 10
    if (len < 10) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F95, 95), PROTO, len);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    uint64_t session_id = nget_ll(payload);
    uint16_t sid = nget_s(payload + 8);

    // 验证 session_id 是否匹配
    if (session_id != ctx->session_id) {
        print("W:", LA_F("%s: session_id mismatch (recv=%" PRIu64 ", expect=%" PRIu64 ")\n", LA_F146, 146),
              PROTO, session_id, ctx->session_id);
        return;
    }

    /*
     * 发送 MSG_RESP_ACK 确认包
     * 服务器收到该确认后，将会停止重发该响应到本地，即结束整个请求-响应流程
     *
     * 协议：SIG_PKT_MSG_RESP_ACK (0x8B)
     * 包头: [type=0x8B | flags=0 | seq=0]
     * 负载: [session_id(8)][sid(2)]
     *   - session_id: A端的会话 ID（用于 O(1) 哈希查找）
     *   - sid: 序列号，与 MSG_RESP 中的 sid 一致
     * 说明: A端确认收到B端的响应，幂等操作，即使已处理过也补发
     */
    {
        uint8_t ack[10]; int n = 0;
        nwrite_ll(ack + n, ctx->session_id); n += 8;
        nwrite_s(ack + n, sid); n += 2;

        print("V:", LA_F("%s_ACK sent, sid=%u\n", LA_F170, 170), PROTO, sid);

        ret_t ret = udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_MSG_RESP_ACK, 0, 0, ack, n);
        if (ret < 0)
            print("E:", LA_F("[UDP] %s_ACK send to %s:%d failed(%d)\n", LA_F359, 359), 
                  PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
        else
            printf(LA_F("[UDP] %s_ACK send to %s:%d, seq=0, flags=0, len=%d\n", LA_F361, 361),
                   PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), n);
    }

    // 仅命中当前挂起请求时，才需要继续解析响应内容
    if (!(ctx->req_state == 2/* waiting RESP */ && ctx->req_sid == sid)) {
        print("V:", LA_F("%s: duplicate/irrelevant response acked (sid=%u, current sid=%u, state=%d)\n", LA_F100, 100),
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

        res_code = (flags & SIG_MSG_FLAG_PEER_OFFLINE) ? SIG_MSG_ERR_PEER_OFFLINE : SIG_MSG_ERR_TIMEOUT;
    }
    else {

        // 正常响应：需要包含 code 和可选的 data
        // 最小长度：session_id(8) + sid(2) + code(1) = 11
        if (len < 11) {
            print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F95, 95), PROTO, len);
            return;
        }
        res_code = payload[10];
        res_data = payload + 11;
        res_size = len - 11;
    }

    print("V:", LA_F("%s: accepted (sid=%u)\n", LA_F83, 83), PROTO, sid);

    ctx->req_state  = 0;
    ctx->req_sid = 0;

    /* 根据 flags 输出不同的日志 */
    if (flags & SIG_MSG_FLAG_PEER_OFFLINE) {
        print("W:", LA_F("%s: RPC fail due to peer offline (sid=%u)\n", LA_F73, 73), PROTO, sid);
    }
    else if (flags & SIG_MSG_FLAG_TIMEOUT) {
        print("W:", LA_F("%s: RPC fail due to relay timeout (sid=%u)\n", LA_F74, 74), PROTO, sid);
    }
    else {
        print("I:", LA_F("%s: RPC complete (sid=%u)\n", LA_F72, 72), PROTO, sid);
    }

    if (s->cfg.on_response)
        s->cfg.on_response((p2p_handle_t)s, sid, res_code, res_data, res_size, s->cfg.userdata);
}

/*
 * 处理 MSG_RESP_ACK，服务器对自己（经由服务器代理）向源方发出的请求的响应的确认
 * 收到该确认后，自己就不会再重发 response 了，且可以记录该 sid 已完成
 *
 * 协议：SIG_PKT_MSG_RESP_ACK (0x8B) - B 端接收
 * 包头: [type=0x8B | flags=0 | seq=0]
 * 负载: [session_id(8)][sid(2)]
 *   - session_id: B端的会话 ID（用于 O(1) 哈希查找）
 *   - sid: 序列号，与 MSG_RESP 中的 sid 对应
 */
void compact_on_response_ack(struct p2p_session *s,
                             const uint8_t *payload, int len,
                             const struct sockaddr_in *from) {
    const char* PROTO = "MSG_RESP_ACK";

    printf(LA_F("[UDP] %s recv from %s:%d, len=%d\n", LA_F351, 351),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), len);

    if (len < 10) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F95, 95), PROTO, len);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    uint64_t session_id = nget_ll(payload);
    uint16_t sid = nget_s(payload + 8);

    // 验证 session_id 是否匹配（可选，增强安全性）
    if (session_id != ctx->session_id) {
        print("W:", LA_F("%s: session_id mismatch (recv=%" PRIu64 ", expect=%" PRIu64 ")\n", LA_F146, 146),
              PROTO, session_id, ctx->session_id);
        return;
    }
    if (ctx->resp_sid != sid) {
        print("V:", LA_F("%s: ignored for sid=%u (current sid=%u)\n", LA_F110, 110), PROTO, sid, ctx->resp_sid);
        return;
    }
    if (ctx->resp_state != 1/* waiting RESP_ACK */) {
        print("V:", LA_F("%s: ignored in invalid state=%d\n", LA_F111, 111), PROTO, (int)ctx->resp_state);
        return;
    }

    // 记录最后完成的 sid
    print("V:", LA_F("%s: accepted (sid=%u)\n", LA_F83, 83), PROTO, sid);

    ctx->rpc_last_sid = ctx->resp_sid;

    // 成功：Server 已收到 B 的 MSG_RESP，结束 RESP 重发
    ctx->resp_sid = 0;
    ctx->resp_state = 0;
    ctx->resp_session_id = 0;

    print("I:", LA_F("%s: RPC finished (sid=%u)\n", LA_F75, 75), PROTO, sid);
}

/*
 * 处理 NAT_PROBE_ACK，NAT 探测响应
 *
 * 协议：SIG_PKT_NAT_PROBE_ACK (0x8D)
 * 包头: [type=0x8D | flags=0 | seq=对应的 NAT_PROBE 请求 seq]
 * 负载: [probe_ip(4) | probe_port(2)]
 *   - probe_ip/port: 服务器在探测端口观察到的客户端源地址（第二次映射）
 *   - seq: 复制请求包的 seq，用于客户端匹配响应
 */
void compact_on_nat_probe_ack(struct p2p_session *s, uint16_t seq,
                              const uint8_t *payload, int len,
                              const struct sockaddr_in *from) {
    const char* PROTO = "NAT_PROBE_ACK";

    printf(LA_F("[UDP] %s recv from %s:%d, seq=%u, len=%d\n", LA_F353, 353),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), seq, len);

    if (len < 6) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F95, 95), PROTO, len);
        return;
    }

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    if (seq != ctx->nat_probe_retries) {
        print("V:", LA_F("%s: ignored for seq=%u (expect=%d)\n", LA_F108, 108),
              PROTO, seq, (int)ctx->nat_probe_retries);
        return;
    }

    struct sockaddr_in probe_mapped;
    memset(&probe_mapped, 0, sizeof(probe_mapped));
    probe_mapped.sin_family = AF_INET;
    memcpy(&probe_mapped.sin_addr.s_addr, payload, 4);
    memcpy(&probe_mapped.sin_port,        payload + 4, 2);

    print("V:", LA_F("%s: accepted, probe_mapped=%s:%d\n", LA_F89, 89),
          PROTO, inet_ntoa(probe_mapped.sin_addr), ntohs(probe_mapped.sin_port));

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

    print("I:", LA_F("%s: completed, mapped=%s:%d probe=%s:%d -> %s\n", LA_F97, 97),
          TASK_NAT_PROBE,
          inet_ntoa(ctx->public_addr.sin_addr), ntohs(ctx->public_addr.sin_port),
          inet_ntoa(probe_mapped.sin_addr),     ntohs(probe_mapped.sin_port),
          p2p_nat_type_str(s->nat_type));
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
    uint64_t now = P_tick_ms();

    // REGISTERING 状态：定期重发 REGISTER
    if (ctx->state == SIGNAL_COMPACT_REGISTERING) {

        if (tick_diff(now, ctx->last_send_time) >= REGISTER_INTERVAL_MS) {

            // 超时检查
            if (ctx->register_attempts++ < MAX_REGISTER_ATTEMPTS) {

                print("I:", LA_F("%s: retry, (attempt %d/%d)\n", LA_F138, 138),
                      TASK_REG, ctx->register_attempts, MAX_REGISTER_ATTEMPTS);

                send_register(s);
                ctx->last_send_time = now;
            }
            else {

                print("W:", LA_F("%s: timeout, max(%d) attempts reached, reset to INIT\n", LA_F157, 157),
                      TASK_REG, MAX_REGISTER_ATTEMPTS);

                ctx->state = SIGNAL_COMPACT_INIT;
            }
        }
    }
    // REGISTERED/READY 状态：定期向服务器发送保活包
    else if (ctx->state == SIGNAL_COMPACT_REGISTERED || ctx->state == SIGNAL_COMPACT_READY) {

        if (tick_diff(now, ctx->last_send_time) >= REGISTER_KEEPALIVE_INTERVAL_MS) {

            /*
             * 发送 ALIVE 保活包
             *
             * 协议：SIG_PKT_ALIVE (0x86)
             * 包头: [type=0x86 | flags=0 | seq=0]
             * 负载: [session_id(8)]
             *   - session_id: 本端会话 ID（来自 REGISTER_ACK）
             * 说明: 保活包，维持服务器上的注册状态
             */
            {   const char* PROTO = "ALIVE";

                if (ctx->session_id) {

                    uint8_t payload[8];
                    nwrite_ll(payload, ctx->session_id);

                    print("V:", LA_F("%s, sent on %s\n", LA_F68, 68),
                          PROTO, ctx->state == SIGNAL_COMPACT_REGISTERED ? "REGISTERED" : "READY");

                    ret_t ret = udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_ALIVE, 0, 0, payload, (int)sizeof(payload));
                    ctx->last_send_time = now;
                    if (ret < 0)
                        print("E:", LA_F("[UDP] %s send to %s:%d failed(%d)\n", LA_F355, 355), 
                              PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port), E_EXT_CODE(ret));
                    else
                        printf(LA_F("[UDP] %s send to %s:%d, seq=0, flags=0, len=%d\n", LA_F358, 358),
                               PROTO, inet_ntoa(ctx->server_addr.sin_addr), ntohs(ctx->server_addr.sin_port),
                               (int)sizeof(payload));
                }
                else print("W:", LA_F("%s skipped: session_id=0\n", LA_F66, 66), PROTO); 
            }
        }
    }

    // 当前（请求端）处于等待（服务器返回的）REQ_ACK 的阶段
    if (ctx->req_state == 1/* waiting REQ_ACK */) {

        if (tick_diff(now, ctx->req_send_time) >= MSG_REQ_INTERVAL_MS) {

            /* 超时失败 */
            if (ctx->req_retries++ < MSG_REQ_MAX_RETRIES) {

                print("I:", LA_F("%s: retry(%d/%d) req (sid=%u)\n", LA_F136, 136),
                      TASK_RPC, ctx->req_retries, MSG_REQ_MAX_RETRIES, ctx->req_sid);

                send_rpc_req(s);
                ctx->req_send_time = now;

                if (ctx->state == SIGNAL_COMPACT_REGISTERED ||
                    ctx->state == SIGNAL_COMPACT_READY)
                    ctx->last_send_time = now;
            }
            else {

                uint16_t sid = ctx->req_sid;
                uint8_t  msg = ctx->req_msg;
                ctx->req_sid = 0;
                ctx->req_state = 0;

                print("W:", LA_F("%s: %s timeout after %d retries (sid=%u)\n", LA_F69, 69),
                      TASK_RPC, "req", MSG_REQ_MAX_RETRIES, sid);

                if (s->cfg.on_response)
                    s->cfg.on_response((p2p_handle_t)s, sid, msg, NULL, -1, s->cfg.userdata);
            }
        }
    }
    
    // 当前（响应端）处于等待（服务器返回的）RESP_ACK 的阶段
    if (ctx->resp_state == 1/* waiting RESP_ACK */) {

        if (tick_diff(now, ctx->resp_send_time) >= MSG_REQ_INTERVAL_MS) {

            /* 超时失败（与 A 端对称，使用相同的超时配置） */
            if (ctx->resp_retries++ < MSG_REQ_MAX_RETRIES) {

                print("I:", LA_F("%s: retry(%d/%d) resp (sid=%u)\n", LA_F137, 137),
                      TASK_RPC, ctx->resp_retries, MSG_REQ_MAX_RETRIES, ctx->resp_sid);

                send_rpc_resp(s);
                ctx->resp_send_time = now;

                if (ctx->state == SIGNAL_COMPACT_REGISTERED ||
                    ctx->state == SIGNAL_COMPACT_READY)
                    ctx->last_send_time = now;
            }
            else {

                uint16_t sid = ctx->resp_sid;
                ctx->resp_sid = 0;
                ctx->resp_state = 0;
                ctx->resp_session_id = 0;

                print("W:", LA_F("%s: %s timeout after %d retries (sid=%u)\n", LA_F69, 69),
                      TASK_RPC, "resp", MSG_REQ_MAX_RETRIES, sid);
            }
        }
    }
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
    uint64_t now = P_tick_ms();

    // ICE 交换阶段：定期重发剩余候选和 FIN，直到收到对端确认（进入 READY）
    if (ctx->state == SIGNAL_COMPACT_ICE) {

        // ICE 交换超时：直接执行 UNREGISTER + RE-REGISTER 即重新初始连接
        // + 由于 ICE 交换是服务器中转实现，所以此时有可能是信令服务器的问题、也可能是对端网络问题，当然也可能是自己网络问题
        //   由于无法确认问题来自哪里，所以不能回退到 REGISTERED，因为三方如果都误认为是别人的问题，那就会一直卡在 ICE 交换阶段无法恢复了
        // ! 注意：如果 p2p 已经是连接过的状态
        //   由于候选支持 trickle 模式，所以 p2p 可能在 ICE 阶段变为已连接状态，即可能已经部分打洞连接成功
        //   此时如果 p2p 是可连接状态，那么问题应该出现在信令服务器或和服务器之间的网络，如果问题出现在之间的网络，那么是可能恢复的
        //   即使信号已经 lost，连接过的网络，也应该由应用来决定是否断开或重连
        if (s->state < P2P_STATE_LOST && tick_diff(now, ctx->last_send_time) >= ICE_TIMEOUT_MS) {

            char local_peer_id[P2P_PEER_ID_MAX];
            char remote_peer_id[P2P_PEER_ID_MAX];
            memcpy(local_peer_id, ctx->local_peer_id, sizeof(local_peer_id));
            memcpy(remote_peer_id, ctx->remote_peer_id, sizeof(remote_peer_id));
            local_peer_id[P2P_PEER_ID_MAX - 1] = '\0';
            remote_peer_id[P2P_PEER_ID_MAX - 1] = '\0';

            print("W:", LA_F("%s: timeout after %d ms, restarting signaling (UNREGISTER + RE-REGISTER)\n", LA_F153, 153),
                  TASK_ICE, ICE_TIMEOUT_MS);

            // 重置双方协商信息
            reset_peer(ctx);

            // 重置 p2p 会话
            p2p_session_reset(s, false);

            // session id 会在服务器重新注册时重新分配
            ctx->session_id = 0;

            // 重新 REGISTER
            // + 这里无需向服务器发送 UNREGISTER 包，因为协议上要求新的 rid 会确保服务器上的旧注册失效
            ctx->state = SIGNAL_COMPACT_REGISTERING;

            // 生成新的注册 rid
            uint32_t rid = ctx->instance_id;
            while (rid == ctx->instance_id) rid = P_rand32();
            ctx->instance_id = rid;

            // 发送注册协议包
            send_register(s);
            ctx->register_attempts = 1;
            ctx->last_send_time = P_tick_ms();

            // 重新发起 TURN Allocate（上一轮分配已随 ICE 重置失效）
            // todo ? turn 为啥 ICE 重置会失效，另外 turn 的生命周期管理设计？
            // turn 的公网地址应该会添加到本地候选队列，不应该失效呀
//            if (!instrument_option(P2P_INST_OPT_RELAY_OFF) && s->cfg.turn_server) {
//                if (p2p_turn_allocate(s) == 0) {
//                    print("I:", LA_F("Requested Relay Candidate from TURN %s", LA_F286, 286), s->cfg.turn_server);
//                }
//            }

            return;
        }

        // 如果有待发送的 trickle 候选，且超过了攒批间隔时间窗口
        if (ctx->trickle_last_pack_time && tick_diff(now, ctx->trickle_last_pack_time) >= TRICKLE_BATCH_MS
            && ctx->trickle_seq_next <= 16 && ctx->trickle_queue[ctx->trickle_seq_next] > 0) {
            send_trickle_candidates(s);
        }

        if (tick_diff(now, ctx->last_send_time) < PEER_INFO_INTERVAL_MS) return;

        print("V:", LA_F("%s, retry remaining candidates and FIN to peer\n", LA_F67, 67), TASK_ICE);

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

    assert(s->signaling_mode == P2P_SIGNALING_MODE_COMPACT);
    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    // 探测端口未知
    if (ctx->state == SIGNAL_COMPACT_INIT || ctx->state == SIGNAL_COMPACT_REGISTERING) {
        return;
    }
    // 不支持探测
    if (!ctx->probe_port) {
        // 服务器不支持 NAT probe，设置为 UNDETECTABLE
        if (s->nat_type != P2P_NAT_UNDETECTABLE) {
            s->nat_type = P2P_NAT_UNDETECTABLE;
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

        print("V:", LA_F("%s: retry(%d/%d) probe\n", LA_F135, 135),
              TASK_NAT_PROBE, ctx->resp_retries, MSG_REQ_MAX_RETRIES);
        send_nat_probe(s);
        ctx->nat_probe_send_time = now;
    }
    else {

        s->nat_type = P2P_NAT_TIMEOUT;
        ctx->nat_probe_retries = -2/* 探测超时 */;

        print("W:", LA_F("%s: timeout after %d retries , type unknown\n", LA_F154, 154), 
              MSG_REQ_MAX_RETRIES, TASK_NAT_PROBE);
    }
}

#pragma clang diagnostic pop
