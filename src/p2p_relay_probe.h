/*
 * Relay 探测机制（信道外可达性检测）
 *
 * 当 NAT 打洞失败/连接中断时，通过服务器 MSG+echo 探测对端可达性：
 *   - 场景1：自己端口飘移 → 向服务器发包会重新打开映射
 *   - 场景2：端口映射改变 → 服务器转发包触发地址变更通知
 *   - 场景3：对端已离线 → 服务器 MSG_REQ_ACK 返回 status=1
 *   - 场景4：对端网络故障 → 服务器 MSG 请求超时
 *
 * 目标：在不重新连接的情况下，自动恢复可恢复的网络故障
 */

#ifndef P2P_RELAY_PROBE_H
#define P2P_RELAY_PROBE_H

#include <stdc.h>

struct p2p_session;

/* 探测配置参数 */
#define RELAY_PROBE_TIMEOUT_MS      5000    // 探测超时（毫秒）
#define RELAY_PROBE_MAX_RETRIES     3       // 最大重试次数
#define RELAY_PROBE_RETRY_INTERVAL  1000    // 重试间隔（毫秒）
#define RELAY_PROBE_REPEAT_INTERVAL 10000   // 重复探测间隔（毫秒）

/*
 * 触发 Relay 探测
 *
 * @param s  会话对象
 * @return   0=成功触发，-1=失败（不符合触发条件或 MSG 不可用）
 *
 * 触发条件：
 *   - 处于 COMPACT 信令模式
 *   - 服务器支持 MSG 机制
 *   - relay_probe_enabled = true
 *   - 当前无正在进行的探测
 */
ret_t relay_probe_trigger(struct p2p_session *s);

/*
 * 周期处理 Relay 探测状态机
 *
 * @param s         会话对象
 * @param now_ms    当前时间（毫秒）
 *
 * 在 p2p_update() 中调用，处理：
 *   - 发送 MSG 探测包（msg=0）
 *   - 超时检测
 *   - 重试逻辑
 */
void relay_probe_tick(struct p2p_session *s, uint64_t now_ms);

/*
 * 处理 MSG_REQ_ACK 响应（探测第一阶段）
 *
 * @param s       会话对象
 * @param sid     MSG 序列号
 * @param status  状态码（0=成功，1=对端离线）
 *
 * 由 compact_on_request_ack() 调用
 */
void relay_probe_on_req_ack(struct p2p_session *s, uint16_t sid, uint8_t status);

/*
 * 处理 MSG_RESP 响应（探测第二阶段，echo 回复）
 *
 * @param s       会话对象
 * @param sid     MSG 序列号
 *
 * 由 compact_on_response() 调用
 */
void relay_probe_on_response(struct p2p_session *s, uint16_t sid);

/*
 * 重置探测状态到 IDLE
 *
 * @param s  会话对象
 */
void relay_probe_reset(struct p2p_session *s);

#endif /* P2P_RELAY_PROBE_H */
