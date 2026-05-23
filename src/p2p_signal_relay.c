/*
 * RELAY 模式信令实现（TCP 长连接）
 *
 * ============================================================================
 * 设计理念：两阶段分离
 * ============================================================================
 *
 * 与 COMPACT 的核心区别：
 *
 *   COMPACT: REG(local_id, remote_id) → 一次建立三方关系
 *   RELAY:   REG(my_name) + SYN0(target_name) → 两步分离
 *
 * 两阶段的意义：
 *   阶段1 (REG):  建立"客户端-服务器"的基础连接
 *                   完成认证、能力协商、保活机制
 *   阶段2 (SYN0):   建立"我-对方"的会话
 *                   支持一个客户端并发多个会话（不同 session_id）
 *
 * 这种设计特别适合 TCP 长连接场景：
 *   - 一次 REG，持续保活
 *   - 多次 SYN0，复用连接
 *   - 每个会话独立 session_id，互不干扰
 */
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#define MOD_TAG "RELAY"

#include "p2p_internal.h"

#define TASK_REG                     "REG"
#define TASK_TOUCH                      "TOUCH"
#define TASK_SYNC                       "SYNC"
#define TASK_SYNC_REMOTE                "SYNC REMOTE"
#define TASK_RELAY                      "RELAY"
#define TASK_RPC                        "RPC"

/* 一个 SYNC 包所承载的候选数量（单位）*/
#define SYNC_CAND_UNIT \
    (((P2P_MAX_PAYLOAD - P2P_SESS_ID_SZ - 1) / (int)sizeof(p2p_candidate_t)) < P2P_RELAY_MAX_CANDS_PER_PACKET \
     ? ((P2P_MAX_PAYLOAD - P2P_SESS_ID_SZ - 1) / (int)sizeof(p2p_candidate_t)) \
     : P2P_RELAY_MAX_CANDS_PER_PACKET)

///////////////////////////////////////////////////////////////////////////////

/*
 * 辅助函数
 */

/*
 * 将消息加入发送队列
 *
 * @param ctx         信令上下文
 * @param type        消息类型
 * @param payload     负载数据
 * @param payload_len 负载长度
 * @return            0=成功，-1=内存分配失败
 */
static ret_t tcp_send(p2p_relay_ctx_t *ctx, const char* PROTO,
                    uint8_t type, const uint8_t *payload, int payload_len,
                    uint64_t now) {

    // 分配 sending chunk
    p2p_send_chunk_t *chunk;
    if (ctx->chunk_recycled) {
        chunk = ctx->chunk_recycled;
        ctx->chunk_recycled = chunk->next;
    } else {        
        chunk = (p2p_send_chunk_t *)malloc(sizeof(p2p_send_chunk_t));
        if (!chunk) {
            print("E:", LA_F("[R] %s%s qsend failed(OOM)\n", LA_F468, 468), type == P2P_RLY_PKT ? "PKT-" : "" , PROTO);
            return E_OUT_OF_MEMORY;  // 内存分配失败
        }
    }
    chunk->len = 0;
    chunk->next = NULL;

    // 填充包头和 payload
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)chunk->data;
    hdr->type = type;
    hdr->size = htons((uint16_t)payload_len);
    if (payload_len > 0) {
        memcpy(chunk->data + sizeof(p2p_relay_hdr_t), payload, (size_t)payload_len);
    }

    // 待发送数据长度
    chunk->len = (int)sizeof(p2p_relay_hdr_t) + payload_len;

    // 加入发送队列
    chunk->next = NULL;    
    if (ctx->send_queue_rear) {
        ctx->send_queue_rear->next = chunk;
        ctx->send_queue_rear = chunk;
    } else {
        ctx->send_queue_head = chunk;
        ctx->send_queue_rear = chunk;
    }
    ++ctx->send_queue_len;

    ctx->last_send_time = now;

    printf(LA_F("[R] %s%s qsend(%d), len=%u\n", LA_F469, 469), type == P2P_RLY_PKT ? "PKT-" : "" , PROTO,
           ctx->send_queue_len, sizeof(p2p_relay_hdr_t) + payload_len);


    return E_NONE;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 解析 SYNC 负载，追加到 session 的 remote_cands[]
 *
 * 格式: [candidate_count(1)][candidates(N*23)]
 */
static void unpack_remote_candidates(struct p2p_session *s, const uint8_t *payload, int len) {
    char _ab[INET6_ADDRSTRLEN];
    if (len < 1) {
        print("E:", LA_F("%s: bad payload len=%d\n", LA_F119, 119), TASK_SYNC_REMOTE, len);
        return;
    }

    int cand_cnt = payload[0];

    if (len < 1 + (int)sizeof(p2p_candidate_t) * cand_cnt) {
        print("E:", LA_F("%s: bad payload(len=%d cand_cnt=%d)\n", LA_F123, 123), 
              TASK_SYNC_REMOTE, len, cand_cnt);
        return;
    }

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    // 解析候选列表
    p2p_remote_candidate_entry_t *c; int offset = 1;
    for (int i = 0; i < cand_cnt; i++, offset += (int)sizeof(p2p_candidate_t)) {
        if (s->remote_cand_cnt >= s->remote_cand_cap) {
            print("W:", LA_F("%s: remote_cands[] full, skipped %d candidates\n", LA_F223, 223),
                  TASK_SYNC_REMOTE, cand_cnt - i);
            break;
        }

        int idx = s->remote_cand_cnt;
        unpack_candidate(c = &s->remote_cands[idx], payload + offset);

        // 检查重复
        int dup_idx = p2p_find_remote_candidate_by_addr(s, &c->addr);
        if (dup_idx >= 0) {
            // 如果已有候选是 prflx（PUNCH 先到），用信令带来的准确类型覆盖
            if (s->remote_cands[dup_idx].type == P2P_CAND_PRFLX && c->type != P2P_CAND_PRFLX) {
                s->remote_cands[dup_idx].type = c->type;
                s->remote_cands[dup_idx].priority = c->priority;
                print("I:", LA_F("%s: promoted prflx cand[%d]<%s:%d> → %s\n", LA_F194, 194),
                      TASK_SYNC_REMOTE, dup_idx, sockAddr_str(&c->addr, _ab, sizeof(_ab)), sockAddr_port(&c->addr),
                      p2p_candidate_type_str(c->type));
            } else {
                print("V:", LA_F("%s: duplicate remote cand<%s:%d>, skipped\n", LA_F134, 134),
                      TASK_SYNC_REMOTE, sockAddr_str(&c->addr, _ab, sizeof(_ab)), sockAddr_port(&c->addr));
            }
            continue;
        }

        const char *type_str;
        uint16_t *cand_cnt_ptr;
        bool opt_off = false;

        if (c->type == P2P_CAND_HOST) {
            type_str = "host";
            cand_cnt_ptr = &s->remote_host_cnt;
            opt_off = s->inst->cfg.test_ice_host_off;
        } else if (c->type == P2P_CAND_SRFLX) {
            type_str = "srflx";
            cand_cnt_ptr = &s->remote_srflx_cnt;
            opt_off = s->inst->cfg.test_ice_srflx_off;
        } else if (c->type == P2P_CAND_RELAY) {
            type_str = "relay";
            cand_cnt_ptr = &s->remote_relay_cnt;
            opt_off = s->inst->cfg.test_ice_relay_off;
        } else {
            print("E:", LA_F("%s: unexpected remote cand type %d, skipped\n", LA_F267, 267),
                  TASK_SYNC_REMOTE, c->type);
            continue;
        }

        if (opt_off
            || (c->addr.family == AF_INET  && s->inst->cfg.test_ice_ipv4_off)
            || (c->addr.family == AF_INET6 && s->inst->cfg.test_ice_ipv6_off)) {
            print("I:", LA_F("%s: remote %s cand[%d]<%s:%d> (disabled)\n", LA_F221, 221),
                  TASK_SYNC_REMOTE, type_str, idx, sockAddr_str(&c->addr, _ab, sizeof(_ab)), sockAddr_port(&c->addr));
            continue;
        }

        ++s->remote_cand_cnt;
        ++*cand_cnt_ptr;

        print("I:", LA_F("%s: remote %s cand[%d]<%s:%d> accepted\n", LA_F222, 222),
              TASK_SYNC_REMOTE, type_str, idx, sockAddr_str(&c->addr, _ab, sizeof(_ab)), sockAddr_port(&c->addr));

        // 启动打洞
        if (sess_ctx->state >= SIG_RELAY_SESS_SYNCING && nat_punch(s, idx) != E_NONE) {
            print("E:", LA_F("%s: punch remote cand[%d]<%s:%d> failed\n", LA_F199, 199),
                  TASK_SYNC_REMOTE, idx, sockAddr_str(&c->addr, _ab, sizeof(_ab)), sockAddr_port(&c->addr));
        }
    }
}

/*
 * 消息发送函数
 */

/*
 * 发送 REG 消息
 *
 * 包头: [type(P2P_RLY_REG) | size(2)]
 * 负载: [name(32)][instance_id(4)]
 */
static void send_online(struct p2p_instance *inst, uint64_t now) {
    const char *PROTO = "REG";

    p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;

    uint8_t payload[P2P_RLY_REG_PSZ];
    memset(payload, 0, sizeof(payload));
    strncpy((char*)payload, sig_ctx->local_peer_id, P2P_PEER_ID_MAX - 1);
    nwrite_l(payload + P2P_PEER_ID_MAX, sig_ctx->instance_id);

    if (tcp_send(sig_ctx, PROTO, P2P_RLY_REG, payload, sizeof(payload), now) != E_NONE) {
        return;
    }

    print("V:", LA_F("%s sent, name='%s' rid=%u\n", LA_F60, 60),
          PROTO, sig_ctx->local_peer_id, sig_ctx->instance_id);
}

