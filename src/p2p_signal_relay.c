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

#define TASK_REG                        "REGISTER"
#define TASK_ICE                        "ICE"
#define TASK_ICE_REMOTE                 "ICE REMOTE"

/* 一个 SYNC 包所承载的候选数量（单位）*/
#define SYNC_CAND_UNIT \
    (((P2P_MAX_PAYLOAD - P2P_RLY_SESS_ID_PSZ - 1) / (int)sizeof(p2p_candidate_t)) < P2P_RELAY_MAX_CANDS_PER_PACKET \
     ? ((P2P_MAX_PAYLOAD - P2P_RLY_SESS_ID_PSZ - 1) / (int)sizeof(p2p_candidate_t)) \
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
static int enqueue_message(p2p_signal_relay_ctx_t *ctx,
                           uint8_t type,
                           const uint8_t *payload,
                           int payload_len) {

    // 1. 从池中分配 chunk（优先从 recycle，否则 malloc）
    p2p_send_chunk_t *chunk;

    // 优先从 recycle 链表取
    if (ctx->chunk_free_list) {
        chunk = ctx->chunk_free_list;
        ctx->chunk_free_list = chunk->next;
        ctx->chunk_free_count--;
    } else {
        // recycle 为空，动态分配
        chunk = (p2p_send_chunk_t *)malloc(sizeof(p2p_send_chunk_t));
        if (!chunk) {
            return -1;  // 内存分配失败
        }
    }
    chunk->len = 0;
    chunk->next = NULL;

    // 2. 填充包头（3 字节）
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)chunk->data;
    hdr->type = type;
    hdr->size = htons((uint16_t)payload_len);

    // 3. 复制 payload
    if (payload_len > 0) {
        memcpy(chunk->data + sizeof(p2p_relay_hdr_t), payload, (size_t)payload_len);
    }

    chunk->len = (int)sizeof(p2p_relay_hdr_t) + payload_len;

    // 4. 加入发送队列
    chunk->next = NULL;    
    if (ctx->send_queue_tail) {
        ctx->send_queue_tail->next = chunk;
        ctx->send_queue_tail = chunk;
    } else {
        ctx->send_queue_head = chunk;
        ctx->send_queue_tail = chunk;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 解析 SYNC 负载，追加到 session 的 remote_cands[]
 *
 * 格式: [candidate_count(1)][candidates(N*23)]
 */
static void unpack_remote_candidates(p2p_session_t *s, const uint8_t *payload, int len) {
    if (len < 1) {
        print("E:", LA_F("%s: bad payload len=%d\n", LA_F498, 498), TASK_ICE_REMOTE, len);
        return;
    }

    int cand_cnt = payload[0];

    if (len < 1 + (int)sizeof(p2p_candidate_t) * cand_cnt) {
        print("E:", LA_F("%s: bad payload(len=%d cand_cnt=%d)\n", LA_F561, 561), 
              TASK_ICE_REMOTE, len, cand_cnt);
        return;
    }

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    // 解析候选列表
    int offset = 1;
    p2p_remote_candidate_entry_t *c;

    for (int i = 0; i < cand_cnt; i++, offset += (int)sizeof(p2p_candidate_t)) {
        if (s->remote_cand_cnt >= s->remote_cand_cap) {
            print("W:", LA_F("%s: remote_cands[] full, skipped %d candidates\n", LA_F505, 505),
                  TASK_ICE_REMOTE, cand_cnt - i);
            break;
        }

        int idx = s->remote_cand_cnt;
        unpack_candidate(c = &s->remote_cands[idx], payload + offset);

        // 检查重复
        if (p2p_find_remote_candidate_by_addr(s, &c->addr) >= 0) {
            print("W:", LA_F("%s: duplicate remote cand<%s:%d>, skipped\n", LA_F500, 500),
                  TASK_ICE_REMOTE, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
            continue;
        }

        const char *type_str;
        uint16_t *cand_cnt_ptr;
        bool opt_off = false;

        if (c->type == P2P_CAND_HOST) {
            type_str = "host";
            cand_cnt_ptr = &s->remote_host_cnt;
            opt_off = s->cfg.test_ice_host_off;
        } else if (c->type == P2P_CAND_SRFLX) {
            type_str = "srflx";
            cand_cnt_ptr = &s->remote_srflx_cnt;
            opt_off = s->cfg.test_ice_srflx_off;
        } else if (c->type == P2P_CAND_RELAY) {
            type_str = "relay";
            cand_cnt_ptr = &s->remote_relay_cnt;
            opt_off = s->cfg.test_ice_relay_off;
        } else {
            print("E:", LA_F("%s: unexpected remote cand type %d, skipped\n", LA_F193, 193),
                  TASK_ICE_REMOTE, c->type);
            continue;
        }

        if (opt_off) {
            print("I:", LA_F("%s: remote %s cand[%d]<%s:%d> (disabled)\n", LA_F423, 423),
                  TASK_ICE_REMOTE, type_str, idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
            continue;
        }

        ++s->remote_cand_cnt;
        ++*cand_cnt_ptr;

        print("I:", LA_F("%s: remote %s cand[%d]<%s:%d> accepted\n", LA_F153, 153),
              TASK_ICE_REMOTE, type_str, idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));

        // 启动打洞
        if (ctx->state >= SIGNAL_RELAY_EXCHANGING && nat_punch(s, idx) != E_NONE) {
            print("E:", LA_F("%s: punch remote cand[%d]<%s:%d> failed\n", LA_F137, 137),
                  TASK_ICE_REMOTE, idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));
        }
    }
}

/*
 * 消息发送函数
 */

/*
 * 发送 ONLINE 消息
 *
 * 协议：P2P_RLY_ONLINE (0x01)
 * 包头: [type(1) | size(2)]
 * 负载: [name(32)][instance_id(4)]
 */
static void send_online(p2p_session_t *s) {
    const char *PROTO = "ONLINE";

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    // 直接构造 payload: name(32) + instance_id(4)
    uint8_t payload[P2P_RLY_ONLINE_PSZ];
    memset(payload, 0, sizeof(payload));
    strncpy((char*)payload, ctx->local_peer_id, P2P_PEER_ID_MAX - 1);
    nwrite_l(payload + P2P_PEER_ID_MAX, ctx->instance_id);

    printf(LA_F("[TCP] %s enqueue, name='%s', rid=%u\n", LA_F577, 577),
           PROTO, ctx->local_peer_id, ctx->instance_id);

    if (enqueue_message(ctx, P2P_RLY_ONLINE, payload, sizeof(payload)) != 0) {
        print("W:", LA_F("%s: send buffer busy, will retry\n", LA_F507, 507), PROTO);
        return;
    }

    print("V:", LA_F("%s enqueued, name='%s', rid=%u\n", LA_F491, 491),
          PROTO, ctx->local_peer_id, ctx->instance_id);

    ctx->last_send_time = P_tick_ms();
}

/*
 * 发送 ALIVE 心跳
 *
 * 协议：P2P_RLY_ALIVE (0x03)
 * 包头: [type(1) | size(2)]
 * 负载: 无
 */
static void send_alive(p2p_session_t *s) {
    const char *PROTO = "ALIVE";

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    if (enqueue_message(ctx, P2P_RLY_ALIVE, NULL, 0) != 0) {
        print("V:", LA_F("%s: send buffer busy, skip\n", LA_F506, 506), PROTO);
        return;
    }

    print("V:", LA_F("%s enqueued\n", LA_F493, 493), PROTO);

    ctx->heartbeat_time = P_tick_ms();
}

/*
 * 发送 SYNC0 请求建立会话
 *
 * 协议：P2P_RLY_SYNC0 (0x04)
 * 包头: [type(1) | size(2)]
 * 负载: [target_name(32)][candidate_count(1)][candidates(N*23)]
 */
static void send_sync0(p2p_session_t *s) {
    const char *PROTO = "SYNC0";

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    uint8_t payload[P2P_PEER_ID_MAX + 1];
    memset(payload, 0, sizeof(payload));
    strncpy((char*)payload, ctx->remote_peer_id, P2P_PEER_ID_MAX - 1);
    payload[P2P_PEER_ID_MAX] = 0; // SYNC0 不携带候选，候选通过后续 SYNC 上送

    printf(LA_F("[TCP] %s enqueue, target='%s'\n", LA_F579, 579),
           PROTO, ctx->remote_peer_id);

    if (enqueue_message(ctx, P2P_RLY_SYNC0, payload, sizeof(payload)) != 0) {
        print("W:", LA_F("%s: send buffer busy, will retry\n", LA_F507, 507), PROTO);
        return;
    }

    print("V:", LA_F("%s enqueued, target='%s'\n", LA_F492, 492), PROTO, ctx->remote_peer_id);
    ctx->last_send_time = P_tick_ms();
}

/*
 * 发送 SYNC 上传候选
 *
 * 协议：P2P_RLY_SYNC (0x06)
 * 包头: [type(1) | size(2)]
 * 负载: [session_id(8)][candidate_count(1)][candidates(N*23)][fin_marker(0|1)]
 *
 * FIN 语义（非独立协议）：
 *   - 仍使用 P2P_RLY_SYNC
 *   - 在 candidates 后追加一个字节 0xFF，表示本端候选发送完成（FIN）
 */
static void send_sync(p2p_session_t *s) {
    const char *PROTO = "SYNC";

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    // 已发过 FIN 则不重复发送
    if (ctx->local_candidates_fin) return;

    // 由状态自动决定：有剩余候选则发候选，否则发 FIN
    bool send_fin = (ctx->next_candidate_index >= (uint16_t)s->local_cand_cnt);

    // TURN 候选仍在收集中，暂不发送 FIN
    if (send_fin && s->turn_pending > 0) return;

    uint8_t payload[P2P_MAX_PAYLOAD];
    int cand_cnt = 0;
    int payload_len = (int)P2P_RLY_SYNC_PSZ(0, false);

    // session_id 位于 payload 首部
    nwrite_ll(payload, ctx->session_id);

    // 发送 FIN（追加 fin_marker = 0xFF）
    if (send_fin) {
        payload[P2P_RLY_SESS_ID_PSZ] = 0;
        payload[P2P_RLY_SESS_ID_PSZ + 1] = P2P_RLY_SYNC_FIN_MARKER;
        payload_len = (int)P2P_RLY_SYNC_PSZ(0, true);
    }
    
    // 打包候选列表
    else {

        // 单包上限：取服务器协商值和本地常量的较小值（服务器为 0 表示使用本地默认）
        uint8_t srv_max = ctx->candidate_sync_max;
        int max_per_pkt = (srv_max > 0 && srv_max < SYNC_CAND_UNIT) ? srv_max : SYNC_CAND_UNIT;

        int start_idx = ctx->next_candidate_index;
        int remaining = s->local_cand_cnt - start_idx;
        cand_cnt = remaining < max_per_pkt ? remaining : max_per_pkt;
        if (cand_cnt < 0) cand_cnt = 0;

        payload[P2P_RLY_SESS_ID_PSZ] = (uint8_t)cand_cnt;

        for (int i = 0; i < cand_cnt; i++) { int idx = start_idx + i;
            pack_candidate(&s->local_cands[idx], payload + payload_len);
            payload_len += (int)sizeof(p2p_candidate_t);
        }
        // next_candidate_index 在 enqueue 成功后才推进，避免 enqueue 失败导致候选丢失
    }

    printf(LA_F("[TCP] %s enqueue, ses_id=%" PRIu64 " cand_cnt=%d fin=%d\n", LA_F578, 578),
           PROTO, ctx->session_id, cand_cnt, send_fin ? 1 : 0);

    if (enqueue_message(ctx, P2P_RLY_SYNC, payload, payload_len) != 0) {
        print("W:", LA_F("%s: send buffer busy, will retry\n", LA_F507, 507), PROTO);
        return;
    }

    if (!send_fin) {
        ctx->next_candidate_index += cand_cnt;
        ctx->last_sent_cand_count = (uint8_t)cand_cnt;
    }

    if (send_fin) {
        print("V:", LA_F("%s sent FIN\n", LA_F495, 495), PROTO);
        ctx->local_candidates_fin = true;
        ctx->last_sent_cand_count = 0;
    } else {
        print("V:", LA_F("%s sent %d candidates, next_idx=%d\n", LA_F494, 494),
              PROTO, cand_cnt, ctx->next_candidate_index);
    }
    ctx->awaiting_sync_ack = true;  /* 等待 ACK 前不发下一批（流控）*/
    ctx->trickle_batch_count = 0;

    ctx->last_send_time = P_tick_ms();
}

/*
 * 发送 FIN 结束会话
 *
 * 协议：P2P_RLY_FIN (0x08)
 * 包头: [type(1) | size(2)]
 * 负载: [session_id(8)]
 */
static void send_fin(p2p_session_t *s) {
    const char *PROTO = "FIN";

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    uint8_t payload[P2P_RLY_FIN_PSZ];
    nwrite_ll(payload, ctx->session_id);

    printf(LA_F("[TCP] %s enqueue, ses_id=%" PRIu64 "\n", LA_F539, 539),
           PROTO, ctx->session_id);

    if (enqueue_message(ctx, P2P_RLY_FIN, payload, sizeof(payload)) != 0) {
        print("W:", LA_F("%s: send buffer busy, will retry\n", LA_F507, 507), PROTO);
        return;
    }

    print("V:", LA_F("%s enqueued, ses_id=%" PRIu64 "\n", LA_F490, 490),
          PROTO, ctx->session_id);
}

/*
 * 首包候选发送前的异步候选等待门控（STUN/TURN）。
 *
 * 目的：避免刚进入 EXCHANGING 时就发送空队列或仅 HOST 候选，导致过早 FIN 或低质量首包。
 *      等待一次性 STUN / TURN 异步收集完成后再发首批，可避免 TURN 候选被提前 FIN 截断。
 *
 * 生效范围：仅对“首批尚未发送”生效（next_candidate_index == 0）。
 */
static bool relay_wait_stun_candidates(p2p_session_t *s) {
    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;
    return ctx->state == SIGNAL_RELAY_EXCHANGING
        && ctx->next_candidate_index == 0
        && !ctx->local_candidates_fin
        && (s->stun_pending > 0 || s->turn_pending > 0);
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 消息接收处理
 */

/*
 * 处理 STATUS（服务器主动返回状态）
 *
 * 协议：P2P_RLY_STATUS (0x00)
 * 负载: [type(1)][status_code(1)][status_msg(N)]
 */
static void handle_relay_status(p2p_session_t *s, const uint8_t *payload, int len) {
    const char *PROTO = "STATUS";

    printf(LA_F("[TCP] %s recv, len=%d\n", LA_F533, 533), PROTO, len);

    if (len < (int)P2P_RLY_STATUS_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        s->sig_relay_ctx.state = SIGNAL_RELAY_ERROR;
        return;
    }

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;
    uint8_t req_type = payload[0];
    uint8_t status_code = payload[1];

    if (len > (int)P2P_RLY_STATUS_PSZ) {
        int msg_len = len - (int)P2P_RLY_STATUS_PSZ;
        print("W:", LA_F("%s: req_type=%u code=%u msg=%.*s\n", LA_F530, 530),
              PROTO, (unsigned)req_type, (unsigned)status_code, msg_len, (const char *)(payload + P2P_RLY_STATUS_PSZ));
    } else {
        print("W:", LA_F("%s: req_type=%u code=%u\n", LA_F565, 565),
              PROTO, (unsigned)req_type, (unsigned)status_code);
    }

    switch (status_code) {
    case P2P_RLY_ERR_PEER_OFFLINE:
        // 对端未连接：会话仍存在，等待对端重新上线
        if (ctx->state >= SIGNAL_RELAY_EXCHANGING) {
            ctx->peer_online = false;
            ctx->state = SIGNAL_RELAY_WAIT_PEER;
            print("I:", LA_F("WAIT_PEER: peer went offline, waiting for reconnect\n", LA_F575, 575));
        }
        break;
    case P2P_RLY_ERR_BUSY:
        // 会话忙：前一个转发未完成，稍后重试
        ctx->awaiting_sync_ack = false;
        ctx->trickle_last_time = P_tick_ms();
        print("V:", LA_F("%s: session busy, will retry\n", LA_F566, 566), PROTO);
        break;
    default:
        // NOT_ONLINE / PROTOCOL / INTERNAL / UNKNOWN → 致命错误
        print("E:", LA_F("%s: fatal error code=%u, entering ERROR state\n", LA_F531, 531),
              PROTO, (unsigned)status_code);
        ctx->state = SIGNAL_RELAY_ERROR;
        break;
    }
}

/*
 * 处理 ONLINE_ACK
 *
 * 协议：P2P_RLY_ONLINE_ACK (0x02)
 * 负载: [features(1)][candidate_sync_max(1)]
 */
static void handle_online_ack(p2p_session_t *s, const uint8_t *payload, int len) {
    const char *PROTO = "ONLINE_ACK";

    printf(LA_F("[TCP] %s recv, len=%d\n", LA_F533, 533), PROTO, len);

    if (len < (int)P2P_RLY_ONLINE_ACK_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    if (ctx->state != SIGNAL_RELAY_WAIT_ONLINE_ACK) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)ctx->state);
        return;
    }

    uint8_t features = payload[0];
    ctx->feature_relay = (features & P2P_RLY_FEATURE_RELAY) != 0;
    ctx->feature_msg = (features & P2P_RLY_FEATURE_MSG) != 0;
    ctx->candidate_sync_max = (len >= (int)P2P_RLY_ONLINE_ACK_PSZ) ? payload[1] : 0;

    print("V:", LA_F("%s: accepted, relay=%s msg=%s cand_max=%d\n", LA_F496, 496),
          PROTO, ctx->feature_relay ? "yes" : "no", ctx->feature_msg ? "yes" : "no",
          ctx->candidate_sync_max ? ctx->candidate_sync_max : P2P_RELAY_MAX_CANDS_PER_PACKET);

    // 切换到 ONLINE 状态
    ctx->state = SIGNAL_RELAY_ONLINE;
    print("I:", LA_F("ONLINE: ready to start session\n", LA_F518, 518));

    ctx->heartbeat_time = P_tick_ms();

    // 触发排队的 sync0 请求
    if (ctx->connected) {
        ctx->state = SIGNAL_RELAY_WAIT_SYNC0_ACK;
        send_sync0(s);
    }
}

/*
 * 处理 SYNC0_ACK
 *
 * 协议：P2P_RLY_SYNC0_ACK (0x05)
 * 负载: [session_id(8)][online(1)]
 */
static void handle_sync0_ack(p2p_session_t *s, const uint8_t *payload, int len) {
    const char *PROTO = "SYNC0_ACK";

    printf(LA_F("[TCP] %s recv, len=%d\n", LA_F533, 533), PROTO, len);

    if (len < (int)P2P_RLY_SYNC0_ACK_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    if (ctx->state != SIGNAL_RELAY_WAIT_SYNC0_ACK) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)ctx->state);
        return;
    }

    uint64_t session_id = nget_ll(payload);
    uint8_t online = payload[P2P_RLY_SESS_ID_PSZ];

    if (online > 1) {
        print("W:", LA_F("%s: invalid online=%u, normalized to 0\n", LA_F508, 508), PROTO, online);
        online = 0;
    }

    // 分配 session_id，并重置所有 ICE exchange 状态（向前兼容断线重连场景）
    ctx->session_id = session_id;
    ctx->peer_online = (online != 0);
    ctx->next_candidate_index = 0;
    ctx->local_candidates_fin = false;
    ctx->remote_candidates_fin = false;
    ctx->awaiting_sync_ack = false;
    ctx->local_delivery_confirmed = false;
    ctx->last_sent_cand_count = 0;

    print("V:", LA_F("%s: accepted, ses_id=%" PRIu64 " peer=%s\n", LA_F497, 497),
          PROTO, ctx->session_id, ctx->peer_online ? "online" : "offline");

    // 重置攒批计数器
    ctx->trickle_batch_count = 0;
    ctx->trickle_last_time = P_tick_ms();

    // SYNC0_ACK 只代表会话建立；候选交换需等待对端在线
    if (!ctx->peer_online) {
        ctx->state = SIGNAL_RELAY_WAIT_PEER;
        print("I:", LA_F("WAIT_PEER: session established, waiting for peer info\n", LA_F576, 576));
        return;
    }

    // 切换到 EXCHANGING 状态，开始上传候选
    ctx->state = SIGNAL_RELAY_EXCHANGING;
    print("I:", LA_F("EXCHANGING: peer=%s, uploading candidates\n", LA_F571, 571),
          ctx->peer_online ? "online" : "offline");

    // 启动 NAT 打洞（使用已收集的候选）
    if (s->remote_cand_cnt > 0) {
        nat_punch(s, -1/* all candidates */);
    }

    if (relay_wait_stun_candidates(s)) {
        print("I:", LA_F("EXCHANGING: waiting for initial STUN/TURN candidates before upload\n", LA_F572, 572));
        return;
    }

    send_sync(s);
}

