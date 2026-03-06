/*
 * P2P 信道外可达性探测实现（统一状态）
 *
 * 内部按 signaling_mode 分发，外部统一调用 probe_trigger/probe_tick/probe_reset。
 * 将原先分散在 p2p_session 中的 12 个字段统一封装到 p2p_probe_ctx_t 中。
 * 
 * 状态设计：
 *   - 公共状态 state：对应 p2p.h 的 p2p_probe_state_t（直接返回给用户）
 *   - 模式细节 phase/step：表示内部流程阶段，用于状态机流转
 */

#define MOD_TAG "PROBE"

#include "p2p_internal.h"
#include "p2p_probe.h"
#include "p2p_signal_compact.h"
#include "p2p_signal_relay.h"
#include "p2p_turn.h"

///////////////////////////////////////////////////////////////////////////////
// 上下文管理
///////////////////////////////////////////////////////////////////////////////

void probe_init(p2p_probe_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = P2P_PROBE_STATE_NONE;
    ctx->mode.compact.phase = PROBE_COMPACT_PHASE_INIT;
    ctx->mode.relay.step = PROBE_RELAY_STEP_INIT;
}

///////////////////////////////////////////////////////////////////////////////
// COMPACT 模式（MSG+echo）
///////////////////////////////////////////////////////////////////////////////

#define TASK_RELAY_PROBE                   "PROXY PROBE"

static ret_t probe_compact_trigger(struct p2p_session *s) {
    p2p_probe_ctx_t *ctx = &s->probe_ctx;

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

    if (ctx->state != P2P_PROBE_STATE_NONE) {
        print("V:", LA_F("%s: already in progress", LA_S56, 160), TASK_RELAY_PROBE);
        return E_BUSY;
    }

    ctx->state = P2P_PROBE_STATE_RUNNING;
    ctx->mode.compact.phase = PROBE_COMPACT_PHASE_SENDING;
    ctx->retries = 0;

    print("I:", LA_F("%s: triggered by echo via server", LA_S63, 167), TASK_RELAY_PROBE);
    return E_NONE;
}

static void probe_compact_tick(struct p2p_session *s, uint64_t now_ms) {
    p2p_probe_ctx_t *ctx = &s->probe_ctx;

    if (ctx->state != P2P_PROBE_STATE_RUNNING) {
        // 完成状态：等待间隔后自动重启
        if (ctx->state == P2P_PROBE_STATE_SUCCESS ||
            ctx->state == P2P_PROBE_STATE_PEER_OFFLINE ||
            ctx->state == P2P_PROBE_STATE_TIMEOUT) {
            
            if (ctx->complete_ms == 0)
                ctx->complete_ms = now_ms;

            if (now_ms - ctx->complete_ms >= PROBE_COMPACT_REPEAT_INTERVAL) {
                print("V:", LA_F("%s: restarting periodic check", LA_S59, 163), TASK_RELAY_PROBE);
                ctx->state = P2P_PROBE_STATE_NONE;
                ctx->mode.compact.phase = PROBE_COMPACT_PHASE_INIT;
                ctx->mode.compact.sid = 0;
                ctx->start_ms = 0;
                ctx->complete_ms = 0;
                ctx->retries = 0;
            }
        }
        return;
    }

    // RUNNING 状态：根据内部阶段流转
    switch (ctx->mode.compact.phase) {

        // 发送 msg=0 空包（服务器会自动 echo 回复）
        case PROBE_COMPACT_PHASE_SENDING: {

            ret_t ret = p2p_signal_compact_request(s, 0, NULL, 0);
            if (ret == E_NONE) {
                ctx->mode.compact.phase = PROBE_COMPACT_PHASE_WAIT_ECHO;
                ctx->mode.compact.sid   = s->sig_compact_ctx.req_sid;
                ctx->start_ms           = now_ms;

                print("I:", LA_F("%s: sent: MSG(msg=0, sid=%u)", LA_F210, 413), TASK_RELAY_PROBE,
                      ctx->mode.compact.sid);
            } else {
                print("W:", LA_F("%s: send failed(%d)", LA_F209, 412), TASK_RELAY_PROBE, ret);
                ctx->state = P2P_PROBE_STATE_NONE;
                ctx->mode.compact.phase = PROBE_COMPACT_PHASE_INIT;
            }
        } break;

        // 等待 echo 回复，超时检测
        case PROBE_COMPACT_PHASE_WAIT_ECHO:

            if (now_ms - ctx->start_ms >= PROBE_COMPACT_TIMEOUT_MS) {
                if (ctx->retries < PROBE_COMPACT_MAX_RETRIES) {
                    ctx->retries++;
                    ctx->mode.compact.phase = PROBE_COMPACT_PHASE_SENDING;
                    print("W:", LA_F("%s: timeout, retry %d/%d", LA_F211, 414), TASK_RELAY_PROBE,
                          ctx->retries, PROBE_COMPACT_MAX_RETRIES);
                } else {
                    ctx->state = P2P_PROBE_STATE_TIMEOUT;
                    ctx->complete_ms = now_ms;
                    print("W:", LA_F("%s: timeout: server cannot reach peer", LA_S62, 166), TASK_RELAY_PROBE);
                }
            }
            break;

        default: break;
    }
}