/*
 * 发送 ALIVE 心跳
 *
 * 包头: [type(P2P_RLY_ALV) | size(2)]
 * 负载: 无
 */
static void send_alive(struct p2p_instance *inst, uint64_t now) {
    const char *PROTO = "ALIVE";

    p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;

    if (tcp_send(sig_ctx, PROTO, P2P_RLY_ALV, NULL, 0, now) != E_NONE) {
        return;
    }

    print("V:", LA_F("%s sent\n", LA_F65, 65), PROTO);
}

/*
 * 发送 SYN0 请求建立会话
 *
 * 包头: [type(P2P_RLY_SYN0) | size(2)]
 * 负载: [target_name(32)][candidate_count(1)][candidates(N*23)]
 */
static void send_sync0(struct p2p_instance *inst, struct p2p_session *s, uint64_t now) {
    const char *PROTO = "SYN0";

    p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;
    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    assert(sess_ctx->state == SIG_RELAY_SESS_WAIT_SYN0_ACK);
    assert(!sess_ctx->candidate_syncing_base && !sess_ctx->candidate_synced_count);
    assert(!sess_ctx->trickle_last_time);

    uint8_t payload[P2P_MAX_PAYLOAD]; int payload_len;
    memset(payload, 0, sizeof(payload));
    strncpy((char*)payload, sess_ctx->remote_peer_id, P2P_PEER_ID_MAX - 1);

    // 如果需要延迟等待 stun ready，则 SYN0 不携带候选
    if (P2P_SESSION_WAITING_STUN(s)) { payload[P2P_PEER_ID_MAX] = 0; payload_len = P2P_PEER_ID_MAX + 1; }
    // 初始携带的候选
    else {

        int cand_cnt = s->local_cand_cnt;
        if (cand_cnt > sig_ctx->candidate_sync_max)
            cand_cnt = sig_ctx->candidate_sync_max;

        payload[P2P_PEER_ID_MAX] = (uint8_t)cand_cnt;

        payload_len = (int)P2P_RLY_SYN0_PSZ(0);
        for (int i = 0; i < cand_cnt; i++) {
            pack_candidate(&s->local_cands[i], payload + payload_len);
            payload_len += (int)sizeof(p2p_candidate_t);
        }
    }

    int cand_sent = payload[P2P_PEER_ID_MAX];

    if (tcp_send(sig_ctx, PROTO, P2P_RLY_SYN0, payload, payload_len, now) != E_NONE) {
        return;
    }

    // SYN0 携带的候选视为已发送（服务器转发后会回 SYNC_ACK 确认）
    sess_ctx->candidate_syncing_base = cand_sent;

    print("V:", LA_F("%s sent, target='%s' cand=%u\n", LA_F64, 64),
          PROTO, sess_ctx->remote_peer_id, cand_sent);
}

/*
 * 发送 SYNC 上传候选
 *
 * 包头: [type(P2P_RLY_SYNC) | size(2)]
 * 负载: [session_id(4)][candidate_count(1)][candidates(N*23)][fin_marker(0|1)]
 *
 * FIN 语义（非独立协议）：
 *   - 仍使用 P2P_RLY_SYNC
 *   - 在 candidates 后追加一个字节 0xFF，表示本端候选发送完成（FIN）
 */
static void send_sync(struct p2p_session *s, uint64_t now) {
    const char *PROTO = "SYNC";

    p2p_relay_ctx_t *sig_ctx = &s->inst->sig_ctx.relay;
    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    // 肯定还未 FIN
    assert(sess_ctx->candidate_syncing_base <= (uint16_t)s->local_cand_cnt);

    // 之前同步发送的已经确认
    assert(sess_ctx->candidate_syncing_base == sess_ctx->candidate_synced_count);

    uint8_t payload[P2P_MAX_PAYLOAD]; int payload_len, cand_cnt, remaining;

    // 写入 session_id
    nwrite_l(payload, s->id);

    // 分配 sid（从 1 起始循环递增）
    uint8_t sid = ++sess_ctx->sync_sid;
    if (!sid) sid = ++sess_ctx->sync_sid;   // 跳过 0（保留给 SYN0 的隐式 sid）
    payload[P2P_SESS_ID_SZ] = sid;

    // 如果候选还未发送完成
    if (sess_ctx->candidate_syncing_base < (uint16_t)s->local_cand_cnt) {

        assert(sess_ctx->trickle_last_time == 0); // 还未进入攒批阶段

        int start_idx = sess_ctx->candidate_syncing_base;
        remaining = s->local_cand_cnt - start_idx;
        if (remaining > sig_ctx->candidate_sync_max) cand_cnt = sig_ctx->candidate_sync_max;
        else { cand_cnt = remaining;

            // 如果已经没有待定的候选地址，追加 fin_marker = 0xFF
            if (s->inst->srflx_active >= s->inst->srflx_count && !s->inst->turn_pending) remaining = 0;
            // 否则启动攒批机制
            else { sess_ctx->trickle_last_time = now; sig_ctx->trickle_sessions++; }
        }

        payload[P2P_SESS_ID_SZ + 1] = (uint8_t)cand_cnt;

        payload_len = (int)P2P_RLY_SYNC_PSZ(0, false);
        for (int i = 0; i < cand_cnt; i++) { int idx = start_idx + i;
            pack_candidate(&s->local_cands[idx], payload + payload_len);
            payload_len += (int)sizeof(p2p_candidate_t);
        }
        if (!remaining) payload[payload_len++] = P2P_RLY_SYNC_FIN_MARKER;
    }
    // 此时调用者要确保已经没有待定的候选地址
    else { assert(s->inst->srflx_active >= s->inst->srflx_count && !s->inst->turn_pending);

        remaining = cand_cnt = 0;

        // 发送 FIN（cand_count=0 + fin_marker = 0xFF）
        payload[P2P_SESS_ID_SZ + 1] = 0;
        payload[P2P_SESS_ID_SZ + 2] = P2P_RLY_SYNC_FIN_MARKER;
        payload_len = (int)P2P_RLY_SYNC_PSZ(0, true);
    }

    if (tcp_send(sig_ctx, PROTO, P2P_RLY_SYNC, payload, payload_len, now) != E_NONE) {
        return;
    }

    print("V:", LA_F("%s sent, ses_id=%u sid=%u cand_base=%d, cand_cnt=%d fin=%d\n", LA_F62, 62),
          PROTO, s->id, sid, sess_ctx->candidate_syncing_base, cand_cnt, remaining ? 0 : 1);

    sess_ctx->candidate_syncing_base += cand_cnt;
    if (!remaining) ++sess_ctx->candidate_syncing_base;
}

/*
 * 发送 FIN 结束会话
 *
 * 包头: [type(P2P_RLY_FIN) | size(2)]
 * 负载: [session_id(4)]
 */
static void compact_send_fin(struct p2p_session *s) {
    const char *PROTO = "FIN";

    uint8_t payload[P2P_RLY_FIN_PSZ];
    nwrite_l(payload, s->id);

    if (tcp_send(&s->inst->sig_ctx.relay, PROTO, P2P_RLY_FIN, payload, sizeof(payload), 0) != E_NONE) {
        return;
    }

    print("V:", LA_F("%s sent, ses_id=%u\n", LA_F63, 63),
          PROTO, s->id);
}

///////////////////////////////////////////////////////////////////////////////

void p2p_signal_relay_init(p2p_relay_ctx_t *ctx) {

    memset(ctx, 0, sizeof(*ctx));
    ctx->state = SIG_RELAY_INIT;
    ctx->sockfd = P_INVALID_SOCKET;
}

