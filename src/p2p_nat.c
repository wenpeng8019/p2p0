/*
 * NAT 穿透实现（纯打洞逻辑）
 *
 * 只负责 PUNCH/ACK/PING/PONG 的发送和接收。
 * 候选列表统一存储在 p2p_session 中，本模块从 session 读取远端候选进行打洞。
 */

#define MOD_TAG "NAT"

#include "p2p_internal.h"
#include "p2p_relay_probe.h"

#define PUNCH_INTERVAL_MS       500         /* 打洞间隔 */
#define PUNCH_TIMEOUT_MS        5000        /* 打洞超时 */
#define PING_INTERVAL_MS        15000       /* 心跳间隔 */
#define PONG_TIMEOUT_MS         30000       /* 心跳超时 */

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
 * 协议：P2P_PKT_PUNCH (0x01)
 * 包头: [type=0x01 | flags=0 | seq=发送方序列号(2B)]
 * 负载: [echo_seq(2B, 网络字节序)] 上次收到对方的 seq
 * 
 * NAT 打洞（统一接口，支持批量启动和单候选追加）
 *
 * @param s        会话对象
 * @param idx      目标候选索引（-1=批量启动所有候选，>=0=单个候选打洞）
 * @return         0=成功，-1=失败（无候选）
 *
 * 用法：
 *   - nat_punch(s, -1)      批量启动所有 remote_cands 的打洞
 *   - nat_punch(s, idx)     向单个候选追加打洞（Trickle ICE）
 */
int nat_punch(p2p_session_t *s, int idx) {
    if (!s) return -1;
    
    nat_ctx_t *n = &s->nat;
    P_clock _clk; P_clock_now(&_clk);
    uint64_t now = clock_ms(_clk);
    
    /* ========== 首次批量或重新启动模式：idx == -1 ========== */

    // todo 如果已经是 connected 状态，是否需要重启打洞？
    if (idx == -1) {
        if (s->remote_cand_cnt == 0) {
            print("E: %s", LA_S("No remote candidates to punch", LA_S51, 171));
            return E_NONE_EXISTS;
        }
        
        // 启动 PUNCHING 状态
        n->state = NAT_PUNCHING;
        n->punch_start = now;
        n->peer_addr = s->remote_cands[0].cand.addr;  /* 默认值，收到 ACK 时会更新 */
        n->rx_confirmed = false;
        n->tx_confirmed = false;

        // 打印详细日志
        if (p2p_get_log_level() == P2P_LOG_LEVEL_VERBOSE) {

            print("V:", LA_F("Start punching all(%d) remote candidates", LA_F154, 656), s->remote_cand_cnt);

            for (int i = 0; i < s->remote_cand_cnt; i++) {
                const char *type_str = "Unknown";

                /* cand.type 语义根据信令模式解读 */
                if (s->signaling_mode == P2P_SIGNALING_MODE_COMPACT) {
                    switch (s->remote_cands[i].cand.type) {
                        case P2P_COMPACT_CAND_HOST: type_str = "Host"; break;
                        case P2P_COMPACT_CAND_SRFLX: type_str = "Srflx"; break;
                        case P2P_COMPACT_CAND_PRFLX: type_str = "Prflx"; break;
                        case P2P_COMPACT_CAND_RELAY: type_str = "Relay"; break;
                    }
                } else {  /* RELAY or PUBSUB mode */
                    switch (s->remote_cands[i].cand.type) {
                        case P2P_ICE_CAND_HOST: type_str = "Host"; break;
                        case P2P_ICE_CAND_SRFLX: type_str = "Srflx"; break;
                        case P2P_ICE_CAND_PRFLX: type_str = "Prflx"; break;
                        case P2P_ICE_CAND_RELAY: type_str = "Relay"; break;
                    }
                }

                print("V:", LA_F("  [%d]: %s:%d (type: %s)", LA_F3, 640), i,
                      inet_ntoa(s->remote_cands[i].cand.addr.sin_addr), ntohs(s->remote_cands[i].cand.addr.sin_port),
                      type_str);
            }
        }

        // 立即向所有候选并行发送打洞包，捎带上次收到的对方 seq
        uint8_t probe_payload[2];
        nwrite_s(probe_payload, n->last_peer_seq);
        for (int i = 0; i < s->remote_cand_cnt; i++) {

            printf(LA_F("Send probe PUNCH pkt to %s:%d, echo_seq=%d", LA_F146, 652),
                   inet_ntoa(s->remote_cands[i].cand.addr.sin_addr),
                   ntohs(s->remote_cands[i].cand.addr.sin_port), n->punch_seq);

            udp_send_packet(s->sock, &s->remote_cands[i].cand.addr, P2P_PKT_PUNCH, 0, ++n->punch_seq, probe_payload, 2);
            s->remote_cands[i].last_punch_send_ms = now;
        }
        n->last_send_time = now;
        
        return E_NONE;
    }
    
    /* ========== Trickle 单候选打洞模式：idx >= 0 ========== */
    
    if (idx < 0 || idx >= s->remote_cand_cnt) {
        print("E:", LA_F("Invalid candidate index: %d (count: %d)", LA_F87, 645), idx, s->remote_cand_cnt);
        return E_OUT_OF_RANGE;
    }

    if (n->state != NAT_CONNECTED) {

        if (n->state == NAT_INIT) {
            n->peer_addr = s->remote_cands[0].cand.addr;
            n->state = NAT_PUNCHING;
            n->punch_start = now;
        }

        struct sockaddr_in *addr = &s->remote_cands[idx].cand.addr;        
        print("V:", LA_F("Punching remote candidate(%d) %s:%d", LA_F106, 647), idx,
              inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));

        // 发送打洞包，捎带上次收到的对方 seq
        uint8_t probe_payload[2];
        nwrite_s(probe_payload, n->last_peer_seq);

        printf(LA_F("Send probe PUNCH pkt to %s:%d, seq=%u", LA_F147, 653),
               inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), n->punch_seq);

        udp_send_packet(s->sock, addr, P2P_PKT_PUNCH, 0, ++n->punch_seq, probe_payload, 2);
        s->remote_cands[idx].last_punch_send_ms = now;

        n->last_send_time = now;
    }
    else print("V:", LA_F("Ignoring punch request for candidate %d since already connected", LA_F75, 644), idx);

    return E_NONE;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 协议：P2P_PKT_PUNCH (0x01)
 * 包头: [type=0x01 | flags=0 | seq=发送方序列号(2B)]
 * 负载: [echo_seq(2B, 网络字节序)]
 *
 * 机制（捎带式 echo，无即时应答）：
 *   双方各自按固定间隔定时发送 PUNCH 包，payload 中的 echo_seq 始终携带
 *   「上次收到对方的 seq」（last_peer_seq）。
 *
 *   双向连通判定：
 *     - 收到对方 PUNCH → 证明 peer→me 方向通了，记录 last_peer_seq
 *     - 收到 echo_seq == 自己最近发的 seq → 证明 me→peer 方向也通了 → NAT_CONNECTED
 *
 *   与即时 ACK 方案对比：
 *     - 即时 ACK：收到探测包后立刻额外发一个确认包，约 2× 包量
 *     - 捎带式：确认信息在下次定时包中捎带，包量减少约 50%
 *     - 代价：连通确认延迟增加最多一个 PUNCH_INTERVAL_MS
 *
 * 处理 PUNCH 包（NAT 打洞、保活）
 */
