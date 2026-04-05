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

#define TASK_RELAY_PROBE                   "PROXY PROBE"

///////////////////////////////////////////////////////////////////////////////

void probe_init(probe_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = P2P_PROBE_STATE_OFFLINE;
    ctx->mode.compact.phase = PROBE_COMPACT_PHASE_INIT;
    ctx->mode.relay.step = PROBE_RELAY_STEP_INIT;
}

void probe_reset(struct p2p_session *s) {
    probe_ctx_t *ctx = &s->probe_ctx;
    
    // 重置统一上下文
    ctx->state = P2P_PROBE_STATE_OFFLINE;
    ctx->mode.compact.phase = PROBE_COMPACT_PHASE_INIT;
    ctx->mode.compact.sid   = 0;
    ctx->mode.relay.step    = PROBE_RELAY_STEP_INIT;
    ctx->start_ms           = 0;
    ctx->complete_ms        = 0;
    ctx->retries            = 0;
}

///////////////////////////////////////////////////////////////////////////////

void probe_trigger(struct p2p_session *s) {
    probe_ctx_t *ctx = &s->probe_ctx;

    // NO_SUPPORT 和 OFFLINE 状态在信令握手时已确定，直接返回
    if (ctx->state == P2P_PROBE_STATE_NO_SUPPORT || 
        ctx->state == P2P_PROBE_STATE_OFFLINE) {
        return;
    }

    // 如果已直连，不应该触发探测
    if (s->state == P2P_STATE_CONNECTED) {
        print("W:", LA_S("%s: triggered on CONNECTED state (unnecessary)", LA_S28, 28), TASK_RELAY_PROBE);
        return;
    }

    // 如果正在探测中，不能重复触发
    if (ctx->state == P2P_PROBE_STATE_RUNNING) {
        print("W:", LA_S("%s: already running, cannot trigger again", LA_S25, 25), TASK_RELAY_PROBE);
        return;
    }

    assert(ctx->state >= P2P_PROBE_STATE_READY);
    ctx->state = P2P_PROBE_STATE_RUNNING;

    switch (s->inst->sig_mode) {

        // 启动 COMPACT 探测
        case P2P_SIGNALING_MODE_COMPACT:
            ctx->mode.compact.phase = PROBE_COMPACT_PHASE_SENDING;
            ctx->retries = 0;
            print("I:", LA_F("%s: triggered via COMPACT msg echo", LA_F188, 188), TASK_RELAY_PROBE);
            break;

        // 启动 RELAY 探测
        case P2P_SIGNALING_MODE_RELAY:
            ctx->mode.relay.step = PROBE_RELAY_STEP_TURN_ALLOC;
            ctx->retries = 0;
            print("I:", LA_F("%s: triggered via RELAY TUNE echo", LA_F189, 189), TASK_RELAY_PROBE);
            break;

        default: assert(false && "Unsupported signaling mode");
    }
}

///////////////////////////////////////////////////////////////////////////////
// COMPACT 回调
///////////////////////////////////////////////////////////////////////////////

void probe_compact_on_req_ack(struct p2p_session *s, uint16_t sid, uint8_t status) {
    probe_ctx_t *ctx = &s->probe_ctx;

    if (ctx->state != P2P_PROBE_STATE_RUNNING) return;
    if (ctx->mode.compact.phase != PROBE_COMPACT_PHASE_WAIT_ECHO) return;
    if (ctx->mode.compact.sid != sid) return;

    // 服务器已向对端转发，保持 RUNNING 继续等待 echo 回复
    if (status == 0) {
        print("I:", LA_S("%s: peer is online, waiting echo", LA_S27, 27), TASK_RELAY_PROBE);
    } else {
        ctx->state = P2P_PROBE_STATE_PEER_OFFLINE;
        ctx->complete_ms = 0;
        print("W:", LA_S("%s: peer is OFFLINE", LA_S26, 26), TASK_RELAY_PROBE);
    }
}

void probe_compact_on_response(struct p2p_session *s, uint16_t sid) {
    probe_ctx_t *ctx = &s->probe_ctx;

    if (ctx->state != P2P_PROBE_STATE_RUNNING) return;
    if (ctx->mode.compact.phase != PROBE_COMPACT_PHASE_WAIT_ECHO) return;
    if (ctx->mode.compact.sid != sid) return;

    ctx->state = P2P_PROBE_STATE_SUCCESS;
    ctx->complete_ms = P_tick_ms();

    print("I:", LA_F("%s: peer reachable via signaling (RTT: %" PRIu64 " ms)", LA_F135, 135), TASK_RELAY_PROBE, 
          tick_diff(ctx->complete_ms, ctx->start_ms));
}