/*
 * 处理 SYNC_ACK（服务器流控确认，包含本批次实际转发数量）
 *
 * 协议：P2P_RLY_SYNC_ACK (0x07)
 * 负载: [session_id(8)][confirmed_count(1)]
 *
 * 流程：
 *   - confirmed_count > 0: 服务器接受了 N 个候选（可能少于上传数）；
 *     回滚 next_candidate_index 到实际接受点，继续发送剩余批次。
 *   - confirmed_count == 0 且 FIN 标记包已发: 所有候选已转发到对端，标记完成。
 *
 * 服务器仅在中转缓冲区有空间时才发送 ACK（流量控制）。
 * 客户端在收到 ACK 前不得发送下一批候选。
 */
static void handle_sync_ack(p2p_session_t *s, const uint8_t *payload, int len) {
    const char *PROTO = "SYNC_ACK";

    printf(LA_F("[TCP] %s recv, len=%d\n", LA_F533, 533), PROTO, len);

    if (len < (int)P2P_RLY_SYNC_ACK_PSZ) {
        print("E:", LA_F("%s: bad payload(%d)\n", LA_F455, 455), PROTO, len);
        return;
    }

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    if (ctx->state < SIGNAL_RELAY_EXCHANGING) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)ctx->state);
        return;
    }

    uint64_t session_id = nget_ll(payload);
    if (session_id != ctx->session_id) {
        print("W:", LA_F("%s: session mismatch(local=%" PRIu64 " recv=%" PRIu64 ")\n", LA_F567, 567),
              PROTO, ctx->session_id, session_id);
        return;
    }

    // 清除流控等待标志
    ctx->awaiting_sync_ack = false;

    uint8_t confirmed_count = payload[P2P_RLY_SESS_ID_PSZ];

    // 如果本端已经发送过 FIN
    if (ctx->local_candidates_fin) {

        if (confirmed_count == 0) {
            ctx->local_delivery_confirmed = true;
            print("I:", LA_F("%s: all candidates delivered to peer (fwd=0 after FIN)\n", LA_F502, 502), PROTO);
        } else {
            print("W:", LA_F("%s: unexpected fwd=%d after FIN, ignored\n", LA_F568, 568), PROTO, confirmed_count);
        }
        return;
    }

    // 对账：服务器实际接受了 confirmed_count 个，回滚多余部分
    // next_candidate_index 已在 send_sync 中按 last_sent_cand_count 推进
    // 实际应停在: (next - last_sent) + confirmed_count
    ctx->next_candidate_index =
        ctx->next_candidate_index - ctx->last_sent_cand_count + confirmed_count;

    print("V:", LA_F("%s: forwarded=%d, next_idx adjusted to %d\n", LA_F563, 563),
          PROTO, confirmed_count, ctx->next_candidate_index);

    // 有就绪候选或达成 FIN 条件，则继续发 info 包
    // + 注意，send_sync 会重新将 awaiting_sync_ack 置 true
    //   也就是如果一直是在 ack 时发送下一个 sync 包，则 awaiting_sync_ack 会一直处于 true 状态
    //   而攒批发送逻辑在 awaiting_sync_ack 为 true 时会跳过发送
    if ((ctx->next_candidate_index < (uint16_t)s->local_cand_cnt) || (!s->turn_pending))
        send_sync(s);

    // 否则切换到攒批等待模式
    else ctx->trickle_last_time = P_tick_ms();
}