ret_t nat_on_punch(p2p_session_t *s, const p2p_packet_hdr_t *hdr,
                   const uint8_t *payload, int len, const struct sockaddr_in *from) {

    nat_ctx_t *n = &s->nat;
    P_clock _clk; P_clock_now(&_clk);
    uint64_t now = clock_ms(_clk);

    // 解析 echo_seq（2字节网络字节序）
    uint16_t echo_seq = 0;
    if (len >= 2) nread_s(&echo_seq, payload);

    printf(LA_F("Received PUNCH pkt from %s:%d, seq=%u, echo_seq=%u, len=%d", LA_F124, 143),
           inet_ntoa(from->sin_addr), ntohs(from->sin_port), hdr->seq, echo_seq, len);

    // 记录对方最新 seq，下次定时发包时捎带作为 echo_seq
    n->last_peer_seq = hdr->seq;

    // 通知 ICE 层（如果启用）
    if (s->cfg.use_ice) {
        p2p_ice_on_check_success(s, from);
    }

    // 更新最后接收时间（保活/心跳超时检测）
    n->last_recv_time = now;

    // ---------- 打洞 / 中继阶段的双向确认逻辑 ----------
    if (n->state == NAT_PUNCHING || n->state == NAT_RELAY) {

        // peer→me 方向：收到对方 PUNCH 即证明入方向通了
        if (!n->rx_confirmed) {
            n->rx_confirmed = true;
            print("I:", LA_F("PUNCH rx confirmed: peer->me path is UP (%s:%d)", LA_F98, 789),
                  inet_ntoa(from->sin_addr), ntohs(from->sin_port));
        }

        // me→peer 方向：对方的 echo_seq != 0，证明对方收到了我们的包
        if (!n->tx_confirmed && echo_seq != 0) {
            n->tx_confirmed = true;
            print("I:", LA_F("PUNCH tx confirmed: me->peer path is UP (peer echoed seq=%u)", LA_F99, 790),
                  echo_seq);
        }

        // 双向均确认 → NAT_CONNECTED
        if (n->rx_confirmed && n->tx_confirmed) {
            n->peer_addr = *from;
            n->state = NAT_CONNECTED;
            print("I:", LA_F("PUNCH bidirectional confirmed: NAT_CONNECTED (%s:%d)", LA_F97, 788),
                  inet_ntoa(from->sin_addr), ntohs(from->sin_port));
            
            // 检查打通的路径是否为同子网（LAN）
            if (route_check_same_subnet(&s->route, from)) {
                // 标记为 LAN 路径（高优先级）
                int lan_idx = path_manager_add_or_update_path(&s->path_mgr, P2P_PATH_LAN, (struct sockaddr_in*)from);
                if (lan_idx >= 0) {
                    path_manager_set_path_state(&s->path_mgr, lan_idx, PATH_STATE_ACTIVE);
                    print("I:", LA_F("LAN path detected and activated: %s:%d", LA_F89, 276),
                          inet_ntoa(from->sin_addr), ntohs(from->sin_port));
                }
            }
        }
    }

    return E_NONE;
}