static void reset_peer(p2p_relay_session_t *sess_ctx, p2p_relay_ctx_t *sig_ctx) {

    sess_ctx->candidate_syncing_base = 0;
    sess_ctx->candidate_synced_count = 0;
    sess_ctx->sync_sid = 0;
    if (sess_ctx->trickle_last_time) {
        sig_ctx->trickle_sessions--;
        sess_ctx->trickle_last_time = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 处理 STATUS（实例级，非会话相关的状态）
 *
 * 由 dispatch_proto 解析后调用，仅处理 req_type < P2P_RLY_SYN0 的情况
 */
static void handle_status(struct p2p_instance *inst, uint8_t type, uint8_t code, const char *msg) {
    const char *PROTO = "STATUS";

    if (msg)
        print("E:", LA_F("%s: req_type=%u code=%u msg=%s\n", LA_F225, 225),
              PROTO, (unsigned)type, (unsigned)code, msg);
    else
        print("E:", LA_F("%s: req_type=%u code=%u\n", LA_F226, 226),
              PROTO, (unsigned)type, (unsigned)code);

    P_sock_close(inst->sig_ctx.relay.sockfd);
    inst->sig_ctx.relay.sockfd = P_INVALID_SOCKET;
    inst->sig_ctx.relay.state = SIG_RELAY_ERROR;
}

/*
 * 处理 REG_ACK
 *
 * 包头: [type(P2P_RLY_REG_ACK) | size(2)]
 * 负载: [features(1)][candidate_sync_max(1)]
 */
static void handle_online_ack(struct p2p_instance *inst, const uint8_t *payload, int len, uint64_t now) {
    const char *PROTO = "REG_ACK";

    if (len < (int)P2P_RLY_REG_S2C_PSZ) {
        print("E:", LA_F("%s: bad payload(%d)\n", LA_F121, 121), PROTO, len);
        return;
    }

    p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;
    if (sig_ctx->state != SIG_RELAY_WAIT_REG_ACK) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F150, 150), PROTO, (int)sig_ctx->state);
        return;
    }

    uint8_t features = payload[0];
    sig_ctx->feature_relay = (features & P2P_RLY_FEATURE_RELAY) != 0;
    sig_ctx->feature_msg = (features & P2P_RLY_FEATURE_MSG) != 0;
    sig_ctx->candidate_sync_max = (len >= (int)P2P_RLY_REG_S2C_PSZ) ? payload[1] : 0;

    const char* def = "";
    if (!sig_ctx->candidate_sync_max) { def = "(default)";
        sig_ctx->candidate_sync_max = SYNC_CAND_UNIT;
    }
    print("V:", LA_F("%s: accepted, cand_max=%d%s relay=%s msg=%s\n", LA_F113, 113),
          TASK_REG, sig_ctx->candidate_sync_max, def, sig_ctx->feature_relay ? "yes" : "no", sig_ctx->feature_msg ? "yes" : "no");

    // 切换到 REG 状态
    sig_ctx->state = SIG_RELAY_REG;
    inst->state = P2P_SIG_ST_READY;
    print("I:", LA_F("%s: ready to start session\n", LA_F211, 211), TASK_REG);

    // 如果服务器支持数据中继
    if (sig_ctx->feature_relay) {

        // 启动数据中继功能
        assert(!inst->signaling_relay_fn);
        inst->signaling_relay_fn = p2p_signal_relay_packet;

        path_manager_enable_signaling(inst, &sig_ctx->server_addr);
        print("I:", LA_F("%s: SIGNALING path enabled (server supports relay)\n", LA_F94, 94), TASK_REG);
    }

    for (struct p2p_session *s = inst->sessions_head; s; s = s->next) {
        p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

        // 上线完成之前，session 肯定处于 WAIT REG 阶段
        assert(sess_ctx->remote_peer_id[0] && sess_ctx->state <= SIG_RELAY_SESS_WAIT_REG);
        if (sess_ctx->state != SIG_RELAY_SESS_WAIT_REG) continue;

        sess_ctx->state = SIG_RELAY_SESS_WAIT_SYN0_ACK;
        print("I:", LA_F("%s: auth_key acquired, auto SYN0 sent\n", LA_F117, 117), TASK_TOUCH);
        send_sync0(inst, s, now);

        // 根据服务器能力设置探测状态
        if (sig_ctx->feature_msg) {
            s->probe.state = P2P_PROBE_STATE_READY;
        } else {
            s->probe.state = P2P_PROBE_STATE_NO_SUPPORT;
        }
    }
}

/*
 * 处理 ALIVE_ACK，服务器保活确认
 *
 * 包头: [type(P2P_RLY_ALIVE_ACK) | size(2)]
 * 负载: 无
 */
void handle_alive_ack(struct p2p_instance *inst, uint64_t now) {
    const char* PROTO = "ALIVE_ACK";

    p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;
    if (sig_ctx->state < SIG_RELAY_REG) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F150, 150), PROTO, (int)sig_ctx->state);
        return;
    }

    print("V:", LA_F("%s: accepted\n", LA_F116, 116), PROTO);

    // 通知路径管理器：ALIVE_ACK 确认（seq=0），完成 RoundTrip 测量
    // 仅当 SIGNALING 作为 relay 路径被启用时才统计 RTT
    if (inst->signaling.active) {
        path_manager_on_sig_alive_recv(inst, now);
    }
}

/*
 * 处理 STATUS（会话级，SYN0/SYNC/DATA/REQ/RSP 等会话相关的状态）
 *
 * 由 dispatch_proto 解析后调用
 */
static void handle_session_status(struct p2p_session *s, uint8_t type, uint8_t code, const char *msg) {
    const char *PROTO = "STATUS";

    struct p2p_instance *inst = s->inst;
    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    const char *lvl = (code == P2P_CODE_READY) ? "V:" : "W:";
    if (msg)
        print(lvl, LA_F("%s: sess_id=%u req_type=%u code=%u msg=%s\n", LA_F237, 237),
              PROTO, s->id, (unsigned)type, (unsigned)code, msg);
    else
        print(lvl, LA_F("%s: sess_id=%u req_type=%u code=%u\n", LA_F238, 238),
              PROTO, s->id, (unsigned)type, (unsigned)code);

    // 会话忙：服务器转发缓冲区满，稍后重试
    if (code == P2P_ERR_BUSY) {

        if (type == P2P_RLY_SYNC || type == P2P_RLY_SYN0) {
            if (sess_ctx->trickle_last_time) sess_ctx->trickle_last_time = P_tick_ms();
            print("V:", LA_F("%s: sync busy, will retry\n", LA_F249, 249), PROTO);
        }
        else if (type == P2P_RLY_PKT) {
            // relay 流控：保持等待，延迟重试
            sess_ctx->awaiting_relay_ready = true;
            print("V:", LA_F("%s: relay busy, will retry\n", LA_F218, 218), PROTO);
        }
    }
    // 服务就绪：解除对应流控
    else if (code == P2P_CODE_READY) {

        if (type == P2P_RLY_PKT) {
            sess_ctx->awaiting_relay_ready = false;
            print("V:", LA_F("%s: relay ready, flow control released\n", LA_F219, 219), PROTO);
        }
    }
    // todo 执行 fin by peer
    else if (code == P2P_ERR_PEER_OFF) {

        print("W:", LA_F("%s: peer offline\n", LA_F188, 188), PROTO);

        p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;
        if (sig_ctx->state >= SIG_RELAY_REG) {
            sess_ctx->state = SIG_RELAY_SESS_WAIT_PEER;
            s->state = P2P_STATE_WAITING;
            print("I:", LA_F("[ST:%s] peer went offline, waiting for reconnect\n", LA_F488, 488), "WAIT_PEER");
        }
    }
    // NOT_REG / PROTOCOL / INTERNAL / UNKNOWN → 致命错误
    else {
        print("E:", LA_F("%s: fatal error code=%u, entering ERROR state\n", LA_F143, 143),
                PROTO, (unsigned)code);

        P_sock_close(inst->sig_ctx.relay.sockfd);
        inst->sig_ctx.relay.sockfd = P_INVALID_SOCKET;
        inst->sig_ctx.relay.state = SIG_RELAY_ERROR;
    }
}

/*
 * 处理 SYN0_ACK
 *
 * 包头: [type(P2P_RLY_SYN0_ACK) | size(2)]
 * 负载: [target_name(32)][session_id(4)][[0xFF]|[candidate_count(1)][candidates(N*23)]]
 * 注: [target_name(32)][session_id(4)] 已剥离
 */
static void handle_sync0_ack(struct p2p_session *s, const uint8_t *payload, uint64_t now) {

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    print("V:", LA_F("%s: accepted (ses_id=%u), peer=%s\n", LA_F107, 107),
          TASK_TOUCH, s->id, payload[0] == 0xFF ? "offline" : "online");

    sess_ctx->state = SIG_RELAY_SESS_WAIT_PEER;
    assert(s->state == P2P_STATE_SIGNALING);
    s->state = P2P_STATE_WAITING;

    // 如果对端未在线
    if (payload[0] == 0xFF) {
        print("I:", LA_F("%s: session offer(st=%s peer=%s), %s\n", LA_F240, 240),
                TASK_TOUCH, "WAIT_PEER", "offline", LA_S("waiting for peer", LA_S29, 29));
        return;
    }

    // 首次确认双方在线，启动 NAT 打洞（即使当前还没收到远程候选也要启动，用于实现打洞整体超时后 fallback 到信令中转）
    // + 该操作会触发 p2p_connecting，并启动 p2p_stun_collect
    nat_punch(s, -1/* all candidates */);

    // 如果需要等待 stun 收集完成再同步
    if (P2P_SESSION_WAITING_STUN(s)) {

        sess_ctx->state = SIG_RELAY_SESS_WAIT_STUN;
        print("I:", LA_F("%s: session established(st=%s peer=%s), %s\n", LA_F239, 239),
              TASK_TOUCH, "WAIT_STUN", "online", LA_S("waiting stun pending", LA_S30, 30));
    }
    // 否则直接进入 SYNCING 状态，开始上传候选
    else {

        sess_ctx->state = SIG_RELAY_SESS_SYNCING;
        print("I:", LA_F("%s: session established(st=%s peer=%s), %s\n", LA_F239, 239),
              TASK_TOUCH, "SYNCING", "online", LA_S("sync candidates", LA_S28, 28));

        // 收到对端 SYN0 隐含确认了本端 SYN0 候选已被转发，直接推进确认计数
        sess_ctx->candidate_synced_count = sess_ctx->candidate_syncing_base;

        if (sess_ctx->candidate_syncing_base < (uint16_t)s->local_cand_cnt || !P2P_CAND_PENDING(s->inst))
            send_sync(s, now);
        else { sess_ctx->trickle_last_time = now; s->inst->sig_ctx.relay.trickle_sessions++; }
    }

    // todo 支持候选列表
}

