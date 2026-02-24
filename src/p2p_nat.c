
/*
 * NAT 穿透实现（纯打洞逻辑）
 *
 * 只负责 PUNCH/ACK/PING/PONG 的发送和接收。
 * 候选列表统一存储在 p2p_session 中，本模块从 session 读取远端候选进行打洞。
 */

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
    uint64_t now = time_ms();
    
    /* ========== 首次批量或重新启动模式：addr == NULL ========== */

    if (addr == NULL) {
        if (s->remote_cand_cnt == 0) {
            P2P_LOG_ERROR("NAT", "%s", MSG(MSG_NAT_PUNCH_ERROR_NO_CAND));
            return -1;
        }
        
        // 启动 PUNCHING 状态
        n->state = NAT_PUNCHING;
        n->punch_start = now;
        n->peer_addr = s->remote_cands[0].cand.addr;  /* 默认值，收到 ACK 时会更新 */

        // 打印详细日志
        if (p2p_get_log_level() == P2P_LOG_LEVEL_VERBOSE) {

            P2P_LOG_VERBOSE("NAT", "%s %d %s", MSG(MSG_NAT_PUNCH_START),
                            s->remote_cand_cnt, MSG(MSG_NAT_PUNCH_CANDIDATES));

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

                P2P_LOG_VERBOSE("NAT", "  [%d] %s: %s:%d", i, type_str,
                                inet_ntoa(s->remote_cands[i].cand.addr.sin_addr),
                                ntohs(s->remote_cands[i].cand.addr.sin_port));
            }
        }

        // 立即向所有候选并行发送打洞包
        for (int i = 0; i < s->remote_cand_cnt; i++) {
            udp_send_packet(s->sock, &s->remote_cands[i].cand.addr, P2P_PKT_PUNCH, 0, 0, NULL, 0);
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

        P2P_LOG_VERBOSE("NAT", "Restart punching from RELAY on new candidate %s:%d",
                        inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));

        // 发送打洞包
        udp_send_packet(s->sock, addr, P2P_PKT_PUNCH, 0, 0, NULL, 0);
        n->last_send_time = now;

        // 更新候选的时间戳
        int idx = nat_find_remote_candidate_index(s, addr);
        if (idx >= 0 && idx < s->remote_cand_cnt) {
            s->remote_cands[idx].last_punch_send_ms = now;
        }
    }
    else {

        P2P_LOG_VERBOSE("NAT", "Ignore punch request to %s:%d since already connected",
                        inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 处理打洞相关数据包
 */
int nat_on_packet(p2p_session_t *s, uint8_t type, const uint8_t *payload, int len, const struct sockaddr_in *from) {
    (void)payload; (void)len;

    nat_ctx_t *n = &s->nat;    
    uint64_t now = time_ms();

    switch (type) {

    case P2P_PKT_PUNCH:

        // 回复应答包
        udp_send_packet(s->sock, from, P2P_PKT_PUNCH_ACK, 0, 0, NULL, 0);

        /* fall through - 收到 PUNCH 也算成功 */

    case P2P_PKT_PUNCH_ACK:

        P2P_LOG_VERBOSE("NAT", "%s %s:%d", type == P2P_PKT_PUNCH ? MSG(MSG_NAT_PUNCH_RECEIVED) : MSG(MSG_NAT_PUNCH_ACK_RECEIVED),
                        inet_ntoa(from->sin_addr), ntohs(from->sin_port));

        // 通知 ICE 层（如果启用）
        if (s->cfg.use_ice) {

            p2p_ice_on_check_success(s, from);
        }

        // 标记连接成功
        if (n->state == NAT_PUNCHING || n->state == NAT_RELAY) {
            n->peer_addr = *from;
            n->state = NAT_CONNECTED;
            n->last_recv_time = now;
            
            P2P_LOG_INFO("NAT", "%s %s:%d", MSG(MSG_NAT_PUNCH_SUCCESS),
                         inet_ntoa(from->sin_addr), ntohs(from->sin_port));

            P2P_LOG_INFO("NAT", "  %s %llu ms", MSG(MSG_NAT_PUNCH_TIME), now - n->punch_start);
        }
        return 0;

    case P2P_PKT_PING:

        // 回复 PONG
        udp_send_packet(s->sock, from, P2P_PKT_PONG, 0, 0, NULL, 0);

        /* fall through */

    case P2P_PKT_PONG:

        n->last_recv_time = now;

        P2P_LOG_VERBOSE("NAT", "%s %s:%d", type == P2P_PKT_PONG ? "received PONG from" : "received PING from",
                        inet_ntoa(from->sin_addr), ntohs(from->sin_port));

        return 0;

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

    uint64_t now = time_ms();
    switch (n->state) {

        case NAT_PUNCHING:

            // 超时判断
            if (now - n->punch_start >= PUNCH_TIMEOUT_MS) {
                
                P2P_LOG_WARN("NAT", "%s (%llu ms), %s", MSG(MSG_NAT_PUNCH_TIMEOUT),
                             now - n->punch_start, MSG(MSG_NAT_PUNCH_SWITCH_RELAY));

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

                    P2P_LOG_DEBUG("NAT", "%s %s:%d (candidate %d)", "",
                                  inet_ntoa(s->remote_cands[i].cand.addr.sin_addr),
                                  ntohs(s->remote_cands[i].cand.addr.sin_port), i);

                    nat_punch(s, &s->remote_cands[i].cand.addr);
                    sent_cnt++;
                }
            }

            // 如果存在未完成的 nat 打洞
            if (sent_cnt > 0) {

                n->last_send_time = now;

                P2P_LOG_VERBOSE("NAT", "%s %s %d/%d %s (elapsed: %llu ms)", MSG(MSG_NAT_PUNCH_PUNCHING),
                                MSG(MSG_NAT_PUNCH_TO), sent_cnt,
                                s->remote_cand_cnt, MSG(MSG_NAT_PUNCH_CANDIDATES),
                                now - n->punch_start);
            }
            break;

        case NAT_CONNECTED:

            // 发送心跳保活包
            if (now - n->last_send_time >= PING_INTERVAL_MS) {

                P2P_LOG_VERBOSE("NAT", "%s %s:%d", "",
                                inet_ntoa(n->peer_addr.sin_addr), ntohs(n->peer_addr.sin_port));

                udp_send_packet(s->sock, &n->peer_addr, P2P_PKT_PING, 0, 0, NULL, 0);
                n->last_send_time = now;
            }

            // 超时检查
            if (n->last_recv_time > 0 && now - n->last_recv_time >= PONG_TIMEOUT_MS) {

                P2P_LOG_WARN("NAT", "%s (%s %d ms)", MSG(MSG_NAT_PUNCH_CONN_LOST),
                             MSG(MSG_NAT_PUNCH_NO_PONG), PONG_TIMEOUT_MS);

                n->state = NAT_INIT;
                return -1;
            }
            break;

        case NAT_RELAY:

            // 中继模式下周期性尝试直连
            if (now - n->last_send_time >= PUNCH_INTERVAL_MS * 4) {
                for (int i = 0; i < s->remote_cand_cnt; i++) {
                    udp_send_packet(s->sock, &s->remote_cands[i].cand.addr, P2P_PKT_PUNCH, 0, 0, NULL, 0);
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
