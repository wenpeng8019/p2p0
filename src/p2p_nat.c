/*
 * NAT 穿透实现（纯打洞逻辑）
 *
 * 只负责 PUNCH/ACK/PING/PONG 的发送和接收。
 * 候选列表统一存储在 p2p_session 中，本模块从 session 读取远端候选进行打洞。
 */

#define MOD_TAG "NAT"

#include "p2p_internal.h"
#include "p2p_probe.h"

#define PUNCH_INTERVAL_MS       500         /* 打洞间隔 */
#define PUNCH_TIMEOUT_MS        5000        /* 打洞超时 */
#define CONN_INTERVAL_MS        500         /* CONN 握手间隔 */
#define CONN_TIMEOUT_MS         3000        /* CONN 握手超时 */
#define PING_INTERVAL_MS        5000        /* 心跳间隔（调整为5秒，适配path_manager 10秒超时）*/
#define PONG_TIMEOUT_MS         30000       /* 心跳超时 */
#define REACHING_RELAY_INTERVAL_MS  300     /* reaching 信令中转间隔（0.3秒，避免频繁中转） */

#define TASK_NAT                "NAT"
#define TASK_PATH               "PATH"
#define TASK_ICE_REMOTE         "ICE REMOTE"

/*
 * 查找或添加远端候选（PRFLX 发现）
 *
 * 从已知 PUNCH/PUNCH_ACK 来源地址查找候选，若不存在则作为 PRFLX 添加。
 * 返回候选索引，或负值表示 OOM。
 */
static int upsert_prflx(p2p_session_t *s, const struct sockaddr_in *from) {

    int idx = p2p_find_remote_candidate_by_addr(s, from);
    if (idx >= 0) return idx;

    if (s->cfg.test_ice_prflx_off) {
        print("I:", LA_F("%s: remote %s cand<%s:%d> (disabled)\n", LA_F154, 154),
              TASK_ICE_REMOTE, "prflx", inet_ntoa(from->sin_addr), ntohs(from->sin_port));
        return -1;
    }

    // 新候选（NAT 映射出的 PRFLX）
    idx = p2p_cand_push_remote(s);
    if (idx < 0) {
        print("E:", LA_F("%s: push remote cand<%s:%d> failed(OOM)", LA_F142, 142),
              TASK_NAT, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
        return idx;
    }

    p2p_remote_candidate_entry_t *c = &s->remote_cands[idx];
    c->addr = *from;
    c->type = P2P_CAND_PRFLX;
    c->priority = 0;

    // 收发分离状态初始化
    c->last_punch_send_ms = 0;
    path_stats_init(&c->stats, 0);

    print("I:", LA_F("%s: remote %s cand[%d]<%s:%d> accepted\n", LA_F153, 153),
          TASK_ICE_REMOTE, "prflx", idx, inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port));

    return idx;
}

/*
 * 清理 reaching 队列
 *
 * 释放所有 reaching 队列中的节点内存，并重置队列头尾指针。
 * 在进入 NAT_CLOSED 状态时调用，确保资源正确释放。
 *
 * @param n  NAT 上下文
 */
static void clear_reaching_queue(nat_ctx_t *n) {
    punch_reaching_t *node;
    while (n->reaching_head) {
        node = n->reaching_head;
        n->reaching_head = node->next;
        free(node);
    }
    n->reaching_rear = NULL;
    while(n->reaching_recycle) {
        node = n->reaching_recycle; n->reaching_recycle = node->next;
        free(node);
    }
}

/*
 * 通过候选路径发送数据包
 *
 * 自动处理 TURN relay 和直连的区别。
 *
 * @param s           会话对象
 * @param cand_idx    候选索引（必须 >= 0）
 * @param type        包类型
 * @param flags       包标志
 * @param seq         包序列号
 * @param payload     负载数据
 * @param payload_len 负载长度
 * @param now_ms      当前时间（毫秒）
 * @param rt_track    是否需要 RoundTrip 追踪
 */
static void cand_send_packet(p2p_session_t *s, int cand_idx, uint8_t type, uint16_t seq,
                             const uint8_t *payload, int payload_len, uint64_t now_ms, bool rt_track) {

    assert(cand_idx >= 0 && cand_idx < s->remote_cand_cnt);
    const struct sockaddr_in *addr = &s->remote_cands[cand_idx].addr;

    ret_t ret;
    if (s->remote_cands[cand_idx].type == P2P_CAND_RELAY)
        ret = p2p_turn_send_packet(s, addr, type, 0, seq, payload, payload_len);
    else ret = p2p_udp_send_packet(s, addr, type, 0, seq, payload, payload_len);

    if (ret < 0) {
        print("E:", LA_F("%s: cand[%d]<%s:%d> send packet failed(%d)", LA_F426, 426),
              TASK_NAT, cand_idx, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), ret);
        return;
    }

    /* 统计：流量统计 + 可选 RTT 追踪（PUNCH=true，其他控制包=false） */
    path_manager_on_packet_send(s, cand_idx, seq, now_ms, 0, rt_track);
}

/*
 * 处理所有 reaching 队列（从 writable 路径发送）
 *
 * 收发分离：当某路径收到 PUNCH 但该路径不可写时，需要从其他 writable 路径发送 REACH。
 * 此函数遍历 reaching 队列，通过指定的 writable 路径发送所有 REACH。
 *
 * @param s              会话对象
 * @param writable_path  可写路径索引（必须 >= 0）
 * @param now_ms         当前时间（毫秒）
 */
static void flush_reaching_queue(p2p_session_t *s, int writable_path, uint64_t now_ms) {
    nat_ctx_t *n = &s->nat;

    punch_reaching_t *node;
    while (n->reaching_head) {

        node = n->reaching_head;
        n->reaching_head = node->next;
        if (!n->reaching_head) n->reaching_rear = NULL;

        // 构造 REACH 负载：回显 target_addr
        uint8_t ack_payload[6];
        memcpy(ack_payload, &node->target.sin_addr.s_addr, 4);
        memcpy(ack_payload + 4, &node->target.sin_port, 2);

        if (writable_path == PATH_IDX_SIGNALING) { assert(s->signaling_relay_fn);

            s->signaling_relay_fn(s, P2P_PKT_REACH, 0, node->seq, ack_payload, sizeof(ack_payload));

            print("V:", LA_F("%s: reaching cand[%d] via signaling relay, seq=%u", LA_F428, 428),
                  TASK_NAT, node->cand_idx, node->seq);
        } 
        else {
            cand_send_packet(s, writable_path, P2P_PKT_REACH, node->seq, ack_payload, 6, now_ms, false);

            const struct sockaddr_in *addr = &s->remote_cands[writable_path].addr;
            print("V:", LA_F("%s: reaching cand[%d] via path[%d] to %s:%d, seq=%u", LA_F149, 149),
                  TASK_NAT, node->cand_idx, writable_path,
                  inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), node->seq);
        }

        free(node);
    }
    while(n->reaching_recycle) {
        node = n->reaching_recycle; n->reaching_recycle = node->next;
        free(node);
    }
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 向指定候选发送打洞包
 *
 * 协议：P2P_PKT_PUNCH (0x01)
 * 包头: [type=0x01 | flags=0 | seq=发送方序列号(2B)]
 * 负载: [target_addr(4B, network order) | target_port(2B, network order)]
 *
 * PUNCH 携带目标地址，接收方在 PUNCH_ACK 中回显。
 * 发送方通过 PUNCH_ACK 确认特定出口路径是 writable。
 */
