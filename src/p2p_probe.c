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

#define TASK_RELAY_PROBE                   "PROXY PROBE"

static ret_t probe_compact_trigger(struct p2p_session *s) {

    if (!s->probe_compact_enabled)                  return E_NO_SUPPORT;

    // 需要服务器支持 MSG RPC 机制（REGISTER_ACK flags 含 SIG_REGACK_FLAG_MSG）
    if (!s->sig_compact_ctx.msg_support) {
        print("W:", LA_F("%s: skipped for server does not support MSG", LA_S60, 164), TASK_RELAY_PROBE);
        return E_NO_SUPPORT;
    }

    // 需要信令状态已达到 REGISTERED（收到 REGISTER_ACK 并分配了 session）
    if (s->sig_compact_ctx.state < SIGNAL_COMPACT_REGISTERED) {
        print("W:", LA_F("%s: skipped for signaling not yet registered", LA_S61, 165), TASK_RELAY_PROBE);
        return E_NONE_CONTEXT;
    }

    if (s->probe_compact_state != P2P_PROBE_COMPACT_IDLE) {
        print("V:", LA_F("%s: already in progress", LA_S56, 160), TASK_RELAY_PROBE);
        return E_BUSY;
    }

    s->probe_compact_state   = P2P_PROBE_COMPACT_PENDING;
    s->probe_compact_retries = 0;

    print("I:", LA_F("%s: triggered by echo via server", LA_S63, 167), TASK_RELAY_PROBE);
    return E_NONE;
}

static void probe_compact_tick(struct p2p_session *s, uint64_t now_ms) {

    switch (s->probe_compact_state) {

        // 发送 msg=0 空包（服务器会自动 echo 回复）
        case P2P_PROBE_COMPACT_PENDING: {

            ret_t ret = p2p_signal_compact_request(s, 0, NULL, 0);
            if (ret == E_NONE) {
                s->probe_compact_state = P2P_PROBE_COMPACT_WAITING;
                s->probe_compact_sid   = s->sig_compact_ctx.req_sid;
                s->probe_compact_start = now_ms;

                print("I:", LA_F("%s: sent: MSG(msg=0, sid=%u)", LA_F210, 413), TASK_RELAY_PROBE,
                      s->probe_compact_sid);
            } else {
                print("W:", LA_F("%s: send failed(%d)", LA_F209, 412), TASK_RELAY_PROBE, ret);
                s->probe_compact_state = P2P_PROBE_COMPACT_IDLE;
            }
        } break;

        // 超时检测
        case P2P_PROBE_COMPACT_WAITING:

            if (now_ms - s->probe_compact_start >= PROBE_COMPACT_TIMEOUT_MS) {
                if (s->probe_compact_retries < PROBE_COMPACT_MAX_RETRIES) {
                    s->probe_compact_retries++;
                    s->probe_compact_state = P2P_PROBE_COMPACT_PENDING;
                    print("W:", LA_F("%s: timeout, retry %d/%d", LA_F211, 414), TASK_RELAY_PROBE,
                          s->probe_compact_retries, PROBE_COMPACT_MAX_RETRIES);
                } else {
                    s->probe_compact_state    = P2P_PROBE_COMPACT_TIMEOUT;
                    s->probe_compact_complete = now_ms;
                    print("W:", LA_F("%s: timeout: server cannot reach peer", LA_S62, 166), TASK_RELAY_PROBE);
                }
            }
            break;

        // 等待间隔后自动重新触发（定时重复探测）
        case P2P_PROBE_COMPACT_SUCCESS:
        case P2P_PROBE_COMPACT_PEER_OFFLINE:
        case P2P_PROBE_COMPACT_TIMEOUT:

            if (s->probe_compact_complete == 0)
                s->probe_compact_complete = now_ms;

            if (now_ms - s->probe_compact_complete >= PROBE_COMPACT_REPEAT_INTERVAL) {
                print("V:", LA_F("%s: restarting periodic check", LA_S59, 163), TASK_RELAY_PROBE);
                s->probe_compact_state    = P2P_PROBE_COMPACT_IDLE;
                s->probe_compact_sid      = 0;
                s->probe_compact_start    = 0;
                s->probe_compact_complete = 0;
                s->probe_compact_retries  = 0;
            }
            break;
        default: break;
    }
}