/*
 * 处理 SYNC confirm（服务器 1:1 确认，回显本批次 sid）
 *
 * 包头: [type(P2P_RLY_SYNC) | size(2)]
 * 负载: [session_id(4)][sid(1)]
 * 注: [session_id(4)] 已剥离
 *
 * 流程（1:1 应答，无 confirm_count）：
 *   - 收到 confirm → 本批次全部候选已转发到对端
 *   - 若本批次是 FIN 批次（syncing_base > local_cand_cnt）→ 迁移至 READY
 *   - 否则继续发送下一批次
 */
static void handle_sync_confirm(struct p2p_session *s, const uint8_t *payload, int len, uint64_t now) {
    (void)len;

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;
    if (sess_ctx->state != SIG_RELAY_SESS_SYNCING) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F150, 150), TASK_SYNC, (int)sess_ctx->state);
        return;
    }

    uint8_t sid = payload[0];

    print("V:", LA_F("%s: sync confirm sid=%u synced=%d base=%d\n", LA_F252, 252),
          TASK_SYNC, sid, sess_ctx->candidate_synced_count, sess_ctx->candidate_syncing_base);

    // 1:1 confirm：本批次全部候选已确认转发
    sess_ctx->candidate_synced_count = sess_ctx->candidate_syncing_base;

    // FIN 批次确认（syncing_base > local_cand_cnt 意味着已发送 FIN）
    if (sess_ctx->candidate_syncing_base > (uint16_t)s->local_cand_cnt) {
        sess_ctx->state = SIG_RELAY_SESS_READY;
        print("I:", LA_F("%s: sync done, st=%s cands=%d\n", LA_F253, 253),
              TASK_SYNC, "READY", sess_ctx->candidate_synced_count);
        return;
    }

    // 收到确认后继续发送下一批次（只有上一批次全部确认后才发送下一批，且还未进入攒批阶段）

    // 如果已没有待收集的候选了
    if (!P2P_CAND_PENDING(s->inst)) {
        if (sess_ctx->trickle_last_time) { sess_ctx->trickle_last_time = 0; s->inst->sig_ctx.relay.trickle_sessions--; }
        send_sync(s, now);
    }
    // 或已经积累了足够的候选；又或者还没进入攒批阶段; 或距离上次发送已经超过攒批时间窗口了
    else if (sess_ctx->candidate_syncing_base < (uint16_t)s->local_cand_cnt &&
                ((s->local_cand_cnt - sess_ctx->candidate_syncing_base >= s->inst->sig_ctx.relay.candidate_sync_max) ||
                !sess_ctx->trickle_last_time ||
                (P_tick_ms() - sess_ctx->trickle_last_time) >= P2P_RELAY_TRICKLE_BATCH_MS)) {

        send_sync(s, now);
    }
    // 还有候选待收集但当前已全部发完，进入攒批等待
    else if (!sess_ctx->trickle_last_time) {
        sess_ctx->trickle_last_time = P_tick_ms(); s->inst->sig_ctx.relay.trickle_sessions++;
    }
}


/*
 * 处理 SYNC（服务器下发对端候选）
 *
 * 包头: [type(P2P_RLY_SYNC) | size(2)]
 * 负载: [session_id(4)][candidate_count(1)][candidates(N*23)][fin_marker(0|1)]
 * 注: [session_id(4)] 已剥离
 */
static void handle_peer_sync(struct p2p_session *s, const uint8_t *payload, int len, uint64_t now) { (void)now;

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;
    if (sess_ctx->state != SIG_RELAY_SESS_SYNCING && sess_ctx->state != SIG_RELAY_SESS_WAIT_STUN) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F150, 150), TASK_SYNC_REMOTE, (int)sess_ctx->state);
        return;
    }

    uint8_t sid = payload[0];
    uint8_t cand_cnt = payload[1]; uint32_t base_len = P2P_RLY_SYNC_PSZ(cand_cnt, false) - P2P_SESS_ID_SZ;
    bool has_fin = false;
    if ((uint32_t)len == base_len + 1u) {
        has_fin = true;
        if (payload[base_len] != P2P_RLY_SYNC_FIN_MARKER) {
            print("E:", LA_F("%s: bad FIN marker=0x%02x\n", LA_F118, 118), TASK_SYNC_REMOTE, payload[base_len]);
            return;
        }
    } else if ((uint32_t)len != base_len) {
        print("E:", LA_F("%s: bad payload(len=%d cand_cnt=%d)\n", LA_F123, 123), TASK_SYNC_REMOTE, len, cand_cnt);
        return;
    }

    // 解析候选列表（首部 sid(1); 尾部可能有 1B FIN 标记）
    unpack_remote_candidates(s, payload + 1, (int)base_len - 1);

    if (has_fin) {
        s->remote_cand_done = true;
        print("I:", LA_F("%s: sync done\n", LA_F254, 254), TASK_SYNC_REMOTE);
    }

    print("V:", LA_F("%s: processed sid=%u synced=%d\n", LA_F193, 193), TASK_SYNC_REMOTE, sid, s->remote_cand_cnt);
}

/*
 * 处理 FIN（服务器转发的对端会话结束通知）
 *
 * 包头: [type(P2P_RLY_FIN) | size(2)]
 * 负载: [session_id(4)]
 * 注: [session_id(4)] 已剥离
 */
static void handle_relay_fin(struct p2p_session *s, const uint8_t *payload, int len, uint64_t now) {
    (void)payload; (void)len; (void)now;

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    // 清理会话状态，回到 WAIT_PEER 被动等待（对端主动断开，不自动重连）
    print("I:", LA_F("%s: session suspend(st=%s)\n", LA_F242, 242),
          TASK_TOUCH, "WAIT_PEER");
    sess_ctx->state = SIG_RELAY_SESS_WAIT_PEER;
    s->id = 0;

    reset_peer(sess_ctx, &s->inst->sig_ctx.relay);

    // 触发 NAT 层断开，让 p2p_update 走正常的 peer_disconnect 路径
    // RELAY FIN 经 TCP 可靠传输，等同于 NAT FIN；即使 NAT FIN（UDP）丢失也能正确触发断开
    if (s->nat.state > NAT_CLOSED) {
        s->nat.state = NAT_CLOSED;
    }
}

/*
 * 处理服务器转发的 DATA/ACK/CRYPTO/... 包
 *
 * 包头: [type(P2P_RLY_PKT) | size(2)]
 * 负载: [session_id(4)][P2P hdr(4)][payload(N)]
 * 注: [session_id(4)] 已剥离
 *
 * 处理流程：
 *   1. 验证 session_id
 *   2. 解析 P2P 包头
 *   3. 根据类型调用相应处理函数（CRYPTO→解密后递归，DATA/ACK→reliable 层）
 */
static void handle_relay_packet(struct p2p_session *s, const uint8_t *payload, int len, uint64_t now) {

    // P2P 包头
    p2p_packet_hdr_t hdr;
    p2p_pkt_hdr_decode(payload, &hdr);

    print("V:", LA_F("%s: pkt recv (ses_id=%u), inner type=%u\n", LA_F192, 192), TASK_RELAY, s->id, hdr.type);

    sockAddr_t from;
    sockAddr_from_v4(&from, &s->inst->sig_ctx.relay.server_addr);
    nat_proto(s, hdr.type, hdr.flags, hdr.seq, payload + P2P_HDR_SIZE, len - P2P_HDR_SIZE,
              &from, now);
}

/*
 * 处理服务器转发的 RPC 请求（B端接收）
 *
 * 负载格式: [session_id(4)][sid(2)][msg(1)][data(N)]
 * 注: [session_id(4)] 已剥离
 */
static void handle_relay_req(struct p2p_session *s, const uint8_t *payload, int len, uint64_t now) { (void)now;

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    uint16_t sid = nget_s(payload);
    uint8_t  msg = payload[2]; const uint8_t *req_data = payload + 3; int req_len = len - 3;

    // 去重：忽略正在处理的相同请求
    if (sess_ctx->resp_sid == sid) {
        print("V:", LA_F("%s: duplicate request ignored (sid=%u)\n", LA_F135, 135), TASK_RPC, sid);
        return;
    }

    // 忽略旧请求
    if (sess_ctx->rpc_last_sid != 0 && !uint16_circle_newer(sid, sess_ctx->rpc_last_sid)) {
        print("V:", LA_F("%s: old request ignored (sid=%u <= last_sid=%u)\n", LA_F178, 178),
              TASK_RPC, sid, sess_ctx->rpc_last_sid);
        return;
    }

    sess_ctx->resp_sid = sid;

    // msg=0: 自动 echo 回复
    if (msg == 0) {
        print("V:", LA_F("%s msg=0: echo reply (sid=%u)\n", LA_F43, 43), TASK_RPC, sid);
        p2p_signal_relay_response(s, 0, req_data, req_len);
        return;
    }

    print("V:", LA_F("%s req accepted (ses_id=%u), sid=%u msg=%u\n", LA_F45, 45), TASK_RPC, s->id, sid, msg);

    if (s->inst->cfg.on_request)
        s->inst->cfg.on_request((p2p_session_t)s, sid, msg, req_data, req_len, s->inst->cfg.userdata);
}

/*
 * 处理服务器转发的 RPC 响应（A端接收）
 *
 * 负载格式: [session_id(4)][sid(2)][code(1)][data(N)]
 * 注: [session_id(4)] 已剥离
 */