static void nat_send_punch(p2p_session_t *s, const char *reason,
                           p2p_remote_candidate_entry_t *entry, uint64_t now) {
    const char* PROTO = "PUNCH";
    nat_ctx_t *n = &s->nat;

    if (++n->punch_seq == 0) n->punch_seq = 1;  // 跳过 seq=0 的情况

    int send_path = (int)(entry - s->remote_cands);

    // 状态: INIT → PROBING（首次发送 PUNCH 时进入探测状态）
    if (entry->stats.state == PATH_STATE_INIT) {
        entry->stats.state = PATH_STATE_PROBING;
    }

    // 构造负载: [target_addr(4B) | target_port(2B)]
    uint8_t payload[6];
    memcpy(payload, &entry->addr.sin_addr.s_addr, 4);  // network order
    memcpy(payload + 4, &entry->addr.sin_port, 2);     // network order

    // PUNCH 是唯一需要 per-packet RTT 的 NAT 控制包，rt_track=true
    cand_send_packet(s, send_path, P2P_PKT_PUNCH, n->punch_seq,
                     payload, sizeof(payload), now, true);

    print("V:", LA_F("%s sent to %s:%d for %s, seq=%d, path=%d", LA_F54, 54),
          PROTO, inet_ntoa(entry->addr.sin_addr), ntohs(entry->addr.sin_port),
          reason, n->punch_seq, send_path);

    entry->last_punch_send_ms = now;
}

/*
 * 向对端发送 CONN 包（数据层连接握手）
 *
 * 协议：P2P_PKT_CONN (0x03)
 * 包头: [type=0x03 | flags=0 | seq=conn_seq(2B)]
 * 负载: 无
 *
 * 在双向连通确认后（rx_confirmed && tx_confirmed）发送，
 * 通知对端可以开始数据传输。
 */
static void nat_send_conn(p2p_session_t *s, uint64_t now) {
    const char* PROTO = "CONN";
    nat_ctx_t *n = &s->nat;

    // 使用统一发送接口（自动处理 TURN/信令中转/直连）
    p2p_send_packet(s, &s->active_addr, P2P_PKT_CONN, 0, 0, NULL, 0, now);

    if (s->path_type == P2P_PATH_SIGNALING) {
        print("V:", LA_F("%s sent via signaling relay", LA_F427, 427), PROTO);
    } else {
        print("V:", LA_F("%s sent to %s:%d", LA_F56, 56),
              PROTO, inet_ntoa(s->active_addr.sin_addr), ntohs(s->active_addr.sin_port));
    }

    n->last_conn_send_ms = now;
}