/*
 * 处理 SYNC（服务器下发对端候选）
 *
 * 协议：P2P_RLY_SYNC (0x06)
 * 负载: [session_id(8)][candidate_count(1)][candidates(N*23)][fin_marker(0|1)]
 */
static void handle_sync(p2p_session_t *s, const uint8_t *payload, int len, bool is_sync0) {
    const char *PROTO = is_sync0 ? "SYNC0" : "SYNC";

    printf(LA_F("[TCP] %s recv, len=%d\n", LA_F533, 533), PROTO, len);

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    if (ctx->state < SIGNAL_RELAY_WAIT_PEER) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)ctx->state);
        return;
    }

    if (len < (int)P2P_RLY_SYNC_PSZ(0, false)) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    // 首次收到 SYNC 视为对端上线，启动候选交换
    if (ctx->state == SIGNAL_RELAY_WAIT_PEER) {

        ctx->peer_online = true;
        ctx->state = SIGNAL_RELAY_EXCHANGING;
        print("I:", LA_F("EXCHANGING: first sync received, peer online\n", LA_F514, 514));

        // 启动 NAT 打洞（使用当前已收集的远端候选）
        if (s->remote_cand_cnt > 0) {
            nat_punch(s, -1/* all candidates */);
        }

        if (relay_wait_stun_candidates(s)) {
            print("I:", LA_F("EXCHANGING: waiting for initial STUN/TURN candidates before upload\n", LA_F572, 572));
        } else {
            send_sync(s);
        }
    }

    // session_id 位于 payload 首部
    uint64_t session_id = nget_ll(payload);
    if (session_id != ctx->session_id) {

        // SYNC0：对端重新发起连接，强制重置会话（类似 compact 的 info0 session 重置）
        if (is_sync0) {
            print("W:", LA_F("%s: session renewed by peer SYNC0 (local=%" PRIu64 " recv=%" PRIu64 ")\n", LA_F580, 580),
                  PROTO, ctx->session_id, session_id);

            // 通知业务层连接断开（session 被对方重置）
            if (s->state >= P2P_STATE_LOST) {
                if (s->cfg.on_state) s->cfg.on_state(s, s->state, P2P_STATE_CLOSED, s->cfg.userdata);
            }

            // 重置 p2p 会话
            p2p_session_reset(s, false);

            // 重置信令层会话状态
            ctx->session_id = session_id;
            ctx->peer_online = true;
            ctx->next_candidate_index = 0;
            ctx->local_candidates_fin = false;
            ctx->remote_candidates_fin = false;
            ctx->awaiting_sync_ack = false;
            ctx->local_delivery_confirmed = false;
            ctx->last_sent_cand_count = 0;
            ctx->trickle_batch_count = 0;
            ctx->trickle_last_time = P_tick_ms();

            ctx->state = SIGNAL_RELAY_EXCHANGING;
            print("I:", LA_F("EXCHANGING: session reset by peer SYNC0\n", LA_F581, 581));

            if (!relay_wait_stun_candidates(s)) {
                send_sync(s);
            }
        } else {
            print("W:", LA_F("%s: session mismatch(local=%" PRIu64 " recv=%" PRIu64 ")\n", LA_F567, 567),
                  PROTO, ctx->session_id, session_id);
            return;
        }
    }

    uint8_t cand_cnt = payload[P2P_RLY_SESS_ID_PSZ];
    uint32_t base_len = (uint32_t)P2P_RLY_SYNC_PSZ(cand_cnt, false);
    bool has_fin = false;
    if ((uint32_t)len == base_len + 1u) {
        has_fin = true;
        if (payload[base_len] != P2P_RLY_SYNC_FIN_MARKER) {
            print("E:", LA_F("%s: bad FIN marker=0x%02x\n", LA_F99, 99), PROTO, payload[base_len]);
            return;
        }
    } else if ((uint32_t)len != base_len) {
        print("E:", LA_F("%s: bad payload(len=%d cand_cnt=%d)\n", LA_F561, 561), PROTO, len, cand_cnt);
        return;
    }

    // 解析候选列表（首部 session_id；尾部可能有 1B FIN 标记）
    unpack_remote_candidates(s, payload + P2P_RLY_SESS_ID_PSZ, (int)(base_len - P2P_RLY_SESS_ID_PSZ));

    if (has_fin) {
        ctx->remote_candidates_fin = true;
        print("I:", LA_F("%s: received FIN marker from peer\n", LA_F564, 564), TASK_ICE_REMOTE);
    }

    print("V:", LA_F("%s: processed, remote_cand_cnt=%d\n", LA_F503, 503),
          PROTO, s->remote_cand_cnt);
}