static void handle_relay_resp(struct p2p_session *s, const uint8_t *payload, int len, uint64_t now) { (void)now;


    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    uint16_t sid  = nget_s(payload);
    uint8_t  code = payload[2]; const uint8_t *res_data = payload + 3; int res_len = len - 3;

    // 仅命中当前挂起请求
    if (!(sess_ctx->req_state == 1 && sess_ctx->req_sid == sid)) {
        print("E:", LA_F("%s: irrelevant response (sid=%u, current sid=%u, state=%d)\n", LA_F160, 160),
              TASK_RPC, sid, sess_ctx->req_sid, (int)sess_ctx->req_state);
        return;
    }

    // 错误响应
    if (code >= P2P_RPC_ERR_PEER_OFF) {
        if (code == P2P_RPC_ERR_PEER_OFF)
            print("W:", LA_F("%s: peer offline (sid=%u)\n", LA_F186, 186), TASK_RPC, sid);
        else
            print("W:", LA_F("%s: timeout (sid=%u)\n", LA_F256, 256), TASK_RPC, sid);

        sess_ctx->req_state = 0;
        sess_ctx->req_sid   = 0;

        if (s->inst->cfg.on_response)
            s->inst->cfg.on_response((p2p_session_t)s, sid, code, NULL, -1, s->inst->cfg.userdata);
        return;
    }

    print("V:", LA_F("%s: complete (ses_id=%u), sid=%u code=%u\n", LA_F131, 131), TASK_RPC, s->id, sid, code);

    sess_ctx->req_state = 0;
    sess_ctx->req_sid   = 0;

    if (s->inst->cfg.on_response)
        s->inst->cfg.on_response((p2p_session_t)s, sid, code, res_data, res_len, s->inst->cfg.userdata);
}

/*
 * 协议分发
 */
static void dispatch_proto(struct p2p_instance *inst, uint64_t now) {

    p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;

    do { const char *PROTO;

        if (sig_ctx->hdr.type == P2P_RLY_STA) { PROTO = "STATUS";
            printf(LA_F("[R] %s recv, len=%d\n", LA_F466, 466), PROTO, sig_ctx->hdr.size);

            const int st_sz = P2P_RLY_STA_PSZ(0, 0);
            if (sig_ctx->hdr.size < st_sz) {
                print("E:", LA_F("%s: bad payload(%d)\n", LA_F121, 121), PROTO, sig_ctx->hdr.size);
            }

            uint8_t req_type = sig_ctx->payload[0], status_code = sig_ctx->payload[1];

            // 对于 client 级别的状态，直接处理；对于 session 级别的状态，根据 req_type 定位对应 session 后再处理
            if (req_type < P2P_RLY_SYN0) {

                if (sig_ctx->hdr.size > st_sz) { sig_ctx->payload[sizeof(sig_ctx->payload)-1] = 0;
                    handle_status(inst, req_type, status_code, (const char*)sig_ctx->payload + st_sz);
                } else handle_status(inst, req_type, status_code, NULL);
                break;
            }

            // 对于 client/session 临界级别的状态
            if (req_type == P2P_RLY_SYN0) {

                if (sig_ctx->hdr.size < P2P_RLY_STA_PSZ(1, 0)) {
                    print("E:", LA_F("%s: bad payload(%d, type=%u)\n", LA_F122, 122), PROTO, sig_ctx->hdr.size, req_type);
                    return;
                }

                struct p2p_session* s = inst->sessions_head;
                for(; s; s = s->next) {
                    if (strncmp(s->sig_sess.relay.remote_peer_id, (char*)sig_ctx->payload + st_sz, P2P_PEER_ID_MAX-1) == 0) {

                        if (sig_ctx->hdr.size > P2P_RLY_STA_PSZ(1, 0)) { sig_ctx->payload[sizeof(sig_ctx->payload)-1] = 0;
                            handle_session_status(s, req_type, status_code, (const char*)sig_ctx->payload + P2P_RLY_STA_PSZ(1, 0));
                        } else handle_session_status(s, req_type, status_code, NULL);
                        break;
                    }
                }
                if (!s) {
                    print("W:", LA_F("%s: no session for peer_id=%.*s (req_type=%u)\n", LA_F169, 169),
                          PROTO, (int)(P2P_PEER_ID_MAX-1), (char*)sig_ctx->payload + 1, (unsigned)req_type);
                }
                break;
            }

            // 对于 session 级别的状态

            if (sig_ctx->hdr.size < P2P_RLY_STA_PSZ(2, 0)) {
                print("E:", LA_F("%s: bad payload(%d, type=%u)\n", LA_F122, 122), PROTO, sig_ctx->hdr.size, req_type);
                return;
            }

            uint8_t* ptr = sig_ctx->payload + st_sz;
            uint32_t session_id = nget_l(ptr);
            struct p2p_session* s = inst->sessions_head;
            for(; s; s = s->next) {
                if (session_id == s->id) {

                    if (sig_ctx->hdr.size > P2P_RLY_STA_PSZ(2, 0)) { sig_ctx->payload[sizeof(sig_ctx->payload)-1] = 0;
                        handle_session_status(s, req_type, status_code, (const char*)sig_ctx->payload + P2P_RLY_STA_PSZ(2, 0));
                    } else handle_session_status(s, req_type, status_code, NULL);
                    break;
                }
            }
            if (!s) {
                print("W:", LA_F("%s: no session for session_id=%u (req_type=%u)\n", LA_F171, 171),
                      PROTO, session_id, (unsigned)req_type);
            }
            break;
        }

        if (sig_ctx->hdr.type == P2P_RLY_REG) {
            printf(LA_F("[R] %s recv, len=%d\n", LA_F466, 466), "REG ACK", sig_ctx->hdr.size);

            handle_online_ack(inst, sig_ctx->payload, (int)sig_ctx->hdr.size, now);
            break;
        }
        if (sig_ctx->hdr.type == P2P_RLY_ALV) {
            printf(LA_F("[R] %s recv, len=%d\n", LA_F466, 466), "ALV ACK", sig_ctx->hdr.size);

            handle_alive_ack(inst, now);
            break;
        }

        if (sig_ctx->hdr.type == P2P_RLY_SYN0) { PROTO = "SYN0";

            printf(LA_F("[R] %s recv, len=%d\n", LA_F466, 466), PROTO, sig_ctx->hdr.size);

            if (sig_ctx->hdr.size < (int)P2P_RLY_SYN0_S2C_PSZ(0)) {
                print("E:", LA_F("%s: bad payload(%d)\n", LA_F121, 121), PROTO, sig_ctx->hdr.size);
                return;
            }

            uint8_t* ptr = sig_ctx->payload + P2P_PEER_ID_MAX;
            uint32_t session_id = nget_l(ptr);
            if (!session_id) {
                print("W:", LA_F("%s: missing session_id in payload\n", LA_F163, 163), PROTO);
                return;
            }

            struct p2p_session* s = inst->sessions_head; p2p_relay_session_t *sess_ctx = NULL;
            for(; s; s = s->next) { sess_ctx = &s->sig_sess.relay;
                if (strncmp(sess_ctx->remote_peer_id, (char*)sig_ctx->payload, P2P_PEER_ID_MAX-1) == 0) break;
            }
            if (!s) {
                print("W:", LA_F("%s: no session for peer_id=%.*s\n", LA_F170, 170),
                      PROTO, (int)(P2P_PEER_ID_MAX-1), (char*)sig_ctx->payload);
                return;
            }

            if (sess_ctx->state == SIG_RELAY_SESS_WAIT_SYN0_ACK) {

                assert(!s->id);
                s->id = session_id;
                if (s->inst->cfg.use_ice) p2p_ice_set_ufrag_from_id(s);

                handle_sync0_ack(s, ptr + P2P_SESS_ID_SZ, now);

                return;
            }

            if (sess_ctx->state != SIG_RELAY_SESS_WAIT_PEER || P2P_RLY_IS_SYN0_PEER_OFF(sig_ctx->payload)) {
                print("V:", LA_F("%s: ignored in state=%d\n", LA_F150, 150), PROTO, (int)sess_ctx->state);
                return;
            }

            assert(s->id);
            if (s->id != session_id) {

                // 重置信令层会话状态
                reset_peer(sess_ctx, sig_ctx);

                // 通知业务层连接断开（session 被对方重置）
                if (s->state >= P2P_STATE_LOST) {
                    if (s->inst->cfg.on_state) s->inst->cfg.on_state((p2p_session_t)s, s->state, P2P_STATE_CLOSED, s->inst->cfg.userdata);
                }

                // 重置 p2p 会话
                p2p_session_reset(s, false);

                // 初始化 p2p 新会话状态
                uint32_t old_id = s->id;
                s->id = session_id;
                if (s->inst->cfg.use_ice) p2p_ice_set_ufrag_from_id(s);
                s->state = P2P_STATE_WAITING;

                sess_ctx->state = SIG_RELAY_SESS_WAIT_PEER;
                print("W:", LA_F("%s: session reset by peer(old=%u new=%u), %s\n", LA_F241, 241),
                        TASK_TOUCH, old_id, session_id, LA_S("resync for peer", LA_S27, 27));
            }

            // 首次收到 SYN0 视为对端上线，启动候选交换
            if (sess_ctx->state == SIG_RELAY_SESS_WAIT_PEER) {

                // 首次确认双方在线，启动 NAT 打洞
                // + 该操作会触发 p2p_connecting，并启动 p2p_stun_collect
                nat_punch(s, -1/* all candidates */);

                if (P2P_SESSION_WAITING_STUN(s)) {
                    sess_ctx->state = SIG_RELAY_SESS_WAIT_STUN;
                    print("I:", LA_F("%s: session established(st=%s peer=%s), %s\n", LA_F239, 239),
                                  TASK_TOUCH, "WAIT_STUN", "sync0", LA_S("waiting stun pending", LA_S30, 30));
                }
                else {
                    sess_ctx->state = SIG_RELAY_SESS_SYNCING;
                    print("I:", LA_F("%s: session established(st=%s peer=%s), %s\n", LA_F239, 239),
                            TASK_TOUCH, "SYNCING", "sync0", LA_S("sync candidates", LA_S28, 28));

                    // SYN0 携带的候选可能尚未被 SYNC_ACK 确认
                    if (sess_ctx->candidate_synced_count == sess_ctx->candidate_syncing_base) {
                        if (sess_ctx->candidate_syncing_base < (uint16_t)s->local_cand_cnt || !P2P_CAND_PENDING(s->inst))
                            send_sync(s, now);
                        else { sess_ctx->trickle_last_time = now; sig_ctx->trickle_sessions++; }
                    }
                }
            }
            // 可能已经在（本端的）sync0_ack 时就直接进入到了 SYNCING 状态了
            // + 对于这种情况，则肯定已经执行过 nat_punch -1 了
            else if (sess_ctx->state != SIG_RELAY_SESS_SYNCING && sess_ctx->state != SIG_RELAY_SESS_WAIT_STUN) {
                print("V:", LA_F("%s: ignored in state=%d\n", LA_F150, 150), PROTO, (int)sess_ctx->state);
                return;
            }

            // 如果存在首批同步的数据
            if (ptr[P2P_SESS_ID_SZ/* candidate_count */] || sig_ctx->hdr.size > P2P_RLY_SYN0_S2C_PSZ(0))
                handle_peer_sync(s, ptr + P2P_SESS_ID_SZ, (int)(sig_ctx->hdr.size - P2P_PEER_ID_MAX - P2P_SESS_ID_SZ), now);

            break;
        }

        uint16_t payload_min; void (*handler)(struct p2p_session*, const uint8_t*, int, uint64_t);
        switch (sig_ctx->hdr.type) {
            case P2P_RLY_SYNC:
                PROTO = "SYNC"; payload_min = P2P_RLY_SYNC_CONFIRM_PSZ;
                handler = P2P_RLY_IS_SYNC_CONFIRM(&sig_ctx->hdr) ? handle_sync_confirm : handle_peer_sync;
            break;
            case P2P_RLY_FIN:
                PROTO = "FIN"; payload_min = P2P_RLY_FIN_PSZ; handler = handle_relay_fin; break;
            case P2P_RLY_PKT:
                PROTO = "PKT"; payload_min = P2P_RLY_PKT_PSZ(0); handler = handle_relay_packet; break;
            case P2P_RLY_REQ:
                PROTO = "REQ"; payload_min = P2P_RLY_RPC_MIN_PSZ; handler = handle_relay_req; break;
            case P2P_RLY_RSP:
                PROTO = "RSP"; payload_min = P2P_RLY_RPC_MIN_PSZ; handler = handle_relay_resp; break;
            default:
                print("W:", LA_F("[R] Unknown proto type %d\n", LA_F482, 482), sig_ctx->hdr.type);
                return;
        }

        printf(LA_F("[R] %s recv, len=%d\n", LA_F466, 466), PROTO, sig_ctx->hdr.size);

        if (sig_ctx->hdr.size < payload_min) {
            print("E:", LA_F("%s: bad payload(%d)\n", LA_F121, 121), PROTO, sig_ctx->hdr.size);
            return;
        }

        uint32_t session_id = nget_l(sig_ctx->payload);
        if (!session_id) {
            print("W:", LA_F("%s: missing session_id in payload\n", LA_F163, 163), PROTO);
            return;
        }

        struct p2p_session* s = inst->sessions_head;
        for(; s; s = s->next) {
            if (s->id == session_id) {
                handler(s, sig_ctx->payload + P2P_SESS_ID_SZ, (int)(sig_ctx->hdr.size - P2P_SESS_ID_SZ), now);
                break;
            }
        }
        if (!s) {
            print("W:", LA_F("%s: no session for session_id=%u\n", LA_F172, 172),
                  PROTO, session_id);
            return;
        }

    } while(0);

    sig_ctx->last_recv_time = P_tick_ms();
}

