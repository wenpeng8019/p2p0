/*
 * RELAY 模式信令实现（TCP 长连接）
 *
 * ============================================================================
 * 设计理念：两阶段分离
 * ============================================================================
 *
 * 与 COMPACT 的核心区别：
 *
 *   COMPACT: REGISTER(local_id, remote_id) → 一次建立三方关系
 *   RELAY:   ONLINE(my_name) + SYNC0(target_name) → 两步分离
 *
 * 两阶段的意义：
 *   阶段1 (ONLINE):  建立"客户端-服务器"的基础连接
 *                   完成认证、能力协商、保活机制
 *   阶段2 (SYNC0):   建立"我-对方"的会话
 *                   支持一个客户端并发多个会话（不同 session_id）
 *
 * 这种设计特别适合 TCP 长连接场景：
 *   - 一次 ONLINE，持续保活
 *   - 多次 SYNC0，复用连接
 *   - 每个会话独立 session_id，互不干扰
 */
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#define MOD_TAG "RELAY"

#include "p2p_internal.h"

#define TASK_ONLINE                     "ONLINE"
#define TASK_TOUCH                      "TOUCH"
#define TASK_SYNC                       "SYNC"
#define TASK_SYNC_REMOTE                "SYNC REMOTE"
#define TASK_RELAY                      "RELAY"
#define TASK_RPC                        "RPC"

