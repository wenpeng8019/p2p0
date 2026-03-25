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
 *   RELAY:   ONLINE(my_name) + CONNECT(target_name) → 两步分离
 *
 * 两阶段的意义：
 *   阶段1 (ONLINE):  建立"客户端-服务器"的基础连接
 *                   完成认证、能力协商、保活机制
 *   阶段2 (CONNECT): 建立"我-对方"的会话
 *                   支持一个客户端并发多个会话（不同 session_id）
 *
 * 这种设计特别适合 TCP 长连接场景：
 *   - 一次 ONLINE，持续保活
 *   - 多次 CONNECT，复用连接
 *   - 每个会话独立 session_id，互不干扰
 *
 * ============================================================================
 * 日志原则
 * ============================================================================
 *
 * - printf 用于调试级别的日志输出; print() 用于正式级别的日志输出（V/I/W/E）
 * - 对于主控流程（如 xxx_tick）
 *   > 在状态变更、或执行子步骤前，输出 I 级日志说明当前步骤和状态
 *   > 操作结果出现异常时，输出 W/E 级日志说明异常情况
 * - 对于响应流程（如收到某个包）
 *   > 在收到包时，调试打印包的详细信息；
 *   > 处理协议过程中，如果出现错误，输出 W/E 级日志说明异常情况
 *   > 如果最终成功处理了协议包，最后输出 V 级日志说明处理结果
 * - 对于发包操作
 *   > 在发送包前，调试打印包的详细信息
 *   > 如果发送操作失败，输出 W/E 级日志说明异常情况
 *   > 如果发送成功，输出 V 级日志说明发送结果
 */
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#define MOD_TAG "RELAY"

#include "p2p_internal.h"

#define TASK_REG                        "REGISTER"
#define TASK_ICE                        "ICE"
#define TASK_ICE_REMOTE                 "ICE REMOTE"

/* 一个 PEER_INFO 包所承载的候选数量（单位）*/
#define PEER_INFO_CAND_UNIT \
    (((P2P_MAX_PAYLOAD - sizeof(uint64_t) - 4) / (int)sizeof(p2p_candidate_t)) < P2P_RELAY_MAX_CANDS_PER_PACKET \
     ? ((P2P_MAX_PAYLOAD - sizeof(uint64_t) - 4) / (int)sizeof(p2p_candidate_t)) \
     : P2P_RELAY_MAX_CANDS_PER_PACKET)

///////////////////////////////////////////////////////////////////////////////

/*
 * chunk 池管理（动态申请 + recycle 链表）
 */

/* 初始化 chunk 池 */
static void chunk_pool_init(p2p_signal_relay_ctx_t *ctx) {
    ctx->chunk_free_list = NULL;
    ctx->chunk_free_count = 0;
}

/* 从池中分配一个 chunk（优先从 recycle，否则 malloc）*/
static p2p_send_chunk_t *chunk_alloc(p2p_signal_relay_ctx_t *ctx) {
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
            return NULL;  // 内存分配失败
        }
    }
    
    // 初始化 chunk
    chunk->len = 0;
    chunk->offset = 0;
    chunk->next = NULL;
    
    return chunk;
}

/* 回收 chunk 到池（加入 recycle 链表）*/
static void chunk_free(p2p_signal_relay_ctx_t *ctx, p2p_send_chunk_t *chunk) {
    if (!chunk) return;
    
    // 加入 recycle 链表头部
    chunk->next = ctx->chunk_free_list;
    ctx->chunk_free_list = chunk;
    ctx->chunk_free_count++;
}