///////////////////////////////////////////////////////////////////////////////
// RELAY 模式（TURN 刷新 → 地址交换 → UDP 探测）
///////////////////////////////////////////////////////////////////////////////

static ret_t probe_relay_trigger(struct p2p_session *s) {
    p2p_probe_ctx_t *ctx = &s->probe_ctx;

    // 需要 TCP 信令通道已登录（SIGNAL_CONNECTED）
    if (s->sig_relay_ctx.state != SIGNAL_CONNECTED) {
        print("W:", LA_F("%s: skipped: relay signaling not connected", LA_S70, 174), TASK_RELAY_PROBE);
        return E_NONE_CONTEXT;
    }

    if (ctx->state != P2P_PROBE_STATE_NONE) {
        print("V:", LA_F("%s: already in progress", LA_S67, 171), TASK_RELAY_PROBE);
        return E_BUSY;
    }

    ctx->state = P2P_PROBE_STATE_RUNNING;
    ctx->mode.relay.step = PROBE_RELAY_STEP_TURN_ALLOC;
    ctx->retries = 0;

    print("I:", LA_F("%s: triggered: refreshing TURN allocation", LA_S71, 175), TASK_RELAY_PROBE);
    return E_NONE;
}

static void probe_relay_tick(struct p2p_session *s, uint64_t now_ms) {
    p2p_probe_ctx_t *ctx = &s->probe_ctx;

    if (ctx->state != P2P_PROBE_STATE_RUNNING) {
        // 完成状态：等待间隔后自动重启
        if (ctx->state == P2P_PROBE_STATE_SUCCESS ||
            ctx->state == P2P_PROBE_STATE_PEER_OFFLINE ||
            ctx->state == P2P_PROBE_STATE_TIMEOUT) {
            
            if (ctx->complete_ms == 0)
                ctx->complete_ms = now_ms;

            if (now_ms - ctx->complete_ms >= PROBE_RELAY_REPEAT_INTERVAL) {
                print("V:", LA_F("%s: restarting periodic check", LA_S69, 173), TASK_RELAY_PROBE);
                ctx->state = P2P_PROBE_STATE_NONE;
                ctx->mode.relay.step = PROBE_RELAY_STEP_INIT;
                ctx->start_ms = 0;
                ctx->complete_ms = 0;
                ctx->retries = 0;
            }
        }
        return;
    }

    // RUNNING 状态：根据内部步骤流转
    switch (ctx->mode.relay.step) {

        case PROBE_RELAY_STEP_TURN_ALLOC:
            // 步骤1：重新分配 TURN 地址
            {
                ret_t ret = p2p_turn_allocate(s);
                if (ret == E_NONE) {
                    ctx->start_ms = now_ms;
                    print("I:", LA_F("%s: TURN allocation request sent", LA_S73, 177), TASK_RELAY_PROBE);
                    // 保持 TURN_ALLOC 状态，等待回调
                } else {
                    print("W:", LA_F("%s: TURN allocation failed: ret=%d", LA_F213, 416), TASK_RELAY_PROBE, ret);
                    ctx->state = P2P_PROBE_STATE_TIMEOUT;
                    ctx->complete_ms = now_ms;
                }
            }
            break;

        case PROBE_RELAY_STEP_ADDR_EXCHANGE:
            // 步骤2：通过信令服务器交换新地址
            // （实际发送在 probe_relay_on_turn_success 进入此步骤时触发）
            if (now_ms - ctx->start_ms >= PROBE_RELAY_EXCHANGE_TIMEOUT_MS) {
                if (ctx->retries < PROBE_RELAY_MAX_RETRIES) {
                    ctx->retries++;
                    print("W:", LA_F("%s: exchange timeout, retry %d/%d", LA_F216, 419), TASK_RELAY_PROBE,
                          ctx->retries, PROBE_RELAY_MAX_RETRIES);
                    // TODO: 重新发送地址交换请求
                } else {
                    ctx->state = P2P_PROBE_STATE_PEER_OFFLINE;
                    ctx->complete_ms = now_ms;
                    print("W:", LA_F("%s: exchange timeout: peer offline?", LA_S68, 172), TASK_RELAY_PROBE);
                }
            }
            break;

        case PROBE_RELAY_STEP_UDP_PROBE:
            // 步骤3：发送 UDP 探测包（通过 TURN 中继）
            if (now_ms - ctx->start_ms >= PROBE_RELAY_UDP_TIMEOUT_MS) {
                if (ctx->retries < PROBE_RELAY_MAX_RETRIES) {
                    ctx->retries++;
                    print("W:", LA_F("%s: UDP timeout, retry %d/%d", LA_F215, 418), TASK_RELAY_PROBE,
                          ctx->retries, PROBE_RELAY_MAX_RETRIES);
                    // TODO: 重新发送 UDP 探测包
                } else {
                    ctx->state = P2P_PROBE_STATE_TIMEOUT;
                    ctx->complete_ms = now_ms;
                    print("W:", LA_F("%s: UDP timeout: network issue", LA_S76, 180), TASK_RELAY_PROBE);
                }
            }
            break;

        default: break;
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
    p2p_probe_ctx_t *ctx = &s->probe_ctx;
    
    // 重置统一上下文
    ctx->state = P2P_PROBE_STATE_NONE;
    ctx->mode.compact.phase = PROBE_COMPACT_PHASE_INIT;
    ctx->mode.compact.sid   = 0;
    ctx->mode.relay.step    = PROBE_RELAY_STEP_INIT;
    ctx->start_ms           = 0;
    ctx->complete_ms        = 0;
    ctx->retries            = 0;
    // enabled 不重置，保持用户配置
}

///////////////////////////////////////////////////////////////////////////////
// COMPACT 回调
///////////////////////////////////////////////////////////////////////////////

void probe_compact_on_req_ack(struct p2p_session *s, uint16_t sid, uint8_t status) {
    p2p_probe_ctx_t *ctx = &s->probe_ctx;

    if (ctx->state != P2P_PROBE_STATE_RUNNING) return;
    if (ctx->mode.compact.phase != PROBE_COMPACT_PHASE_WAIT_ECHO) return;
    if (ctx->mode.compact.sid != sid) return;

    // 服务器已向对端转发，保持 RUNNING 继续等待 echo 回复
    if (status == 0) {
        print("I:", LA_S("%s: REQ_ACK: peer is online, waiting echo", LA_S58, 162), TASK_RELAY_PROBE);
    } else {
        ctx->state = P2P_PROBE_STATE_PEER_OFFLINE;
        ctx->complete_ms = 0;
        print("W:", LA_S("%s: peer is OFFLINE (REQ_ACK status=1)", LA_S57, 161), TASK_RELAY_PROBE);
    }
}

void probe_compact_on_response(struct p2p_session *s, uint16_t sid) {
    p2p_probe_ctx_t *ctx = &s->probe_ctx;

    if (ctx->state != P2P_PROBE_STATE_RUNNING) return;
    if (ctx->mode.compact.phase != PROBE_COMPACT_PHASE_WAIT_ECHO) return;
    if (ctx->mode.compact.sid != sid) return;

    uint64_t now_ms = P_tick_ms();
    uint64_t rtt    = now_ms - ctx->start_ms;

    ctx->state = P2P_PROBE_STATE_SUCCESS;
    ctx->complete_ms = now_ms;

    print("I:", LA_F("%s: SUCCESS: peer reachable via server (RTT: %" PRIu64 " ms)", 0, 0), TASK_RELAY_PROBE, rtt);
}

///////////////////////////////////////////////////////////////////////////////
// RELAY 回调
///////////////////////////////////////////////////////////////////////////////

void probe_relay_on_turn_success(struct p2p_session *s) {
    p2p_probe_ctx_t *ctx = &s->probe_ctx;

    if (ctx->state != P2P_PROBE_STATE_RUNNING) return;
    if (ctx->mode.relay.step != PROBE_RELAY_STEP_TURN_ALLOC) return;

    ctx->mode.relay.step = PROBE_RELAY_STEP_ADDR_EXCHANGE;
    ctx->start_ms = P_tick_ms();
    print("I:", LA_S("%s: TURN allocated, starting address exchange", LA_S72, 176), TASK_RELAY_PROBE);
}

void probe_relay_on_exchange_done(struct p2p_session *s, bool success) {
    p2p_probe_ctx_t *ctx = &s->probe_ctx;

    if (ctx->state != P2P_PROBE_STATE_RUNNING) return;
    if (ctx->mode.relay.step != PROBE_RELAY_STEP_ADDR_EXCHANGE) return;

    if (success) {
        ctx->mode.relay.step = PROBE_RELAY_STEP_UDP_PROBE;
        ctx->start_ms = P_tick_ms();
        print("I:", LA_S("%s: address exchange success, sending UDP probe", LA_S66, 170), TASK_RELAY_PROBE);
    } else {
        ctx->state = P2P_PROBE_STATE_PEER_OFFLINE;
        ctx->complete_ms = P_tick_ms();
        print("W:", LA_S("%s: address exchange failed: peer OFFLINE", LA_S64, 168), TASK_RELAY_PROBE);
    }
}

void probe_relay_on_udp_response(struct p2p_session *s) {
    p2p_probe_ctx_t *ctx = &s->probe_ctx;

    if (ctx->state != P2P_PROBE_STATE_RUNNING) return;
    if (ctx->mode.relay.step != PROBE_RELAY_STEP_UDP_PROBE) return;

    uint64_t now_ms = P_tick_ms();
    uint64_t rtt    = now_ms - ctx->start_ms;

    ctx->state = P2P_PROBE_STATE_SUCCESS;
    ctx->complete_ms = now_ms;

    print("I:", LA_F("%s: SUCCESS: UDP reachable via TURN (RTT: %" PRIu64 " ms)", 0, 0), TASK_RELAY_PROBE, rtt);
}