/*
 * 处理 FIN（服务器转发的对端会话结束通知）
 *
 * 协议：P2P_RLY_FIN (0x08)
 * 负载: [session_id(8)]
 */
static void handle_relay_fin(p2p_session_t *s, const uint8_t *payload, int len) {
    const char *PROTO = "FIN";

    printf(LA_F("[TCP] %s recv, len=%d\n", LA_F533, 533), PROTO, len);

    if (len < (int)P2P_RLY_SESS_ID_PSZ) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F562, 562), PROTO, len);
        return;
    }

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    uint64_t session_id = nget_ll(payload);
    if (session_id != ctx->session_id) {
        print("W:", LA_F("%s: session mismatch(local=%" PRIu64 " recv=%" PRIu64 ")\n", LA_F567, 567),
              PROTO, ctx->session_id, session_id);
        return;
    }

    print("I:", LA_F("%s: peer closed session %" PRIu64 "\n", LA_F504, 504), PROTO, session_id);

    // 清理会话状态，回到 ONLINE 状态
    ctx->session_id = 0;
    ctx->peer_online = false;
    ctx->connected = false;
    memset(ctx->remote_peer_id, 0, sizeof(ctx->remote_peer_id));
    ctx->next_candidate_index = 0;
    ctx->local_candidates_fin = false;
    ctx->remote_candidates_fin = false;
    ctx->awaiting_sync_ack = false;
    ctx->local_delivery_confirmed = false;
    ctx->last_sent_cand_count = 0;
    ctx->trickle_batch_count = 0;
    ctx->trickle_last_time = 0;

    ctx->state = SIGNAL_RELAY_ONLINE;
}