/* 销毁池中所有回收的 chunk（释放内存）*/
static void chunk_pool_destroy(p2p_signal_relay_ctx_t *ctx) {
    p2p_send_chunk_t *chunk = ctx->chunk_free_list;
    while (chunk) {
        p2p_send_chunk_t *next = chunk->next;
        free(chunk);
        chunk = next;
    }
    ctx->chunk_free_list = NULL;
    ctx->chunk_free_count = 0;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 发送队列管理
 */

/* 初始化发送队列 */
static void send_queue_init(p2p_signal_relay_ctx_t *ctx) {
    ctx->send_queue_head = NULL;
    ctx->send_queue_tail = NULL;
    ctx->send_queue_count = 0;
}

/* 将 chunk 加入队列尾部 */
static void send_queue_enqueue(p2p_signal_relay_ctx_t *ctx, p2p_send_chunk_t *chunk) {
    chunk->next = NULL;
    
    if (ctx->send_queue_tail) {
        ctx->send_queue_tail->next = chunk;
        ctx->send_queue_tail = chunk;
    } else {
        ctx->send_queue_head = chunk;
        ctx->send_queue_tail = chunk;
    }
    ctx->send_queue_count++;
}

/* 从队列头部取出 chunk */
static p2p_send_chunk_t *send_queue_dequeue(p2p_signal_relay_ctx_t *ctx) {
    if (ctx->send_queue_head == NULL) {
        return NULL;
    }
    
    p2p_send_chunk_t *chunk = ctx->send_queue_head;
    ctx->send_queue_head = chunk->next;
    
    if (ctx->send_queue_head == NULL) {
        ctx->send_queue_tail = NULL;
    }
    
    ctx->send_queue_count--;
    chunk->next = NULL;
    
    return chunk;
}

/* 清空队列并回收所有 chunk */
static void send_queue_clear(p2p_signal_relay_ctx_t *ctx) {
    p2p_send_chunk_t *chunk;
    while ((chunk = send_queue_dequeue(ctx)) != NULL) {
        chunk_free(ctx, chunk);  // 回收到 recycle 链表
    }
}

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
    p2p_send_chunk_t *chunk = chunk_alloc(ctx);
    if (!chunk) {
        // 内存分配失败
        return -1;
    }

    // 2. 填充包头（9 字节）
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)chunk->data;
    hdr->magic = htonl(P2P_RLY_MAGIC);
    hdr->type = type;
    hdr->length = htonl((uint32_t)payload_len);

    // 3. 复制 payload
    if (payload_len > 0) {
        memcpy(chunk->data + 9, payload, (size_t)payload_len);
    }

    chunk->len = 9 + payload_len;
    chunk->offset = 0;

    // 4. 加入发送队列
    send_queue_enqueue(ctx, chunk);

    return 0;
}

/* 分配接收缓冲区 */
static int alloc_recv_payload(p2p_signal_relay_ctx_t *ctx, int size) {
    if (ctx->payload && ctx->payload_capacity >= size) {
        ctx->offset = 0;
        ctx->expected = size;
        return 0;
    }
    if (ctx->payload) {
        free(ctx->payload);
        ctx->payload = NULL;
    }
    ctx->payload = (uint8_t*)malloc((size_t)size);
    if (!ctx->payload) {
        print("E:", LA_F("Failed to allocate %d bytes for payload\n", LA_F470, 470), size);
        return -1;
    }
    ctx->payload_capacity = size;
    ctx->offset = 0;
    ctx->expected = size;
    return 0;
}

