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
#define PING_INTERVAL_MS        5000        /* 心跳间隔（调整为5秒，适配path_manager 10秒超时）*/
#define PONG_TIMEOUT_MS         30000       /* 心跳超时 */

#define TASK_NAT                "NAT"

/*
 * 向指定候选发送打洞包
 *
 * 协议：P2P_PKT_PUNCH (0x01)
 * 包头: [type=0x01 | flags=0 | seq=发送方序列号(2B)]
 * 负载: 无
 */
static void nat_send_punch(p2p_session_t *s, const char *reason,
                           p2p_remote_candidate_entry_t *entry, uint64_t now) {
    const char* PROTO = "PUNCH";
    nat_ctx_t *n = &s->nat;

    if (++n->punch_seq == 0) n->punch_seq = 1;  // 跳过 seq=0 的情况

    int send_path = (int)(entry - s->remote_cands);

    udp_send_packet(s->sock, &entry->cand.addr, P2P_PKT_PUNCH, 0, n->punch_seq, NULL, 0);

    print("V:", LA_F("%s sent to %s:%d for %s, seq=%d, path=%d", LA_F12, 108),
          PROTO, inet_ntoa(entry->cand.addr.sin_addr), ntohs(entry->cand.addr.sin_port),
          reason, n->punch_seq, send_path);

    entry->last_punch_send_ms = now;

    // 记录发送包用于 RTT 测量（收到 PUNCH_ACK 时通过 seq 匹配计算精确 per-path RTT）
    path_manager_on_packet_send(s, send_path, n->punch_seq, now);
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 初始化打洞上下文
 */
void nat_init(nat_ctx_t *n) {
    memset(n, 0, sizeof(*n));
    n->state = NAT_INIT;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * NAT 打洞（统一接口，支持批量启动和单候选追加）
 *
 * @param s        会话对象
 * @param idx      目标候选索引（-1=批量启动所有候选，>=0=单个候选打洞）
 * @return         0=成功，!0=失败
 *
 * 用法：
 *   - nat_punch(s, -1)      批量启动所有 remote_cands 的打洞
 *   - nat_punch(s, idx)     向单个候选追加打洞（Trickle ICE）
 */
ret_t nat_punch(p2p_session_t *s, int idx) {

    P_check(s != NULL, return E_INVALID;)
    
    nat_ctx_t *n = &s->nat;
    uint64_t now = P_tick_ms();
    
    /* ========== 首次批量或重新启动模式：idx == -1 ========== */

    if (idx == -1) {

        if (s->remote_cand_cnt == 0) {
            print("E:", LA_F("%s: no remote candidates to punch", LA_F76, 172), TASK_NAT);
            return E_NONE_EXISTS;
        }

        // 已连接时忽略（避免破坏已建立的连接）
        if (n->state == NAT_CONNECTED) {
            print("V:", LA_F("%s: already connected, ignoring batch punch request", LA_F46, 142), TASK_NAT);
            return E_NONE;
        }
        
        // 启动/重启 PUNCHING 状态（适用于 INIT/CLOSED/DISCONNECTED/RELAY）
        n->state = NAT_PUNCHING;
        n->punch_start = now;
        n->rx_confirmed = false;
        n->tx_confirmed = false;
        n->peer_addr = s->remote_cands[0].cand.addr;  /* 默认值，收到 ACK 时会更新 */

        print("I:", LA_F("%s: start punching all(%d) remote candidates", LA_F98, 194), TASK_NAT, s->remote_cand_cnt);

        // 打印详细日志
        if (p2p_get_log_level() == P2P_LOG_LEVEL_VERBOSE) {

            for (int i = 0; i < s->remote_cand_cnt; i++) {
                const char *type_str = "Unknown";
                switch ((p2p_cand_type_t)s->remote_cands[i].cand.type) {
                    case P2P_CAND_HOST:  type_str = "Host";  break;
                    case P2P_CAND_SRFLX: type_str = "Srflx"; break;
                    case P2P_CAND_PRFLX: type_str = "Prflx"; break;
                    case P2P_CAND_RELAY: type_str = "Relay"; break;
                }

                print("V:", LA_F("  [%d]<%s:%d> (type: %s)", LA_F3, 99), i,
                      inet_ntoa(s->remote_cands[i].cand.addr.sin_addr), ntohs(s->remote_cands[i].cand.addr.sin_port),
                      type_str);
            }
        }

        // 立即向所有候选并行发送打洞包
        for (int i = 0; i < s->remote_cand_cnt; i++)
            nat_send_punch(s, LA_W("punch", LA_W12, 13), &s->remote_cands[i], now);
        n->last_send_time = now;
        
        return E_NONE;
    }
    
    /* ========== Trickle 单候选打洞模式：idx >= 0 ========== */
    
    if (idx < 0 || idx >= s->remote_cand_cnt) {
        print("E:", LA_F("%s: invalid cand idx: %d (count: %d)", LA_F70, 166), TASK_NAT, idx, s->remote_cand_cnt);
        return E_OUT_OF_RANGE;
    }

    p2p_remote_candidate_entry_t *entry = &s->remote_cands[idx];

    // 已连接状态：直接发送打洞包建立新路径，不改变连接状态
    if (n->state == NAT_CONNECTED) {

        print("I:", LA_F("%s: punching additional cand<%s:%d>[%d] while connected", LA_F82, 178), TASK_NAT,
              inet_ntoa(entry->cand.addr.sin_addr), ntohs(entry->cand.addr.sin_port), idx);
    }
    else {
        print("I:", LA_F("%s: punching remote cand<%s:%d>[%d]", LA_F83, 179), TASK_NAT,
              inet_ntoa(entry->cand.addr.sin_addr), ntohs(entry->cand.addr.sin_port), idx);

        // 首次或重新启动时初始化状态（INIT/CLOSED）
        if (n->state == NAT_INIT || n->state == NAT_CLOSED) {
            n->state = NAT_PUNCHING;
            n->punch_start = now;
            n->rx_confirmed = false;
            n->tx_confirmed = false;
            n->peer_addr = s->remote_cands[0].cand.addr;
        }
    }

    // 发送打洞包
    nat_send_punch(s, LA_W("punch", LA_W12, 13), entry, now);
    n->last_send_time = now;

    return E_NONE;
}

/*
 * 发送 FIN 包通知对端主动断开连接
 *
 * 协议：P2P_PKT_FIN (0x03)
 * 包头: [type=0x03 | flags=0 | seq=0]
 * 负载: 无
 */
void nat_send_fin(p2p_session_t *s) {
    const char* PROTO = "FIN";

    udp_send_packet(s->sock, &s->active_addr, P2P_PKT_FIN, 0, 0, NULL, 0);

    print("V:", LA_F("%s sent to %s:%d", LA_F11, 107),
          PROTO, inet_ntoa(s->active_addr.sin_addr), ntohs(s->active_addr.sin_port));
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 处理 PUNCH 包（NAT 打洞、保活）
 *
 * 收到后立即回复 PUNCH_ACK（从同一路径回复，用于精确 RTT 测量）
 * PUNCH 本身证明 peer→me 方向联通
 *
 * 协议：P2P_PKT_PUNCH (0x01)
 * 包头: [type=0x01 | flags=0 | seq=发送方序列号(2B)]
 * 负载: 无
 */
void nat_on_punch(p2p_session_t *s, const p2p_packet_hdr_t *hdr,
                   const struct sockaddr_in *from) {
    const char* PROTO = "PUNCH";

    printf(LA_F("Recv %s pkt from %s:%d seq=%u", LA_F180, 276),
          PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), hdr->seq);

    /*
     * 立即回复 PUNCH_ACK（从收到包的同一地址回复，确保回程走相同路径）
     *  发送 PUNCH_ACK（即时回复对方的 PUNCH）
     *
     * 协议：P2P_PKT_PUNCH_ACK (0x02)
     * 包头: [type=0x02 | flags=0 | seq=回传对方的 PUNCH seq(2B)]
     * 负载: 无
    */
    {
        const char* PROTO_ACK = "PUNCH_ACK";

        udp_send_packet(s->sock, from, P2P_PKT_PUNCH_ACK, 0, hdr->seq, NULL, 0);

        print("V:", LA_F("%s sent to %s:%d, echo_seq=%u", LA_F13, 109),
              PROTO_ACK, inet_ntoa(from->sin_addr), ntohs(from->sin_port), hdr->seq);
    }

    nat_ctx_t *n = &s->nat;
    uint64_t now = P_tick_ms();

    int remote_cnt_before = s->remote_cand_cnt;
    int cand_idx = p2p_upsert_remote_candidate(s, from, P2P_CAND_PRFLX, true);
    if (cand_idx < 0) {
        print("E:", LA_F("%s: failed to track cand<%s:%d>, dropping", LA_F60, 156),
              TASK_NAT, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
        return;
    }

    print("V:", LA_F("%s: accepted from cand[%d]", LA_F37, 133), PROTO, cand_idx);

    // 更新最后接收时间（保活/心跳超时检测）
    n->last_recv_time = now;

    if (cand_idx >= remote_cnt_before) {
        print("I:", LA_F("%s: discovered unsynced prflx cand<%s:%d>[%d]", LA_F51, 147),
              TASK_NAT, inet_ntoa(from->sin_addr), ntohs(from->sin_port), cand_idx);
    }

    // 通知 ICE 层（如果启用）
    if (s->cfg.use_ice) {
        p2p_ice_on_check_success(s, from);
    }

    // 更新路径信息为可达
    s->remote_cands[cand_idx].reachable = true;
    /* 仅在路径尚未被 health_check 管理时激活；ACTIVE/DEGRADED/RECOVERING 由 health_check 维护 */
    if (s->remote_cands[cand_idx].stats.state != PATH_STATE_ACTIVE &&
        s->remote_cands[cand_idx].stats.state != PATH_STATE_DEGRADED &&
        s->remote_cands[cand_idx].stats.state != PATH_STATE_RECOVERING) {
        path_manager_set_path_state(s, cand_idx, PATH_STATE_ACTIVE);
    }
    s->remote_cands[cand_idx].stats.is_lan = route_check_same_subnet(&s->route, from);

    if (path_manager_get_active_idx(s) < 0 || s->remote_cands[cand_idx].stats.is_lan) {
        path_manager_set_active(s, cand_idx);
    }

    // 更新接收统计（防止超时误判）
    path_manager_on_packet_recv(s, cand_idx, now);

    // NAT_CONNECTED 状态：只收集路径状态信息，不需要双向确认逻辑
    if (n->state == NAT_CONNECTED) {
        return;
    }

    // 非打洞相关状态：忽略
    if (n->state != NAT_PUNCHING && n->state != NAT_RELAY && n->state != NAT_LOST) {
        print("W:", LA_F("%s: ignored in state(%d)", LA_F67, 163), TASK_NAT, n->state);
        return;
    }

    // ---------- 双向确认逻辑 ----------

    // peer→me 方向：收到 PUNCH 即证明入方向通了
    if (!n->rx_confirmed) {
        n->rx_confirmed = true;
        print("I:", LA_F("%s: rx confirmed: peer->me path is UP (%s:%d)", LA_F91, 187),
                TASK_NAT, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    }

    // 双向均确认 → NAT_CONNECTED（tx_confirmed 由 nat_on_punch_ack 设置）
    if (n->rx_confirmed && n->tx_confirmed) {
        n->state = NAT_CONNECTED;
        n->peer_addr = s->remote_cands[cand_idx].cand.addr;
        print("I:", LA_F("%s: bidirectional confirmed: NAT_CONNECTED (%s:%d)", LA_F49, 145),
                TASK_NAT, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    }
}

/*
 * 处理 PUNCH_ACK 包（PUNCH 的即时确认）
 *
 * 收到 PUNCH_ACK 证明 me→peer 方向联通，且 ACK.seq 用于精确 RTT 测量。
 *
 * 协议：P2P_PKT_PUNCH_ACK (0x02)
 * 包头: [type=0x02 | flags=0 | seq=回传的 PUNCH seq(2B)]
 * 负载: 无
 */
void nat_on_punch_ack(p2p_session_t *s, const p2p_packet_hdr_t *hdr,
                      const struct sockaddr_in *from) {
    const char* PROTO = "PUNCH_ACK";

    nat_ctx_t *n = &s->nat;
    uint64_t now = P_tick_ms();

    printf(LA_F("Recv %s pkt from %s:%d echo_seq=%u", LA_F179, 275),
          PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port), hdr->seq);

    // 更新最后接收时间
    n->last_recv_time = now;

    int cand_idx = p2p_upsert_remote_candidate(s, from, P2P_CAND_PRFLX, true);
    if (cand_idx < 0) {
        print("E:", LA_F("%s: failed to track cand<%s:%d>, dropping", LA_F60, 156),
              TASK_NAT, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
        return;
    }

    // 更新路径为可达
    s->remote_cands[cand_idx].reachable = true;
    /* 仅在路径尚未被 health_check 管理时激活；ACTIVE/DEGRADED/RECOVERING 由 health_check 维护 */
    if (s->remote_cands[cand_idx].stats.state != PATH_STATE_ACTIVE &&
        s->remote_cands[cand_idx].stats.state != PATH_STATE_DEGRADED &&
        s->remote_cands[cand_idx].stats.state != PATH_STATE_RECOVERING) {
        path_manager_set_path_state(s, cand_idx, PATH_STATE_ACTIVE);
    }

    // RTT 测量：通过 seq 匹配 pending_packets 计算精确 per-path RTT
    if (hdr->seq > 0)
        path_manager_on_packet_ack(s, hdr->seq, now);
    path_manager_on_packet_recv(s, cand_idx, now);

    // NAT_CONNECTED 状态：只更新路径统计
    if (n->state == NAT_CONNECTED) {
        return;
    }

    // 非打洞相关状态：忽略
    if (n->state != NAT_PUNCHING && n->state != NAT_RELAY && n->state != NAT_LOST) {
        return;
    }

    // ---------- 双向确认逻辑 ----------

    // peer→me 方向：收到 PUNCH_ACK 也证明入方向通（对方能发包给我们）
    if (!n->rx_confirmed) {
        n->rx_confirmed = true;
        print("I:", LA_F("%s: rx confirmed: peer->me path is UP (%s:%d)", LA_F91, 187),
                TASK_NAT, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    }

    // me→peer 方向：收到 PUNCH_ACK 证明我们的 PUNCH 到达了对方
    if (!n->tx_confirmed) {
        n->tx_confirmed = true;
        print("I:", LA_F("%s: tx confirmed: me->peer path is UP (echoed seq=%u)", LA_F112, 208),
                TASK_NAT, hdr->seq);
    }

    // 双向均确认 → NAT_CONNECTED
    if (n->rx_confirmed && n->tx_confirmed) {
        n->state = NAT_CONNECTED;
        n->peer_addr = s->remote_cands[cand_idx].cand.addr;
        print("I:", LA_F("%s: bidirectional confirmed: NAT_CONNECTED (%s:%d)", LA_F49, 145),
                TASK_NAT, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    }
}

/*
 * 协议：P2P_PKT_FIN (0x22)
 * 包头: [type=0x22 | flags=0 | seq=0]
 * 负载: 无
 * 
 * 处理 FIN 包（连接断开）
 */
void nat_on_fin(p2p_session_t *s, const struct sockaddr_in *from) {
    const char* PROTO = "FIN";

    printf(LA_F("Recv %s pkt from %s:%d", LA_F178, 274),
          PROTO, inet_ntoa(from->sin_addr), ntohs(from->sin_port));

    print("V:", LA_F("%s: accepted", LA_F34, 130), PROTO);

    // 收到对端主动断开通知，标记 NAT 层为已关闭
    if (s->nat.state != NAT_CLOSED) {
        s->nat.state = NAT_CLOSED;
        print("I:", LA_F("%s: received FIN from peer, marking NAT as CLOSED", LA_F84, 180), TASK_NAT);
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

            // 超时判断
            if (now_ms - n->punch_start >= PUNCH_TIMEOUT_MS) {
                
                // ICE 候选交换完成后
                // + 只有在 ICE 候选交换完成后才判定打洞超时，否则可能还有新候选在路上，该状态值由信令层负责设置维护
                if (s->ice_exchange_done) {

                    print("W:", LA_F("%s: timeout after %" PRIu64 " ms (ICE done), switching to RELAY", LA_F32, 289),
                          TASK_NAT, now_ms - n->punch_start);

                    n->state = NAT_RELAY;           // 标记进入中继模式，
                    n->rx_confirmed = false;        // 重置双向确认标志（在中继期间继续尝试打洞）
                    n->tx_confirmed = false;

                    // 启动信通外对端探测（如果支持）
                    probe_trigger(s);
                }
                else print("V:", LA_F("%s: timeout but ICE exchange not done yet (%" PRIu64 " ms elapsed, mode=%d), waiting for more candidates", 0, 0),
                           TASK_NAT, now_ms - n->punch_start, s->signaling_mode);
            }

            // 如果存在未完成的 nat 打洞
            int sent_cnt = 0;
            for (int i = 0; i < s->remote_cand_cnt; i++) {
                if (s->remote_cands[i].last_punch_send_ms == 0 ||
                    now_ms - s->remote_cands[i].last_punch_send_ms >= PUNCH_INTERVAL_MS) {

                    nat_punch(s, i);
                    sent_cnt++;
                }
            }
            if (sent_cnt) {
                print("V:", LA_F("%s: punching %d/%d candidates (elapsed: %" PRIu64 " ms)", LA_F8, 265),
                      TASK_NAT, sent_cnt, s->remote_cand_cnt, now_ms - n->punch_start);
            }
            break;

        case NAT_CONNECTED:

            // 超时检查
            if (n->last_recv_time > 0 && now_ms - n->last_recv_time >= PONG_TIMEOUT_MS) {

                print("W:", LA_F("%s: no response for %" PRIu64 " ms, connection lost", 0, 0),
                      TASK_NAT, now_ms - n->last_recv_time);

                n->state = NAT_LOST;
                break;
            }

            // 向所有可达候选发送保活包（复用 PUNCH 包）-
            if (now_ms - n->last_send_time >= PING_INTERVAL_MS) {

                int alive_cnt = 0;
                for (int i = 0; i < s->remote_cand_cnt; i++) {
                    if (s->remote_cands[i].reachable) {

                        nat_send_punch(s, LA_W("alive", LA_W0, 1), &s->remote_cands[i], now_ms);
                        alive_cnt++;
                    }
                }
                if (alive_cnt) {
                    n->last_send_time = now_ms;
                    print("V:", LA_F("%s: keep alive to %d reachable cand(s)", LA_F74, 170), TASK_NAT, alive_cnt);
                }
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
            // 推进探测状态机；首次进入时若为 READY 则自动触发
            probe_tick(s, now_ms);
            if (s->probe_ctx.state == P2P_PROBE_STATE_READY) {
                probe_trigger(s);
            }
            break;

        case NAT_RELAY:

            // 推进探测状态机
            probe_tick(s, now_ms);

            // 中继模式下周期性尝试直连
            if (now_ms - n->last_send_time >= PUNCH_INTERVAL_MS * 4) {

                for (int i = 0; i < s->remote_cand_cnt; i++) {
                    nat_send_punch(s, LA_W("retry", LA_W16, 17), &s->remote_cands[i], now_ms);
                }
                n->last_send_time = now_ms;
            }
            break;

        default:
            break;
    }
}