/*
 * 消息分发
 */
static void dispatch_message(p2p_session_t *s) {
    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    switch (ctx->hdr.type) {
        case P2P_RLY_STATUS:
            handle_relay_status(s, ctx->payload, ntohs(ctx->hdr.size));
            break;

        case P2P_RLY_ONLINE_ACK:
            handle_online_ack(s, ctx->payload, ntohs(ctx->hdr.size));
            break;

        case P2P_RLY_SYNC0_ACK:
            handle_sync0_ack(s, ctx->payload, ntohs(ctx->hdr.size));
            break;

        case P2P_RLY_SYNC_ACK:
            handle_sync_ack(s, ctx->payload, ntohs(ctx->hdr.size));
            break;

        case P2P_RLY_SYNC0:  // 服务器转发对端 SYNC0 首批候选（格式同 SYNC）
        case P2P_RLY_SYNC:
            handle_sync(s, ctx->payload, ntohs(ctx->hdr.size), ctx->hdr.type == P2P_RLY_SYNC0);
            break;

        case P2P_RLY_FIN:
            handle_relay_fin(s, ctx->payload, ntohs(ctx->hdr.size));
            break;

        default:
            print("W:", LA_F("Unknown message type %d\n", LA_F529, 529), ctx->hdr.type);
            break;
    }

    ctx->last_recv_time = P_tick_ms();
}

