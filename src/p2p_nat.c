/*
 * NAT 穿透实现（纯打洞逻辑）
 *
 * 只负责 PUNCH/ACK/PING/PONG 的发送和接收。
 * 候选列表统一存储在 p2p_session 中，本模块从 session 读取远端候选进行打洞。
 */

#define MOD_TAG "NAT"

#include "p2p_internal.h"

#define PUNCH_INTERVAL_MS       500         /* 打洞间隔 */
#define PUNCH_TIMEOUT_MS        5000        /* 打洞超时 */
#define PING_INTERVAL_MS        15000       /* 心跳间隔 */
#define PONG_TIMEOUT_MS         30000       /* 心跳超时 */

static int nat_find_remote_candidate_index(p2p_session_t *s, const struct sockaddr_in *to) {

    for (int i = 0; i < s->remote_cand_cnt; i++) {
        if (s->remote_cands[i].cand.addr.sin_addr.s_addr == to->sin_addr.s_addr &&
            s->remote_cands[i].cand.addr.sin_port == to->sin_port) {
            return i;
        }
    }
    return -1;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 初始化打洞上下文
 */
void nat_init(nat_ctx_t *n) {
    memset(n, 0, sizeof(*n));
    n->state = NAT_INIT;
}

/*
 * NAT 打洞（统一接口，支持批量启动和单候选追加）
 *
 * @param s        会话对象
 * @param addr     目标地址（NULL=批量启动所有候选，非NULL=单个候选打洞）
 * @param verbose  详细日志开关（仅批量启动时有效）
 * @return         0=成功，-1=失败（无候选）
 *
 * 用法：
 *   - nat_punch(s, NULL, verbose)  批量启动所有 remote_cands 的打洞
 *   - nat_punch(s, &addr, 0)       向单个候选追加打洞（Trickle ICE）
 */
int nat_punch(p2p_session_t *s, const struct sockaddr_in *addr) {
    if (!s) return -1;
    
    nat_ctx_t *n = &s->nat;
    P_clock _clk; P_clock_now(&_clk);
    uint64_t now = clock_ms(_clk);
    
    /* ========== 首次批量或重新启动模式：addr == NULL ========== */

    // todo 如果已经是 connected 状态，是否需要重启打洞？
    if (addr == NULL) {
        if (s->remote_cand_cnt == 0) {
            printf("E: %s", LA_S("ERROR: No remote candidates to punch", LA_S22, 171));
            return -1;
        }
        
        // 启动 PUNCHING 状态
        n->state = NAT_PUNCHING;
        n->punch_start = now;
        n->peer_addr = s->remote_cands[0].cand.addr;  /* 默认值，收到 ACK 时会更新 */

        // 打印详细日志
        if (p2p_get_log_level() == P2P_LOG_LEVEL_VERBOSE) {

            // Verbose: START punching (output disabled in current implementation)

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

                // Verbose: candidate details (output disabled)
            }
        }

        // 立即向所有候选并行发送打洞包（echo_seq=0 表示探测包）
        uint8_t probe_payload[2] = {0, 0};  // echo_seq = 0
        for (int i = 0; i < s->remote_cand_cnt; i++) {
            udp_send_packet(s->sock, &s->remote_cands[i].cand.addr, P2P_PKT_PUNCH, 0, ++n->punch_seq, probe_payload, 2);
            s->remote_cands[i].last_punch_send_ms = now;
        }
        n->last_send_time = now;
        
        return 0;
    }
    
    /* ========== Trickle 单候选打洞模式：addr != NULL ========== */
    
    // RELAY 模式下收到新候选时，重启打洞窗口（Trickle ICE 触发）
    if (n->state != NAT_CONNECTED) {

        if (n->state == NAT_INIT)
            n->peer_addr = s->remote_cands[0].cand.addr;

        n->state = NAT_PUNCHING;
        n->punch_start = now;

        // Verbose: restarting punch (output disabled)

        // 发送打洞包（echo_seq=0 表示探测包）
        uint8_t probe_payload[2] = {0, 0};
        udp_send_packet(s->sock, addr, P2P_PKT_PUNCH, 0, ++n->punch_seq, probe_payload, 2);
        n->last_send_time = now;

        // 更新候选的时间戳
        int idx = nat_find_remote_candidate_index(s, addr);
        if (idx >= 0 && idx < s->remote_cand_cnt) {
            s->remote_cands[idx].last_punch_send_ms = now;
        }
    }
    else {

        // Verbose: ignoring duplicate punch (output disabled)
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 处理打洞相关数据包
 */
int nat_on_packet(p2p_session_t *s, const p2p_packet_hdr_t *hdr,
                  const uint8_t *payload, int len, const struct sockaddr_in *from) {

    nat_ctx_t *n = &s->nat;
    P_clock _clk; P_clock_now(&_clk);
    uint64_t now = clock_ms(_clk);

    switch (hdr->type) {

    case P2P_PKT_PUNCH: {
        
        // 解析 echo_seq（2字节网络字节序）
        uint16_t echo_seq = 0;
        if (len >= 2) {
            echo_seq = ((uint16_t)payload[0] << 8) | payload[1];
        }

        // 如果 echo_seq 为 0，说明这是探测包，需要回复确认
        if (echo_seq == 0) {
            // Verbose: received probe
            
            // 回复确认包：echo 对方的 seq
            uint8_t reply_payload[2];
            reply_payload[0] = (uint8_t)(hdr->seq >> 8);
            reply_payload[1] = (uint8_t)(hdr->seq & 0xFF);
            
            udp_send_packet(s->sock, from, P2P_PKT_PUNCH, 0, ++n->punch_seq, reply_payload, 2);
            
            // Verbose: sent confirmation
        }
        // 如果 echo_seq 不为 0，说明这是对我之前 PUNCH 的确认
        else if (echo_seq == n->punch_seq) {
            // Verbose: received valid confirmation -> NAT_CONNECTED
        } else {
            // Verbose: received stale confirmation (echo_seq mismatch)
        }

        // 通知 ICE 层（如果启用）
        if (s->cfg.use_ice) {
            p2p_ice_on_check_success(s, from);
        }

        // 标记连接成功
        if (n->state == NAT_PUNCHING || n->state == NAT_RELAY) {
            n->peer_addr = *from;
            n->state = NAT_CONNECTED;
            n->last_recv_time = now;
            
            // Info: SUCCESS
        }
        
        // 更新最后接收时间（保活）
        n->last_recv_time = now;

        return 0;
    }

    default:
        return 1;  /* 未处理 */
    }
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 周期调用，发送打洞包和心跳
 */
int nat_tick(p2p_session_t *s) {
    nat_ctx_t *n = &s->nat;

    P_clock _clk; P_clock_now(&_clk);
    uint64_t now = clock_ms(_clk);
    switch (n->state) {

        case NAT_PUNCHING:

            // 超时判断
            if (now - n->punch_start >= PUNCH_TIMEOUT_MS) {
                
                printf("W:", LA_F("%s (%llu ms), %s", LA_F32, 289),
                       LA_W("TIMEOUT: Punch failed after", LA_W111, 134),
                       now - n->punch_start, LA_W("attempts, switching to RELAY", LA_W9, 12));

                n->state = NAT_RELAY;
                return 0;
            }

            int sent_cnt = 0;
            for (int i = 0; i < s->remote_cand_cnt; i++) {

                bool should_send = false;
                if (s->remote_cands[i].last_punch_send_ms == 0 ||
                    now - s->remote_cands[i].last_punch_send_ms >= PUNCH_INTERVAL_MS) {
                    should_send = true;
                    s->remote_cands[i].last_punch_send_ms = now;
                }

                if (should_send) {

                    printf("D:", LA_F("%s %s:%d (candidate %d)", LA_F21, 278), "",
                                  inet_ntoa(s->remote_cands[i].cand.addr.sin_addr),
                                  ntohs(s->remote_cands[i].cand.addr.sin_port), i);

                    nat_punch(s, &s->remote_cands[i].cand.addr);
                    sent_cnt++;
                }
            }

            // 如果存在未完成的 nat 打洞
            if (sent_cnt > 0) {

                n->last_send_time = now;

                printf("V:", LA_F("%s %s %d/%d %s (elapsed: %llu ms)", LA_F8, 265), LA_W("PUNCHING: Attempt", LA_W75, 89),
                                LA_S("to", LA_S79, 209), sent_cnt,
                                s->remote_cand_cnt, LA_W("candidates", LA_W17, 21),
                                now - n->punch_start);
            }
            break;

        case NAT_CONNECTED:

            // 发送保活包（复用 PUNCH 包，echo_seq=0）
            if (now - n->last_send_time >= PING_INTERVAL_MS) {

// Verbose: KEEPALIVE

                uint8_t probe_payload[2] = {0, 0};
                udp_send_packet(s->sock, &n->peer_addr, P2P_PKT_PUNCH, 0, ++n->punch_seq, probe_payload, 2);
                n->last_send_time = now;
            }

            // 超时检查
            if (n->last_recv_time > 0 && now - n->last_recv_time >= PONG_TIMEOUT_MS) {

                // Warning: connection lost

                n->state = NAT_DISCONNECTED;
                return -1;
            }
            break;

        case NAT_DISCONNECTED:
            // 连接断开，不再自动重连（由上层决定是否重启打洞或切换到 RELAY）
            break;

        case NAT_RELAY:

            // 中继模式下周期性尝试直连（echo_seq=0）
            if (now - n->last_send_time >= PUNCH_INTERVAL_MS * 4) {
                uint8_t probe_payload[2] = {0, 0};
                for (int i = 0; i < s->remote_cand_cnt; i++) {
                    udp_send_packet(s->sock, &s->remote_cands[i].cand.addr, P2P_PKT_PUNCH, 0, ++n->punch_seq, probe_payload, 2);
                    s->remote_cands[i].last_punch_send_ms = now;
                }
                n->last_send_time = now;
            }
            break;

        default:
            break;
    }

    return 0;
}