/*
 * 协议：P2P_PKT_FIN (0x22)
 * 包头: [type=0x22 | flags=0 | seq=0]
 * 负载: 无
 * 
 * 处理 FIN 包（连接断开）
 */
ret_t nat_on_fin(p2p_session_t *s, const struct sockaddr_in *from) {

    printf(LA_F("Received FIN pkt from %s:%d, NAT state will be closed", LA_F115, 145),
           inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    
    // 收到对端主动断开通知，标记 NAT 层为已关闭
    s->nat.state = NAT_CLOSED;
    
    return E_NONE;
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
                
                print("W:", LA_F("TIMEOUT: Punch failed after %" PRIu64 " ms, switching to RELAY", LA_F32, 289),
                      now_ms - n->punch_start);

                // 进入中继模式，重置双向确认标志，在中继期间继续尝试打洞
                n->rx_confirmed = false;
                n->tx_confirmed = false;
                n->state = NAT_RELAY;
                
                // 触发 Relay 探测：通过服务器检测对端可达性
                relay_probe_trigger(s);
            }

            int sent_cnt = 0;
            for (int i = 0; i < s->remote_cand_cnt; i++) {

                if (s->remote_cands[i].last_punch_send_ms == 0 ||
                    now_ms - s->remote_cands[i].last_punch_send_ms >= PUNCH_INTERVAL_MS) {
                    nat_punch(s, i);
                    sent_cnt++;
                }
            }

            // 如果存在未完成的 nat 打洞
            if (sent_cnt > 0) {
                print("V:", LA_F("ATTEMPT: punch to %d/%d candidates (elapsed: %" PRIu64 " ms)", LA_F8, 265),
                      sent_cnt, s->remote_cand_cnt, now_ms - n->punch_start);
            }
            break;

        case NAT_CONNECTED:

            // 发送保活包（复用 PUNCH 包，echo_seq=0）
            if (now_ms - n->last_send_time >= PING_INTERVAL_MS) {

                uint8_t probe_payload[2]; nwrite_s(probe_payload, n->last_peer_seq);

                printf(LA_F("Send alive PUNCH pkt to %s:%d, seq=%u", LA_F144, 651),
                       inet_ntoa(n->peer_addr.sin_addr), ntohs(n->peer_addr.sin_port), n->punch_seq);

                udp_send_packet(s->sock, &n->peer_addr, P2P_PKT_PUNCH, 0, ++n->punch_seq, probe_payload, 2);
                n->last_send_time = now_ms;

                print("V:", LA_F("Keep-alive to %s:%d", LA_F88, 96),
                      inet_ntoa(n->peer_addr.sin_addr), ntohs(n->peer_addr.sin_port));
            }

            // 超时检查
            if (n->last_recv_time > 0 && now_ms - n->last_recv_time >= PONG_TIMEOUT_MS) {

                print("W:", LA_F("TIMEOUT: No response from peer for (%" PRIu64 " ms), connection lost", 0, 0),
                      now_ms - n->last_recv_time);

                n->state = NAT_DISCONNECTED;
            }
            break;

        // 连接断开（由上层决定是否重启打洞或切换到 RELAY）
        case NAT_DISCONNECTED:
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
            // 处理 Relay 探测状态机（检测对端状态）
            
            relay_probe_tick(s, now_ms);
            
            // 首次进入此状态时触发探测
            if (s->relay_probe_state == P2P_RELAY_PROBE_IDLE) {
                relay_probe_trigger(s);
            }
            break;

        case NAT_RELAY:

            // 处理 Relay 探测状态机
            relay_probe_tick(s, now_ms);

            // 中继模式下周期性尝试直连，携带 last_peer_seq
            if (now_ms - n->last_send_time >= PUNCH_INTERVAL_MS * 4) {

                uint8_t probe_payload[2]; nwrite_s(probe_payload, n->last_peer_seq);
                for (int i = 0; i < s->remote_cand_cnt; i++) {

                    printf(LA_F("Send relay PUNCH pkt to %s:%d, echo_seq=%d", LA_F148, 654),
                           inet_ntoa(s->remote_cands[i].cand.addr.sin_addr),
                           ntohs(s->remote_cands[i].cand.addr.sin_port), n->last_peer_seq);

                    udp_send_packet(s->sock, &s->remote_cands[i].cand.addr, P2P_PKT_PUNCH, 0, ++n->punch_seq, probe_payload, 2);
                    s->remote_cands[i].last_punch_send_ms = now_ms;
                }

                n->last_send_time = now_ms;
            }
            break;

        default:
            break;
    }
}