///////////////////////////////////////////////////////////////////////////////

/*
 * API 实现
 */

void p2p_signal_relay_init(p2p_signal_relay_ctx_t *ctx) {

    memset(ctx, 0, sizeof(*ctx));
    ctx->state = SIGNAL_RELAY_INIT;
    ctx->sockfd = P_INVALID_SOCKET;
}

ret_t p2p_signal_relay_online(struct p2p_session *s, const char *local_peer_id,
                              const struct sockaddr_in *server) {

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;
    P_check(ctx->state == SIGNAL_RELAY_INIT, return E_NONE_CONTEXT;)

    ctx->server_addr = *server;

    // instance_id 机制：
    // - 首次 online()（instance_id == 0）：生成新的随机 ID，建立全新会话
    // - 重连 online()（instance_id != 0）：复用已有 ID，服务器保留会话状态
    if (!ctx->instance_id) {
        uint32_t rid = 0;
        while (!rid) rid = P_rand32();
        ctx->instance_id = rid;
    }

    // local_peer_id 保存
    strncpy(ctx->local_peer_id, local_peer_id, P2P_PEER_ID_MAX - 1);
    ctx->local_peer_id[P2P_PEER_ID_MAX - 1] = '\0';

    // 创建 TCP socket
    ctx->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->sockfd == P_INVALID_SOCKET) {
        print("E:", LA_F("Failed to create TCP socket\n", LA_F516, 516));
        return E_UNKNOWN;
    }

    // 设置非阻塞
    if (P_sock_nonblock(ctx->sockfd, true) != E_NONE) {
        print("E:", LA_F("Failed to set socket non-blocking\n", LA_F517, 517));
        P_sock_close(ctx->sockfd);
        ctx->sockfd = P_INVALID_SOCKET;
        return E_UNKNOWN;
    }

    // 连接到服务器
    print("I:", LA_F("Connecting to %s:%d\n", LA_F513, 513),
          inet_ntoa(server->sin_addr), ntohs(server->sin_port));

    int ret = connect(ctx->sockfd, (struct sockaddr *)&ctx->server_addr,
                      sizeof(ctx->server_addr));

    // 连接立即成功（少见）
    if (ret == 0) {
        print("I:", LA_F("TCP connected immediately, sending ONLINE\n", LA_F523, 523));
        ctx->state = SIGNAL_RELAY_WAIT_ONLINE_ACK;
        send_online(s);
    }
    // 连接进行中
    else if (P_sock_is_inprogress()) {
        ctx->state = SIGNAL_RELAY_ONLINE_ING;
        ctx->last_send_time = P_tick_ms();
    }
    else {
        print("E:", LA_F("TCP connect failed\n", LA_F522, 522));
        P_sock_close(ctx->sockfd);
        ctx->sockfd = P_INVALID_SOCKET;
        return E_UNKNOWN;
    }

    return E_NONE;
}

ret_t p2p_signal_relay_offline(struct p2p_session *s) {
    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    if (ctx->sockfd != P_INVALID_SOCKET) {
        P_sock_close(ctx->sockfd);
        ctx->sockfd = P_INVALID_SOCKET;
    }

    ctx->offset = 0;
    ctx->recv_state = RECV_STATE_HEADER;

    // 清理发送队列
    p2p_send_chunk_t *chunk;
    while ((chunk = ctx->send_queue_head)) {
        ctx->send_queue_head = chunk->next;
        free(chunk);
    }
    ctx->send_queue_tail = NULL;
    ctx->send_offset = 0;

    // 销毁 chunk 池（释放所有回收池中的 chunk）
    while ((chunk = ctx->chunk_free_list)) {
        ctx->chunk_free_list = chunk->next;
        free(chunk);
    }
    ctx->chunk_free_count = 0;

    ctx->connected = false;
    ctx->trickle_batch_count = 0;  /* 重置攒批计数器 */
    ctx->trickle_last_time = 0;     /* 重置攒批时间戳 */
    ctx->state = SIGNAL_RELAY_INIT;

    return E_NONE;
}

ret_t p2p_signal_relay_connect(struct p2p_session *s, const char *remote_peer_id) {
    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    P_check(remote_peer_id && remote_peer_id[0], return E_INVALID;)
    if (ctx->state == SIGNAL_RELAY_INIT || ctx->state == SIGNAL_RELAY_ERROR) {
        return E_NONE_CONTEXT;
    }

    // 如果当前已经连接过：target 相同则幂等成功；不同则视为忙
    if (ctx->connected) {
        return strcmp(ctx->remote_peer_id, remote_peer_id) == 0 ? E_NONE : E_BUSY;
    }

    // 设置连接目标
    strncpy(ctx->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX - 1);
    ctx->remote_peer_id[P2P_PEER_ID_MAX - 1] = '\0';
    ctx->connected = true;

    // 已上线：立即发送 SYNC0；否则等待 ONLINE_ACK 后自动触发
    if (ctx->state == SIGNAL_RELAY_ONLINE) {
        ctx->state = SIGNAL_RELAY_WAIT_SYNC0_ACK;
        send_sync0(s);
    }

    return E_NONE;
}

