/*
 * P2P 信道外可达性探测实现
 *
 * 内部按 signaling_mode 分发，外部统一调用 probe_trigger/probe_tick/probe_reset。
 */

#define MOD_TAG "PROBE"

#include "p2p_internal.h"
#include "p2p_probe.h"
#include "p2p_signal_compact.h"
#include "p2p_signal_relay.h"
#include "p2p_turn.h"

///////////////////////////////////////////////////////////////////////////////
// COMPACT 模式（MSG+echo）
///////////////////////////////////////////////////////////////////////////////

static ret_t probe_compact_trigger(struct p2p_session *s) {

    if (!s->probe_compact_enabled)                  return E_NO_SUPPORT;

    // 需要服务器支持 MSG 机制（REGISTER_ACK flags 含 SIG_REGACK_FLAG_MSG）
    if (!s->sig_compact_ctx.msg_support) {
        print("W:", LA_S("probe(compact) skipped: server does not support MSG", 0, 0));
        return E_NO_SUPPORT;
    }

    // 需要信令状态已达到 REGISTERED（收到 REGISTER_ACK 并分配了 session）
    if (s->sig_compact_ctx.state < SIGNAL_COMPACT_REGISTERED) {
        print("W:", LA_S("probe(compact) skipped: signaling not yet registered", 0, 0));
        return E_NONE_CONTEXT;
    }

    if (s->probe_compact_state != P2P_PROBE_COMPACT_IDLE) {
        print("V:", LA_S("probe(compact) already in progress", 0, 0));
        return E_BUSY;
    }

    s->probe_compact_state   = P2P_PROBE_COMPACT_PENDING;
    s->probe_compact_retries = 0;

    print("I:", LA_S("probe(compact) triggered: MSG echo via server", 0, 0));
    return E_NONE;
}