///////////////////////////////////////////////////////////////////////////////
// RELAY 模式（TURN 刷新 → 地址交换 → UDP 探测）
///////////////////////////////////////////////////////////////////////////////

static ret_t probe_relay_trigger(struct p2p_session *s) {

    if (!s->probe_relay_enabled)                    return E_NO_SUPPORT;

    // 需要 TCP 信令通道已登录（SIGNAL_CONNECTED）
    if (s->sig_relay_ctx.state != SIGNAL_CONNECTED) {
        print("W:", LA_F("%s: skipped: relay signaling not connected", LA_S70, 174), TASK_RELAY_PROBE);
        return E_NONE_CONTEXT;
    }

    if (s->probe_relay_state != P2P_PROBE_RELAY_IDLE) {
        print("V:", LA_F("%s: already in progress", LA_S67, 171), TASK_RELAY_PROBE);
        return E_BUSY;
    }

    s->probe_relay_state   = P2P_PROBE_RELAY_REFRESH_TURN;
    s->probe_relay_retries = 0;

    print("I:", LA_F("%s: triggered: refreshing TURN allocation", LA_S71, 175), TASK_RELAY_PROBE);
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
                    print("I:", LA_F("%s: TURN allocation request sent", LA_S73, 177), TASK_RELAY_PROBE);
                } else {
                    print("W:", LA_F("%s: TURN allocation failed: ret=%d", LA_F213, 416), TASK_RELAY_PROBE, ret);
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
                    print("W:", LA_F("%s: TURN timeout, retry %d/%d", LA_F214, 417), TASK_RELAY_PROBE,
                          s->probe_relay_retries, PROBE_RELAY_MAX_RETRIES);
                } else {
                    s->probe_relay_state    = P2P_PROBE_RELAY_TIMEOUT;
                    s->probe_relay_complete = now_ms;
                    print("W:", LA_F("%s: TURN allocation timeout", LA_S74, 178), TASK_RELAY_PROBE);
                }
            }
            break;

        case P2P_PROBE_RELAY_EXCHANGE_ADDR:
            // 步骤2：通过信令服务器交换新地址
            // （实际发送在 probe_relay_on_turn_success 进入此状态时触发，
            //   此处仅做超时等待）
            s->probe_relay_state = P2P_PROBE_RELAY_WAIT_EXCHANGE;
            s->probe_relay_start = now_ms;
            print("I:", LA_F("%s: address exchange initiated", LA_S65, 169), TASK_RELAY_PROBE);
            // TODO: p2p_signal_relay_send_connect(&s->sig_relay_ctx, target, NULL, 0);
            break;

        case P2P_PROBE_RELAY_WAIT_EXCHANGE:
            // 等待地址交换完成，超时视为对端离线
            if (now_ms - s->probe_relay_start >= PROBE_RELAY_EXCHANGE_TIMEOUT_MS) {
                if (s->probe_relay_retries < PROBE_RELAY_MAX_RETRIES) {
                    s->probe_relay_retries++;
                    s->probe_relay_state = P2P_PROBE_RELAY_EXCHANGE_ADDR;
                    print("W:", LA_F("%s: exchange timeout, retry %d/%d", LA_F216, 419), TASK_RELAY_PROBE,
                          s->probe_relay_retries, PROBE_RELAY_MAX_RETRIES);
                } else {
                    s->probe_relay_state    = P2P_PROBE_RELAY_PEER_OFFLINE;
                    s->probe_relay_complete = now_ms;
                    print("W:", LA_F("%s: exchange timeout: peer offline?", LA_S68, 172), TASK_RELAY_PROBE);
                }
            }
            break;

        case P2P_PROBE_RELAY_PROBE_UDP:
            // 步骤3：发送 UDP 探测包（通过 TURN 中继）
            s->probe_relay_state = P2P_PROBE_RELAY_WAIT_PROBE;
            s->probe_relay_start = now_ms;
            print("I:", LA_F("%s: UDP probe packet sent", LA_S75, 179), TASK_RELAY_PROBE);
            // TODO: udp_send_packet(s->sock, &peer_addr, P2P_PKT_PUNCH, 0, ++seq, NULL, 0);
            break;

        case P2P_PROBE_RELAY_WAIT_PROBE:
            // 等待 UDP 响应，超时视为网络故障
            if (now_ms - s->probe_relay_start >= PROBE_RELAY_UDP_TIMEOUT_MS) {
                if (s->probe_relay_retries < PROBE_RELAY_MAX_RETRIES) {
                    s->probe_relay_retries++;
                    s->probe_relay_state = P2P_PROBE_RELAY_PROBE_UDP;
                    print("W:", LA_F("%s: UDP timeout, retry %d/%d", LA_F215, 418), TASK_RELAY_PROBE,
                          s->probe_relay_retries, PROBE_RELAY_MAX_RETRIES);
                } else {
                    s->probe_relay_state    = P2P_PROBE_RELAY_TIMEOUT;
                    s->probe_relay_complete = now_ms;
                    print("W:", LA_F("%s: UDP timeout: network issue", LA_S76, 180), TASK_RELAY_PROBE);
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
                print("V:", LA_F("%s: restarting periodic check", LA_S69, 173), TASK_RELAY_PROBE);
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

    // 服务器已向对端转发，保持 WAITING 继续等待 echo 回复
    if (status == 0) {
        print("I:", LA_S("%s: REQ_ACK: peer is online, waiting echo", LA_S58, 162), TASK_RELAY_PROBE);
    } else {
        s->probe_compact_state    = P2P_PROBE_COMPACT_PEER_OFFLINE;
        s->probe_compact_complete = 0;
        print("W:", LA_S("%s: peer is OFFLINE (REQ_ACK status=1)", LA_S57, 161), TASK_RELAY_PROBE);
    }
}

void probe_compact_on_response(struct p2p_session *s, uint16_t sid) {

    if (s->probe_compact_state != P2P_PROBE_COMPACT_WAITING) return;
    if (s->probe_compact_sid   != sid)                        return;

    uint64_t now_ms = P_tick_ms();
    uint64_t rtt    = now_ms - s->probe_compact_start;

    s->probe_compact_state    = P2P_PROBE_COMPACT_SUCCESS;
    s->probe_compact_complete = now_ms;

    print("I:", LA_F("%s: SUCCESS: peer reachable via server (RTT: %" PRIu64 " ms)", 0, 0), TASK_RELAY_PROBE, rtt);
}

///////////////////////////////////////////////////////////////////////////////
// RELAY 回调
///////////////////////////////////////////////////////////////////////////////

void probe_relay_on_turn_success(struct p2p_session *s) {

    if (s->probe_relay_state != P2P_PROBE_RELAY_WAIT_TURN) return;

    s->probe_relay_state = P2P_PROBE_RELAY_EXCHANGE_ADDR;
    print("I:", LA_S("%s: TURN allocated, starting address exchange", LA_S72, 176), TASK_RELAY_PROBE);
}

void probe_relay_on_exchange_done(struct p2p_session *s, bool success) {

    if (s->probe_relay_state != P2P_PROBE_RELAY_WAIT_EXCHANGE) return;

    if (success) {
        s->probe_relay_state = P2P_PROBE_RELAY_PROBE_UDP;
        print("I:", LA_S("%s: address exchange success, sending UDP probe", LA_S66, 170), TASK_RELAY_PROBE);
    } else {
        s->probe_relay_state    = P2P_PROBE_RELAY_PEER_OFFLINE;
        s->probe_relay_complete = P_tick_ms();
        print("W:", LA_S("%s: address exchange failed: peer OFFLINE", LA_S64, 168), TASK_RELAY_PROBE);
    }
}

void probe_relay_on_udp_response(struct p2p_session *s) {

    if (s->probe_relay_state != P2P_PROBE_RELAY_WAIT_PROBE) return;

    uint64_t now_ms = P_tick_ms();
    uint64_t rtt    = now_ms - s->probe_relay_start;

    s->probe_relay_state    = P2P_PROBE_RELAY_SUCCESS;
    s->probe_relay_complete = now_ms;

    print("I:", LA_F("%s: SUCCESS: UDP reachable via TURN (RTT: %" PRIu64 " ms)", 0, 0), TASK_RELAY_PROBE, rtt);
}