///////////////////////////////////////////////////////////////////////////////

ret_t p2p_signal_relay_online(struct p2p_instance *inst, const char *local_peer_id,
                              const struct sockaddr_in *server) {

    p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;
    P_check(sig_ctx->state == SIG_RELAY_INIT, return E_NONE_CONTEXT;)

    sig_ctx->server_addr = *server;

    // 每次 online() 生成新的实例 ID（加密安全随机数），用于服务器区分重新连接会话
    uint32_t rid = sig_ctx->instance_id;
    while (rid == sig_ctx->instance_id) rid = P_rand32();
    sig_ctx->instance_id = rid;

    // local_peer_id 保存
    strncpy(sig_ctx->local_peer_id, local_peer_id, P2P_PEER_ID_MAX - 1);
    sig_ctx->local_peer_id[P2P_PEER_ID_MAX - 1] = '\0';

    // 创建 TCP socket
    sig_ctx->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sig_ctx->sockfd == P_INVALID_SOCKET) {
        print("E:", LA_F("[R] Failed to create TCP socket\n", LA_F472, 472));
        return E_UNKNOWN;
    }

    // 设置非阻塞
    if (P_sock_nonblock(sig_ctx->sockfd, true) != E_NONE) {
        print("E:", LA_F("[R] Failed to set socket non-blocking\n", LA_F473, 473));
        P_sock_close(sig_ctx->sockfd);
        sig_ctx->sockfd = P_INVALID_SOCKET;
        return E_UNKNOWN;
    }

    // 连接到服务器
    print("I:", LA_F("[R] Connecting to %s:%d\n", LA_F470, 470),
          inet_ntoa(server->sin_addr), ntohs(server->sin_port));

    int ret = connect(sig_ctx->sockfd, (struct sockaddr *)&sig_ctx->server_addr,
                      sizeof(sig_ctx->server_addr));

    // 连接立即成功（少见）
    if (ret == 0) {
        print("I:", LA_F("[R] TCP connected immediately, sending REG\n", LA_F476, 476));
        sig_ctx->state = SIG_RELAY_WAIT_REG_ACK;
        send_online(inst, P_tick_ms());
    }
    // 连接进行中
    else if (P_sock_is_inprogress()) {
        sig_ctx->state = SIG_RELAY_CONNECTING;
        sig_ctx->last_send_time = P_tick_ms();
    }
    else {
        print("E:", LA_F("[R] TCP connect failed(%d)\n", LA_F474, 474), P_sock_errno());
        P_sock_close(sig_ctx->sockfd);
        sig_ctx->sockfd = P_INVALID_SOCKET;
        return E_UNKNOWN;
    }

    return E_NONE;
}

ret_t p2p_signal_relay_offline(struct p2p_instance *inst) {

    p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;
    if (sig_ctx->state == SIG_RELAY_INIT) return E_NONE;

    if (sig_ctx->sockfd != P_INVALID_SOCKET) {
        P_sock_close(sig_ctx->sockfd);
    }

    // 清理发送队列
    p2p_send_chunk_t *chunk;
    while ((chunk = sig_ctx->send_queue_head)) {
        sig_ctx->send_queue_head = chunk->next;
        free(chunk);
    }
    sig_ctx->send_queue_len = 0;

    // 销毁 chunk 池（释放所有回收池中的 chunk）
    while ((chunk = sig_ctx->chunk_recycled)) {
        sig_ctx->chunk_recycled = chunk->next;
        free(chunk);
    }

    p2p_signal_relay_init(sig_ctx);
    return E_NONE;
}

//-----------------------------------------------------------------------------

void p2p_signal_relay_stun_ready(struct p2p_session *s) {

    assert(s->inst->srflx_active >= s->inst->srflx_count);

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    if (sess_ctx->state == SIG_RELAY_SESS_WAIT_STUN) {

        sess_ctx->state = SIG_RELAY_SESS_SYNCING;
        print("I:", LA_F("%s: stun collection ready, auto SYNC sent\n", LA_F248, 248), TASK_TOUCH);

        // 同步发送首批候选（如果有）
        assert(sess_ctx->candidate_syncing_base == 0);
        if (s->local_cand_cnt || !s->inst->turn_pending)
            send_sync(s, P_tick_ms());
        else { sess_ctx->trickle_last_time = P_tick_ms(); s->inst->sig_ctx.relay.trickle_sessions++; }
    }
}

void p2p_signal_relay_trickle_candidate(struct p2p_session *s) {

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    if (sess_ctx->state == SIG_RELAY_SESS_SYNCING) {

        // 还没进入 trickle 阶段
        // + 也就是 sync_ack 首次将现有的候选全部同步完成，但又存在待收集的异步候选（如 STUN/TURN）
        if (!sess_ctx->trickle_last_time) return;

        // 检查是否有新候选
        if (sess_ctx->candidate_syncing_base >= s->local_cand_cnt) {
            assert(sess_ctx->candidate_syncing_base == s->local_cand_cnt);
            return;
        }

        // 如果上次发送后还没有收到对端的 SYNC_AC
        if (sess_ctx->candidate_synced_count < sess_ctx->candidate_syncing_base) return;
        assert(sess_ctx->candidate_synced_count == sess_ctx->candidate_syncing_base);

        // 发送控制：如果已没有待收集的候选了
        if (s->inst->srflx_active >= s->inst->srflx_count && !s->inst->turn_pending) {

            sess_ctx->trickle_last_time = 0; s->inst->sig_ctx.relay.trickle_sessions--;
            send_sync(s, P_tick_ms());
        }
        // 或已经积累了足够的候选；又或者距离上次发送已经超过攒批时间窗口了
        else if ((s->local_cand_cnt - sess_ctx->candidate_syncing_base >= s->inst->sig_ctx.relay.candidate_sync_max) ||
                 (P_tick_ms() - sess_ctx->trickle_last_time) >= P2P_RELAY_TRICKLE_BATCH_MS) {

            send_sync(s, P_tick_ms());
        }
    }
    // todo: >SIG_RELAY_SESS_SYNCING 用于动态更新 srflx 地址的变更
}