///////////////////////////////////////////////////////////////////////////////
// RELAY 回调
///////////////////////////////////////////////////////////////////////////////

void probe_relay_on_turn_success(struct p2p_session *s) {
    probe_ctx_t *ctx = &s->probe_ctx;

    if (ctx->state != P2P_PROBE_STATE_RUNNING) return;
    if (ctx->mode.relay.step != PROBE_RELAY_STEP_TURN_ALLOC) return;

    ctx->mode.relay.step = PROBE_RELAY_STEP_ADDR_EXCHANGE;
    ctx->start_ms = P_tick_ms();
    print("I:", LA_S("%s: TURN allocated, starting address exchange", LA_S29, 29), TASK_RELAY_PROBE);
}

void probe_relay_on_exchange_done(struct p2p_session *s, bool success) {
    probe_ctx_t *ctx = &s->probe_ctx;

    if (ctx->state != P2P_PROBE_STATE_RUNNING) return;
    if (ctx->mode.relay.step != PROBE_RELAY_STEP_ADDR_EXCHANGE) return;

    if (success) {
        ctx->mode.relay.step = PROBE_RELAY_STEP_UDP_PROBE;
        ctx->start_ms = P_tick_ms();
        print("I:", LA_S("%s: address exchange success, sending UDP probe", LA_S24, 24), TASK_RELAY_PROBE);
    } else {
        ctx->state = P2P_PROBE_STATE_PEER_OFFLINE;
        ctx->complete_ms = P_tick_ms();
        print("W:", LA_S("%s: address exchange failed: peer OFFLINE", LA_S23, 23), TASK_RELAY_PROBE);
    }
}

void probe_relay_on_udp_response(struct p2p_session *s) {
    probe_ctx_t *ctx = &s->probe_ctx;

    if (ctx->state != P2P_PROBE_STATE_RUNNING) return;
    if (ctx->mode.relay.step != PROBE_RELAY_STEP_UDP_PROBE) return;

    uint64_t now_ms = P_tick_ms();
    uint64_t rtt    = tick_diff(now_ms, ctx->start_ms);

    ctx->state = P2P_PROBE_STATE_SUCCESS;
    ctx->complete_ms = now_ms;

    print("I:", LA_F("%s: SUCCESS: UDP reachable via TURN (RTT: %" PRIu64 " ms)", LA_F82, 82), TASK_RELAY_PROBE, rtt);
}

///////////////////////////////////////////////////////////////////////////////