/* 一个 SYNC 包所承载的候选数量（单位）*/
#define SYNC_CAND_UNIT \
    (((P2P_MAX_PAYLOAD - P2P_SESS_ID_PSZ - 1) / (int)sizeof(p2p_candidate_t)) < P2P_RELAY_MAX_CANDS_PER_PACKET \
     ? ((P2P_MAX_PAYLOAD - P2P_SESS_ID_PSZ - 1) / (int)sizeof(p2p_candidate_t)) \
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
            print("E:", LA_F("[R] %s%s qsend failed(OOM)\n", 0, 0), type == P2P_RLY_PACKET ? "PKT-" : "" , PROTO);
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

    printf(LA_F("[R] %s%s qsend(%d), len=%u\n", 0, 0), type == P2P_RLY_PACKET ? "PKT-" : "" , PROTO,
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
    if (len < 1) {
        print("E:", LA_F("%s: bad payload len=%d\n", LA_F498, 498), TASK_SYNC_REMOTE, len);
        return;
    }

    int cand_cnt = payload[0];

    if (len < 1 + (int)sizeof(p2p_candidate_t) * cand_cnt) {
        print("E:", LA_F("%s: bad payload(len=%d cand_cnt=%d)\n", LA_F561, 561), 
              TASK_SYNC_REMOTE, len, cand_cnt);
        return;
    }

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    // 解析候选列表
    p2p_remote_candidate_entry_t *c; int offset = 1;
    for (int i = 0; i < cand_cnt; i++, offset += (int)sizeof(p2p_candidate_t)) {
        if (s->remote_cand_cnt >= s->remote_cand_cap) {
            print("W:", LA_F("%s: remote_cands[] full, skipped %d candidates\n", LA_F505, 505),
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
                print("I:", LA_F("%s: promoted prflx cand[%d]<%s:%d> → %s\n", LA_F601, 601),
                      TASK_SYNC_REMOTE, dup_idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port),
                      p2p_candidate_type_str(c->type));
            } else {
                print("V:", LA_F("%s: duplicate remote cand<%s:%d>, skipped\n", LA_F500, 500),
                      TASK_SYNC_REMOTE, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
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
            print("E:", LA_F("%s: unexpected remote cand type %d, skipped\n", LA_F193, 193),
                  TASK_SYNC_REMOTE, c->type);
            continue;
        }

        if (opt_off) {
            print("I:", LA_F("%s: remote %s cand[%d]<%s:%d> (disabled)\n", LA_F423, 423),
                  TASK_SYNC_REMOTE, type_str, idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
            continue;
        }

        ++s->remote_cand_cnt;
        ++*cand_cnt_ptr;

        print("I:", LA_F("%s: remote %s cand[%d]<%s:%d> accepted\n", LA_F153, 153),
              TASK_SYNC_REMOTE, type_str, idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));

        // 启动打洞
        if (sess_ctx->state >= SIG_RELAY_SESS_SYNCING && nat_punch(s, idx) != E_NONE) {
            print("E:", LA_F("%s: punch remote cand[%d]<%s:%d> failed\n", LA_F137, 137),
                  TASK_SYNC_REMOTE, idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
        }
    }
}

/*
 * 消息发送函数
 */

/*
 * 发送 ONLINE 消息
 *
 * 包头: [type(P2P_RLY_ONLINE) | size(2)]
 * 负载: [name(32)][instance_id(4)]
 */
static void send_online(struct p2p_instance *inst, uint64_t now) {
    const char *PROTO = "ONLINE";

    p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;

    uint8_t payload[P2P_RLY_ONLINE_PSZ];
    memset(payload, 0, sizeof(payload));
    strncpy((char*)payload, sig_ctx->local_peer_id, P2P_PEER_ID_MAX - 1);
    nwrite_l(payload + P2P_PEER_ID_MAX, sig_ctx->instance_id);

    if (tcp_send(sig_ctx, PROTO, P2P_RLY_ONLINE, payload, sizeof(payload), now) != E_NONE) {
        return;
    }

    print("V:", LA_F("%s sent, name='%s' rid=%u\n", LA_F491, 491),
          PROTO, sig_ctx->local_peer_id, sig_ctx->instance_id);
}

/*
 * 发送 ALIVE 心跳
 *
 * 包头: [type(P2P_RLY_ALIVE) | size(2)]
 * 负载: 无
 */
static void send_alive(struct p2p_instance *inst, uint64_t now) {
    const char *PROTO = "ALIVE";

    p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;

    if (tcp_send(sig_ctx, PROTO, P2P_RLY_ALIVE, NULL, 0, now) != E_NONE) {
        return;
    }

    print("V:", LA_F("%s sent\n", LA_F493, 493), PROTO);
}

/*
 * 发送 SYNC0 请求建立会话
 *
 * 包头: [type(P2P_RLY_SYNC0) | size(2)]
 * 负载: [target_name(32)][candidate_count(1)][candidates(N*23)]
 */
static void send_sync0(struct p2p_instance *inst, struct p2p_session *s, uint64_t now) {
    const char *PROTO = "SYNC0";

    p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;
    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    uint8_t payload[P2P_PEER_ID_MAX + 1];
    memset(payload, 0, sizeof(payload));
    strncpy((char*)payload, sess_ctx->remote_peer_id, P2P_PEER_ID_MAX - 1);
    payload[P2P_PEER_ID_MAX] = 0; // 目前 SYNC0 不携带候选，候选通过后续 SYNC 上送

    if (tcp_send(sig_ctx, PROTO, P2P_RLY_SYNC0, payload, sizeof(payload), now) != E_NONE) {
        return;
    }

    print("V:", LA_F("%s sent, target='%s' cand=%u\n", LA_F492, 492),
          PROTO, sess_ctx->remote_peer_id, 0);
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

    // 如果候选还未发送完成
    if (sess_ctx->candidate_syncing_base < (uint16_t)s->local_cand_cnt) {

        assert(sess_ctx->trickle_last_time == 0); // 还未进入攒批阶段

        int start_idx = sess_ctx->candidate_syncing_base;
        remaining = s->local_cand_cnt - start_idx;
        if (remaining > sig_ctx->candidate_sync_max) cand_cnt = sig_ctx->candidate_sync_max;
        else { cand_cnt = remaining;

            // 如果已经没有待定的候选地址，追加 fin_marker = 0xFF
            if (!s->inst->stun_pending && !s->inst->turn_pending) remaining = 0;
            // 否则启动攒批机制
            else { sess_ctx->trickle_last_time = now; sig_ctx->trickle_sessions++; }
        }

        payload[P2P_SESS_ID_PSZ] = (uint8_t)cand_cnt;

        payload_len = (int)P2P_RLY_SYNC_PSZ(0, false);
        for (int i = 0; i < cand_cnt; i++) { int idx = start_idx + i;
            pack_candidate(&s->local_cands[idx], payload + payload_len);
            payload_len += (int)sizeof(p2p_candidate_t);
        }
        if (!remaining) payload[payload_len++] = P2P_RLY_SYNC_FIN_MARKER;
    }
    // 此时调用者要确保已经没有待定的候选地址
    else { assert(!s->inst->stun_pending && !s->inst->turn_pending);

        remaining = cand_cnt = 0;

        // 发送 FIN（追加 fin_marker = 0xFF）
        payload[P2P_SESS_ID_PSZ] = 0;
        payload[P2P_SESS_ID_PSZ + 1] = P2P_RLY_SYNC_FIN_MARKER;
        payload_len = (int)P2P_RLY_SYNC_PSZ(0, true);
    }

    if (tcp_send(sig_ctx, PROTO, P2P_RLY_SYNC, payload, payload_len, now) != E_NONE) {
        return;
    }

    print("V:", LA_F("%s sent, ses_id=%u cand_base=%d, cand_cnt=%d fin=%d\n", LA_F494, 494),
          PROTO, s->id, sess_ctx->candidate_syncing_base, cand_cnt, remaining ? 0 : 1);

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

    print("V:", LA_F("%s sent, ses_id=%u\n", LA_F490, 490),
          PROTO, s->id);
}

///////////////////////////////////////////////////////////////////////////////

void p2p_signal_relay_init(p2p_relay_ctx_t *ctx) {

    memset(ctx, 0, sizeof(*ctx));
    ctx->state = SIG_RELAY_INIT;
    ctx->sockfd = P_INVALID_SOCKET;
}

static void reset_peer(p2p_relay_session_t *sess_ctx, p2p_relay_ctx_t *sig_ctx) {

    if (sess_ctx->trickle_last_time) sig_ctx->trickle_sessions--;
    sess_ctx->candidate_syncing_base = 0;
    sess_ctx->candidate_synced_count = 0;
    sess_ctx->trickle_last_time = 0;
}

void p2p_signal_relay_syncable(struct p2p_instance *inst, p2p_relay_ctx_t *ctx) {

    assert(ctx->state == SIG_RELAY_ONLINE);
    assert(inst->cfg.skip_stun_pending || inst->stun_pending == 0);

    for (struct p2p_session *s = inst->sessions_head; s; s = s->next) {
        p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

        // 首次变为 SYNCABLE 之前，session 肯定都处于 WAIT SYNCABLE 阶段
        assert(sess_ctx->remote_peer_id[0] && sess_ctx->state == SIG_RELAY_SESS_WAIT_SYNCABLE);

        sess_ctx->state = SIG_RELAY_SESS_WAIT_SYNC0_ACK;
        send_sync0(inst, s, P_tick_ms());
        print("I:", LA_F("%s: syncable ready, auto SYNC0 sent\n", "ONLINE", LA_F475, 475), TASK_TOUCH);
    }
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 处理 STATUS（实例级，非会话相关的状态）
 *
 * 由 dispatch_proto 解析后调用，仅处理 req_type < P2P_RLY_SYNC0 的情况
 */
static void handle_status(struct p2p_instance *inst, uint8_t type, uint8_t code, const char *msg) {
    const char *PROTO = "STATUS";

    if (msg)
        print("E:", LA_F("%s: req_type=%u code=%u msg=%s\n", LA_F530, 530),
              PROTO, (unsigned)type, (unsigned)code, msg);
    else
        print("E:", LA_F("%s: req_type=%u code=%u\n", LA_F565, 565),
              PROTO, (unsigned)type, (unsigned)code);

    P_sock_close(inst->sig_ctx.relay.sockfd);
    inst->sig_ctx.relay.sockfd = P_INVALID_SOCKET;
    inst->sig_ctx.relay.state = SIG_RELAY_ERROR;
}

/*
 * 处理 ONLINE_ACK
 *
 * 包头: [type(P2P_RLY_ONLINE_ACK) | size(2)]
 * 负载: [features(1)][candidate_sync_max(1)]
 */
static void handle_online_ack(struct p2p_instance *inst, const uint8_t *payload, int len, uint64_t now) {
    const char *PROTO = "ONLINE_ACK";

    if (len < (int)P2P_RLY_ONLINE_ACK_PSZ) {
        print("E:", LA_F("%s: bad payload(%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;
    if (sig_ctx->state != SIG_RELAY_WAIT_ONLINE_ACK) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)sig_ctx->state);
        return;
    }

    uint8_t features = payload[0];
    sig_ctx->feature_relay = (features & P2P_RLY_FEATURE_RELAY) != 0;
    sig_ctx->feature_msg = (features & P2P_RLY_FEATURE_MSG) != 0;
    sig_ctx->candidate_sync_max = (len >= (int)P2P_RLY_ONLINE_ACK_PSZ) ? payload[1] : 0;

    const char* def = "";
    if (!sig_ctx->candidate_sync_max) { def = "(default)";
        sig_ctx->candidate_sync_max = SYNC_CAND_UNIT;
    }
    print("V:", LA_F("%s: accepted, cand_max=%d%s relay=%s msg=%s\n", LA_F496, 496),
          TASK_ONLINE, sig_ctx->candidate_sync_max, def, sig_ctx->feature_relay ? "yes" : "no", sig_ctx->feature_msg ? "yes" : "no");

    // 切换到 ONLINE 状态
    sig_ctx->state = SIG_RELAY_ONLINE;
    print("I:", LA_F("%s: ready to start session\n", "ONLINE" LA_F518, 518), TASK_ONLINE);

    // 如果服务器支持数据中继
    if (sig_ctx->feature_relay) {

        // 启动数据中继功能
        assert(!inst->signaling_relay_fn);
        inst->signaling_relay_fn = p2p_signal_relay_packet;

        path_manager_enable_signaling(inst, &sig_ctx->server_addr);
        print("I:", LA_F("%s: SIGNALING path enabled (server supports relay)\n", LA_F320, 320), TASK_ONLINE);
    }

    bool stun_pending = inst->stun_pending && !inst->cfg.skip_stun_pending;
    for (struct p2p_session *s = inst->sessions_head; s; s = s->next) {
        p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

        // 上线完成之前，session 肯定处于 WAIT SYNCABLE 阶段
        assert(sess_ctx->remote_peer_id[0] && sess_ctx->state == SIG_RELAY_SESS_WAIT_SYNCABLE);

        // 如果无需等待 stun 收集的异步候选完成
        if (!stun_pending) {

            sess_ctx->state = SIG_RELAY_SESS_WAIT_SYNC0_ACK;
            send_sync0(inst, s, now);
            print("I:", LA_F("%s: auth_key acquired, auto SYNC0 sent\n", "ONLINE", LA_F475, 475), TASK_TOUCH);
        }
        else {
            print("I:", LA_F("%s: auth_key acquired, waiting stun pending\n", "ONLINE", LA_F476, 476), TASK_TOUCH);
        }

        if (s->state == P2P_STATE_REGISTERING) s->state = P2P_STATE_ONLINE;

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
    if (sig_ctx->state < SIG_RELAY_ONLINE) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)sig_ctx->state);
        return;
    }

    print("V:", LA_F("%s: accepted\n", LA_F597, 597), PROTO);

    // 通知路径管理器：ALIVE_ACK 确认（seq=0），完成 RoundTrip 测量
    // 仅当 SIGNALING 作为 relay 路径被启用时才统计 RTT
    if (inst->signaling.active) {
        path_manager_on_sig_alive_recv(inst, now);
    }
}

/*
 * 处理 STATUS（会话级，SYNC0/SYNC/DATA/REQ/RESP 等会话相关的状态）
 *
 * 由 dispatch_proto 解析后调用
 */
static void handle_session_status(struct p2p_session *s, uint8_t type, uint8_t code, const char *msg) {
    const char *PROTO = "STATUS";

    struct p2p_instance *inst = s->inst;
    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    const char *lvl = (code == P2P_RLY_CODE_READY) ? "V:" : "W:";
    if (msg)
        print(lvl, LA_F("%s: sess_id=%u req_type=%u code=%u msg=%s\n", LA_F530, 530),
              PROTO, s->id, (unsigned)type, (unsigned)code, msg);
    else
        print(lvl, LA_F("%s: sess_id=%u req_type=%u code=%u\n", LA_F565, 565),
              PROTO, s->id, (unsigned)type, (unsigned)code);

    // 会话忙：服务器转发缓冲区满，稍后重试
    if (code == P2P_RLY_ERR_BUSY) {

        if (type == P2P_RLY_SYNC || type == P2P_RLY_SYNC0) {
            if (sess_ctx->trickle_last_time) sess_ctx->trickle_last_time = P_tick_ms();
            print("V:", LA_F("%s: sync busy, will retry\n", LA_F566, 566), PROTO);
        }
        else if (type == P2P_RLY_PACKET) {
            // relay 流控：保持等待，延迟重试
            sess_ctx->awaiting_relay_ready = true;
            print("V:", LA_F("%s: relay busy, will retry\n", LA_F567, 567), PROTO);
        }
    }
    // 服务就绪：解除对应流控
    else if (code == P2P_RLY_CODE_READY) {

        if (type == P2P_RLY_PACKET) {
            sess_ctx->awaiting_relay_ready = false;
            print("V:", LA_F("%s: relay ready, flow control released\n", LA_F582, 582), PROTO);
        }
    }
    // todo 执行 fin by peer
    else if (code == P2P_RLY_ERR_PEER_OFFLINE) {

        print("W:", LA_F("%s: peer offline\n", LA_F594, 594), PROTO);

        p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;
        if (sig_ctx->state >= SIG_RELAY_ONLINE) {
            sess_ctx->state = SIG_RELAY_SESS_WAIT_PEER;
            print("I:", LA_F("[ST:%s] peer went offline, waiting for reconnect\n", "WAIT_PEER", LA_F575, 575));
        }
    }
    // NOT_ONLINE / PROTOCOL / INTERNAL / UNKNOWN → 致命错误
    else {
        print("E:", LA_F("%s: fatal error code=%u, entering ERROR state\n", LA_F531, 531),
                PROTO, (unsigned)code);

        P_sock_close(inst->sig_ctx.relay.sockfd);
        inst->sig_ctx.relay.sockfd = P_INVALID_SOCKET;
        inst->sig_ctx.relay.state = SIG_RELAY_ERROR;
    }
}

/*
 * 处理 SYNC0_ACK
 *
 * 包头: [type(P2P_RLY_SYNC0_ACK) | size(2)]
 * 负载: [target_name(32)][session_id(4)][online(1)]
 * 注: [target_name(32)][session_id(4)] 已剥离
 */
static void handle_sync0_ack(struct p2p_session *s, const uint8_t *payload, uint64_t now) {

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;

    print("V:", LA_F("%s: accepted (ses_id=%u), peer=%s\n", LA_F497, 497),
          TASK_TOUCH, s->id, payload[0] ? "online" : "offline");

    // SYNC0_ACK 只代表会话建立；候选交换需等待对端在线
    if (!payload[0]) {
        sess_ctx->state = SIG_RELAY_SESS_WAIT_PEER;
        print("I:", LA_F("%s: session offer(st=%s peer=%s), waiting for peer\n", "WAIT_PEER", LA_F576, 576),
                TASK_TOUCH, "WAIT_PEER", "online", LA_S("waiting for peer", 0, 0));
        return;
    }

    // 切换到 SYNCING 状态，开始上传候选
    sess_ctx->state = SIG_RELAY_SESS_SYNCING;
    print("I:", LA_F("%s: session established(st=%s peer=%s), %s\n", "SYNCING" 0, 0),
          TASK_TOUCH, "SYNCING", "offline", LA_S("sync candidates", 0, 0));

    // 同步发送首批候选（如果有）
    assert(sess_ctx->candidate_syncing_base == 0);
    if (s->local_cand_cnt || (!s->inst->stun_pending && !s->inst->turn_pending))
        send_sync(s, now);
    else { sess_ctx->trickle_last_time = now; s->inst->sig_ctx.relay.trickle_sessions++; }

    // 启动 NAT 打洞（即使当前没有候选也要启动，以便打洞超时后 fallback 到信令中转）
    nat_punch(s, -1/* all candidates */);
}

/*
 * 处理 SYNC_ACK（服务器流控确认，包含本批次实际转发数量）
 *
 * 包头: [type(P2P_RLY_SYNC_ACK) | size(2)]
 * 负载: [session_id(4)][confirmed_count(1)]
 * 注: [session_id(4)] 已剥离
 *
 * 流程：
 *   - confirmed_count > 0: 服务器接受了 N 个候选（可能少于上传数）；
 *     回滚 candidate_syncing_base 到实际接受点，继续发送剩余批次。
 *   - confirmed_count == 0 且 FIN 标记包已发: 所有候选已转发到对端，标记完成。
 *
 * 服务器仅在中转缓冲区有空间时才发送 ACK（流量控制）
 * 客户端在收到 ACK 前不得发送下一批候选。
 */
static void handle_sync_ack(struct p2p_session *s, const uint8_t *payload, int len, uint64_t now) {
    (void)len;

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;
    if (sess_ctx->state != SIG_RELAY_SESS_SYNCING) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), TASK_SYNC, (int)sess_ctx->state);
        return;
    }

    // 根据协议，confirmed_count 为 0 是服务器确认 fin 包已同步发送给对方的通知
    if (!payload[0]) {

        // 对账：如果本地此时还有候选(或fin)未同步
        if (sess_ctx->candidate_synced_count < s->local_cand_cnt || sess_ctx->candidate_syncing_base <= s->local_cand_cnt) {
            int n = s->local_cand_cnt + s->inst->stun_pending + s->inst->turn_pending;
            print("E:", LA_F("%s: sync fin ack, but cand synced cnt not match sent cnt (cand=%d synced=%d)\n", LA_F569, 569),
                  TASK_SYNC, n, sess_ctx->candidate_synced_count);
            return;
        }

        sess_ctx->state = SIG_RELAY_SESS_READY;
        print("I:", LA_F("%s: sync done, st=%s cands=%d\n", 0, 0),
              TASK_SYNC, "READY", sess_ctx->candidate_synced_count);
        return;
    }

    // 对账：服务器确认的数量不能超过本地未确认的数量
    assert(sess_ctx->candidate_synced_count <= sess_ctx->candidate_syncing_base);
    if (payload[0] > sess_ctx->candidate_syncing_base - sess_ctx->candidate_synced_count) {
        print("E:", LA_F("%s: sync ack confirmed cnt=%d exceeds unacked cnt=%d\n", 0, 0),
              TASK_SYNC, payload[0], sess_ctx->candidate_syncing_base - sess_ctx->candidate_synced_count);
        return;
    }

    sess_ctx->candidate_synced_count += payload[0];

    print("V:", LA_F("%s: sync forwarded, confirmed=%d synced=%d\n", LA_F563, 563),
          TASK_SYNC, payload[0], sess_ctx->candidate_synced_count);

    // 目前策略是，只有上一批次全部确认后才发送下一批，且还未进入攒批阶段
    // + 可改进的点：如果服务器只确认了部分候选，可以考虑提前发送下一批（但所有未确认的同步数量不能超过 candidate_sync_max）
    if (sess_ctx->candidate_synced_count == sess_ctx->candidate_syncing_base) {

        // 如果已没有待收集的候选了
        if (!s->inst->stun_pending && !s->inst->turn_pending) {
            if (sess_ctx->trickle_last_time) { sess_ctx->trickle_last_time = 0; s->inst->sig_ctx.relay.trickle_sessions--; }
            send_sync(s, now);
        }
        // 或已经积累了足够的候选；又或者还没进入攒批阶段; 或距离上次发送已经超过攒批时间窗口了
        else if ((s->local_cand_cnt - sess_ctx->candidate_syncing_base >= s->inst->sig_ctx.relay.candidate_sync_max) ||
                 !sess_ctx->trickle_last_time ||
                 (P_tick_ms() - sess_ctx->trickle_last_time) >= P2P_RELAY_TRICKLE_BATCH_MS) {

            send_sync(s, now);
        }
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
    if (sess_ctx->state != SIG_RELAY_SESS_SYNCING) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), TASK_SYNC_REMOTE, (int)sess_ctx->state);
        return;
    }

    uint8_t cand_cnt = payload[0]; uint32_t base_len = P2P_RLY_SYNC_PSZ(cand_cnt, false) - P2P_SESS_ID_PSZ;
    bool has_fin = false;
    if ((uint32_t)len == base_len + 1u) {
        has_fin = true;
        if (payload[base_len] != P2P_RLY_SYNC_FIN_MARKER) {
            print("E:", LA_F("%s: bad FIN marker=0x%02x\n", LA_F99, 99), TASK_SYNC_REMOTE, payload[base_len]);
            return;
        }
    } else if ((uint32_t)len != base_len) {
        print("E:", LA_F("%s: bad payload(len=%d cand_cnt=%d)\n", LA_F561, 561), TASK_SYNC_REMOTE, len, cand_cnt);
        return;
    }

    // 解析候选列表（首部 session_id；尾部可能有 1B FIN 标记）
    unpack_remote_candidates(s, payload, (int)base_len);

    if (has_fin) {
        s->remote_cand_done = true;
        print("I:", LA_F("%s: sync done\n", LA_F564, 564), TASK_SYNC_REMOTE);
    }

    print("V:", LA_F("%s: processed, synced=%d\n", LA_F503, 503), TASK_SYNC_REMOTE, s->remote_cand_cnt);
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
    print("I:", LA_F("%s: session suspend(st=%s)\n", LA_F504, 504),
          TASK_TOUCH, "WAIT_PEER");
    s->id = 0;
    sess_ctx->state = SIG_RELAY_SESS_WAIT_PEER;

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
 * 包头: [type(P2P_RLY_PACKET) | size(2)]
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

    print("V:", LA_F("%s: pkt recv (ses_id=%u), inner type=%u\n", LA_F93, 93), TASK_RELAY, s->id, hdr.type);

    nat_proto(s, hdr.type, hdr.flags, hdr.seq, payload + P2P_HDR_SIZE, len - P2P_HDR_SIZE,
              &s->inst->sig_ctx.relay.server_addr, now);
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
        print("V:", LA_F("%s: duplicate request ignored (sid=%u)\n", LA_F107, 107), TASK_RPC, sid);
        return;
    }

    // 忽略旧请求
    if (sess_ctx->rpc_last_sid != 0 && !uint16_circle_newer(sid, sess_ctx->rpc_last_sid)) {
        print("V:", LA_F("%s: old request ignored (sid=%u <= last_sid=%u)\n", LA_F131, 131),
              TASK_RPC, sid, sess_ctx->rpc_last_sid);
        return;
    }

    sess_ctx->resp_sid = sid;

    // msg=0: 自动 echo 回复
    if (msg == 0) {
        print("V:", LA_F("%s msg=0: echo reply (sid=%u)\n", LA_F595, 595), TASK_RPC, sid);
        p2p_signal_relay_response(s, 0, req_data, req_len);
        return;
    }

    print("V:", LA_F("%s req accepted (ses_id=%u), sid=%u msg=%u\n", LA_F93, 93), TASK_RPC, s->id, sid, msg);

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
        print("E:", LA_F("%s: irrelevant response (sid=%u, current sid=%u, state=%d)\n", LA_F599, 599),
              TASK_RPC, sid, sess_ctx->req_sid, (int)sess_ctx->req_state);
        return;
    }

    // 错误响应
    if (code >= P2P_MSG_ERR_PEER_OFFLINE) {
        if (code == P2P_MSG_ERR_PEER_OFFLINE)
            print("W:", LA_F("%s: peer offline (sid=%u)\n", LA_F600, 600), TASK_RPC, sid);
        else
            print("W:", LA_F("%s: timeout (sid=%u)\n", LA_F604, 604), TASK_RPC, sid);

        sess_ctx->req_state = 0;
        sess_ctx->req_sid   = 0;

        if (s->inst->cfg.on_response)
            s->inst->cfg.on_response((p2p_session_t)s, sid, code, NULL, -1, s->inst->cfg.userdata);
        return;
    }

    print("V:", LA_F("%s: complete (ses_id=%u), sid=%u code=%u\n", LA_F78, 78), TASK_RPC, s->id, sid, code);

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

        if (sig_ctx->hdr.type == P2P_RLY_STATUS) { PROTO = "STATUS";

            printf(LA_F("[R] %s recv, len=%d\n", LA_F533, 533), PROTO, sig_ctx->hdr.size);

            uint8_t req_type = sig_ctx->payload[0], status_code;
            if (req_type < P2P_RLY_SYNC0) {

                if (sig_ctx->hdr.size < P2P_RLY_STATUS_PSZ(0, 0)) {
                    print("E:", LA_F("%s: bad payload(%d)\n", 0, 0), PROTO, sig_ctx->hdr.size);
                    return;
                }

                status_code = sig_ctx->payload[1];
                if (sig_ctx->hdr.size > P2P_RLY_STATUS_PSZ(0, 0)) { sig_ctx->payload[sizeof(sig_ctx->payload)-1] = 0;
                    handle_status(inst, req_type, status_code, (const char*)sig_ctx->payload + P2P_RLY_STATUS_PSZ(0, 0));
                } else handle_status(inst, req_type, status_code, NULL);
                break;
            }

            if (req_type == P2P_RLY_SYNC0) {

                if (sig_ctx->hdr.size < P2P_RLY_STATUS_PSZ(1, 0)) {
                    print("E:", LA_F("%s: bad payload(%d)\n", 0, 0), PROTO, sig_ctx->hdr.size);
                    return;
                }

                struct p2p_session* s = inst->sessions_head;
                for(; s; s = s->next) {
                    if (strncmp(s->sig_sess.relay.remote_peer_id, (char*)sig_ctx->payload + 1, P2P_PEER_ID_MAX-1) == 0) {

                        status_code = sig_ctx->payload[1+P2P_PEER_ID_MAX];
                        if (sig_ctx->hdr.size > P2P_RLY_STATUS_PSZ(1, 0)) { sig_ctx->payload[sizeof(sig_ctx->payload)-1] = 0;
                            handle_session_status(s, req_type, status_code, (const char*)sig_ctx->payload + P2P_RLY_STATUS_PSZ(1, 0));
                        } else handle_session_status(s, req_type, status_code, NULL);
                        break;
                    }
                }
                if (!s) {
                    print("W:", LA_F("%s: no session for peer_id=%.*s (req_type=%u)\n", 0, 0),
                          PROTO, (int)(P2P_PEER_ID_MAX-1), (char*)sig_ctx->payload + 1, (unsigned)req_type);
                }
                break;
            }

            if (sig_ctx->hdr.size < P2P_RLY_STATUS_PSZ(2, 0)) {
                print("E:", LA_F("%s: bad payload(%d)\n", 0, 0), PROTO, sig_ctx->hdr.size);
                return;
            }

            uint32_t session_id = nget_l(sig_ctx->payload + 1);
            struct p2p_session* s = inst->sessions_head;
            for(; s; s = s->next) {
                if (session_id == s->id) {

                    status_code = sig_ctx->payload[1+P2P_SESS_ID_PSZ];
                    if (sig_ctx->hdr.size > P2P_RLY_STATUS_PSZ(2, 0)) { sig_ctx->payload[sizeof(sig_ctx->payload)-1] = 0;
                        handle_session_status(s, req_type, status_code, (const char*)sig_ctx->payload + P2P_RLY_STATUS_PSZ(2, 0));
                    } else handle_session_status(s, req_type, status_code, NULL);
                    break;
                }
            }
            if (!s) {
                print("W:", LA_F("%s: no session for session_id=%u (req_type=%u)\n", 0, 0),
                      PROTO, session_id, (unsigned)req_type);
            }
            break;
        }

        if (sig_ctx->hdr.type == P2P_RLY_ONLINE_ACK) {

            printf(LA_F("[R] %s recv, len=%d\n", LA_F533, 533), "ONLINE_ACK", sig_ctx->hdr.size);
            handle_online_ack(inst, sig_ctx->payload, (int)sig_ctx->hdr.size, now); 
            break;
        }
        if (sig_ctx->hdr.type == P2P_RLY_ALIVE_ACK) {

            printf(LA_F("[R] %s recv, len=%d\n", LA_F533, 533), "ALIVE_ACK", sig_ctx->hdr.size);
            handle_alive_ack(inst, now); 
            break;
        }

        if (sig_ctx->hdr.type == P2P_RLY_SYNC0_ACK) { PROTO = "SYNC0_ACK";

            printf(LA_F("[R] %s recv, len=%d\n", LA_F533, 533), PROTO, sig_ctx->hdr.size);

            if (sig_ctx->hdr.size < (int)P2P_RLY_SYNC0_ACK_PSZ) {
                print("E:", LA_F("%s: bad payload(%d)\n", LA_F562, 562), PROTO, sig_ctx->hdr.size);
                return;
            }

            uint8_t* ptr = sig_ctx->payload + P2P_PEER_ID_MAX;
            uint32_t session_id = nget_l(ptr);
            if (!session_id) {
                print("W:", LA_F("%s: missing session_id in payload\n", 0, 0), PROTO);
                return;
            }

            struct p2p_session* s = inst->sessions_head;
            for(; s; s = s->next) {
                if (strncmp(s->sig_sess.relay.remote_peer_id, (char*)sig_ctx->payload, P2P_PEER_ID_MAX-1) == 0) break;                
            }
            if (!s) {
                print("W:", LA_F("%s: no session for peer_id=%.*s\n", LA_F601, 601),
                      PROTO, (int)(P2P_PEER_ID_MAX-1), (char*)sig_ctx->payload);
                return;
            }

            if (s->sig_sess.relay.state != SIG_RELAY_SESS_WAIT_SYNC0_ACK) {
                print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)s->sig_sess.relay.state);
                return;
            }

            assert(!s->id);
            s->id = session_id;

            handle_sync0_ack(s, ptr + P2P_SESS_ID_PSZ, now);

            break;
        }

        if (sig_ctx->hdr.type == P2P_RLY_SYNC0) { PROTO = "SYNC0";

            printf(LA_F("[R] %s recv, len=%d\n", LA_F533, 533), PROTO, sig_ctx->hdr.size);

            if (sig_ctx->hdr.size < (int)P2P_RLY_SYNC0_S2C_PSZ(0)) {
                print("E:", LA_F("%s: bad payload(%d)\n", LA_F562, 562), PROTO, sig_ctx->hdr.size);
                return;
            }

            uint8_t* ptr = sig_ctx->payload + P2P_PEER_ID_MAX;
            uint32_t session_id = nget_l(ptr);
            if (!session_id) {
                print("W:", LA_F("%s: missing session_id in payload\n", 0, 0), PROTO);
                return;
            }

            struct p2p_session* s = inst->sessions_head; p2p_relay_session_t *sess_ctx = NULL;
            for(; s; s = s->next) { sess_ctx = &s->sig_sess.relay;
                if (strncmp(sess_ctx->remote_peer_id, (char*)sig_ctx->payload, P2P_PEER_ID_MAX-1) == 0) break;
            }
            if (!s) {
                print("W:", LA_F("%s: no session for peer_id=%.*s\n", LA_F601, 601),
                      PROTO, (int)(P2P_PEER_ID_MAX-1), (char*)sig_ctx->payload);
                return;
            }

            // 协议需要确保服务器转发对方的 SYNC0 前，必须先返回 SYNC0_ACK 给本端
            if (sess_ctx->state < SIG_RELAY_SESS_WAIT_PEER) {
                print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)sess_ctx->state);
                return;
            }

            assert(s->id);
            if (s->id != session_id) {

                // 通知业务层连接断开（session 被对方重置）
                if (s->state >= P2P_STATE_LOST) {
                    if (s->inst->cfg.on_state) s->inst->cfg.on_state((p2p_session_t)s, s->state, P2P_STATE_CLOSED, s->inst->cfg.userdata);
                }

                // 重置 p2p 会话
                p2p_session_reset(s, false);

                // 重置信令层会话状态
                reset_peer(sess_ctx, sig_ctx);

                uint32_t old_id = s->id;
                s->id = session_id;

                sess_ctx->state = SIG_RELAY_SESS_SYNCING;
                print("W:", LA_F("%s: session reset by peer(st=%s old=%u new=%u), %s\n", LA_F580, 580),
                        TASK_TOUCH, "SYNCING", old_id, session_id, LA_S("resync candidates", 0, 0));

                session_id = 0;
            }
            // 首次收到 SYNC 视为对端上线，启动候选交换
            else if (sess_ctx->state == SIG_RELAY_SESS_WAIT_PEER) {

                sess_ctx->state = SIG_RELAY_SESS_SYNCING;
                print("I:", LA_F("%s: session established(st=%s peer=%s), %s\n", "SYNCING" 0, 0),
                        TASK_TOUCH, "SYNCING", "offline", LA_S("sync candidates", 0, 0));
                session_id = 0;
            }
            else {
                print("W:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)sess_ctx->state);
                return;
            }

            // 对端发起会话初始化连接
            if (!session_id) {

                // 同步发送首批候选（如果有）
                assert(sess_ctx->candidate_syncing_base == 0);
                if (s->local_cand_cnt || (!s->inst->stun_pending && !s->inst->turn_pending))
                    send_sync(s, now);
                else { sess_ctx->trickle_last_time = now; sig_ctx->trickle_sessions++; }

                // 启动 NAT 打洞（即使当前没有候选也要启动，以便打洞超时后 fallback 到信令中转）
                nat_punch(s, -1/* all candidates */);
            }

            // 如果存在首批同步的数据
            if (ptr[P2P_SESS_ID_PSZ/* candidate_count */] || sig_ctx->hdr.size > P2P_RLY_SYNC0_S2C_PSZ(0))
                handle_peer_sync(s, ptr+P2P_SESS_ID_PSZ, (int)(sig_ctx->hdr.size-P2P_PEER_ID_MAX-P2P_SESS_ID_PSZ), now);

            break;
        }

        uint16_t payload_min; void (*handler)(struct p2p_session*, const uint8_t*, int, uint64_t);
        switch (sig_ctx->hdr.type) {
            case P2P_RLY_SYNC_ACK:
                PROTO = "SYNC_ACK"; payload_min = P2P_RLY_SYNC_ACK_PSZ; handler = handle_sync_ack; break;
            case P2P_RLY_SYNC:
                PROTO = "SYNC"; payload_min = P2P_RLY_SYNC_PSZ(0, false); handler = handle_peer_sync; break;
            case P2P_RLY_FIN:
                PROTO = "FIN"; payload_min = P2P_RLY_FIN_PSZ; handler = handle_relay_fin; break;
            case P2P_RLY_PACKET:
                PROTO = "PACKET"; payload_min = P2P_RLY_PACKET_PSZ(0); handler = handle_relay_packet; break;
            case P2P_RLY_REQ:
                PROTO = "REQ"; payload_min = P2P_RLY_REQ_MIN_PSZ; handler = handle_relay_req; break;
            case P2P_RLY_RESP:
                PROTO = "RESP"; payload_min = P2P_RLY_RESP_MIN_PSZ; handler = handle_relay_resp; break;
            default:
                print("W:", LA_F("[R] Unknown proto type %d\n", LA_F529, 529), sig_ctx->hdr.type);
                return;
        }

        printf(LA_F("[R] %s recv, len=%d\n", LA_F533, 533), PROTO, sig_ctx->hdr.size);

        if (sig_ctx->hdr.size < payload_min) {
            print("E:", LA_F("%s: bad payload(%d)\n", LA_F562, 562), PROTO, sig_ctx->hdr.size);
            return;
        }

        uint32_t session_id = nget_l(sig_ctx->payload);
        if (!session_id) {
            print("W:", LA_F("%s: missing session_id in payload\n", 0, 0), PROTO);
            return;
        }

        struct p2p_session* s = inst->sessions_head;
        for(; s; s = s->next) {
            if (s->id == session_id) {
                handler(s, sig_ctx->payload + P2P_SESS_ID_PSZ, (int)(sig_ctx->hdr.size - P2P_SESS_ID_PSZ), now);
                break;
            }
        }
        if (!s) {
            print("W:", LA_F("%s: no session for session_id=%u\n", 0, 0),
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
        print("E:", LA_F("[R] Failed to create TCP socket\n", LA_F516, 516));
        return E_UNKNOWN;
    }

    // 设置非阻塞
    if (P_sock_nonblock(sig_ctx->sockfd, true) != E_NONE) {
        print("E:", LA_F("[R] Failed to set socket non-blocking\n", LA_F517, 517));
        P_sock_close(sig_ctx->sockfd);
        sig_ctx->sockfd = P_INVALID_SOCKET;
        return E_UNKNOWN;
    }

    // 连接到服务器
    print("I:", LA_F("[R] Connecting to %s:%d\n", LA_F513, 513),
          inet_ntoa(server->sin_addr), ntohs(server->sin_port));

    int ret = connect(sig_ctx->sockfd, (struct sockaddr *)&sig_ctx->server_addr,
                      sizeof(sig_ctx->server_addr));

    // 连接立即成功（少见）
    if (ret == 0) {
        print("I:", LA_F("[R] TCP connected immediately, sending ONLINE\n", LA_F523, 523));
        sig_ctx->state = SIG_RELAY_WAIT_ONLINE_ACK;
        send_online(inst, P_tick_ms());
    }
    // 连接进行中
    else if (P_sock_is_inprogress()) {
        sig_ctx->state = SIG_RELAY_CONNECTING;
        sig_ctx->last_send_time = P_tick_ms();
    }
    else {
        print("E:", LA_F("[R] TCP connect failed(%d)\n", LA_F522, 522), P_sock_errno());
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


    // 已上线：立即发送 SYNC0；否则等待 ONLINE_ACK 后自动触发
    if (sig_ctx->state == SIG_RELAY_ONLINE && (!s->inst->stun_pending || s->inst->cfg.skip_stun_pending)) {
        sess_ctx->state = SIG_RELAY_SESS_WAIT_SYNC0_ACK;
        send_sync0(s->inst, s, P_tick_ms());
    }
    else sess_ctx->state = SIG_RELAY_SESS_WAIT_SYNCABLE;

    return E_NONE;
}

ret_t p2p_signal_relay_disconnect(struct p2p_session *s) {

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;
    if (!sess_ctx->remote_peer_id[0]) return E_NONE;        // 没有建立过配对

    // 如果尚未完成在线：直接取消 connect 连接状态
    if (sess_ctx->state == SIG_RELAY_SESS_WAIT_SYNCABLE)
        sess_ctx->state = SIG_RELAY_SESS_SUSPENDED;
    if (sess_ctx->state == SIG_RELAY_SESS_SUSPENDED) {
        *sess_ctx->remote_peer_id = 0;
        return E_NONE;
    }

    // 如果正在通过信令服务器申请建立连接中
    if (sess_ctx->state < SIG_RELAY_SESS_WAIT_PEER) {
        return E_BUSY;
    }

    print("I:", LA_F("[R] Disconnected, back to ONLINE state\n", LA_F536, 536));

    // 发送 FIN 消息
    compact_send_fin(s);

    // 清理 peer 会话状态
    reset_peer(sess_ctx, &s->inst->sig_ctx.relay);
    memset(sess_ctx->remote_peer_id, 0, sizeof(sess_ctx->remote_peer_id));

    // 清理会话状态
    s->id = 0;
    return E_NONE;
}

void p2p_signal_relay_trickle_candidate(struct p2p_session *s) {

    p2p_relay_session_t *sess_ctx = &s->sig_sess.relay;
    P_check(sess_ctx->state <= SIG_RELAY_SESS_SYNCING, return;)

    // 还没进入 trickle 阶段
    // + 也就是 sync_ack 首次将现有的候选全部同步完成，但又存在待收集的异步候选（如 STUN/TURN）
    if (!sess_ctx->trickle_last_time) return;

    // 检查是否有新候选
    if (sess_ctx->candidate_syncing_base >= s->local_cand_cnt) {
        assert(sess_ctx->candidate_syncing_base == s->local_cand_cnt);
        return;
    }

    // 如果上次发送后还没有收到对端的 SYNC_AC
    if (sess_ctx->candidate_synced_count < sess_ctx->candidate_syncing_base) {
        return;
    }
    assert(sess_ctx->candidate_synced_count == sess_ctx->candidate_syncing_base);

    // 发送控制：如果已没有待收集的候选了
    if (!s->inst->stun_pending && !s->inst->turn_pending) {

        sess_ctx->trickle_last_time = 0; s->inst->sig_ctx.relay.trickle_sessions--;
        send_sync(s, P_tick_ms());
    }
    // 或已经积累了足够的候选；又或者距离上次发送已经超过攒批时间窗口了
    else if ((s->local_cand_cnt - sess_ctx->candidate_syncing_base >= s->inst->sig_ctx.relay.candidate_sync_max) ||
             (P_tick_ms() - sess_ctx->trickle_last_time) >= P2P_RELAY_TRICKLE_BATCH_MS) {

        send_sync(s, P_tick_ms());
    }
}

/*
 * 通过 RELAY 服务器转发数据包（DATA/ACK/CRYPTO/REACH/CONN/CONN_ACK）
 *
 * 包头: [type(P2P_RLY_PACKET) | size(2)]
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
        print("V:", LA_F("%s throttled: awaiting READY\n", LA_F592, 592), TASK_RELAY);
        return E_BUSY;
    }

    // 所有类型统一使用 P2P_RLY_PACKET 隧道，内层 P2P hdr 保留真实类型
    const char *proto;
    switch (type) {
        case P2P_PKT_DATA:     proto = "DATA";     break;
        case P2P_PKT_ACK:      proto = "ACK";      break;
        case P2P_PKT_CRYPTO:   proto = "CRYPTO";   break;
        case P2P_PKT_REACH:    proto = "REACH";    break;
        case P2P_PKT_CONN:     proto = "CONN";     break;
        case P2P_PKT_CONN_ACK: proto = "CONN_ACK"; break;
        default:
            print("E:", LA_F("%s: unsupported type 0x%02x\n", LA_F593, 593), TASK_RELAY, type);
            return E_INVALID;
    }

    // 构造负载: [session_id(4)][P2P hdr(4)][payload]
    uint8_t relay_payload[P2P_MAX_PAYLOAD];
    int total_len = P2P_SESS_ID_PSZ + P2P_HDR_SIZE + payload_len;
    if (total_len > P2P_MAX_PAYLOAD) {
        print("E:", LA_F("%s: pkt payload exceeds limit (%d > %d)\n", LA_F586, 586), TASK_RELAY, proto, total_len, P2P_MAX_PAYLOAD);
        return E_OUT_OF_CAPACITY;
    }

    nwrite_l(relay_payload, s->id);
    p2p_pkt_hdr_encode(relay_payload + P2P_SESS_ID_PSZ, type, flags, seq);
    if (payload_len > 0 && payload)
        memcpy(relay_payload + P2P_SESS_ID_PSZ + P2P_HDR_SIZE, payload, payload_len);

    ret_t ret = tcp_send(sig_ctx, proto, P2P_RLY_PACKET, relay_payload, total_len, P_tick_ms());
    if (ret != E_NONE) return ret;

    sess_ctx->awaiting_relay_ready = true;

    print("V:", LA_F("%s %s sent (ses_id=%u), seq=%u flags=0x%02x len=%u\n", LA_F583, 583),
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
    P_check(sess_ctx->state >= SIG_RELAY_SESS_WAIT_SYNC0_ACK, return E_NONE_CONTEXT;)
    p2p_relay_ctx_t *sig_ctx = &s->inst->sig_ctx.relay;
    if (!sig_ctx->feature_msg) {
        print("E:", LA_F("%s: not supported by server\n", LA_F447, 447), TASK_RPC);
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
    nwrite_l(payload + n, s->id); n += P2P_SESS_ID_PSZ;
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

    print("I:", LA_F("%s req (ses_id=%u), sid=%u msg=%u len=%d\n", LA_F605, 605), TASK_RPC,
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
        print("E:", LA_F("%s: no pending request\n", LA_F607, 607), TASK_RPC);
        return E_INVALID;
    }

    // 构造
    uint8_t payload[P2P_MAX_PAYLOAD]; int n = 0;
    nwrite_l(payload + n, s->id); n += P2P_SESS_ID_PSZ;
    nwrite_s(payload + n, sess_ctx->resp_sid); n += 2;
    payload[n++] = code;
    if (len > 0 && data) {
        memcpy(payload + n, data, (size_t)len);
        n += len;
    }

    p2p_relay_ctx_t *sig_ctx = &s->inst->sig_ctx.relay;
    ret_t ret = tcp_send(sig_ctx, "RESP", P2P_RLY_RESP, payload, n, P_tick_ms());
    if (ret != E_NONE) return ret;

    print("I:", LA_F("%s resp (ses_id=%u), sid=%u code=%u len=%d\n", LA_F606, 606), TASK_RPC,
          s->id, sess_ctx->resp_sid, code, len);

    // 标记请求已处理
    sess_ctx->rpc_last_sid = sess_ctx->resp_sid;
    sess_ctx->resp_sid = 0;
    return E_NONE;
}

///////////////////////////////////////////////////////////////////////////////


void p2p_signal_relay_tick_recv(struct p2p_instance *inst, uint64_t now) {

    p2p_relay_ctx_t *sig_ctx = &inst->sig_ctx.relay;
    if (sig_ctx->state < SIG_RELAY_WAIT_ONLINE_ACK ||
        sig_ctx->sockfd == P_INVALID_SOCKET) {
        return;
    }

    // ONLINE_ING 状态：检查连接是否完成
    if (sig_ctx->state == SIG_RELAY_CONNECTING) {

        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sig_ctx->sockfd, &wfds);

        struct timeval tv = {0, 0};
        int ret = select((int)sig_ctx->sockfd + 1, NULL, &wfds, NULL, &tv);

        // 连接成功，发送 ONLINE
        if (ret > 0 && FD_ISSET(sig_ctx->sockfd, &wfds)) {
            print("I:", LA_F("[R] TCP connected, sending ONLINE\n", LA_F524, 524));
            sig_ctx->state = SIG_RELAY_WAIT_ONLINE_ACK;
            send_online(inst, now);
        } else if (ret < 0) {
            print("E:", LA_F("[R] TCP connect select failed(%d)\n", LA_F521, 521), P_sock_errno());
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
                        print("E:", LA_F("[R] payload size %u exceeds limit %u\n", LA_F538, 538),
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
                        dispatch_proto(inst, now);                // 无 payload，直接派发
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
                    dispatch_proto(inst, now);                    // payload 完整，分发
                    sig_ctx->recv_state = RECV_STATE_HEADER;        // 准备读取下一个包
                    sig_ctx->offset = 0;
                }

                continue;
            } 
        }

        if (n == 0) { // 连接关闭            
            print("I:", LA_F("[R] TCP connection closed by peer\n", LA_F525, 525));
            P_sock_close(sig_ctx->sockfd);
            sig_ctx->sockfd = P_INVALID_SOCKET;
            sig_ctx->state = SIG_RELAY_ERROR;
            return;
        }
        else if (!P_sock_is_wouldblock()) {   // 出现错误
            print("E:", LA_F("[R] TCP recv error(%d)\n", LA_F526, 526), P_sock_errno());
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
    if (sig_ctx->state == SIG_RELAY_WAIT_ONLINE_ACK) {
        if (tick_diff(now, sig_ctx->last_send_time) > P2P_RELAY_ACK_TIMEOUT_MS) {
            print("E:", LA_F("[R] %s timeout\n", LA_F519, 519), "ONLINE_ACK");
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
    if (sig_ctx->state >= SIG_RELAY_ONLINE) {
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

            print("E:", LA_F("[R] TCP connection closed during send\n", LA_F527, 527));
            P_sock_close(sig_ctx->sockfd);
            sig_ctx->sockfd = P_INVALID_SOCKET;
            sig_ctx->state = SIG_RELAY_ERROR;
            return;
        }
        else if (!P_sock_is_wouldblock()) { // 出现错误

            print("E:", LA_F("[R] TCP send error(%d)\n", LA_F534, 534), P_sock_errno());
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