static void nat_send_conn_ack(p2p_session_t *s, uint64_t now) {
    const char* PROTO = "CONN_ACK";
    
    if (instrument_option(P2P_INST_OPT_NAT_CONN_ACK_OFF)) return;

    // 使用统一发送接口（自动处理 TURN/信令中转/直连）
    p2p_send_packet(s, &s->active_addr, P2P_PKT_CONN_ACK, 0, 0, NULL, 0, now);

    if (s->path_type == P2P_PATH_SIGNALING) {
        print("V:", LA_F("%s sent via signaling relay", LA_F427, 427), PROTO);
    } else {
        print("V:", LA_F("%s sent to %s:%d", LA_F56, 56),
              PROTO, inet_ntoa(s->active_addr.sin_addr), ntohs(s->active_addr.sin_port));
    }
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 初始化打洞上下文
 */
void nat_init(nat_ctx_t *n) {
    memset(n, 0, sizeof(*n));
    n->state = NAT_INIT;
}

void nat_reset(nat_ctx_t *n) {

    // 清空 reaching 队列
    clear_reaching_queue(n);
    nat_init(n);
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 执行双向确认后的状态转换
 *
 * 当 rx_confirmed 和 tx_confirmed 都为 true 时调用此函数，
 * 执行进入 NAT_CONNECTING 状态的操作。
 *
 * 前置条件（由调用方保证）：
 *   - n->state == NAT_PUNCHING
 *   - s->rx_confirmed && s->tx_confirmed
 *
 * @param s          会话对象
 * @param path_idx   路径索引（-1=SIGNALING, >=0=候选）
 * @param now        当前时间戳
 * @param reason     日志原因（如 "batch relay", "trickle relay", "relay+punch"）
 */
static void bidirectional_confirmed(p2p_session_t *s, int cand_path, uint64_t now, const char *reason) {
    nat_ctx_t *n = &s->nat;
    
    // 设置活跃路径（用于 nat_send_conn）
    p2p_set_active_path(s, cand_path);

    nat_send_conn(s, now);

    // 双向确认完成，进入 CONNECTING 状态
    n->state = NAT_CONNECTING;
    n->conn_start_ms = now;
    
    print("I:", LA_F("%s: PUNCHING → CONNECTING (%s%s)", LA_F102, 102),
          TASK_NAT, reason, cand_path == PATH_IDX_SIGNALING ? ", signaling" : "");
}

/*
 * 处理 relay 候选
 *
 * relay 候选不需要打洞，直接激活路径。首次激活时设置 tx_confirmed 并检查双向确认。
 *
 * @param s         会话对象
 * @param cand_idx  relay 候选索引
 * @param now       当前时间戳
 * @param reason    日志原因（如 "batch relay", "trickle relay"）
 */
static void relay_confirmed(p2p_session_t *s, int cand_idx, uint64_t now, const char *reason) {

    nat_ctx_t *n = &s->nat;
    
    // relay 候选：激活路径
    path_manager_set_path_state(s, cand_idx, PATH_STATE_ACTIVE);
    
    // 首次设置 tx_confirmed（从 false → true）
    if (!s->tx_confirmed) { s->tx_confirmed = true;

        print("I:", LA_F("%s: path[%d] relay UP", LA_F152, 152), 
              TASK_PATH, cand_idx);
        
        // 首次确认 tx，flush reaching queue
        flush_reaching_queue(s, cand_idx, now);
        
        // 检查双向确认：
        //   正常场景：批量模式首次启动，rx_confirmed 为 false，不会触发
        //   特殊场景：对端先启动，本端在 nat_on_punch() 中已设置 rx_confirmed=true，会触发
        // 
        // 注意：即使触发了双向确认进入 CONNECTING，也不跳出循环，继续向剩余候选发送 PUNCH
        // 目的：发现所有可能路径、测量所有 RTT、提供路径冗余
        if (s->rx_confirmed && n->state == NAT_PUNCHING) {
            bidirectional_confirmed(s, cand_idx, now, reason);
        }
    }
}

/*
 * NAT 打洞
 * + 这是针对模块外部的打洞接口，受到 NAT 状态机的约束
 *   并会根据不同状态执行不同的业务逻辑
 *
 * 此外，对于模块内部的打洞调用，例如 keep-alive
 * 则直接调用 nat_send_punch，不受 NAT 状态机约束
 *
 * @param s        会话对象
 * @param idx      目标候选索引（-1=批量启动所有候选，>=0=单个候选打洞）
 *
 * 用法：
 *   - nat_punch(s, -1)      批量启动所有 remote_cands 的打洞
 *   - nat_punch(s, idx)     向单个候选追加打洞（Trickle ICE）
 */
ret_t nat_punch(p2p_session_t *s, int idx) {

    P_check(s != NULL, return E_INVALID;)
    
    nat_ctx_t *n = &s->nat;

    /* ========== 首次批量或重新启动模式：idx == -1 ========== */

    if (idx == -1) {

        // 批量打洞只允许首次启动（INIT 或 CLOSED 状态）
        if (n->state >= NAT_PUNCHING) {
            print("V:", LA_F("%s: batch punch skip (state=%d, use trickle)", LA_F98, 98), TASK_NAT, n->state);
            return E_NONE_CONTEXT;
        }
        
        n->state = NAT_PUNCHING;
        n->punch_start = P_tick_ms();
        
        // 这里只重置 tx_confirmed，保留 rx_confirmed 原值（需要考虑对端先启动打洞的场景）
        s->tx_confirmed = false;
        
        if (s->remote_cand_cnt == 0) {
            print("W:", LA_F("%s: batch punch: no cand, wait trickle", LA_F126, 126), TASK_NAT);
        }

        print("I:", LA_F("%s: batch punch start (%d cands)", LA_F173, 173), TASK_NAT, s->remote_cand_cnt);

        // 遍历所有候选：relay 直接激活路径，所有候选都发送 PUNCH 包（用于双向确认和 RTT 测量）
        for (int i = 0; i < s->remote_cand_cnt; i++) {

            print("V:", LA_F("%s: punch cand[%d] %s:%d (%s)", LA_F140, 140), TASK_NAT, i,
                  inet_ntoa(s->remote_cands[i].addr.sin_addr), ntohs(s->remote_cands[i].addr.sin_port),
                  p2p_candidate_type_str((p2p_cand_type_t)s->remote_cands[i].type));

            // relay 候选：直接激活路径（不需要打洞），但也发 PUNCH 
            // + 一个情况是，如果双方都只有 relay 候选，则需要一个 PUNCH 包开启双向确认，此外也可用于 RTT 测量
            if (s->remote_cands[i].type == P2P_CAND_RELAY) {
                relay_confirmed(s, i, n->punch_start, "batch relay");
            }
            
            // 所有候选（包括 relay）都发送 PUNCH 包
            nat_send_punch(s, LA_W("punch", LA_W10, 10), &s->remote_cands[i], n->punch_start);
        }
        
        return E_NONE;
    }
    
    /* ========== Trickle 单候选打洞模式：idx >= 0 ========== */
    
    if (idx < 0 || idx >= s->remote_cand_cnt) {
        print("E:", LA_F("%s: invalid cand idx: %d (count: %d)", LA_F119, 119), TASK_NAT, idx, s->remote_cand_cnt);
        return E_OUT_OF_RANGE;
    }

    uint64_t now = P_tick_ms();
    p2p_remote_candidate_entry_t *entry = &s->remote_cands[idx];

    // 首次或重新启动时初始化状态（INIT/CLOSED）
    if (n->state < NAT_PUNCHING) {
        n->state = NAT_PUNCHING;
        n->punch_start = now;

        // 只重置 tx_confirmed，保留 rx_confirmed 原值（需要考虑对端先启动打洞的场景）
        s->tx_confirmed = false;

        print("I:", LA_F("%s: trickle punch start", LA_F174, 174), TASK_NAT);
    }

    print("I:", LA_F("%s: punch cand[%d] %s:%d (%s)", LA_F140, 140), TASK_NAT, idx,
          inet_ntoa(entry->addr.sin_addr), ntohs(entry->addr.sin_port),
          p2p_candidate_type_str((p2p_cand_type_t)entry->type));

    // 根据候选类型处理
    if (entry->type == P2P_CAND_RELAY) {
        relay_confirmed(s, idx, now, "trickle relay");
    }
    
    // 所有候选（包括 relay）都发送 PUNCH 包（用于双向确认和 RTT 测量）
    nat_send_punch(s, LA_W("punch", LA_W10, 10), entry, now);

    return E_NONE;
}

/*
 * 发送 FIN 包通知对端主动断开连接
 *
 * 协议：P2P_PKT_FIN (0x30)
 * 包头: [type=0x30 | flags=0 | seq=0]
 * 负载: 无
 */
void nat_send_fin(p2p_session_t *s) {
    const char* PROTO = "FIN";

    if (s->nat.state < NAT_LOST) {
        print("E:", LA_F("%s: not connected, cannot send FIN", LA_F129, 129), PROTO);
        return;
    }

    if (s->active_path < 0) return;
    assert(s->path_type != P2P_PATH_SIGNALING);

    uint64_t now = P_tick_ms();
    cand_send_packet(s, s->active_path, P2P_PKT_FIN, 0, NULL, 0, now, false);

    print("V:", LA_F("%s sent to %s:%d", LA_F56, 56),
          PROTO, inet_ntoa(s->active_addr.sin_addr), ntohs(s->active_addr.sin_port));

    for (int i = 2; i--;) {
        P_usleep(50 * 1000); // 50ms 间隔重发
        cand_send_packet(s, s->active_path, P2P_PKT_FIN, 0, NULL, 0, now, false);
    }
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 处理 STUN 包（ICE connectivity check 响应）
 *
 * 注意事项：
 *   - ICE connectivity check 是双向的：
 *     * 收到 Binding Request（含 PRIORITY）→ rx_confirmed = true
 *     * 收到 Binding Response（含 XOR-MAPPED-ADDRESS）→ tx_confirmed = true
 *   - 双向确认后（rx + tx），NAT 状态机会在 nat_tick() 中转换为 NAT_CONNECTED
 */
void nat_on_stun_packet(struct p2p_session *s, const struct sockaddr_in *from,
                       uint64_t now, uint16_t msg_type, const uint8_t *buf, int len) {

    // 前提是包含 ICE 属性
    assert(p2p_stun_has_ice_attrs(buf, len));

    // 获取候选路径，或添加为 peer-reflexive 候选（ICE 标准支持自动添加）
    int path_idx = upsert_prflx(s, from);
    if (path_idx < 0) {
        print("W:", LA_F("Recv ICE-STUN from %s:%d, upsert prflx failed", LA_F544, 544),
              inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    }

    nat_ctx_t *n = &s->nat;

    // Binding Request（对端发来的 connectivity check）
    if (msg_type == 0x0001) {

        printf(LA_F("Recv ICE-STUN Binding Request from candidate %d (%s:%d)", LA_F422, 422),
               path_idx, inet_ntoa(from->sin_addr), ntohs(from->sin_port));

        // 如果使用 ICE 协议打洞
        if (s->cfg.use_ice) {

            // 回复 Binding Response（RFC 8445 Section 7.3.1.4）
            // 事务 ID 从请求包 buf[8..19] 复制，XOR-MAPPED-ADDRESS 填写对端观测地址
            uint8_t resp[128];
            int resp_len = p2p_stun_build_binding_response(resp, sizeof(resp), buf + 8, from, NULL);
            if (resp_len > 0) {
                p2p_udp_send_to(s, from, resp, resp_len);
                path_manager_on_packet_send(s, path_idx, 0, now, 0, false);
            }
        }

        // 控制包不计入流量统计（size=0），但更新路径活跃时间、包计数和重置超时计数器
        path_manager_on_packet_recv(s, path_idx, now, len, false, 0);

        // 标记 peer→me 方向 打通
        if (s->rx_confirmed) return;
        s->rx_confirmed = true;
        print("I:", LA_F("%s: path rx UP (%s:%d)", LA_F162, 162),
              TASK_NAT, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    }
    // Binding Response（对端回复我们的 connectivity check）
    else if (msg_type == 0x0101) {
        
        printf(LA_F("Recv ICE-STUN Binding Response from candidate %d (%s:%d)", LA_F547, 547),
               path_idx, inet_ntoa(from->sin_addr), ntohs(from->sin_port));

        // ACK 控制包不计入流量统计（size=0），这里需要进行 RoundTrip 测量
        path_manager_on_packet_recv(s, path_idx, now, 0, true, 0);

        // 如果该路径之前已激活，只更新统计即可，直接返回
        if (path_is_selectable(s->remote_cands[path_idx].stats.state)) return;

        // 首次激活路径（INIT/PROBING/FAILED → ACTIVE），收到 REACH 证明路径可写
        path_manager_set_path_state(s, path_idx, PATH_STATE_ACTIVE);

        print("I:", LA_F("%s: path[%d] UP (%s:%d)", LA_F132, 132),
            TASK_PATH, path_idx, inet_ntoa(from->sin_addr), ntohs(from->sin_port));

         // NAT_RELAY 状态下 retry 打洞成功：直接升级到 CONNECTED（保持数据层一致性）
        // + p2p_update 会对该改状态的变更进行同步
        if (n->state == NAT_RELAY) {
            print("I:", LA_F("%s: RELAY → CONNECTED (direct path recovered)", LA_F77, 77), TASK_NAT);

            n->state = NAT_CONNECTED;
            n->last_keepalive_send_ms = now;
            return;
        }
        // 只有 PUNCHING 状态才需要双向确认逻辑
        // LOST 已建立过连接，不需要双向确认（由上层决定恢复策略）
        if (n->state != NAT_PUNCHING) return;

        // ---------- 双向确认逻辑 ----------

        // peer→me 方向：收到 PUNCH_ACK 也证明入方向通（对方能发包给我们）
        if (!s->rx_confirmed) { s->rx_confirmed = true;

            print("I:", LA_F("%s: path rx UP (%s:%d)", LA_F162, 162),
                TASK_NAT, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
        }

        // me→peer 方向：收到 REACH 证明我们的 PUNCH 到达了对方
        if (!s->tx_confirmed) { s->tx_confirmed = true;

            print("I:", LA_F("%s: path tx UP", LA_F549, 549),
                TASK_NAT);

            // 首次确认 tx_confirmed，处理所有 reaching 队列（此后不会再有新的 pending）
            flush_reaching_queue(s, path_idx, now);
        }

        // 对于首个打通的路径，直接（硬切）作为 p2p 活跃路径
        // 注意：使用 path_idx 而不是 cand_idx，因为 path_idx 才是被确认可写的路径
        p2p_set_active_path(s, path_idx);

        n->state = NAT_CONNECTED;
        n->last_keepalive_send_ms = now;
        p2p_connected(s, now);

        print("I:", LA_F("%s: PUNCHING → %s", LA_F103, 103), TASK_NAT, "CONNECTED");
    }
    else print("W:", LA_F("Recv unknown ICE-STUN msg_type=0x%04x from %s:%d", LA_F550, 550),
               msg_type, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
}

/*
 * 处理 PUNCH 包
 *
 * 协议：P2P_PKT_PUNCH (0x01)
 * 负载: [target_addr(4B) | target_port(2B)]
 *   target_addr 是发送方发送 PUNCH 的目标地址，接收方在 PUNCH_ACK 中回显。
 *
 * 流程:
 *   1. 标记 peer→me 方向打通，即 rx_confirmed（能收到说明路径入方向通）
 *   2. 解析 target_addr，并通过发送 REACH 包 echo 回显给对端（告诉对方它走的哪个地址是通的）
 *
 * 注意：在任何状态下都可以设置 rx_confirmed（包括 NAT_INIT）
 * 场景：对端先启动打洞，本端还在 INIT 状态就收到 PUNCH
 *      此时设置 rx_confirmed=true，等待本端调用 nat_punch() 启动时保留此状态
 *
 */
void nat_on_punch(p2p_session_t *s, const p2p_packet_hdr_t *hdr,
                  const uint8_t *payload, int payload_len,
                  const struct sockaddr_in *from, uint64_t now) {
    const char* PROTO = "PUNCH";

    printf(LA_F("Recv %s pkt from %s:%d seq=%u", LA_F307, 307),
          PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), hdr->seq);

    // 校验负载长度
    if (payload_len < 6) {
        print("W:", LA_F("%s: invalid payload len=%d (need 6)", LA_F419, 419), PROTO, payload_len);
        return;
    }

    nat_ctx_t *n = &s->nat;

    // 更新最后接收时间
    n->last_recv_time = now;

    // punch 包肯定不会来自信令服务器
    assert(!sockaddr_equal(from, &s->signaling.addr));
    int cand_idx = upsert_prflx(s, from);
    if (cand_idx < 0) return;

    // 解析负载中的 target_addr
    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    memcpy(&target_addr.sin_addr.s_addr, payload, 4);
    memcpy(&target_addr.sin_port, payload + 4, 2);

    print("V:", LA_F("%s: accepted as cand[%d], target=%s:%d", LA_F179, 179),
          PROTO, cand_idx, inet_ntoa(target_addr.sin_addr), ntohs(target_addr.sin_port));

    // ---------- 发送 REACH（收发分离策略） ----------
    {   const char* PROTO2 = "REACH";

        // 构造 REACH 负载：回显 target_addr
        uint8_t ack_payload[6];
        memcpy(ack_payload, &target_addr.sin_addr.s_addr, 4);
        memcpy(ack_payload + 4, &target_addr.sin_port, 2);

        // 来源路径已激活，直接原路回复
        if (!instrument_option(P2P_INST_OPT_NAT_REACH_BACKWARD_OFF)
            && path_is_selectable(s->remote_cands[cand_idx].stats.state)) {

            cand_send_packet(s, cand_idx, P2P_PKT_REACH, hdr->seq, ack_payload, 6, now, false);
            print("V:", LA_F("%s sent to %s:%d (writable), echo_seq=%u", LA_F53, 53),
                  PROTO2, inet_ntoa(from->sin_addr), ntohs(from->sin_port), hdr->seq);
        }
        else {
            // 来源路径不可写，尝试找其他可写候选路径
            // 注意：这里不能使用信令路径（PATH_IDX_SIGNALING），只能使用候选数组中的 p2p 路径
            int best_path = path_manager_select_best_path(s);
            if (!instrument_option(P2P_INST_OPT_NAT_REACH_FORWARD_OFF)
                && (!instrument_option(P2P_INST_OPT_NAT_REACH_BACKWARD_OFF) || best_path != cand_idx)
                && best_path >= 0 && path_is_selectable(s->remote_cands[best_path].stats.state)) {

                cand_send_packet(s, best_path, P2P_PKT_REACH, hdr->seq, ack_payload, 6, now, false);
                print("V:", LA_F("%s sent via best path[%d] to %s:%d, echo_seq=%u", LA_F57, 57),
                      PROTO2, best_path,
                      inet_ntoa(s->remote_cands[best_path].addr.sin_addr),
                      ntohs(s->remote_cands[best_path].addr.sin_port), hdr->seq);
            }
            // 没有可用的 writable 候选路径
            else {

                // 尝试原路发送
                if (!instrument_option(P2P_INST_OPT_NAT_REACH_BACKWARD_OFF)) {
                    cand_send_packet(s, cand_idx, P2P_PKT_REACH, hdr->seq, ack_payload, 6, now, false);
                    print("V:", LA_F("%s_ACK sent to %s:%d (try), echo_seq=%u", LA_F195, 195),
                          PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), hdr->seq);
                }

                // 缓存 reaching，等待冷打洞策略处理
                // 1. 去重：同一 target_addr 只保留最新 seq
                // 2. 优先级排序：按候选优先级插入队列（高优先级在前）
                if (!instrument_option(P2P_INST_OPT_NAT_REACH_FORWARD_OFF)) {

                    // 查找是否已存在相同 target_addr 的节点
                    punch_reaching_t *existing = NULL;
                    for (punch_reaching_t *p = n->reaching_head; p != NULL; p = p->next) {
                        if (sockaddr_equal(&p->target, &target_addr)) {
                            existing = p;
                            break;
                        }
                    }
                    
                    // 按优先级插入（高优先级在前）
                    if (!existing) {
                        punch_reaching_t *node = n->reaching_recycle;
                        if (node) n->reaching_recycle = node->next;
                        else { node = (punch_reaching_t *)malloc(sizeof(punch_reaching_t));
                            if (!node) {
                                print("E:", LA_F("%s: reaching alloc OOM", LA_F143, 143), TASK_NAT);
                                return;
                            }
                        }
                        
                        node->seq = hdr->seq;
                        node->target = target_addr;
                        node->cand_idx = cand_idx;
                        node->next = NULL;
                        
                        // 获取当前候选的优先级
                        uint32_t priority = s->remote_cands[cand_idx].priority;
                        
                        // 按优先级插入（降序：高优先级在前）
                        punch_reaching_t *prev_insert = NULL;
                        punch_reaching_t *curr = n->reaching_head;
                        while (curr != NULL && s->remote_cands[curr->cand_idx].priority >= priority) {
                            prev_insert = curr;
                            curr = curr->next;
                        }
                        
                        // 插入节点
                        if (prev_insert == NULL) { // 插入队头
                            node->next = n->reaching_head;
                            n->reaching_head = node;
                            if (!n->reaching_rear) {
                                n->reaching_rear = node;  // 队列之前为空
                            }
                        } else { // 插入中间或队尾
                            node->next = prev_insert->next;
                            prev_insert->next = node;
                            if (node->next == NULL) {
                                n->reaching_rear = node;  // 更新队尾
                            }
                        }
                        
                        print("V:", LA_F("%s: reaching enqueued: cand[%d], seq=%u, priority=%u", LA_F145, 145),
                            TASK_NAT, cand_idx, hdr->seq, priority);
                    }
                    // 更新为最新的 seq
                    else if (uint16_circle_newer(hdr->seq, existing->seq)) {

                        print("V:", LA_F("%s: reaching updated: cand[%d], seq=%u->%u", LA_F148, 148),
                                TASK_NAT, cand_idx, existing->seq, hdr->seq);

                        existing->seq = hdr->seq;
                    }

                } // if (!instrument_option(P2P_INST_OPT_NAT_REACH_FORWARD_OFF))
            }
        }
    }

    // PUNCH 控制包不计入流量统计（size=0），但更新路径活跃时间、包计数和重置超时计数器
    path_manager_on_packet_recv(s, cand_idx, now, 0, false, 0);

    // 标记 peer→me 方向 打通
    if (s->rx_confirmed) return;
    s->rx_confirmed = true;
    print("I:", LA_F("%s: path rx UP (%s:%d)", LA_F162, 162),
            TASK_NAT, inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    // 检查双向确认：
    // + relay 候选会导致 tx_confirmed=true；但肯定还未收到 REACH，因为任何入方式的包都会导致 rx_confirmed=true
    if (s->tx_confirmed && n->state == NAT_PUNCHING) {
        bidirectional_confirmed(s, cand_idx, now, "relay + on punch");
    }
}

/*
 * 处理 REACH 包（PUNCH 到达确认）
 *
 * 收到 REACH 证明 me→peer 方向联通，且 seq 用于精确 RTT 测量。
 *
 * 协议：P2P_PKT_REACH (0x02)
 * 负载: [target_addr(4B) | target_port(2B)]
 *   target_addr 是我方发送 PUNCH 时携带的目标地址，被对方回显。
 *   通过匹配 target_addr 可以确认特定出口路径是 writable。
 */
void nat_on_reach(p2p_session_t *s, const p2p_packet_hdr_t *hdr,
                  const uint8_t *payload, int payload_len,
                  const struct sockaddr_in *from, uint64_t now) {
    const char* PROTO = "REACH";

    // 校验负载长度：标准格式 6B (target_addr)
    if (payload_len < 6) {
        print("W:", LA_F("%s: bad payload(%d)", LA_F121, 121), 
              PROTO, payload_len);
        return;
    }

    nat_ctx_t *n = &s->nat;

    printf(LA_F("Recv %s pkt from %s:%d echo_seq=%u", LA_F306, 306),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), hdr->seq);

    // 更新最后接收时间
    n->last_recv_time = now;

    // 检查是否是信令服务器转发的 REACH（from 是信令服务器地址）
    // + 信令服务器不应作为 PRFLX 候选添加
    int path_idx = -1;
    if (!sockaddr_equal(from, &s->signaling.addr)) {
        path_idx = upsert_prflx(s, from);
        if (path_idx < 0) {
            print("W:", LA_F("%s: ignored, upsert %s:%d failed", LA_F437, 437),
                  PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
            return;
        }
    }

    // ---------- 解析负载中的 target_addr ----------
    // target_addr 是我方发送 PUNCH 时的目标地址，现在被回显
    // 找到对应的 candidate 并标记为 writable
    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    memcpy(&target_addr.sin_addr.s_addr, payload, 4);
    memcpy(&target_addr.sin_port, payload + 4, 2);

    // 查找匹配 target_addr 的 candidate
    int target_path = p2p_find_remote_candidate_by_addr(s, &target_addr);
    if (target_path < 0) {
        print("W:", LA_F("%s: unknown target cand %s:%d", LA_F443, 443),
              PROTO, inet_ntoa(target_addr.sin_addr), ntohs(target_addr.sin_port));
        return;
    }

    // ========== RTT 测量（混合模式，区分原路/跨路径） ==========
    // 
    // 关键逻辑：
    //   - target_path：我方发送 PUNCH 的路径（从 target_addr 解析）
    //   - cand_idx：   对端返回 REACH 的路径（from 地址）
    // 
    // 原路返回（target_path == cand_idx）：
    //   PUNCH(A) → [NAT] → peer → [NAT] → REACH(A)
    //   测量的是单条路径 A 的真实 RTT，记录到 probe_rtt_direct
    // 
    // 跨路径返回（target_path != cand_idx）：
    //   PUNCH(A) → [NAT] → peer → [NAT] → REACH(B)
    //   测量的是路径 A 出 + 路径 B 入的组合延迟（冷打洞单向网络场景）
    //   这不是路径 A 的真实 RTT，仅作为初步参考，记录到 probe_rtt_cross
    //   路径选择时会降权处理（+30% 惩罚）
    // 
    // 只有直连路径才需要 RTT 测量和统计更新（信令转发的 REACH 不更新 cand_idx 统计）
    if (path_idx >= 0) {
        // PUNCH_ACK 控制包不计入流量统计（size=0），hdr->seq > 0 时完成 RoundTrip 测量
        path_manager_on_packet_recv(s, path_idx, now, 0, hdr->seq > 0, hdr->seq);
    }

    // 如果该路径之前已激活，只更新统计即可，直接返回
    if (path_is_selectable(s->remote_cands[target_path].stats.state)) return;

    // 首次激活路径（INIT/PROBING/FAILED → ACTIVE），收到 REACH 证明路径可写
    path_manager_set_path_state(s, target_path, PATH_STATE_ACTIVE);

    print("I:", LA_F("%s: path[%d] UP (%s:%d)", LA_F132, 132),
          TASK_PATH, target_path, inet_ntoa(target_addr.sin_addr), ntohs(target_addr.sin_port));

    // NAT_RELAY 状态下 retry 打洞成功：直接升级到 CONNECTED（保持数据层一致性）
    // + p2p_update 会对该改状态的变更进行同步
    if (n->state == NAT_RELAY) {
        print("I:", LA_F("%s: RELAY → CONNECTED (direct path recovered)", LA_F77, 77), TASK_NAT);

        n->state = NAT_CONNECTED;
        n->last_keepalive_send_ms = now;
        return;
    }

    // 只有 PUNCHING 状态才需要双向确认逻辑
    // LOST 已建立过连接，不需要双向确认（由上层决定恢复策略）
    if (n->state != NAT_PUNCHING) return;

    // ---------- 双向确认逻辑 ----------

    // peer→me 方向：收到 PUNCH_ACK 也证明入方向通（对方能发包给我们）
    if (!s->rx_confirmed) { s->rx_confirmed = true;

        print("I:", LA_F("%s: path rx UP (%s:%d)", LA_F162, 162),
              TASK_NAT, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    }

    // me→peer 方向：收到 REACH 证明我们的 PUNCH 到达了对方
    if (!s->tx_confirmed) { s->tx_confirmed = true;

        print("I:", LA_F("%s: path tx UP (echo seq=%u)", LA_F190, 190),
              TASK_NAT, hdr->seq);

        // 首次确认 tx_confirmed，处理所有 reaching 队列（此后不会再有新的 pending）
        flush_reaching_queue(s, target_path, now);
    }

    // 双向均确认 → NAT_CONNECTING（开始数据层握手）

    // 如果已确认对方进入了 CONNECTING，则自己直接进入 CONNECTED
    // + 此时无需再向对方发送 CONN 包，直接回复 CONN_ACK 确认即可
    if (n->peer_connecting) {

        n->peer_connecting = false;
        nat_send_conn_ack(s, now);

        // 对于首个打通的路径，直接（硬切）作为 p2p 活跃路径
        // 注意：使用 target_path 而不是 cand_idx，因为 target_path 才是被确认可写的路径
        p2p_set_active_path(s, target_path);

        n->state = NAT_CONNECTED;
        n->last_keepalive_send_ms = now;
        p2p_connected(s, now);

        print("I:", LA_F("%s: PUNCHING → %s (peer CONNECTING)", LA_F548, 548), TASK_NAT, "CONNECTED");
    }
    // 正常流程：进入 CONNECTING，发送 CONN
    // 注意：使用 target_path 而不是 cand_idx，因为 target_path 才是被确认可写的路径
    else bidirectional_confirmed(s, target_path, now, "punch+reach");
}

/*
 * 处理 CONN 包（数据层连接请求）
 *
 * 协议：P2P_PKT_CONN (0x03)
 * 包头: [type=0x03 | flags=0 | seq=对方conn_seq(2B)]
 * 负载: 无
 * 
 * 收到 CONN 后，立即回复 CONN_ACK 并进入 NAT_CONNECTED 状态。
 */
void nat_on_conn(struct p2p_session *s, const p2p_packet_hdr_t *hdr,
                 const struct sockaddr_in *from, uint64_t now) {
    const char* PROTO = "CONN";
    nat_ctx_t *n = &s->nat;

    printf(LA_F("Recv %s pkt from %s:%d seq=%u", LA_F307, 307),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), hdr->seq);

    // 更新最后接收时间
    n->last_recv_time = now;

    // 检查是否是信令服务器转发的 CONN，即通过信令服务中转连接（from 是信令服务器地址）
    // + 信令服务器不应作为 PRFLX 候选添加
    int path_idx = -1;
    if (!sockaddr_equal(from, &s->signaling.addr)) {
        path_idx = upsert_prflx(s, from);
        if (path_idx < 0) {
            print("W:", LA_F("%s: CONN ignored, upsert %s:%d failed", LA_F435, 435), 
                  PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
            return;
        }
    }

    // CONN 控制包不计入流量统计（size=0），但更新路径活跃时间、包计数和重置超时计数器
    path_manager_on_packet_recv(s, path_idx, now, 0, false, 0);

    // 如果本端还未进入 NAT_CONNECTING 状态，标记对方已经进入 CONNECTING 状态
    // + 此时需要等到本端进入 connected 后再回复 CONN_ACK，因为对方收到 CONN_ACK 后会立刻进入 CONNECTED 状态
    if (n->state < NAT_CONNECTING) {
        n->peer_connecting = true;
        print("V:", LA_F("%s: recorded peer conn_seq=%u for future CONN_ACK", LA_F151, 151),
              PROTO, hdr->seq);
        return;
    }

    // 进入 NAT_CONNECTING 状态后收到 CONN，立即回复 CONN_ACK
    // + 注意，本端此时可能已经是 NAT_CONNECTED 状态，因为如果本端是先发送的 CONN
    //   那么对端对本地的 CONN_ACK 可能会先于对端自己的 CONN 到达，
    //   而本端在收到自己的 CONN_ACK 后会直接进入 NAT_CONNECTED 状态
    nat_send_conn_ack(s, now);

    if (n->state == NAT_CONNECTING) { assert(!n->peer_connecting);
        n->state = s->active_path < 0 ? NAT_RELAY : NAT_CONNECTED;
        n->last_keepalive_send_ms = now;
        p2p_connected(s, now);
        print("I:", LA_F("%s: CONNECTING → %s (recv CONN)", LA_F72, 72), TASK_NAT, n->state == NAT_RELAY ? "RELAY" : "CONNECTED");
    }
}

/*
 * 处理 CONN_ACK 包（数据层连接确认）
 *
 * 协议：P2P_PKT_CONN_ACK (0x04)
 * 包头: [type=0x04 | flags=0 | seq=echo conn_seq(2B)]
 * 负载: 无
 * 
 * 收到 CONN_ACK 后，停止发送 CONN，进入 NAT_CONNECTED 状态。
 */
void nat_on_conn_ack(struct p2p_session *s, const p2p_packet_hdr_t *hdr,
                     const struct sockaddr_in *from, uint64_t now) {
    const char* PROTO = "CONN_ACK";
    nat_ctx_t *n = &s->nat;

    if (n->state < NAT_CONNECTING) {
        print("E:", LA_F("Ignore %s pkt from %s:%d, not connecting", LA_F421, 421), PROTO,
              inet_ntoa(from->sin_addr), ntohs(from->sin_port));
        return;
    }

    printf(LA_F("Recv %s pkt from %s:%d echo_seq=%u", LA_F306, 306),
          PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), hdr->seq);

    // 更新最后接收时间
    n->last_recv_time = now;

    // 检查是否是信令服务器转发的 CONN，即通过信令服务中转连接（from 是信令服务器地址）
    // + 信令服务器不应作为 PRFLX 候选添加
    int path_idx = -1;
    if (!sockaddr_equal(from, &s->signaling.addr)) {
        path_idx = upsert_prflx(s, from);
        if (path_idx < 0) {
            print("W:", LA_F("%s: CONN_ACK ignored, upsert %s:%d failed", LA_F436, 436), 
                  PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
            return;
        }
    }

    print("V:", LA_F("%s: recv from cand[%d]", LA_F91, 91), PROTO, path_idx);

    // CONN_ACK 控制包不计入流量统计（size=0），但更新路径活跃时间、包计数和重置超时计数器
    path_manager_on_packet_recv(s, path_idx, now, 0, false, 0);

    // 进入 NAT_CONNECTED 状态
    if (n->state == NAT_CONNECTING) { assert(!n->peer_connecting);
        n->state = s->active_path < 0 ? NAT_RELAY : NAT_CONNECTED;
        n->last_keepalive_send_ms = now;
        p2p_connected(s, now);
        print("I:", LA_F("%s: CONNECTING → %s (recv CONN_ACK)", LA_F73, 73), TASK_NAT, n->state == NAT_RELAY ? "RELAY" : "CONNECTED");
    }
}

/*
 * 处理 DATA 数据包（P2P_PKT_DATA）
 */
void nat_on_data(struct p2p_session *s, const struct sockaddr_in *from, uint64_t now, uint16_t seq, int data_len) {
    const char* PROTO = "DATA";
    nat_ctx_t *n = &s->nat;

    if (n->state < NAT_CONNECTING) {

        // ICE 模式没有 CONN 握手，所以对方可能先发送数据包
        if (n->state < NAT_PUNCHING || !s->cfg.use_ice) {
            print("E:", LA_F("Ignore %s pkt from %s:%d, valid state(%d)", LA_F439, 439), PROTO,
                inet_ntoa(from->sin_addr), ntohs(from->sin_port), n->state);
        }
        return;
    }

    printf(LA_F("Recv %s pkt from %s:%d, seq=%u, len=%d", LA_F309, 309),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), seq, data_len - P2P_HDR_SIZE);
    
    n->last_recv_time = now;

    // 记录统计数据包接收
    int path_idx = p2p_find_path_by_addr(s, from);
    if (path_idx >= -1)
        path_manager_on_packet_recv(s, path_idx, now, data_len, false, 0);

    // NAT_CONNECTING → NAT_CONNECTED（首次收到数据包）
    if (n->state <= NAT_CONNECTING) { assert(!n->peer_connecting);
        n->state = s->active_path < 0 ? NAT_RELAY : NAT_CONNECTED;
        n->last_keepalive_send_ms = now;
        p2p_connected(s, now);
        print("I:", LA_F("%s: CONNECTING → %s (recv DATA)", LA_F74, 74), TASK_NAT, n->state == NAT_RELAY ? "RELAY" : "CONNECTED");
    }
}

/*
 * 处理 ACK 包（P2P_PKT_ACK）
 */
void nat_on_data_ack(struct p2p_session *s, const struct sockaddr_in *from,
                     uint64_t now, uint16_t ack_seq, uint32_t sack) {
    const char* PROTO = "ACK";
    nat_ctx_t *n = &s->nat;

    // 收到 DATA ACK 说明自己至少曾经发送过数据包，也就是之前肯定已经是 connected 状态了
    if (n->state < NAT_LOST) {
        print("E:", LA_F("Ignore %s pkt from %s:%d, not connected", LA_F420, 420), PROTO,
              inet_ntoa(from->sin_addr), ntohs(from->sin_port));
        return;
    }

    printf(LA_F("Recv %s pkt from %s:%d, ack_seq=%u, sack=0x%08x", LA_F308, 308),
           PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), ack_seq, sack);

    n->last_recv_time = now;

    // ACK 控制包不计入流量统计（size=0），但需要更新路径活跃时间、包计数和重置超时计数器
    int path_idx = p2p_find_path_by_addr(s, from);
    if (path_idx >=-1) {
        path_manager_on_packet_recv(s, path_idx, now, 0, false, 0);
    }
}

/*
 * 协议：P2P_PKT_FIN (0x30)
 * 包头: [type=0x30 | flags=0 | seq=0]
 * 负载: 无
 * 
 * 处理 FIN 包（连接断开）
 */
void nat_on_fin(p2p_session_t *s, const struct sockaddr_in *from) {
    const char* PROTO = "FIN";

    printf(LA_F("Recv %s pkt from %s:%d", LA_F305, 305),
          PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    print("V:", LA_F("%s: accepted", LA_F87, 87), PROTO);

    // 收到对端主动断开通知，标记 NAT 层为已关闭
    if (s->nat.state != NAT_CLOSED) {
        s->nat.state = NAT_CLOSED;
        
        // 清理 reaching queue
        clear_reaching_queue(&s->nat);

        print("I:", LA_F("%s: → CLOSED (recv FIN)", LA_F150, 150), TASK_NAT);
    }
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 周期调用，发送打洞包和心跳
 */
void nat_tick(p2p_session_t *s, uint64_t now_ms) {

    nat_ctx_t *n = &s->nat;

    switch (n->state) {

        case NAT_PUNCHING:

            // ICE 候选交换完成后
            if (s->remote_cand_done) {

                // 首次 ICE 候选交换完成后，重置 punch 超时计时，以便准确计算打洞超时
                if (!n->punching) { n->punching = 1; n->punch_start = now_ms; }

                // 如果打洞超时
                // + 注意，即使此时 remote_cand_cnt == 0（没有任何候选），也得等到超时后再 fallback
                //   因为这个过程可能会出现 prflx candidate 地址
                else if (!instrument_option(P2P_INST_OPT_TIMEOUT_OFF)
                         && tick_diff(now_ms, n->punch_start) >= PUNCH_TIMEOUT_MS) {

                    // 如果没有信令中转服务可用
                    // + 最佳路径只有在没有任何其他可用路径后，且存在信令中转服务时，才会返回 PATH_IDX_SIGNALING
                    if (n->punching < 0 || path_manager_select_best_path(s) < PATH_IDX_SIGNALING) {

                        print("E:", LA_F("%s: PUNCHING → CLOSED (timeout %" PRIu64 "ms, %s signaling relay)", LA_F138, 138),
                              TASK_NAT, tick_diff(now_ms, n->punch_start), n->punching < 0 ? "using" : "no");

                        n->state = NAT_CLOSED;          // 标记为已关闭（打洞失败）
                        clear_reaching_queue(n);        // 清理 reaching queue
                        break;
                    }

                    // 信令服务中转支持读写
                    assert(path_manager_select_best_path(s) == PATH_IDX_SIGNALING);
                    n->punching = -1;                   // 标记为正在进行 relay punching
                    n->punch_start = now_ms;            // 重置打洞开始时间，进入 relay punching 阶段

                    print("W:", LA_F("%s: punch timeout, fallback punching using signaling relay", LA_F425, 425), TASK_NAT);

                    // 如果之前没有写路径
                    if (!s->tx_confirmed) { s->tx_confirmed = true;

                        // 如果已经收到对方数据包
                        // 首次确认 tx_confirmed，处理所有 reaching 队列（此后不会再有新的 pending）
                        if (s->rx_confirmed) flush_reaching_queue(s, PATH_IDX_SIGNALING, now_ms);
                        else s->rx_confirmed = true;
                    }
                    else s->rx_confirmed = true;

                    // 对方已进入 CONNECTING，直接回复 CONN_ACK 并进入 CONNECTED
                    if (n->peer_connecting) {

                        n->peer_connecting = false;
                        nat_send_conn_ack(s, now_ms);

                        // 设置活跃路径（用于 nat_send_conn_ack）
                        s->active_path = PATH_IDX_SIGNALING;
                        s->active_addr = s->signaling.addr;
                        s->path_type = P2P_PATH_SIGNALING;

                        n->state = NAT_RELAY;
                        n->last_keepalive_send_ms = now_ms;
                        p2p_connected(s, now_ms);
                        
                        print("I:", LA_F("%s: PUNCHING → RELAY (peer CONNECTING)", LA_F438, 438), TASK_NAT);
                    } 
                    // 正常流程：进入 CONNECTING，发送 CONN
                    else bidirectional_confirmed(s, PATH_IDX_SIGNALING, now_ms, "signaling relay");
                }
            }
            else if (!instrument_option(P2P_INST_OPT_TIMEOUT_OFF) 
                     && tick_diff(now_ms, n->punch_start) >= PUNCH_TIMEOUT_MS) {

                print("V:", LA_F("%s: timeout but ICE exchange not done yet (%" PRIu64 " ms elapsed, mode=%d), waiting for more candidates", LA_F183, 183),
                        TASK_NAT, tick_diff(now_ms, n->punch_start), s->signaling_mode);
            }

            // 周期性向所有候选发送打洞包（包括 relay）
            if (!instrument_option(P2P_INST_OPT_RETRY_OFF)) {

                int sent_cnt = 0;
                for (int i = 0; i < s->remote_cand_cnt; i++) {

                    if (tick_diff(now_ms, s->remote_cands[i].last_punch_send_ms) >= PUNCH_INTERVAL_MS) {

                        nat_send_punch(s, LA_W("punch", LA_W10, 10), &s->remote_cands[i], now_ms);
                        sent_cnt++;
                    }
                }
                if (sent_cnt) {

                    print("V:", LA_F("%s: punching %d/%d candidates (elapsed: %" PRIu64 " ms)", LA_F139, 139),
                        TASK_NAT, sent_cnt, s->remote_cand_cnt, tick_diff(now_ms, n->punch_start));
                }
            }
            break;

        case NAT_CONNECTING:

            // 超时检查
            if (!instrument_option(P2P_INST_OPT_TIMEOUT_OFF) 
                && tick_diff(now_ms, n->conn_start_ms) >= CONN_TIMEOUT_MS) {
                
                print("E:", LA_F("%s: CONN timeout after %" PRIu64 "ms", LA_F69, 69),
                      TASK_NAT, tick_diff(now_ms, n->conn_start_ms));
                
                // 此时双工读写已经确认（肯定存在至少一个写路径），但发送的 conn 包一直无应答
                print("E:", LA_F("%s: CONNECTING → CLOSED (timeout, no relay)", LA_F71, 71), TASK_NAT);

                n->state = NAT_CLOSED;
                clear_reaching_queue(n);  // 清理 reaching queue                    

                break;
            }

            // 周期发送 CONN 包
            if (!instrument_option(P2P_INST_OPT_RETRY_OFF)
                && tick_diff(now_ms, n->last_conn_send_ms) >= CONN_INTERVAL_MS) {
                nat_send_conn(s, now_ms);
            }
            break;

        case NAT_CONNECTED:

            // 超时检查
            if (!instrument_option(P2P_INST_OPT_TIMEOUT_OFF) 
                && (n->last_recv_time && tick_diff(now_ms, n->last_recv_time) >= PONG_TIMEOUT_MS)) {

                print("W:", LA_F("%s: CONNECTED → LOST (no response %" PRIu64 "ms)\n", LA_F127, 127),
                      TASK_NAT, tick_diff(now_ms, n->last_recv_time));

                n->state = NAT_LOST;
                break;
            }

            // 向所有可写候选发送保活包（复用 PUNCH 包）
            // + 包括 relay：需要测量 relay 路径的 RTT 和质量，供 path_manager 选路使用
            if (!instrument_option(P2P_INST_OPT_NAT_ALIVE_PUNCH_OFF) 
                && tick_diff(now_ms, n->last_keepalive_send_ms) >= PING_INTERVAL_MS) {

                int alive_cnt = 0;
                for (int i = 0; i < s->remote_cand_cnt; i++) {
                    if (path_is_selectable(s->remote_cands[i].stats.state)) {
                        nat_send_punch(s, LA_W("alive", LA_W1, 1), &s->remote_cands[i], now_ms);
                        alive_cnt++;
                    }
                }
                if (alive_cnt) {
                    n->last_keepalive_send_ms = now_ms;
                    print("V:", LA_F("%s: keep-alive sent (%d cands)", LA_F124, 124), TASK_NAT, alive_cnt);
                }
            }
            break;

        case NAT_RELAY:

            // 推进探测状态机
            probe_tick(s, now_ms);

            // 中继模式下周期性尝试直连（打洞）
            if (s->remote_cand_cnt 
                && !instrument_option(P2P_INST_OPT_NAT_ALIVE_PUNCH_OFF) 
                && tick_diff(now_ms, n->last_retry_send_ms) >= PUNCH_INTERVAL_MS * 4) {

                for (int i = 0; i < s->remote_cand_cnt; i++) {
                    nat_send_punch(s, LA_W("retry", LA_W14, 14), &s->remote_cands[i], now_ms);
                }
                n->last_retry_send_ms = now_ms;
            }
            break;

        // 连接丢失（由上层决定是否重启打洞或切换到 RELAY）
        case NAT_LOST:
        // ---- 可能原因 ----
        //
        // 1. 对方主动下线 / 进程崩溃
        //    对方已停止发送 PUNCH，不会恢复。
        //    恢复：等待对方重新上线并重新走 REGISTER→打洞流程。
        //
        // 2. 对方临时不可达（移动网络切换 WiFi↔4G、短暂断网）
        //    对方进程仍在运行，NAT 映射可能不变。
        //    恢复：对方网络恢复后继续发送 PUNCH，本端重新收到即可
        //          回到 CONNECTED。需要上层将状态回退到 PUNCHING 并重启打洞。
        //
        // 3. 本端 NAT 映射刷新 / 端口回收
        //    本端 socket 仍打开，但 NAT 设备因超时回收了映射条目。
        //    对方发往旧公网地址的包将被丢弃。
        //    恢复：本端重新向对方发送 PUNCH 即可刷新 NAT 映射。
        //          若双方同时映射失效，则需通过信令服务器重新交换地址。
        //          若服务器支持地址动态更新维护机制（PEER_INFO seq=0 + base_index
        //          地址变更通知），则无需重新 REGISTER，服务器会在 ALIVE/REGISTER
        //          包中检测到源地址变化并主动通知对端。
        //
        // 4. 本端 socket 被关闭后重新打开（端口重映射）
        //    新 socket 绑定的本地端口可能不同 → NAT 分配新的公网端口。
        //    对方发往旧公网地址的包全部丢失。
        //    恢复：若服务器支持地址动态更新维护机制，则客户端向服务器发送
        //          任意包（ALIVE/REGISTER）后，服务器即可检测到新的公网地址并
        //          通过地址变更通知告知对端，无需重新打洞流程。
        //          否则必须重新 REGISTER 交换新公网地址，然后重新打洞。
        //          （对应 instance_id 变更 → 服务器重置会话）
        //
        // 5. 对称 NAT 端口漂移
        //    对方 NAT 为 Symmetric 类型，映射端口在空闲一段时间后被回收，
        //    新的出站包获得不同公网端口，旧端口失效。
        //    恢复：需要对方通过信令服务器通知新地址（地址变更通知机制，
        //          PEER_INFO seq=0 + base_index 循环序号）。
        //          若服务器支持地址动态更新维护机制，则对方只需继续发送
        //          ALIVE/REGISTER，服务器自动检测源地址变化并通知本端。
        //
        // 6. 中间路径变更（ISP 路由调整、防火墙规则更新）
        //    双方 NAT 映射未变，但中间某跳丢弃了 UDP 包。
        //    恢复：取决于变更类型：
        //      - ISP 路由调整：通常是暂时的，持续发送 PUNCH 可在路由收敛后自动恢复。
        //      - 防火墙规则更新：取决于规则性质。
        //        · 临时规则（如速率限制触发、DDoS 防护临时封禁）：等待规则超时后自动恢复。
        //        · 永久规则（如运营商封禁 UDP、企业防火墙策略）：即使重新打洞也无法连通，
        //          只能降级到 RELAY 中继或切换传输协议（如 TCP/WebSocket）。
        //
        // ---- 当前策略 ----
        //   不自动重连。由上层（p2p.c）决策：
        //   - 回退到 PUNCHING 重试打洞（适用于原因 2/3/6-临时）
        //   - 切换到 RELAY 中继模式（适用于打洞多次失败后的降级，或原因 6-永久）
        //   - 触发重新 REGISTER（适用于原因 4/5，需要重新交换地址）
        //   - 直接通知上层断开（适用于原因 1，对方已不可达）

            // 推进探测状态机
            probe_tick(s, now_ms);

            // 首次进入时若为 READY 则自动触发
            if (!instrument_option(P2P_INST_OPT_AUTO_PROBE_OFF) 
                && s->probe_ctx.state == P2P_PROBE_STATE_READY)
                probe_trigger(s);
            
            break;

        default:
            break;
    }

    // 检查 reaching 队列，发送冷打洞 REACH 包（兜底方案）
    // 适用于 PUNCHING 和 LOST 状态
    //
    // 问题：冷打洞时，双方都是单向写的情况（对称NAT），PUNCH 包无法原路返回
    //      导致双方永远收不到 REACH 应答，无法确认 tx_confirmed
    //
    // 解决方案：
    //   策略1：信令中转（如果服务器支持 signaling_relay_fn）
    //          通过信令服务器中转 REACH 包，效率高但依赖服务器
    //   策略2：广播模式（不依赖信令服务器）
    //          向所有候选路径广播 REACH 包（排列组合），总有一条路径能通
    if ((n->state == NAT_PUNCHING || n->state == NAT_LOST) &&
        n->reaching_head && tick_diff(now_ms, n->last_reaching_send_ms) >= REACHING_RELAY_INTERVAL_MS) {
        
        n->last_reaching_send_ms = now_ms;

        // 构造 REACH 负载：[target_addr(6)]
        uint8_t reach_payload[6];
        memcpy(reach_payload, &n->reaching_head->target.sin_addr.s_addr, 4);
        memcpy(reach_payload + 4, &n->reaching_head->target.sin_port, 2);
        
        // 策略 1：信令中转模式（如果服务器支持）
        if (s->signaling_relay_fn) {

            ret_t ret = s->signaling_relay_fn(s, P2P_PKT_REACH, 0, n->reaching_head->seq, reach_payload, 6);
            if (ret == E_NONE) {

                // 发送成功，将节点出队并释放
                punch_reaching_t *node = n->reaching_head;
                n->reaching_head = node->next;
                if (!n->reaching_head) n->reaching_rear = NULL;
                
                print("V:", LA_F("%s: reaching relay via signaling SUCCESS, seq=%u", LA_F147, 147),
                      TASK_NAT, node->seq);

                node->next = n->reaching_recycle;
                n->reaching_recycle = node;
            } 
            // 发送失败（信令服务器未就绪），保留节点等待下次重试
            else print("V:", LA_F("%s: reaching relay via signaling FAILED (ret=%d), seq=%u", LA_F146, 146),
                       TASK_NAT, ret, n->reaching_head->seq);            
        }
        // 策略 2：广播模式（不依赖信令服务器）
        else {

            // 向所有候选路径广播 REACH 包（排列组合策略）
            int broadcast_cnt = 0;
            for (int i = 0; i < s->remote_cand_cnt; i++) {

                // 跳过 target 地址（已原路发送）
                if (sockaddr_equal(&s->remote_cands[i].addr, &n->reaching_head->target)) continue;

                cand_send_packet(s, i, P2P_PKT_REACH, n->reaching_head->seq, reach_payload, 6, now_ms, false);
                broadcast_cnt++;
            }
            
            // 将节点出队并释放
            punch_reaching_t *node = n->reaching_head;
            n->reaching_head = node->next;
            if (!n->reaching_head) n->reaching_rear = NULL;
            
            print("V:", LA_F("%s: reaching broadcast to %d cand(s), seq=%u", LA_F144, 144),
                  TASK_NAT, broadcast_cnt, node->seq);

            node->next = n->reaching_recycle;
            n->reaching_recycle = node;
        }
    }
}