//-----------------------------------------------------------------------------

ret_t p2p_signal_relay_connect(struct p2p_session *s, const char *remote_peer_id) {

    P_check(remote_peer_id && remote_peer_id[0], return E_INVALID;)

    p2p_relay_ctx_t *sig_ctx = &s->inst->sig_ctx.relay;
    if (sig_ctx->state == SIG_RELAY_INIT) {
        return E_NONE_CONTEXT;
    }

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    // 如果当前已经连接过：target 相同则幂等成功；不同则视为忙
    if (sess_ctx->remote_peer_id[0]) {
        return strcmp(sess_ctx->remote_peer_id, remote_peer_id) == 0 ? E_NONE : E_BUSY;
    }

    // 设置连接目标
    strncpy(sess_ctx->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX - 1);
    sess_ctx->remote_peer_id[P2P_PEER_ID_MAX - 1] = '\0';

    // 已上线：立即发送 SYN0；否则等待 REG_ACK 后自动触发
    if (sig_ctx->state == SIG_RELAY_REG) {
        sess_ctx->state = SIG_RELAY_SESS_WAIT_SYN0_ACK;
        send_sync0(s->inst, s, P_tick_ms());
    }
    else sess_ctx->state = SIG_RELAY_SESS_WAIT_REG;

    return E_NONE;
}

ret_t p2p_signal_relay_disconnect(struct p2p_session *s) {

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;
    if (!sess_ctx->remote_peer_id[0]) return E_NONE;        // 没有建立过配对

    // 如果尚未完成在线：直接取消 connect 连接状态
    if (sess_ctx->state == SIG_RELAY_SESS_WAIT_REG)
        sess_ctx->state = SIG_RELAY_SESS_SUSPENDED;
    if (sess_ctx->state == SIG_RELAY_SESS_SUSPENDED) {
        *sess_ctx->remote_peer_id = 0;
        return E_NONE;
    }

    // 如果正在通过信令服务器申请建立连接中
    if (sess_ctx->state < SIG_RELAY_SESS_WAIT_PEER) {
        return E_BUSY;
    }

    print("I:", LA_F("[R] Disconnected, back to REG state\n", LA_F471, 471));

    // 发送 FIN 消息, fixme 为啥是 compact_send_fin ？
    compact_send_fin(s);

    // 清理 peer 会话状态
    reset_peer(sess_ctx, &s->inst->sig_ctx.relay);
    memset(sess_ctx->remote_peer_id, 0, sizeof(sess_ctx->remote_peer_id));

    // 清理会话状态
    s->id = 0;
    return E_NONE;
}

/*
 * 通过 RELAY 服务器转发数据包（DATA/ACK/CRYPTO/REACH/CONN/CONN_ACK）
 *
 * 包头: [type(P2P_RLY_PKT) | size(2)]
 * 负载: [session_id(4)][P2P packet header(4)][payload(N)]
 * 内层 P2P hdr.type 区分实际包类型。
 *
 * 流控：发送后设置 awaiting_relay_ready，收到 STATUS(READY) 后清除。
 */
ret_t p2p_signal_relay_packet(struct p2p_session *s,
                              uint8_t type, uint8_t flags, uint16_t seq,
                              const void *payload, uint16_t payload_len) {

    p2p_relay_ctx_t *sig_ctx = &s->inst->sig_ctx.relay;
    P_check(!payload_len || payload, return E_INVALID;)
    P_check(sig_ctx->feature_relay, return E_NO_SUPPORT;)
    P_check(s->id, return E_NONE_CONTEXT;)

    // 流控检查：等待上一个转发完成
    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;
    if (sess_ctx->awaiting_relay_ready) {
        print("V:", LA_F("%s throttled: awaiting READY\n", LA_F67, 67), TASK_RELAY);
        return E_BUSY;
    }

    // 所有类型统一使用 P2P_RLY_PKT 隧道，内层 P2P hdr 保留真实类型
    const char *proto;
    switch (type) {
        case P2P_PKT_DATA:     proto = "DATA";     break;
        case P2P_PKT_ACK:      proto = "ACK";      break;
        case P2P_PKT_CRYPTO:   proto = "CRYPTO";   break;
        case P2P_PKT_REACH:    proto = "REACH";    break;
        case P2P_PKT_CONN:     proto = "CONN";     break;
        case P2P_PKT_CONN_ACK: proto = "CONN_ACK"; break;
        default:
            print("E:", LA_F("%s: unsupported type 0x%02x\n", LA_F271, 271), TASK_RELAY, type);
            return E_INVALID;
    }

    // 构造负载: [session_id(4)][P2P hdr(4)][payload]
    uint8_t relay_payload[P2P_MAX_PAYLOAD];
    int total_len = P2P_SESS_ID_SZ + P2P_HDR_SIZE + payload_len;
    if (total_len > P2P_MAX_PAYLOAD) {
        print("E:", LA_F("%s: pkt payload exceeds limit (%d > %d)\n", LA_F191, 191), TASK_RELAY, proto, total_len, P2P_MAX_PAYLOAD);
        return E_OUT_OF_CAPACITY;
    }

    nwrite_l(relay_payload, s->id);
    p2p_pkt_hdr_encode(relay_payload + P2P_SESS_ID_SZ, type, flags, seq);
    if (payload_len > 0 && payload)
        memcpy(relay_payload + P2P_SESS_ID_SZ + P2P_HDR_SIZE, payload, payload_len);

    ret_t ret = tcp_send(sig_ctx, proto, P2P_RLY_PKT, relay_payload, total_len, P_tick_ms());
    if (ret != E_NONE) return ret;

    sess_ctx->awaiting_relay_ready = true;

    print("V:", LA_F("%s %s sent (ses_id=%u), seq=%u flags=0x%02x len=%u\n", LA_F33, 33),
          TASK_RELAY, proto, s->id, seq, flags, payload_len);

    return E_NONE;
}

/*
 * 通过 RELAY 服务器向对端发起 RPC 请求
 * 负载: [session_id(4)][sid(2)][msg(1)][data(N)]
 */
ret_t p2p_signal_relay_request(struct p2p_session *s,
                               uint8_t msg, const void *data, int len) {

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    P_check(len == 0 || data, return E_INVALID;)
    P_check(len >= 0 && len <= P2P_MSG_DATA_MAX, return E_INVALID;)
    P_check(sess_ctx->state >= SIG_RELAY_SESS_WAIT_SYN0_ACK, return E_NONE_CONTEXT;)
    p2p_relay_ctx_t *sig_ctx = &s->inst->sig_ctx.relay;
    if (!sig_ctx->feature_msg) {
        print("E:", LA_F("%s: not supported by server\n", LA_F174, 174), TASK_RPC);
        return E_NO_SUPPORT;
    }

    if (sess_ctx->req_state != 0) return E_BUSY;

    // 生成非零循环序列号
    static uint16_t _sid_base = 0;
    uint16_t sid = ++_sid_base;
    if (sid == 0) sid = ++_sid_base;

    sess_ctx->req_state = 1/* waiting REQ_ACK */;
    sess_ctx->req_sid   = sid;
    sess_ctx->req_msg   = msg;

    uint8_t payload[P2P_MAX_PAYLOAD]; int n = 0;
    nwrite_l(payload + n, s->id); n += P2P_SESS_ID_SZ;
    nwrite_s(payload + n, sid); n += 2;
    payload[n++] = msg;
    if (len > 0 && data) {
        memcpy(payload + n, data, (size_t)len);
        n += len;
    }

    ret_t ret = tcp_send(sig_ctx, "REQ", P2P_RLY_REQ, payload, n, P_tick_ms());
    if (ret != E_NONE) {
        sess_ctx->req_state = 0;
        sess_ctx->req_sid   = 0;
        return ret;
    }

    print("I:", LA_F("%s req (ses_id=%u), sid=%u msg=%u len=%d\n", LA_F44, 44), TASK_RPC,
          s->id, sid, msg, len);
    return E_NONE;
}

/*
 * 通过 RELAY 服务器向请求方回复 RPC 响应
 * 负载: [session_id(4)][sid(2)][code(1)][data(N)]
 */