/* 释放接收缓冲区 */
static void free_recv_payload(p2p_signal_relay_ctx_t *ctx) {
    if (ctx->payload) {
        free(ctx->payload);
        ctx->payload = NULL;
        ctx->payload_capacity = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 解析 PEER_INFO 负载，追加到 session 的 remote_cands[]
 *
 * 格式: [session_id(8)][candidate_count(1)][reserved(3)][candidates(N*23)]
 */
static void unpack_remote_candidates(p2p_session_t *s, const uint8_t *payload, int len) {
    if (len < (int)sizeof(uint64_t) + 4) {
        print("E:", LA_F("%s: bad payload len=%d\n", LA_F100, 100), TASK_ICE_REMOTE, len);
        return;
    }

    uint64_t session_id = nget_ll(payload);
    int cand_cnt = payload[sizeof(uint64_t)];

    if (len < (int)sizeof(uint64_t) + 4 + (int)sizeof(p2p_candidate_t) * cand_cnt) {
        print("E:", LA_F("%s: bad payload(len=%d cand_cnt=%d)\n", LA_F99, 99), 
              TASK_ICE_REMOTE, len, cand_cnt);
        return;
    }

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    // 验证 session_id
    if (ctx->session_id != session_id) {
        print("W:", LA_F("%s: session mismatch(local=%" PRIu64 " recv=%" PRIu64 ")\n", LA_F462, 462),
              TASK_ICE_REMOTE, ctx->session_id, session_id);
        return;
    }

    // FIN 标识（候选接收完成）
    if (cand_cnt == 0) {
        print("I:", LA_F("%s: received FIN from peer\n", LA_F460, 460), TASK_ICE_REMOTE);
        ctx->remote_candidates_fin = true;
        return;
    }

    // 解析候选列表
    int offset = (int)sizeof(uint64_t) + 4;
    p2p_remote_candidate_entry_t *c;

    for (int i = 0; i < cand_cnt; i++, offset += (int)sizeof(p2p_candidate_t)) {
        if (s->remote_cand_cnt >= s->remote_cand_cap) {
            print("W:", LA_F("%s: remote_cands[] full, skipped %d candidates\n", LA_F461, 461),
                  TASK_ICE_REMOTE, cand_cnt - i);
            break;
        }

        int idx = s->remote_cand_cnt;
        unpack_candidate(c = &s->remote_cands[idx], payload + offset);

        // 检查重复
        if (p2p_find_remote_candidate_by_addr(s, &c->addr) >= 0) {
            print("W:", LA_F("%s: duplicate remote cand<%s:%d>, skipped\n", LA_F457, 457),
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
 * 构建 PEER_INFO 的候选队列，返回 payload 总长度
 *
 * 格式: [session_id(8)][candidate_count(1)][reserved(3)][candidates(N*23)]
 */
static int pack_local_candidates(p2p_session_t *s, uint8_t *payload, int *r_cand_cnt) {
    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    int n = 0;
    nwrite_ll(payload + n, ctx->session_id);
    n += 8;

    // 计算可发送的候选数量
    int start_idx = ctx->next_candidate_index;
    int remaining = s->local_cand_cnt - start_idx;
    int to_send = remaining < PEER_INFO_CAND_UNIT ? remaining : PEER_INFO_CAND_UNIT;

    if (to_send < 0) to_send = 0;

    payload[n++] = (uint8_t)to_send;        // candidate_count
    payload[n++] = 0;                       // reserved
    payload[n++] = 0;
    payload[n++] = 0;

    *r_cand_cnt = to_send;

    // 打包候选列表
    for (int i = 0; i < to_send; i++) {
        int idx = start_idx + i;
        pack_candidate(&s->local_cands[idx], payload + n);
        n += (int)sizeof(p2p_candidate_t);
    }

    return n;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 消息发送函数
 */

/*
 * 发送 ONLINE 消息
 *
 * 协议：P2P_RLY_ONLINE (0x01)
 * 包头: [magic(4) | type(1) | length(4)]
 * 负载: [name(32)]
 */
static void send_online(p2p_session_t *s) {
    const char *PROTO = "ONLINE";

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    uint8_t payload[P2P_PEER_ID_MAX];
    memset(payload, 0, sizeof(payload));
    strncpy((char*)payload, ctx->local_peer_id, P2P_PEER_ID_MAX - 1);

    printf(LA_F("[TCP] %s enqueue, name='%s'\n", LA_F397, 397),
           PROTO, ctx->local_peer_id);

    if (enqueue_message(ctx, P2P_RLY_ONLINE, payload, sizeof(payload)) != 0) {
        print("W:", LA_F("%s: send buffer busy, will retry\n", LA_F501, 501), PROTO);
        return;
    }

    print("V:", LA_F("%s enqueued, name='%s'\n", LA_F449, 449),
          PROTO, ctx->local_peer_id);

    ctx->last_send_time = P_tick_ms();
}

/*
 * 发送 ALIVE 心跳
 *
 * 协议：P2P_RLY_ALIVE (0x03)
 * 包头: [magic(4) | type(1) | length(4)]
 * 负载: 无
 */
static void send_alive(p2p_session_t *s) {
    const char *PROTO = "ALIVE";

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    if (enqueue_message(ctx, P2P_RLY_ALIVE, NULL, 0) != 0) {
        print("V:", LA_F("%s: send buffer busy, skip\n", LA_F502, 502), PROTO);
        return;
    }

    print("V:", LA_F("%s enqueued\n", LA_F452, 452), PROTO);

    ctx->heartbeat_time = P_tick_ms();
}

/*
 * 发送 CONNECT 请求建立会话
 *
 * 协议：P2P_RLY_CONNECT (0x04)
 * 包头: [magic(4) | type(1) | length(4)]
 * 负载: [target_name(32)]
 */
static void send_connect(p2p_session_t *s) {
    const char *PROTO = "CONNECT";

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    uint8_t payload[P2P_PEER_ID_MAX];
    memset(payload, 0, sizeof(payload));
    strncpy((char*)payload, ctx->remote_peer_id, P2P_PEER_ID_MAX - 1);

    printf(LA_F("[TCP] %s enqueue, target='%s'\n", LA_F485, 485),
           PROTO, ctx->remote_peer_id);

    if (enqueue_message(ctx, P2P_RLY_CONNECT, payload, sizeof(payload)) != 0) {
        print("W:", LA_F("%s: send buffer busy, will retry\n", LA_F501, 501), PROTO);
        return;
    }

    print("V:", LA_F("%s enqueued, target='%s'\n", LA_F451, 451), PROTO, ctx->remote_peer_id);

    ctx->connect_time = P_tick_ms();
    ctx->connect_attempts++;
}

/*
 * 发送 PEER_INFO 上传候选
 *
 * 协议：P2P_RLY_PEER_INFO (0x06)
 * 包头: [magic(4) | type(1) | length(4)]
 * 负载: [session_id(8)][candidate_count(1)][reserved(3)][candidates(N*23)]
 */
static void send_peer_info(p2p_session_t *s, bool send_fin) {
    const char *PROTO = "PEER_INFO";

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    uint8_t payload[P2P_MAX_PAYLOAD];
    int cand_cnt = 0;
    int payload_len;

    if (send_fin) {
        // 发送 FIN（candidate_count = 0）
        nwrite_ll(payload, ctx->session_id);
        payload[8] = 0;     // candidate_count = 0
        payload[9] = 0;     // reserved
        payload[10] = 0;
        payload[11] = 0;
        payload_len = 12;
    } else {
        // 打包候选列表
        payload_len = pack_local_candidates(s, payload, &cand_cnt);
    }

    printf(LA_F("[TCP] %s enqueue, ses_id=%" PRIu64 " cand_cnt=%d fin=%d\n", LA_F486, 486),
           PROTO, ctx->session_id, cand_cnt, send_fin ? 1 : 0);

    if (enqueue_message(ctx, P2P_RLY_PEER_INFO, payload, payload_len) != 0) {
        print("W:", LA_F("%s: send buffer busy, will retry\n", LA_F501, 501), PROTO);
        return;
    }

    if (send_fin) {
        print("V:", LA_F("%s sent FIN\n", LA_F448, 448), PROTO);
        ctx->local_candidates_fin = true;
    } else {
        print("V:", LA_F("%s sent %d candidates, next_idx=%d\n", LA_F61, 61),
              PROTO, cand_cnt, ctx->next_candidate_index + cand_cnt);
        ctx->next_candidate_index += cand_cnt;
    }

    ctx->last_send_time = P_tick_ms();
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 消息接收处理
 */

/*
 * 处理 ONLINE_ACK
 *
 * 协议：P2P_RLY_ONLINE_ACK (0x02)
 * 负载: [features(1)][reserved(3)]
 */
static void handle_online_ack(p2p_session_t *s, const uint8_t *payload, int len) {
    const char *PROTO = "ONLINE_ACK";

    printf(LA_F("[TCP] %s recv, len=%d\n", LA_F393, 393), PROTO, len);

    if (len < 4) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F455, 455), PROTO, len);
        return;
    }

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    if (ctx->state != SIGNAL_RELAY_WAIT_ONLINE_ACK) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)ctx->state);
        return;
    }

    uint8_t features = payload[0];
    ctx->feature_forward = (features & P2P_RLY_FEATURE_FORWARD) != 0;
    ctx->feature_msg = (features & P2P_RLY_FEATURE_MSG) != 0;

    print("V:", LA_F("%s: accepted, forward=%s msg=%s\n", LA_F95, 95),
          PROTO, ctx->feature_forward ? "yes" : "no", ctx->feature_msg ? "yes" : "no");

    // 切换到 ONLINE 状态
    ctx->state = SIGNAL_RELAY_ONLINE;
    print("I:", LA_F("ONLINE: ready to start session\n", LA_F472, 472));

    ctx->heartbeat_time = P_tick_ms();
}

/*
 * 处理 CONNECT_ACK
 *
 * 协议：P2P_RLY_CONNECT_ACK (0x05)
 * 负载: [status(1)][reserved(3)][session_id(8)]
 */
static void handle_connect_ack(p2p_session_t *s, const uint8_t *payload, int len) {
    const char *PROTO = "CONNECT_ACK";

    printf(LA_F("[TCP] %s recv, len=%d\n", LA_F393, 393), PROTO, len);

    if (len < 12) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F455, 455), PROTO, len);
        return;
    }

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    if (ctx->state != SIGNAL_RELAY_WAIT_CONNECT_ACK) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)ctx->state);
        return;
    }

    uint8_t status = payload[0];
    uint64_t session_id = nget_ll(payload + 4);

    if (status == 0xFF) {
        print("E:", LA_F("%s: error, target not found\n", LA_F176, 176), PROTO);
        ctx->state = SIGNAL_RELAY_ERROR;
        return;
    }

    // 分配 session_id
    ctx->session_id = session_id;
    ctx->peer_online = (status == 0);

    print("V:", LA_F("%s: accepted, ses_id=%" PRIu64 " peer=%s\n", LA_F454, 454),
          PROTO, ctx->session_id, ctx->peer_online ? "online" : "offline");

    // 切换到 EXCHANGING 状态，开始上传候选
    ctx->state = SIGNAL_RELAY_EXCHANGING;
    print("I:", LA_F("EXCHANGING: peer=%s, uploading candidates\n", LA_F296, 296),
          ctx->peer_online ? "online" : "offline");

    // 启动 NAT 打洞（使用已收集的候选）
    if (s->remote_cand_cnt > 0) {
        nat_punch(s, -1/* all candidates */);
    }

    // 立即发送第一批候选
    if (s->local_cand_cnt > 0) {
        send_peer_info(s, false);
    } else {
        // 没有候选，直接发送 FIN
        send_peer_info(s, true);
    }
}