ret_t p2p_signal_relay_disconnect(struct p2p_session *s) {
    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    // 如果没有连接过
    if (!ctx->connected) {
        return E_NONE;
    }

    // 如果尚未完成在线：直接取消 connect 连接状态
    if (ctx->state <= SIGNAL_RELAY_ONLINE) {
        *ctx->remote_peer_id = 0;
        ctx->connected = false;
        return E_NONE;
    }

    // 如果正在通过信令服务器申请建立连接中
    if (ctx->state < SIGNAL_RELAY_WAIT_PEER) {
        return E_BUSY;
    }

    // 发送 FIN 消息
    send_fin(s);

    memset(ctx->remote_peer_id, 0, sizeof(ctx->remote_peer_id));
    ctx->connected = false;

    // 清理会话状态
    ctx->session_id = 0;
    ctx->peer_online = false;
    ctx->next_candidate_index = 0;
    ctx->local_candidates_fin = false;
    ctx->remote_candidates_fin = false;
    ctx->awaiting_sync_ack = false;
    ctx->local_delivery_confirmed = false;
    ctx->last_sent_cand_count = 0;
    ctx->trickle_batch_count = 0;
    ctx->trickle_last_time = 0;
    
    print("I:", LA_F("Disconnected, back to ONLINE state\n", LA_F536, 536));

    // 回到 ONLINE 状态（保持与服务器的连接）
    ctx->state = SIGNAL_RELAY_ONLINE;

    return E_NONE;
}

void p2p_signal_relay_trickle_candidate(struct p2p_session *s) {
    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    /* 状态校验：只在候选交换或完成阶段处理 trickle（与 COMPACT 对齐）*/
    if ((ctx->state != SIGNAL_RELAY_EXCHANGING && ctx->state != SIGNAL_RELAY_READY) || 
        ctx->local_candidates_fin) {
        return;
    }

    if (ctx->state == SIGNAL_RELAY_EXCHANGING && ctx->next_candidate_index == 0) {
        if (s->stun_pending > 0 || s->turn_pending > 0) return;

        if (!ctx->awaiting_sync_ack) {
            send_sync(s);
        }
        return;
    }

    /* 检查是否有新候选 */
    if (ctx->next_candidate_index >= s->local_cand_cnt) {
        return;
    }

    /* O(1) 累加攒批计数：每次本地异步候选（STUN/TURN）增加 1 个 */
    ctx->trickle_batch_count++;

    /* 流控门：等待 SYNC_ACK 期间只累积，不发送
     * handle_sync_ack 收到回复后会主动触发下一批 */
    if (ctx->awaiting_sync_ack) {
        return;
    }

    /* 攒批时间窗口控制 */
    uint64_t now = P_tick_ms();
    uint8_t max_per_pkt = ctx->candidate_sync_max ? ctx->candidate_sync_max : P2P_RELAY_MAX_CANDS_PER_PACKET;
    bool should_send = false;

    /* 满足两个条件之一就发送：
     * 1. 当前批累积了最大数量
     * 2. 上次发送后已经过了攒批时间窗口 */
    if (ctx->trickle_batch_count >= max_per_pkt) {
        print("I:", LA_F("Trickle TURN: batch full (%d cands), sending\n", LA_F573, 573),
              ctx->trickle_batch_count);
        should_send = true;
    } else if (ctx->trickle_last_time && (now - ctx->trickle_last_time) >= P2P_RELAY_TRICKLE_BATCH_MS) {
        print("I:", LA_F("Trickle TURN: batch timeout (%d cands), sending\n", LA_F574, 574),
              ctx->trickle_batch_count);
        should_send = true;
    }

    if (should_send) {
        send_sync(s);
        /* 注意：send_sync 内部会重置 trickle_batch_count */
    }
}

///////////////////////////////////////////////////////////////////////////////


void p2p_signal_relay_tick_recv(struct p2p_session *s) {

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;
    if (ctx->state <= SIGNAL_RELAY_INIT || ctx->sockfd == P_INVALID_SOCKET) {
        return;
    }

    // ONLINE_ING 状态：检查连接是否完成
    if (ctx->state == SIGNAL_RELAY_ONLINE_ING) {

        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(ctx->sockfd, &wfds);

        struct timeval tv = {0, 0};
        int ret = select((int)ctx->sockfd + 1, NULL, &wfds, NULL, &tv);

        // 连接成功，发送 ONLINE
        if (ret > 0 && FD_ISSET(ctx->sockfd, &wfds)) {
            print("I:", LA_F("TCP connected, sending ONLINE\n", LA_F524, 524));
            ctx->state = SIGNAL_RELAY_WAIT_ONLINE_ACK;
            send_online(s);
        } else if (ret < 0) {
            print("E:", LA_F("TCP connect failed (select error)\n", LA_F521, 521));
            ctx->state = SIGNAL_RELAY_ERROR;
        }
        return;
    }

    // 接收数据（状态机）
    for(;;) {
        ssize_t n;

        // 如果当前处于读取包头阶段
        // + 开始读取一个新包，或之前只读取了部分包头，继续读取包头
        if (ctx->recv_state == RECV_STATE_HEADER) {

            n = recv(ctx->sockfd,
                     (char *)ctx->hdr_buf + ctx->offset,
                     (int)sizeof(p2p_relay_hdr_t) - ctx->offset,
                     0);
            if (n > 0) { ctx->offset += n;
                
                // 如果包头已经完整，解析
                if (ctx->offset == (int)sizeof(p2p_relay_hdr_t)) {
                    memcpy(&ctx->hdr, ctx->hdr_buf, sizeof(p2p_relay_hdr_t));
                    ctx->hdr.size = ntohs(ctx->hdr.size);

                    // 验证 payload 大小
                    if (ctx->hdr.size > P2P_MAX_PAYLOAD) {
                        print("E:", LA_F("payload size %u exceeds limit %u\n", LA_F538, 538),
                              ctx->hdr.size, P2P_MAX_PAYLOAD);
                        ctx->state = SIGNAL_RELAY_ERROR;
                        return;
                    }

                    // 切换到读取 payload
                    if (ctx->hdr.size > 0) {
                        ctx->recv_state = RECV_STATE_PAYLOAD;
                    } else {
                        dispatch_message(s);                    // 无 payload，直接派发
                        ctx->recv_state = RECV_STATE_HEADER;    // 准备读取下一个包
                    }
                    ctx->offset = 0;
                }

                continue;
            } 
        } 
        else {

            // 读取 payload
            n = recv(ctx->sockfd, (char *)ctx->payload + ctx->offset, ctx->hdr.size - ctx->offset, 0);
            if (n > 0) { ctx->offset += n;
                
                if (ctx->offset == ctx->hdr.size) {
                    dispatch_message(s);                        // payload 完整，分发
                    ctx->recv_state = RECV_STATE_HEADER;        // 准备读取下一个包
                    ctx->offset = 0;
                }

                continue;
            } 
        }

        if (n == 0) { // 连接关闭            
            print("I:", LA_F("TCP connection closed by peer\n", LA_F525, 525));
            ctx->state = SIGNAL_RELAY_ERROR;
        }
        else if (!P_sock_is_wouldblock()) {   // 出现错误
            print("E:", LA_F("TCP recv error\n", LA_F526, 526));
            ctx->state = SIGNAL_RELAY_ERROR;
        }
        return;
    }
}

