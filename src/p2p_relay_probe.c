/*
 * Relay 探测机制实现（信道外可达性检测）
 */

#define MOD_TAG "RELAY_PROBE"

#include "p2p_internal.h"
#include "p2p_relay_probe.h"

/*
 * 触发 Relay 探测
 */
ret_t relay_probe_trigger(struct p2p_session *s) {
    
    // 检查触发条件
    if (!s->relay_probe_enabled) {
        return E_NO_SUPPORT;
    }
    
    if (s->signaling_mode != P2P_SIGNALING_MODE_COMPACT) {
        return E_NO_SUPPORT;
    }
    
    if (!s->sig_compact_ctx.msg_support) {
        print("W:", LA_S("Relay probe skipped: server does not support MSG", LA_S_NEW1, 0));
        return E_NO_SUPPORT;
    }
    
    if (s->relay_probe_state != P2P_RELAY_PROBE_IDLE) {
        print("V:", LA_S("Relay probe already in progress", LA_S_NEW2, 0));
        return E_BUSY;
    }
    
    // 触发探测
    s->relay_probe_state = P2P_RELAY_PROBE_PENDING;
    s->relay_probe_retries = 0;
    
    print("I:", LA_S("Relay probe triggered (detecting peer reachability via server)", LA_S_NEW3, 0));
    
    return E_NONE;
}

/*
 * 周期处理 Relay 探测状态机
 */
void relay_probe_tick(struct p2p_session *s, uint64_t now_ms) {
    
    switch (s->relay_probe_state) {
        
        case P2P_RELAY_PROBE_IDLE:
            // 空闲，无需处理
            break;
            
        case P2P_RELAY_PROBE_PENDING:
            // 发起探测：发送 msg=0 空包
            {
                // 使用空数据的 MSG 请求
                ret_t ret = p2p_signal_compact_request(s, 0, NULL, 0);
                if (ret == E_NONE) {
                    s->relay_probe_state = P2P_RELAY_PROBE_WAITING;
                    s->relay_probe_sid = s->sig_compact_ctx.msg_sid;  // 记录探测使用的 sid
                    s->relay_probe_start = now_ms;
                    
                    print("I:", LA_F("Relay probe sent: MSG(msg=0, sid=%u)", LA_F_NEW1, 0), 
                          s->relay_probe_sid);
                } else {
                    print("W:", LA_F("Relay probe failed to send MSG: ret=%d", LA_F_NEW2, 0), ret);
                    s->relay_probe_state = P2P_RELAY_PROBE_IDLE;
                }
            }
            break;
            
        case P2P_RELAY_PROBE_WAITING:
            // 等待响应，检查超时
            if (now_ms - s->relay_probe_start >= RELAY_PROBE_TIMEOUT_MS) {
                
                if (s->relay_probe_retries < RELAY_PROBE_MAX_RETRIES) {
                    // 重试
                    s->relay_probe_retries++;
                    s->relay_probe_state = P2P_RELAY_PROBE_PENDING;
                    
                    print("W:", LA_F("Relay probe timeout, retry %d/%d", LA_F_NEW3, 0),
                          s->relay_probe_retries, RELAY_PROBE_MAX_RETRIES);
                } else {
                    // 超时失败
                    s->relay_probe_state = P2P_RELAY_PROBE_TIMEOUT;
                    
                    print("W:", LA_S("Relay probe timeout: server cannot reach peer", LA_S_NEW4, 0));
                    
                    // TODO: 通知上层或触发重连逻辑
                }
            }
            break;
            
        case P2P_RELAY_PROBE_SUCCESS:
        case P2P_RELAY_PROBE_PEER_OFFLINE:
        case P2P_RELAY_PROBE_TIMEOUT:
            // 探测完成，等待间隔后自动重置并重新触发（定时重复探测）
            if (s->relay_probe_complete == 0) {
                s->relay_probe_complete = now_ms;  // 记录完成时间
            }
            
            if (now_ms - s->relay_probe_complete >= RELAY_PROBE_REPEAT_INTERVAL) {
                // 间隔时间到，重置并重新触发
                print("V:", LA_S("Relay probe: restarting periodic check", LA_S_NEW7, 0));
                relay_probe_reset(s);
            }
            break;
    }
}

/*
 * 处理 MSG_REQ_ACK 响应
 */
void relay_probe_on_req_ack(struct p2p_session *s, uint16_t sid, uint8_t status) {
    
    // 检查是否是探测包的响应
    if (s->relay_probe_state != P2P_RELAY_PROBE_WAITING) {
        return;
    }
    
    if (s->relay_probe_sid != sid) {
        return;
    }
    
    if (status == 0) {
        // 成功：服务器已向对端转发，等待 echo 响应
        print("I:", LA_S("Relay probe: REQ_ACK received, peer is online", LA_S_NEW5, 0));
        // 保持 WAITING 状态，继续等待 MSG_RESP
    } else {
        // 对端离线
        s->relay_probe_state = P2P_RELAY_PROBE_PEER_OFFLINE;
        
        print("W:", LA_S("Relay probe: peer is OFFLINE (REQ_ACK status=1)", LA_S_NEW6, 0));
        
        // TODO: 通知上层对端离线，可能需要重连或等待
    }
}

/*
 * 处理 MSG_RESP 响应（echo 回复）
 */
void relay_probe_on_response(struct p2p_session *s, uint16_t sid) {
    
    // 检查是否是探测包的响应
    if (s->relay_probe_state != P2P_RELAY_PROBE_WAITING) {
        return;
    }
    
    if (s->relay_probe_sid != sid) {
        return;
    }
    
    // 探测成功！对端可达且响应了 echo
    s->relay_probe_state = P2P_RELAY_PROBE_SUCCESS;
    
    uint64_t now_ms;
    P_clock _clk; P_clock_now(&_clk);
    now_ms = clock_ms(_clk);
    
    uint64_t rtt = now_ms - s->relay_probe_start;
    
    print("I:", LA_F("Relay probe SUCCESS: peer reachable via server (RTT: %" PRIu64 " ms)", LA_F_NEW4, 0), rtt);
    
    // TODO: 根据探测成功的结果，尝试恢复连接
    // 可能的操作：
    // 1. 如果是端口飘移，向服务器发包已重新打开映射，继续尝试打洞
    // 2. 如果收到地址变更通知，更新对端地址并重新打洞
    // 3. 如果只是临时网络波动，直接恢复连接
}

/*
 * 重置探测状态
 */
void relay_probe_reset(struct p2p_session *s) {
    s->relay_probe_state = P2P_RELAY_PROBE_IDLE;
    s->relay_probe_sid = 0;
    s->relay_probe_start = 0;
    s->relay_probe_complete = 0;
    s->relay_probe_retries = 0;
}