/*
 * 处理 PEER_INFO（服务器下发对端候选）
 *
 * 协议：P2P_RLY_PEER_INFO (0x06)
 * 负载: [session_id(8)][candidate_count(1)][reserved(3)][candidates(N*23)]
 */
static void handle_peer_info(p2p_session_t *s, const uint8_t *payload, int len) {
    const char *PROTO = "PEER_INFO";

    printf(LA_F("[TCP] %s recv, len=%d\n", LA_F393, 393), PROTO, len);

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    if (ctx->state < SIGNAL_RELAY_EXCHANGING) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)ctx->state);
        return;
    }

    // 解析候选列表
    unpack_remote_candidates(s, payload, len);

    print("V:", LA_F("%s: processed, remote_cand_cnt=%d\n", LA_F459, 459),
          PROTO, s->remote_cand_cnt);
}

/*
 * 处理 PEER_INFO_ACK（服务器确认候选上传）
 *
 * 协议：P2P_RLY_PEER_INFO_ACK (0x07)
 * 负载: [session_id(8)][status(1)][reserved(3)]
 */
static void handle_peer_info_ack(p2p_session_t *s, const uint8_t *payload, int len) {
    const char *PROTO = "PEER_INFO_ACK";

    printf(LA_F("[TCP] %s recv, len=%d\n", LA_F393, 393), PROTO, len);

    if (len < 12) {
        print("E:", LA_F("%s: bad payload(len=%d)\n", LA_F455, 455), PROTO, len);
        return;
    }

    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    if (ctx->state < SIGNAL_RELAY_EXCHANGING) {
        print("V:", LA_F("%s: ignored in state=%d\n", LA_F117, 117), PROTO, (int)ctx->state);
        return;
    }

    uint64_t session_id = nget_ll(payload);
    uint8_t status = payload[8];

    if (session_id != ctx->session_id) {
        print("W:", LA_F("%s: session mismatch(local=%" PRIu64 " recv=%" PRIu64 ")\n", LA_F462, 462),
              PROTO, ctx->session_id, session_id);
        return;
    }

    switch (status) {
        case 0:  // 已转发给对端
            print("V:", LA_F("%s: forwarded to peer\n", LA_F458, 458), PROTO);
            break;

        case 1:  // 已缓存（对端离线）
            print("V:", LA_F("%s: cached (peer offline)\n", LA_F456, 456), PROTO);
            ctx->peer_online = false;
            break;

        case 2:  // 缓存满（停止上传）
            print("W:", LA_F("%s: storage full, stop uploading\n", LA_F464, 464), PROTO);
            ctx->local_candidates_fin = true;  // 强制结束上传
            break;

        default:
            print("W:", LA_F("%s: unknown status %d\n", LA_F465, 465), PROTO, status);
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 消息分发
 */
static void dispatch_message(p2p_session_t *s) {
    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    switch (ctx->hdr.type) {
        case P2P_RLY_ONLINE_ACK:
            handle_online_ack(s, ctx->payload, ntohl(ctx->hdr.length));
            break;

        case P2P_RLY_CONNECT_ACK:
            handle_connect_ack(s, ctx->payload, ntohl(ctx->hdr.length));
            break;

        case P2P_RLY_PEER_INFO:
            handle_peer_info(s, ctx->payload, ntohl(ctx->hdr.length));
            break;

        case P2P_RLY_PEER_INFO_ACK:
            handle_peer_info_ack(s, ctx->payload, ntohl(ctx->hdr.length));
            break;

        default:
            print("W:", LA_F("Unknown message type %d\n", LA_F482, 482), ctx->hdr.type);
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
    
    // 初始化 chunk 池和发送队列
    chunk_pool_init(ctx);
    send_queue_init(ctx);
    ctx->sending = NULL;
}

ret_t p2p_signal_relay_online(struct p2p_session *s, const char *local_peer_id,
                              const struct sockaddr_in *server) {
    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;
    P_check(ctx->state == SIGNAL_RELAY_INIT, return E_NONE_CONTEXT;)

    ctx->server_addr = *server;

    // local_peer_id 保存
    memset(ctx->local_peer_id, 0, sizeof(ctx->local_peer_id));
    strncpy(ctx->local_peer_id, local_peer_id, P2P_PEER_ID_MAX - 1);
    ctx->local_peer_id[P2P_PEER_ID_MAX - 1] = '\0';

    // 创建 TCP socket
    ctx->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->sockfd == P_INVALID_SOCKET) {
        print("E:", LA_F("Failed to create TCP socket\n", LA_F471, 471));
        return E_UNKNOWN;
    }

    // 设置非阻塞
    if (P_sock_nonblock(ctx->sockfd, true) != E_NONE) {
        print("E:", LA_F("Failed to set socket non-blocking\n", LA_F505, 505));
        P_sock_close(ctx->sockfd);
        ctx->sockfd = P_INVALID_SOCKET;
        return E_UNKNOWN;
    }

    // 连接到服务器
    print("I:", LA_F("Connecting to %s:%d\n", LA_F469, 469),
          inet_ntoa(server->sin_addr), ntohs(server->sin_port));

    int ret = connect(ctx->sockfd, (struct sockaddr *)&ctx->server_addr,
                      sizeof(ctx->server_addr));

    if (ret == 0) {
        // 连接立即成功（少见）
        print("I:", LA_F("TCP connected immediately, sending ONLINE\n", LA_F478, 478));
        ctx->state = SIGNAL_RELAY_WAIT_ONLINE_ACK;
        send_online(s);
    } else if (P_sock_is_inprogress()) {
        // 连接进行中
        ctx->state = SIGNAL_RELAY_CONNECTING;
        ctx->last_send_time = P_tick_ms();
    } else {
        print("E:", LA_F("TCP connect failed\n", LA_F477, 477));
        P_sock_close(ctx->sockfd);
        ctx->sockfd = P_INVALID_SOCKET;
        return E_UNKNOWN;
    }

    return E_NONE;
}

ret_t p2p_signal_relay_connect(struct p2p_session *s, const char *remote_peer_id) {
    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;
    P_check(ctx->state == SIGNAL_RELAY_ONLINE, return E_NONE_CONTEXT;)

    // 保存 remote_peer_id
    memset(ctx->remote_peer_id, 0, sizeof(ctx->remote_peer_id));
    strncpy(ctx->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX - 1);
    ctx->remote_peer_id[P2P_PEER_ID_MAX - 1] = '\0';

    // 切换状态并发送 CONNECT
    ctx->state = SIGNAL_RELAY_WAIT_CONNECT_ACK;
    ctx->connect_attempts = 0;
    send_connect(s);

    return E_NONE;
}

void p2p_signal_relay_tick_recv(struct p2p_session *s) {
    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    if (ctx->state <= SIGNAL_RELAY_INIT || ctx->sockfd == P_INVALID_SOCKET) {
        return;
    }

    // CONNECTING 状态：检查连接是否完成
    if (ctx->state == SIGNAL_RELAY_CONNECTING) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(ctx->sockfd, &wfds);

        struct timeval tv = {0, 0};
        int ret = select((int)ctx->sockfd + 1, NULL, &wfds, NULL, &tv);

        if (ret > 0 && FD_ISSET(ctx->sockfd, &wfds)) {
            // 连接成功，发送 ONLINE
            print("I:", LA_F("TCP connected, sending ONLINE\n", LA_F478, 478));
            ctx->state = SIGNAL_RELAY_WAIT_ONLINE_ACK;
            send_online(s);
        } else if (ret < 0) {
            print("E:", LA_F("TCP connect failed (select error)\n", LA_F476, 476));
            ctx->state = SIGNAL_RELAY_ERROR;
        }
        return;
    }

    // 接收数据（状态机）
    while (1) {
        int n;

        if (ctx->recv_state == RECV_STATE_HEADER) {
            // 读取包头
            n = recv(ctx->sockfd, (char *)ctx->hdr_buf + ctx->offset,
                     9 - ctx->offset, 0);

            if (n > 0) {
                ctx->offset += n;
                if (ctx->offset == 9) {
                    // 包头完整，解析
                    memcpy(&ctx->hdr, ctx->hdr_buf, 9);
                    ctx->hdr.magic = ntohl(ctx->hdr.magic);
                    ctx->hdr.length = ntohl(ctx->hdr.length);

                    if (ctx->hdr.magic != P2P_RLY_MAGIC) {
                        print("E:", LA_F("Bad magic 0x%08x\n", LA_F466, 466), ctx->hdr.magic);
                        ctx->state = SIGNAL_RELAY_ERROR;
                        return;
                    }

                    // 切换到读取 payload
                    if (ctx->hdr.length > 0) {
                        if (alloc_recv_payload(ctx, (int)ctx->hdr.length) != 0) {
                            ctx->state = SIGNAL_RELAY_ERROR;
                            return;
                        }
                        ctx->recv_state = RECV_STATE_PAYLOAD;
                    } else {
                        // 无 payload，直接分发
                        dispatch_message(s);
                        ctx->offset = 0;
                        ctx->recv_state = RECV_STATE_HEADER;
                    }
                }
            } else if (n == 0) {
                // 连接关闭
                print("I:", LA_F("TCP connection closed by peer\n", LA_F479, 479));
                ctx->state = SIGNAL_RELAY_ERROR;
                return;
            } else {
                if (!P_sock_is_wouldblock()) {
                    print("E:", LA_F("TCP recv error\n", LA_F480, 480));
                    ctx->state = SIGNAL_RELAY_ERROR;
                }
                return;  // 等待下次
            }

        } else {  // RECV_STATE_PAYLOAD
            // 读取 payload
            n = recv(ctx->sockfd, (char *)ctx->payload + ctx->offset,
                     ctx->expected - ctx->offset, 0);

            if (n > 0) {
                ctx->offset += n;
                if (ctx->offset == ctx->expected) {
                    // payload 完整，分发
                    dispatch_message(s);
                    ctx->offset = 0;
                    ctx->recv_state = RECV_STATE_HEADER;
                }
            } else if (n == 0) {
                // 连接关闭
                print("I:", LA_F("TCP connection closed by peer\n", LA_F479, 479));
                ctx->state = SIGNAL_RELAY_ERROR;
                return;
            } else {
                if (!P_sock_is_wouldblock()) {
                    print("E:", LA_F("TCP recv error\n", LA_F480, 480));
                    ctx->state = SIGNAL_RELAY_ERROR;
                }
                return;  // 等待下次
            }
        }
    }
}

void p2p_signal_relay_tick_send(struct p2p_session *s) {
    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;
    uint64_t now = P_tick_ms();

    if (ctx->state <= SIGNAL_RELAY_CONNECTING || ctx->sockfd == P_INVALID_SOCKET) {
        return;
    }

    // ====================================================================
    // 推进发送队列
    // ====================================================================
    
    // 如果当前没有正在发送的 chunk，从队列取一个
    if (!ctx->sending) {
        ctx->sending = send_queue_dequeue(ctx);
        if (!ctx->sending) {
            // 队列为空，无需发送
            goto protocol_maintenance;
        }
    }

    // 发送当前 chunk
    p2p_send_chunk_t *chunk = ctx->sending;
    int n = send(ctx->sockfd,
                 (const char *)(chunk->data + chunk->offset),
                 chunk->len - chunk->offset,
                 0);

    if (n > 0) {
        chunk->offset += n;
        if (chunk->offset == chunk->len) {
            // chunk 发送完成，回收到池
            chunk_free(ctx, chunk);
            ctx->sending = NULL;
        }
    } else if (n < 0) {
        if (!P_sock_is_wouldblock()) {
            print("E:", LA_F("TCP send error\n", LA_F503, 503));
            
            // 错误时回收当前 chunk
            chunk_free(ctx, chunk);
            ctx->sending = NULL;
            
            ctx->state = SIGNAL_RELAY_ERROR;
            return;
        }
        // 发送缓冲区满（EAGAIN/EWOULDBLOCK），等待下次
    }

protocol_maintenance:
    // ====================================================================
    // 协议状态维护
    // ====================================================================

    // WAIT_ONLINE_ACK: 检查超时
    if (ctx->state == SIGNAL_RELAY_WAIT_ONLINE_ACK) {
        if (tick_diff(now, ctx->last_send_time) > P2P_RELAY_ONLINE_ACK_TIMEOUT_MS) {
            print("E:", LA_F("ONLINE_ACK timeout\n", LA_F473, 473));
            ctx->state = SIGNAL_RELAY_ERROR;
        }
        return;
    }

    // ONLINE 以上：心跳保活
    if (ctx->state >= SIGNAL_RELAY_ONLINE) {
        if (tick_diff(now, ctx->heartbeat_time) > P2P_RELAY_HEARTBEAT_INTERVAL_MS) {
            send_alive(s);
        }
    }

    // WAIT_CONNECT_ACK: 重传 CONNECT
    if (ctx->state == SIGNAL_RELAY_WAIT_CONNECT_ACK) {
        if (tick_diff(now, ctx->connect_time) > P2P_RELAY_CONNECT_ACK_TIMEOUT_MS) {
            if (ctx->connect_attempts >= P2P_RELAY_CONNECT_RETRY_MAX) {
                print("E:", LA_F("CONNECT timeout, max retries reached\n", LA_F467, 467));
                ctx->state = SIGNAL_RELAY_ERROR;
            } else {
                print("W:", LA_F("CONNECT timeout, retrying (%d/%d)\n", LA_F468, 468),
                      ctx->connect_attempts, P2P_RELAY_CONNECT_RETRY_MAX);
                send_connect(s);
            }
        }
        return;
    }

    // EXCHANGING: 上传候选（Trickle）
    if (ctx->state == SIGNAL_RELAY_EXCHANGING) {
        if (!ctx->local_candidates_fin) {
            // 检查是否有新候选
            if (ctx->next_candidate_index < s->local_cand_cnt) {
                // 攒批发送（避免过于频繁）
                if (tick_diff(now, ctx->trickle_last_time) > P2P_RELAY_TRICKLE_BATCH_MS) {
                    send_peer_info(s, false);
                    ctx->trickle_last_time = now;
                }
            } else if (ctx->next_candidate_index >= s->local_cand_cnt) {
                // 所有候选已发送，发送 FIN
                send_peer_info(s, true);
            }
        }

        // 检查是否完成交换
        if (ctx->local_candidates_fin && ctx->remote_candidates_fin) {
            print("I:", LA_F("READY: candidate exchange completed\n", LA_F474, 474));
            ctx->state = SIGNAL_RELAY_READY;
        }
    }
}

void p2p_signal_relay_trickle_turn(struct p2p_session *s) {
    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    if (ctx->state < SIGNAL_RELAY_EXCHANGING || ctx->local_candidates_fin) {
        return;
    }

    // TURN 候选收集完成，立即发送
    if (ctx->next_candidate_index < s->local_cand_cnt) {
        print("I:", LA_F("Trickle TURN: uploading new candidates\n", LA_F481, 481));
        send_peer_info(s, false);
        ctx->trickle_last_time = P_tick_ms();
    }
}

ret_t p2p_signal_relay_disconnect(struct p2p_session *s) {
    p2p_signal_relay_ctx_t *ctx = &s->sig_relay_ctx;

    if (ctx->sockfd != P_INVALID_SOCKET) {
        P_sock_close(ctx->sockfd);
        ctx->sockfd = P_INVALID_SOCKET;
    }

    // 清理接收缓冲区
    free_recv_payload(ctx);
    
    // 清理发送队列和当前发送的 chunk
    if (ctx->sending) {
        chunk_free(ctx, ctx->sending);
        ctx->sending = NULL;
    }
    send_queue_clear(ctx);
    
    // 销毁 chunk 池（释放所有回收池中的 chunk）
    chunk_pool_destroy(ctx);
    
    ctx->state = SIGNAL_RELAY_INIT;

    return E_NONE;
}

#pragma clang diagnostic pop