void p2p_signal_relay_tick_send(struct p2p_session *s) {

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;
    if (ctx->state <= SIGNAL_RELAY_ONLINE_ING || ctx->sockfd == P_INVALID_SOCKET) {
        return;
    }

    uint64_t now = P_tick_ms();

    // ====================================================================
    // 推进发送队列（循环发送直到队列为空或 socket WOULDBLOCK）
    // ====================================================================
    
    while (ctx->send_queue_head) {

        p2p_send_chunk_t *chunk = ctx->send_queue_head;
        ssize_t n = send(ctx->sockfd, (const char *)(chunk->data + ctx->send_offset),
                         chunk->len - ctx->send_offset, 0);

        if (n > 0) { ctx->send_offset += (int)n;
            
            // chunk 发送完成
            if (ctx->send_offset == chunk->len) {
                if (!((ctx->send_queue_head = chunk->next)))
                    ctx->send_queue_tail = NULL;
                ctx->send_offset = 0;  // 重置偏移，准备发送下一个 chunk
                
                // 回收到池
                chunk->next = ctx->chunk_free_list;
                ctx->chunk_free_list = chunk;
                ctx->chunk_free_count++;
                
                // 继续发送下一个 chunk
                continue;
            }
            
            // chunk 部分发送，继续尝试
            continue;
            
        } 
        else if (!n) {  // 连接关闭

            print("E:", LA_F("TCP connection closed during send\n", LA_F527, 527));
            ctx->state = SIGNAL_RELAY_ERROR;
            return;
        }
        else if (!P_sock_is_wouldblock()) { // 出现错误

            print("E:", LA_F("TCP send error\n", LA_F534, 534));
            ctx->state = SIGNAL_RELAY_ERROR;
            
            // 错误时出队并回收当前 chunk
            // fixme: 是全部回收，还是只回收当前 chunk？
            ctx->send_queue_head = chunk->next;
            if (!ctx->send_queue_head) {
                ctx->send_queue_tail = NULL;
            }
            ctx->send_offset = 0;  // 重置偏移
            
            chunk->next = ctx->chunk_free_list;
            ctx->chunk_free_list = chunk;
            ctx->chunk_free_count++;
            
            return;            
        }
    }

    // ====================================================================
    // 协议状态维护
    // ====================================================================

    // 服务器应答超时检查
    if (ctx->state == SIGNAL_RELAY_WAIT_ONLINE_ACK ||
        ctx->state == SIGNAL_RELAY_WAIT_SYNC0_ACK) {
        if (tick_diff(now, ctx->last_send_time) > P2P_RELAY_ACK_TIMEOUT_MS) {
            print("E:", LA_F("%s timeout\n", LA_F519, 519), 
                  ctx->state == SIGNAL_RELAY_WAIT_ONLINE_ACK ? "ONLINE_ACK" : "SYNC0_ACK");
            ctx->state = SIGNAL_RELAY_ERROR;
        }
        return;
    }

    // 心跳保活
    if (ctx->state >= SIGNAL_RELAY_ONLINE) {
        if (tick_diff(now, ctx->heartbeat_time) > P2P_RELAY_HEARTBEAT_INTERVAL_MS) {
            send_alive(s);
        }
    }

    // EXCHANGING: 上传候选（Trickle）
    if (ctx->state == SIGNAL_RELAY_EXCHANGING) {

        if (relay_wait_stun_candidates(s)) {
            return;
        }

        // 攒批等待模式：ACK 已收到但当时无就绪候选，等待异步候选（STUN/TURN）积累
        // 正常情况下由 handle_sync_ack 快速路径驱动，tick 仅作兜底
        if (!ctx->awaiting_sync_ack) {

            if (ctx->next_candidate_index < (uint16_t)s->local_cand_cnt) {

                // 攒批窗口到期后发送
                if (tick_diff(now, ctx->trickle_last_time) > P2P_RELAY_TRICKLE_BATCH_MS) {
                    send_sync(s);
                    ctx->trickle_last_time = now;
                }
            }
            // TURN 收集完成，所有候选就绪，发送 FIN
            else if (!s->turn_pending && !ctx->local_candidates_fin)
                send_sync(s);
        }

        // READY: 本端所有候选已被服务器确认转发到对端，且已收到对端全部候选
        if (ctx->local_delivery_confirmed && ctx->remote_candidates_fin) {
            print("I:", LA_F("READY: candidate exchange completed\n", LA_F520, 520));
            ctx->state = SIGNAL_RELAY_READY;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
#pragma clang diagnostic pop