ret_t p2p_signal_relay_response(struct p2p_session *s,
                                uint8_t code, const void *data, int len) {


    P_check(len >= 0 && len <= P2P_MSG_DATA_MAX, return E_INVALID;)
    P_check(len == 0 || data, return E_INVALID;)
    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;
    if (!sess_ctx->resp_sid) {
        print("E:", LA_F("%s: no pending request\n", LA_F166, 166), TASK_RPC);
        return E_INVALID;
    }

    // 构造
    uint8_t payload[P2P_MAX_PAYLOAD]; int n = 0;
    nwrite_l(payload + n, s->id); n += P2P_SESS_ID_SZ;
    nwrite_s(payload + n, sess_ctx->resp_sid); n += 2;
    payload[n++] = code;
    if (len > 0 && data) {
        memcpy(payload + n, data, (size_t)len);
        n += len;
    }

    p2p_relay_ctx_t *sig_ctx = &s->inst->sig_ctx.relay;
    ret_t ret = tcp_send(sig_ctx, "RSP", P2P_RLY_RSP, payload, n, P_tick_ms());
    if (ret != E_NONE) return ret;

    print("I:", LA_F("%s resp (ses_id=%u), sid=%u code=%u len=%d\n", LA_F47, 47), TASK_RPC,
          s->id, sess_ctx->resp_sid, code, len);

    // 标记请求已处理
    sess_ctx->rpc_last_sid = sess_ctx->resp_sid;
    sess_ctx->resp_sid = 0;
    return E_NONE;
}

///////////////////////////////////////////////////////////////////////////////


void p2p_signal_relay_tick_recv(struct p2p_instance *inst, uint64_t now) {

    p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;
    if (sig_ctx->state < SIG_RELAY_CONNECTING ||
        sig_ctx->sockfd == P_INVALID_SOCKET) {
        return;
    }

    // REG_ING 状态：检查连接是否完成
    if (sig_ctx->state == SIG_RELAY_CONNECTING) {

        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sig_ctx->sockfd, &wfds);

        struct timeval tv = {0, 0};
        int ret = select((int)sig_ctx->sockfd + 1, NULL, &wfds, NULL, &tv);

        // 连接成功，发送 REG
        if (ret > 0 && FD_ISSET(sig_ctx->sockfd, &wfds)) {
            print("I:", LA_F("[R] TCP connected, sending REG\n", LA_F477, 477));
            sig_ctx->state = SIG_RELAY_WAIT_REG_ACK;
            send_online(inst, now);
        } else if (ret < 0) {
            print("E:", LA_F("[R] TCP connect select failed(%d)\n", LA_F475, 475), P_sock_errno());
            P_sock_close(sig_ctx->sockfd);
            sig_ctx->sockfd = P_INVALID_SOCKET;
            sig_ctx->state = SIG_RELAY_ERROR;
        }
        return;
    }

    // 接收数据（状态机）
    for(;;) {
        ssize_t n;

        // 如果当前处于读取包头阶段
        // + 开始读取一个新包，或之前只读取了部分包头，继续读取包头
        if (sig_ctx->recv_state == RECV_STATE_HEADER) {

            n = recv(sig_ctx->sockfd,
                     (char *)sig_ctx->hdr_buf + sig_ctx->offset,
                     (int)sizeof(p2p_relay_hdr_t) - sig_ctx->offset,
                     0);
            if (n > 0) { sig_ctx->offset += n;
                
                // 如果包头已经完整，解析
                if (sig_ctx->offset == (int)sizeof(p2p_relay_hdr_t)) {
                    memcpy(&sig_ctx->hdr, sig_ctx->hdr_buf, sizeof(p2p_relay_hdr_t));
                    sig_ctx->hdr.size = ntohs(sig_ctx->hdr.size);

                    // 验证 payload 大小
                    if (sig_ctx->hdr.size > P2P_MAX_PAYLOAD) {
                        print("E:", LA_F("[R] payload size %u exceeds limit %u\n", LA_F483, 483),
                              sig_ctx->hdr.size, P2P_MAX_PAYLOAD);
                        P_sock_close(sig_ctx->sockfd);
                        sig_ctx->sockfd = P_INVALID_SOCKET;
                        sig_ctx->state = SIG_RELAY_ERROR;
                        return;
                    }

                    // 切换到读取 payload
                    if (sig_ctx->hdr.size > 0) {
                        sig_ctx->recv_state = RECV_STATE_PAYLOAD;
                    } else {
                        dispatch_proto(inst, now);                  // 无 payload，直接派发
                        sig_ctx->recv_state = RECV_STATE_HEADER;    // 准备读取下一个包
                    }
                    sig_ctx->offset = 0;
                }

                continue;
            }
        }
        else {

            // 读取 payload
            n = recv(sig_ctx->sockfd, (char *)sig_ctx->payload + sig_ctx->offset, sig_ctx->hdr.size - sig_ctx->offset, 0);
            if (n > 0) { sig_ctx->offset += n;
                
                if (sig_ctx->offset == sig_ctx->hdr.size) {
                    dispatch_proto(inst, now);                      // payload 完整，分发
                    sig_ctx->recv_state = RECV_STATE_HEADER;        // 准备读取下一个包
                    sig_ctx->offset = 0;
                }

                continue;
            } 
        }

        if (n == 0) { // 连接关闭            
            print("I:", LA_F("[R] TCP connection closed by peer\n", LA_F478, 478));
            P_sock_close(sig_ctx->sockfd);
            sig_ctx->sockfd = P_INVALID_SOCKET;
            sig_ctx->state = SIG_RELAY_ERROR;
            return;
        }
        else if (!P_sock_is_wouldblock()) {   // 出现错误
            print("E:", LA_F("[R] TCP recv error(%d)\n", LA_F480, 480), P_sock_errno());
            P_sock_close(sig_ctx->sockfd);
            sig_ctx->sockfd = P_INVALID_SOCKET;
            sig_ctx->state = SIG_RELAY_ERROR;
            return;
        }
        break; // WOULDBLOCK: 退出 recv 循环，继续执行下方协议状态维护
    }

    // ====================================================================
    // 协议状态维护
    // ====================================================================

    // 服务器应答超时检查
    if (sig_ctx->state == SIG_RELAY_WAIT_REG_ACK) {
        if (tick_diff(now, sig_ctx->last_send_time) > P2P_RELAY_ACK_TIMEOUT_MS) {
            print("E:", LA_F("[R] %s timeout\n", LA_F467, 467), "REG_ACK");
            P_sock_close(sig_ctx->sockfd);
            sig_ctx->sockfd = P_INVALID_SOCKET;
            sig_ctx->state = SIG_RELAY_ERROR;
        }
        return;
    }

    if (!sig_ctx->trickle_sessions) return;

    for (struct p2p_session *s = inst->sessions_head; s; s = s->next) {
        p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

        // SYNCING: 攒批等待模式（上一批已确认 && 有新候选）
        if (sess_ctx->state == SIG_RELAY_SESS_SYNCING && sess_ctx->trickle_last_time &&
            sess_ctx->candidate_synced_count == sess_ctx->candidate_syncing_base &&
            sess_ctx->candidate_syncing_base < (uint16_t) s->local_cand_cnt) {
            send_sync(s, now);
            sess_ctx->trickle_last_time = now;
        }
    }
}

void p2p_signal_relay_tick_send(struct p2p_instance *inst, uint64_t now) {

    p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;
    if (sig_ctx->state <= SIG_RELAY_CONNECTING ||
        sig_ctx->sockfd == P_INVALID_SOCKET) {
        return;
    }

    // ====================================================================
    // 协议状态维护
    // ====================================================================

    // 心跳保活
    if (sig_ctx->state >= SIG_RELAY_REG) {
        if (tick_diff(now, sig_ctx->last_send_time) > P2P_RELAY_HEARTBEAT_INTERVAL_MS) {
            send_alive(inst, now);
        }
    }

    // ====================================================================
    // 推进发送队列（循环发送直到队列为空或 socket WOULDBLOCK）
    // ====================================================================
    
    while (sig_ctx->send_queue_head) {

        p2p_send_chunk_t *chunk = sig_ctx->send_queue_head;
        ssize_t n = send(sig_ctx->sockfd, (const char *)(chunk->data + sig_ctx->send_offset),
                         chunk->len - sig_ctx->send_offset, 0);

        if (n > 0) { sig_ctx->send_offset += (int)n;
            
            // chunk 发送完成
            if (sig_ctx->send_offset == chunk->len) {
                if (!((sig_ctx->send_queue_head = chunk->next)))
                    sig_ctx->send_queue_rear = NULL;
                sig_ctx->send_offset = 0;  // 重置偏移，准备发送下一个 chunk
                --sig_ctx->send_queue_len;
                
                // 回收到池
                chunk->next = sig_ctx->chunk_recycled;
                sig_ctx->chunk_recycled = chunk;

                // 继续发送下一个 chunk
                continue;
            }
            
            // chunk 部分发送，继续尝试
            continue;
            
        } 
        else if (!n) {  // 连接关闭

            print("E:", LA_F("[R] TCP connection closed during send\n", LA_F479, 479));
            P_sock_close(sig_ctx->sockfd);
            sig_ctx->sockfd = P_INVALID_SOCKET;
            sig_ctx->state = SIG_RELAY_ERROR;
            return;
        }
        else if (!P_sock_is_wouldblock()) { // 出现错误

            print("E:", LA_F("[R] TCP send error(%d)\n", LA_F481, 481), P_sock_errno());
            P_sock_close(sig_ctx->sockfd);
            sig_ctx->sockfd = P_INVALID_SOCKET;
            sig_ctx->state = SIG_RELAY_ERROR;
            
            // 错误时出队并回收当前 chunk
            // fixme: 是全部回收，还是只回收当前 chunk？
            sig_ctx->send_queue_head = chunk->next;
            if (!sig_ctx->send_queue_head) {
                sig_ctx->send_queue_rear = NULL;
            }
            sig_ctx->send_offset = 0;  // 重置偏移
            --sig_ctx->send_queue_len;
            
            chunk->next = sig_ctx->chunk_recycled;
            sig_ctx->chunk_recycled = chunk;

            return;            
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
#pragma clang diagnostic pop