static void probe_compact_tick(struct p2p_session *s, uint64_t now_ms) {
    probe_ctx_t *ctx = &s->probe_ctx;

    if (ctx->state != P2P_PROBE_STATE_RUNNING) {

        // 完成状态：等待间隔后自动重启
        if (ctx->state > P2P_PROBE_STATE_RUNNING) {
            
            if (ctx->complete_ms == 0) ctx->complete_ms = now_ms;

            // 有效性超时重置
            else if (tick_diff(now_ms, ctx->complete_ms) >= PROBE_COMPACT_REPEAT_INTERVAL * 1000) {

                print("V:", LA_F("%s: restarting periodic check", LA_F157, 157), TASK_RELAY_PROBE);
                ctx->state = P2P_PROBE_STATE_READY;
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

            print("I:", LA_F("%s: sent MSG(msg=0, sid=%u)", LA_F164, 164), TASK_RELAY_PROBE, ctx->mode.compact.sid);
            ret_t ret = p2p_signal_compact_request(s, 0, NULL, 0);
            if (ret != E_NONE) {
                ctx->mode.compact.phase = PROBE_COMPACT_PHASE_WAIT_ECHO;
                ctx->mode.compact.sid   = s->sig_sess.compact.req_sid;
                ctx->start_ms           = now_ms;
            } else { print("W:", LA_F("%s: send failed(%d)", LA_F163, 163), TASK_RELAY_PROBE, ret);
                ctx->state = P2P_PROBE_STATE_READY;
                ctx->mode.compact.phase = PROBE_COMPACT_PHASE_INIT;
            }
        } break;

        // 等待 echo 回复，超时检测
        case PROBE_COMPACT_PHASE_WAIT_ECHO:

            if (tick_diff(now_ms, ctx->start_ms) >= PROBE_COMPACT_TIMEOUT_MS) {
                if (ctx->retries < PROBE_COMPACT_MAX_RETRIES) {
                    ctx->retries++;
                    ctx->mode.compact.phase = PROBE_COMPACT_PHASE_SENDING;
                    print("W:", LA_F("%s: timeout, retry %d/%d", LA_F186, 186), TASK_RELAY_PROBE,
                          ctx->retries, PROBE_COMPACT_MAX_RETRIES);
                } else {
                    ctx->state = P2P_PROBE_STATE_PEER_TIMEOUT;
                    ctx->complete_ms = now_ms;
                    print("W:", LA_F("%s: timeout, peer did not respond", LA_F185, 185), TASK_RELAY_PROBE);
                }
            }
            break;

        default: break;
    }
}

static void probe_relay_tick(struct p2p_session *s, uint64_t now_ms) {
    probe_ctx_t *ctx = &s->probe_ctx;

    if (ctx->state != P2P_PROBE_STATE_RUNNING) {
        // 完成状态：等待间隔后自动重启
        if (ctx->state == P2P_PROBE_STATE_SUCCESS ||
            ctx->state == P2P_PROBE_STATE_PEER_OFFLINE ||
            ctx->state == P2P_PROBE_STATE_TIMEOUT ||
            ctx->state == P2P_PROBE_STATE_PEER_TIMEOUT ||
            ctx->state == P2P_PROBE_STATE_OFFLINE) {
            
            if (ctx->complete_ms == 0) ctx->complete_ms = now_ms;

            if (tick_diff(now_ms, ctx->complete_ms) >= PROBE_RELAY_REPEAT_INTERVAL) {
                print("V:", LA_F("%s: restarting periodic check", LA_F157, 157), TASK_RELAY_PROBE);
                ctx->state = P2P_PROBE_STATE_READY;
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
                ret_t ret = p2p_turn_allocate(s->inst);
                if (ret == E_NONE) {
                    ctx->start_ms = now_ms;
                    print("I:", LA_F("%s: TURN allocation request sent", LA_F84, 84), TASK_RELAY_PROBE);
                    // 保持 TURN_ALLOC 状态，等待回调
                } else {
                    print("W:", LA_F("%s: TURN allocation failed: ret=%d", LA_F83, 83), TASK_RELAY_PROBE, ret);
                    ctx->state = P2P_PROBE_STATE_TIMEOUT;
                    ctx->complete_ms = now_ms;
                }
            }
            break;

        case PROBE_RELAY_STEP_ADDR_EXCHANGE:
            // 步骤2：通过信令服务器交换新地址
            // （实际发送在 probe_relay_on_turn_success 进入此步骤时触发）
            if (tick_diff(now_ms, ctx->start_ms) >= PROBE_RELAY_EXCHANGE_TIMEOUT_MS) {
                if (ctx->retries < PROBE_RELAY_MAX_RETRIES) {
                    ctx->retries++;
                    print("W:", LA_F("%s: exchange timeout, retry %d/%d", LA_F110, 110), TASK_RELAY_PROBE,
                          ctx->retries, PROBE_RELAY_MAX_RETRIES);
                    // TODO: 重新发送地址交换请求
                } else {
                    ctx->state = P2P_PROBE_STATE_PEER_TIMEOUT;
                    ctx->complete_ms = now_ms;
                    print("W:", LA_F("%s: exchange timeout: peer not responding", LA_F111, 111), TASK_RELAY_PROBE);
                }
            }
            break;

        case PROBE_RELAY_STEP_UDP_PROBE:
            // 步骤3：发送 UDP 探测包（通过 TURN 中继）
            if (tick_diff(now_ms, ctx->start_ms) >= PROBE_RELAY_UDP_TIMEOUT_MS) {
                if (ctx->retries < PROBE_RELAY_MAX_RETRIES) {
                    ctx->retries++;
                    print("W:", LA_F("%s: UDP timeout, retry %d/%d", LA_F85, 85), TASK_RELAY_PROBE,
                          ctx->retries, PROBE_RELAY_MAX_RETRIES);
                    // TODO: 重新发送 UDP 探测包
                } else {
                    ctx->state = P2P_PROBE_STATE_PEER_TIMEOUT;
                    ctx->complete_ms = now_ms;
                    print("W:", LA_F("%s: UDP timeout: peer not responding", LA_F86, 86), TASK_RELAY_PROBE);
                }
            }
            break;

        default: break;
    }
}

void probe_tick(struct p2p_session *s, uint64_t now_ms) {
    switch (s->inst->sig_mode) {
        case P2P_SIGNALING_MODE_COMPACT: probe_compact_tick(s, now_ms); break;
        case P2P_SIGNALING_MODE_RELAY:   probe_relay_tick(s, now_ms);   break;
        default: break;
    }
}