static void probe_compact_tick(struct p2p_session *s, uint64_t now_ms) {

    switch (s->probe_compact_state) {

        case P2P_PROBE_COMPACT_IDLE:
            break;

        case P2P_PROBE_COMPACT_PENDING:
            // 发送 msg=0 空包（服务器会自动 echo 回复）
            {
                ret_t ret = p2p_signal_compact_request(s, 0, NULL, 0);
                if (ret == E_NONE) {
                    s->probe_compact_state = P2P_PROBE_COMPACT_WAITING;
                    s->probe_compact_sid   = s->sig_compact_ctx.msg_sid;
                    s->probe_compact_start = now_ms;

                    print("I:", LA_F("probe(compact) sent: MSG(msg=0, sid=%u)", 0, 0),
                          s->probe_compact_sid);
                } else {
                    print("W:", LA_F("probe(compact) send failed: ret=%d", 0, 0), ret);
                    s->probe_compact_state = P2P_PROBE_COMPACT_IDLE;
                }
            }
            break;

        case P2P_PROBE_COMPACT_WAITING:
            // 超时检测
            if (now_ms - s->probe_compact_start >= PROBE_COMPACT_TIMEOUT_MS) {
                if (s->probe_compact_retries < PROBE_COMPACT_MAX_RETRIES) {
                    s->probe_compact_retries++;
                    s->probe_compact_state = P2P_PROBE_COMPACT_PENDING;
                    print("W:", LA_F("probe(compact) timeout, retry %d/%d", 0, 0),
                          s->probe_compact_retries, PROBE_COMPACT_MAX_RETRIES);
                } else {
                    s->probe_compact_state    = P2P_PROBE_COMPACT_TIMEOUT;
                    s->probe_compact_complete = now_ms;
                    print("W:", LA_S("probe(compact) timeout: server cannot reach peer", 0, 0));
                }
            }
            break;

        case P2P_PROBE_COMPACT_SUCCESS:
        case P2P_PROBE_COMPACT_PEER_OFFLINE:
        case P2P_PROBE_COMPACT_TIMEOUT:
            // 等待间隔后自动重新触发（定时重复探测）
            if (s->probe_compact_complete == 0)
                s->probe_compact_complete = now_ms;

            if (now_ms - s->probe_compact_complete >= PROBE_COMPACT_REPEAT_INTERVAL) {
                print("V:", LA_S("probe(compact) restarting periodic check", 0, 0));
                s->probe_compact_state    = P2P_PROBE_COMPACT_IDLE;
                s->probe_compact_sid      = 0;
                s->probe_compact_start    = 0;
                s->probe_compact_complete = 0;
                s->probe_compact_retries  = 0;
            }
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////
// RELAY 模式（TURN 刷新 → 地址交换 → UDP 探测）
///////////////////////////////////////////////////////////////////////////////

static ret_t probe_relay_trigger(struct p2p_session *s) {

    if (!s->probe_relay_enabled)                    return E_NO_SUPPORT;

    // 需要 TCP 信令通道已登录（SIGNAL_CONNECTED）
    if (s->sig_relay_ctx.state != SIGNAL_CONNECTED) {
        print("W:", LA_S("probe(relay) skipped: relay signaling not connected", 0, 0));
        return E_NONE_CONTEXT;
    }

    if (s->probe_relay_state != P2P_PROBE_RELAY_IDLE) {
        print("V:", LA_S("probe(relay) already in progress", 0, 0));
        return E_BUSY;
    }

    s->probe_relay_state   = P2P_PROBE_RELAY_REFRESH_TURN;
    s->probe_relay_retries = 0;

    print("I:", LA_S("probe(relay) triggered: refreshing TURN allocation", 0, 0));
    return E_NONE;
}

static void probe_relay_tick(struct p2p_session *s, uint64_t now_ms) {

    switch (s->probe_relay_state) {

        case P2P_PROBE_RELAY_IDLE:
            break;

        case P2P_PROBE_RELAY_REFRESH_TURN:
            // 步骤1：重新分配 TURN 地址
            {
                ret_t ret = p2p_turn_allocate(s);
                if (ret == E_NONE) {
                    s->probe_relay_state = P2P_PROBE_RELAY_WAIT_TURN;
                    s->probe_relay_start = now_ms;
                    print("I:", LA_S("probe(relay) TURN allocation request sent", 0, 0));
                } else {
                    print("W:", LA_F("probe(relay) TURN allocation failed: ret=%d", 0, 0), ret);
                    s->probe_relay_state    = P2P_PROBE_RELAY_TIMEOUT;
                    s->probe_relay_complete = now_ms;
                }
            }
            break;

        case P2P_PROBE_RELAY_WAIT_TURN:
            // 等待 TURN 响应，超时重试
            if (now_ms - s->probe_relay_start >= PROBE_RELAY_TURN_TIMEOUT_MS) {
                if (s->probe_relay_retries < PROBE_RELAY_MAX_RETRIES) {
                    s->probe_relay_retries++;
                    s->probe_relay_state = P2P_PROBE_RELAY_REFRESH_TURN;
                    print("W:", LA_F("probe(relay) TURN timeout, retry %d/%d", 0, 0),
                          s->probe_relay_retries, PROBE_RELAY_MAX_RETRIES);
                } else {
                    s->probe_relay_state    = P2P_PROBE_RELAY_TIMEOUT;
                    s->probe_relay_complete = now_ms;
                    print("W:", LA_S("probe(relay) TURN allocation timeout", 0, 0));
                }
            }
            break;

        case P2P_PROBE_RELAY_EXCHANGE_ADDR:
            // 步骤2：通过信令服务器交换新地址
            // （实际发送在 probe_relay_on_turn_success 进入此状态时触发，
            //   此处仅做超时等待）
            s->probe_relay_state = P2P_PROBE_RELAY_WAIT_EXCHANGE;
            s->probe_relay_start = now_ms;
            print("I:", LA_S("probe(relay) address exchange initiated", 0, 0));
            // TODO: p2p_signal_relay_send_connect(&s->sig_relay_ctx, target, NULL, 0);
            break;

        case P2P_PROBE_RELAY_WAIT_EXCHANGE:
            // 等待地址交换完成，超时视为对端离线
            if (now_ms - s->probe_relay_start >= PROBE_RELAY_EXCHANGE_TIMEOUT_MS) {
                if (s->probe_relay_retries < PROBE_RELAY_MAX_RETRIES) {
                    s->probe_relay_retries++;
                    s->probe_relay_state = P2P_PROBE_RELAY_EXCHANGE_ADDR;
                    print("W:", LA_F("probe(relay) exchange timeout, retry %d/%d", 0, 0),
                          s->probe_relay_retries, PROBE_RELAY_MAX_RETRIES);
                } else {
                    s->probe_relay_state    = P2P_PROBE_RELAY_PEER_OFFLINE;
                    s->probe_relay_complete = now_ms;
                    print("W:", LA_S("probe(relay) exchange timeout: peer offline?", 0, 0));
                }
            }
            break;

        case P2P_PROBE_RELAY_PROBE_UDP:
            // 步骤3：发送 UDP 探测包（通过 TURN 中继）
            s->probe_relay_state = P2P_PROBE_RELAY_WAIT_PROBE;
            s->probe_relay_start = now_ms;
            print("I:", LA_S("probe(relay) UDP probe packet sent", 0, 0));
            // TODO: udp_send_packet(s->sock, &peer_addr, P2P_PKT_PUNCH, 0, ++seq, NULL, 0);
            break;

        case P2P_PROBE_RELAY_WAIT_PROBE:
            // 等待 UDP 响应，超时视为网络故障
            if (now_ms - s->probe_relay_start >= PROBE_RELAY_UDP_TIMEOUT_MS) {
                if (s->probe_relay_retries < PROBE_RELAY_MAX_RETRIES) {
                    s->probe_relay_retries++;
                    s->probe_relay_state = P2P_PROBE_RELAY_PROBE_UDP;
                    print("W:", LA_F("probe(relay) UDP timeout, retry %d/%d", 0, 0),
                          s->probe_relay_retries, PROBE_RELAY_MAX_RETRIES);
                } else {
                    s->probe_relay_state    = P2P_PROBE_RELAY_TIMEOUT;
                    s->probe_relay_complete = now_ms;
                    print("W:", LA_S("probe(relay) UDP timeout: network issue", 0, 0));
                }
            }
            break;

        case P2P_PROBE_RELAY_SUCCESS:
        case P2P_PROBE_RELAY_PEER_OFFLINE:
        case P2P_PROBE_RELAY_TIMEOUT:
            // 等待间隔后自动重新触发（定时重复探测）
            if (s->probe_relay_complete == 0)
                s->probe_relay_complete = now_ms;

            if (now_ms - s->probe_relay_complete >= PROBE_RELAY_REPEAT_INTERVAL) {
                print("V:", LA_S("probe(relay) restarting periodic check", 0, 0));
                s->probe_relay_state    = P2P_PROBE_RELAY_IDLE;
                s->probe_relay_start    = 0;
                s->probe_relay_complete = 0;
                s->probe_relay_retries  = 0;
            }
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////
// 统一外部接口
///////////////////////////////////////////////////////////////////////////////

ret_t probe_trigger(struct p2p_session *s) {
    switch (s->signaling_mode) {
        case P2P_SIGNALING_MODE_COMPACT: return probe_compact_trigger(s);
        case P2P_SIGNALING_MODE_RELAY:   return probe_relay_trigger(s);
        default:                         return E_NO_SUPPORT;
    }
}

void probe_tick(struct p2p_session *s, uint64_t now_ms) {
    switch (s->signaling_mode) {
        case P2P_SIGNALING_MODE_COMPACT: probe_compact_tick(s, now_ms); break;
        case P2P_SIGNALING_MODE_RELAY:   probe_relay_tick(s, now_ms);   break;
        default: break;
    }
}

void probe_reset(struct p2p_session *s) {
    // 同时重置两侧（防止模式切换时遗留脏状态）
    s->probe_compact_state    = P2P_PROBE_COMPACT_IDLE;
    s->probe_compact_sid      = 0;
    s->probe_compact_start    = 0;
    s->probe_compact_complete = 0;
    s->probe_compact_retries  = 0;

    s->probe_relay_state    = P2P_PROBE_RELAY_IDLE;
    s->probe_relay_start    = 0;
    s->probe_relay_complete = 0;
    s->probe_relay_retries  = 0;
}

///////////////////////////////////////////////////////////////////////////////
// COMPACT 回调
///////////////////////////////////////////////////////////////////////////////

void probe_compact_on_req_ack(struct p2p_session *s, uint16_t sid, uint8_t status) {

    if (s->probe_compact_state != P2P_PROBE_COMPACT_WAITING) return;
    if (s->probe_compact_sid   != sid)                        return;

    if (status == 0) {
        // 服务器已向对端转发，保持 WAITING 继续等待 echo 回复
        print("I:", LA_S("probe(compact) REQ_ACK: peer is online, waiting echo", 0, 0));
    } else {
        s->probe_compact_state    = P2P_PROBE_COMPACT_PEER_OFFLINE;
        s->probe_compact_complete = 0;
        print("W:", LA_S("probe(compact) peer is OFFLINE (REQ_ACK status=1)", 0, 0));
    }
}

void probe_compact_on_response(struct p2p_session *s, uint16_t sid) {

    if (s->probe_compact_state != P2P_PROBE_COMPACT_WAITING) return;
    if (s->probe_compact_sid   != sid)                        return;

    P_clock _clk; P_clock_now(&_clk);
    uint64_t now_ms = clock_ms(_clk);
    uint64_t rtt    = now_ms - s->probe_compact_start;

    s->probe_compact_state    = P2P_PROBE_COMPACT_SUCCESS;
    s->probe_compact_complete = now_ms;

    print("I:", LA_F("probe(compact) SUCCESS: peer reachable via server (RTT: %" PRIu64 " ms)", 0, 0), rtt);
}

///////////////////////////////////////////////////////////////////////////////
// RELAY 回调
///////////////////////////////////////////////////////////////////////////////

void probe_relay_on_turn_success(struct p2p_session *s) {

    if (s->probe_relay_state != P2P_PROBE_RELAY_WAIT_TURN) return;

    s->probe_relay_state = P2P_PROBE_RELAY_EXCHANGE_ADDR;
    print("I:", LA_S("probe(relay) TURN allocated, starting address exchange", 0, 0));
}

void probe_relay_on_exchange_done(struct p2p_session *s, bool success) {

    if (s->probe_relay_state != P2P_PROBE_RELAY_WAIT_EXCHANGE) return;

    if (success) {
        s->probe_relay_state = P2P_PROBE_RELAY_PROBE_UDP;
        print("I:", LA_S("probe(relay) address exchange success, sending UDP probe", 0, 0));
    } else {
        P_clock _clk; P_clock_now(&_clk);
        s->probe_relay_state    = P2P_PROBE_RELAY_PEER_OFFLINE;
        s->probe_relay_complete = clock_ms(_clk);
        print("W:", LA_S("probe(relay) address exchange failed: peer OFFLINE", 0, 0));
    }
}

void probe_relay_on_udp_response(struct p2p_session *s) {

    if (s->probe_relay_state != P2P_PROBE_RELAY_WAIT_PROBE) return;

    P_clock _clk; P_clock_now(&_clk);
    uint64_t now_ms = clock_ms(_clk);
    uint64_t rtt    = now_ms - s->probe_relay_start;

    s->probe_relay_state    = P2P_PROBE_RELAY_SUCCESS;
    s->probe_relay_complete = now_ms;

    print("I:", LA_F("probe(relay) SUCCESS: UDP reachable via TURN (RTT: %" PRIu64 " ms)", 0, 0), rtt);
}
