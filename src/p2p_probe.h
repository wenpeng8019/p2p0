/*
 * P2P 信道外可达性探测（统一上下文）
 *
 * 当 NAT 打洞失败（转入 RELAY 状态）或连接断开（DISCONNECTED）时触发，
 * 通过信道外通讯检测对端可达性，辅助判断故障原因并自动恢复。
 *
 * 支持两种信令模式，统一封装在 p2p_probe_ctx_t 中：
 *
 * COMPACT 模式（MSG+echo）：
 *   发送 MSG(msg=0) → 服务器收到后转发到对端 → 对端自动 echo 回复
 *   单次往返即可判断：对端在线且服务器通路正常
 *
 * RELAY 模式（分步探测）：
 *   1. 刷新 TURN 地址分配（TURN Allocate）
 *   2. 通过信令服务器交换新地址（可判断对端是否在线）
 *   3. 发送 UDP 探测包到对端（通过 TURN 中继）
 *   TCP 信令在线不代表 UDP 路径可通，所以需要独立的 UDP 探测
 *
 * 设计优化：
 *   - 统一上下文：合并两种模式的通用字段（start/complete/retries）
 *   - 状态分离：使用 union 保存模式特定的状态
 *   - 接口统一：外部调用无需关心内部模式
 * 
* ============================================================================
 * Relay 探测状态（信道外可达性检测）
 * ============================================================================
 *
 * 当 NAT 打洞失败/连接中断时，通过服务器 MSG+echo 探测对端可达性：
 *   - 场景1：自己端口飘移 → 向服务器发包会重新打开映射
 *   - 场景2：端口映射改变 → 服务器转发包触发地址变更通知
 *   - 场景3：对端已离线 → 服务器 MSG_REQ_ACK 返回 status=1
 *   - 场景4：对端网络故障 → 服务器 MSG 请求超时
 *
 * 目标：在不重新连接的情况下，自动恢复可恢复的网络故障
 *
 * 注意：探测状态定义和上下文现已统一封装在 p2p_probe.h 的 p2p_probe_ctx_t 中
 */

#ifndef P2P_PROBE_H
#define P2P_PROBE_H

#include <stdc.h>

struct p2p_session;

/* ============================================================================
 * COMPACT 模式探测配置
 * ============================================================================ */
#define PROBE_COMPACT_TIMEOUT_MS       5000    /* 单次探测超时（毫秒）*/
#define PROBE_COMPACT_MAX_RETRIES      3       /* 最大重试次数 */
#define PROBE_COMPACT_RETRY_INTERVAL   1000    /* 重试间隔（毫秒）*/
#define PROBE_COMPACT_REPEAT_INTERVAL  10000   /* 重复探测间隔（毫秒）*/

/* ============================================================================
 * RELAY 模式探测配置
 * ============================================================================ */
#define PROBE_RELAY_TURN_TIMEOUT_MS     8000   /* TURN 分配超时（毫秒）*/
#define PROBE_RELAY_EXCHANGE_TIMEOUT_MS 5000   /* 地址交换超时（毫秒）*/
#define PROBE_RELAY_UDP_TIMEOUT_MS      3000   /* UDP 探测超时（毫秒）*/
#define PROBE_RELAY_MAX_RETRIES         2      /* 最大重试次数 */
#define PROBE_RELAY_REPEAT_INTERVAL     15000  /* 重复探测间隔（毫秒）*/

/* ============================================================================
 * 探测内部流程细节（模式特定）
 * ============================================================================ */

/* COMPACT 模式内部阶段（细化 RUNNING 状态）*/
typedef enum {
    PROBE_COMPACT_PHASE_INIT = 0,       // 初始化
    PROBE_COMPACT_PHASE_SENDING,        // 发送 MSG
    PROBE_COMPACT_PHASE_WAIT_ACK,       // 等待 MSG_REQ_ACK
    PROBE_COMPACT_PHASE_WAIT_ECHO       // 等待 MSG_RESP echo
} probe_compact_phase_t;

/* RELAY 模式内部步骤（细化 RUNNING 状态）*/
typedef enum {
    PROBE_RELAY_STEP_INIT = 0,          // 初始化
    PROBE_RELAY_STEP_TURN_ALLOC,        // TURN 地址分配
    PROBE_RELAY_STEP_ADDR_EXCHANGE,     // 地址交换
    PROBE_RELAY_STEP_UDP_PROBE          // UDP 探测
} probe_relay_step_t;

/* ============================================================================
 * 统一探测上下文
 * ============================================================================ */
typedef struct {
    /* 公共状态（对应 include/p2p.h 的 p2p_probe_state_t）*/
    p2p_probe_state_t state;        // 统一状态（NONE/RUNNING/SUCCESS/PEER_OFFLINE/TIMEOUT）
    uint64_t          start_ms;     // 探测开始时间（毫秒）
    uint64_t          complete_ms;  // 探测完成时间（毫秒）
    int               retries;      // 当前重试计数
    
    /* 模式特定细节（union 节省内存）*/
    union {
        struct {
            probe_compact_phase_t phase;  // 内部流程阶段
            uint16_t              sid;    // MSG 序列号
        } compact;
        
        struct {
            probe_relay_step_t step;      // 内部流程步骤
        } relay;
    } mode;
} p2p_probe_ctx_t;

/* ============================================================================
 * 上下文管理
 * ============================================================================ */

/*
 * 初始化探测上下文
 */
void probe_init(p2p_probe_ctx_t *ctx);

/* ============================================================================
 * 统一接口 — 调用方无需关心信令模式
 * ============================================================================ */

/*
 * 触发探测（自动根据 signaling_mode 选择实现）
 *
 * @return 0=成功触发，-1=不符合条件（未启用/不支持/已在进行中）
 */
ret_t probe_trigger(struct p2p_session *s);

/*
 * 周期驱动探测状态机（在 nat_tick 中调用）
 *
 * @param now_ms  当前时间（毫秒）
 */
void probe_tick(struct p2p_session *s, uint64_t now_ms);

/*
 * 重置探测状态到 IDLE
 */
void probe_reset(struct p2p_session *s);

/* ============================================================================
 * COMPACT 模式专用回调（在 compact_on_request_ack / compact_on_response 中调用）
 * ============================================================================ */

/*
 * 处理 MSG_REQ_ACK 响应
 * @param sid     MSG 序列号
 * @param status  0=转发成功/对端在线，1=对端离线
 */
void probe_compact_on_req_ack(struct p2p_session *s, uint16_t sid, uint8_t status);

/*
 * 处理 MSG_RESP echo 回复
 * @param sid  MSG 序列号
 */
void probe_compact_on_response(struct p2p_session *s, uint16_t sid);

/* ============================================================================
 * RELAY 模式专用回调（在 turn/signal_relay 层中调用）
 * ============================================================================ */

/*
 * TURN 地址分配成功
 */
void probe_relay_on_turn_success(struct p2p_session *s);

/*
 * 信令地址交换完成
 * @param success  true=对端在线且地址已交换，false=对端离线
 */
void probe_relay_on_exchange_done(struct p2p_session *s, bool success);

/*
 * 收到 UDP 探测响应包
 */
void probe_relay_on_udp_response(struct p2p_session *s);

#endif /* P2P_PROBE_H */
