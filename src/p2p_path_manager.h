/*
 * 多路径管理器 (Multi-Path Manager)
 */

#ifndef P2P_PATH_MANAGER_H
#define P2P_PATH_MANAGER_H

#include "predefine.h"

/* 前向声明 */
struct p2p_session;

/* 路径选择策略 */
typedef enum {
    P2P_PATH_STRATEGY_CONNECTION_FIRST = 0,         // P2P 直连优先（节省带宽成本）
    P2P_PATH_STRATEGY_PERFORMANCE_FIRST,            // 传输效率优先（最低延迟）
    P2P_PATH_STRATEGY_HYBRID                        // 混合模式（平衡成本和性能）
} p2p_path_strategy_t;

/* 路径质量等级 */
typedef enum {
    PATH_QUALITY_EXCELLENT = 4,                     // 优秀（RTT < 50ms, 丢包 < 1%）
    PATH_QUALITY_GOOD = 3,                          // 良好（RTT < 100ms, 丢包 < 3%）
    PATH_QUALITY_FAIR = 2,                          // 一般（RTT < 200ms, 丢包 < 5%）
    PATH_QUALITY_POOR = 1,                          // 较差（RTT < 500ms, 丢包 < 10%）
    PATH_QUALITY_BAD = 0                            // 很差（RTT >= 500ms 或丢包 >= 10%）
} path_quality_t;

/*
 * 路径状态
 *
 * 状态机：
 *
 *     INIT ──→ PROBING ──→ ACTIVE ──→ DEGRADED
 *                            ↑           ↓
 *                            └───────────┘
 *                            ↑
 *     RECOVERING ──────────┘
 *           ↓
 *        FAILED ────→ RECOVERING (30s后)
 *
 * 状态变更位置:
 *   INIT     → PROBING    : p2p_nat.c: nat_send_punch()
 *   PROBING  → ACTIVE     : p2p_nat.c: nat_on_punch() / nat_on_punch_ack()
 *   ACTIVE   ↔ DEGRADED   : p2p_path_manager.c: health_check_one_path()
 *   *        → FAILED     : p2p_path_manager.c: health_check_one_path()
 *   FAILED   → RECOVERING : p2p_path_manager.c: health_check_one_path()
 *   RECOVERING → ACTIVE   : p2p_path_manager.c: health_check_one_path()
 *   RECOVERING → FAILED   : p2p_path_manager.c: health_check_one_path()
 */
typedef enum {
    PATH_STATE_INIT = 0,                            // 初始化（远端候选添加时的默认状态）
    PATH_STATE_PROBING,                             // 探测中（已发送 PUNCH，等待响应）
    PATH_STATE_FAILED,                              // 失败（连续超时）
    PATH_STATE_ACTIVE,                              // 可用（双向可达）
    PATH_STATE_DEGRADED,                            // 降级（RTT>300ms 或 loss>10%）
    PATH_STATE_RECOVERING                           // 恢复中（FAILED 30s 后重新探测）
} path_state_t;

/* RTT 样本统计 */
#define RTT_SAMPLE_COUNT    10                      // 样本缓冲区容量

/*
 * 路径统计信息
 */
typedef struct {
    /* 路径状态 */
    path_state_t        state;                      // 路径状态
    
    /* 性能指标（用于路径选择）
     * 
     * RTT 测量分层策略（混合模式）：
     *   1. 探测层原路返回测量：
     *      - probe_rtt_direct:      最新测量值（原始 RTT）
     *      - probe_rtt_direct_srtt: EWMA 平滑值（用于路径选择，抗抖动）
     *      - probe_rtt_direct_rttvar: RTT 方差（用于抖动评估）
     * 
     *   2. 探测层跨路径测量：
     *      - probe_rtt_cross: 跨路径测量值（不平滑，仅参考）
     *      - 场景：PUNCH(A) → peer → REACH(B)，测量的是 A出+B入 的组合延迟
     *      - 用途：冷打洞单向网络时的初步排序，选择时降权 +30%
     * 
     *   3. 数据层 SRTT：
     *      - data_rtt: 来自 reliable 层的 SRTT（已平滑，最可靠）
     * 
     *   4. 综合 RTT：
     *      - rtt_ms: 从三个源中自动选择最优值
     *      - 优先级：data_rtt > probe_rtt_direct_srtt > probe_rtt_cross*1.3
     *      - rtt_variance: 综合抖动指标
     */
    uint32_t            probe_rtt_direct;           // 探测层原路最新测量（0=未测量）
    uint32_t            probe_rtt_direct_srtt;      // 探测层原路 EWMA 平滑值（路径选择用）
    uint32_t            probe_rtt_direct_rttvar;    // 探测层原路 RTT 方差（抖动评估用）
    uint32_t            probe_rtt_cross;            // 探测层跨路径测量（0=未测量，仅参考）
    uint32_t            data_rtt;                   // 数据层 SRTT（0=未测量，最可靠）
    
    uint32_t            rtt_ms;                     // 当前综合 RTT（毫秒，自动选择最优）
    uint32_t            rtt_min;                    // 最小 RTT（基线）
    uint32_t            rtt_max;                    // 最大 RTT
    uint32_t            rtt_variance;               // 综合延迟抖动
    float               loss_rate;                  // 综合丢包率（0.0-1.0），取 probe 和 data 两层的最大值
    float               data_loss_rate;             // 数据层丢包率（来自 reliable/SCTP/DTLS 传输层）
    uint64_t            bandwidth_bps;              // 估计带宽（bps）
    
    uint32_t            rtt_samples[RTT_SAMPLE_COUNT]; // RTT 样本环形缓冲区
    int                 rtt_sample_idx;             // 当前样本索引
    int                 rtt_sample_count;           // 有效样本数
    
    /* 健康检查 */
    uint64_t            last_recv_ms;               // 最后接收时间
    uint64_t            last_send_ms;               // 最后发送时间
    uint64_t            probe_seq;                  // 探测序列号
    int                 consecutive_timeouts;       // 连续超时次数
    
    /* 流量统计 */
    uint64_t            total_bytes_sent;
    uint64_t            total_bytes_recv;
    uint64_t            total_packets_sent;
    uint64_t            total_packets_recv;
    uint64_t            total_packets_lost;
    
    /* 路径质量预测 */
    uint64_t            last_quality_check_ms;      // 上次质量检查时间
    path_quality_t      quality;                    // 当前质量等级
    float               quality_score;              // 质量评分（0.0-1.0）
    float               quality_trend;              // 质量趋势（-1.0=恶化, 0=稳定, 1.0=改善）
    int                 stability_score;            // 稳定性评分（0-100）
    
    /* 成本估算（用于策略决策） */
    int                 cost_score;                 // 0=免费(LAN/PUNCH), 1-10=中继成本
    bool                is_lan;                     // 是否为 LAN 路径（同子网直连）
} path_stats_t;

//-----------------------------------------------------------------------------

/* 数据包跟踪（用于 RTT 测量）*/
#define MAX_PENDING_PACKETS 32
typedef struct {
    uint32_t            seq;                        // 数据包序列号
    uint64_t            sent_time_ms;               // 发送时间戳
    int                 path_idx;                   // 发送路径索引
} packet_track_t;

/* 路径切换历史记录（用于分析和防抖动） */
#define MAX_SWITCH_HISTORY 20
typedef struct {
    uint64_t            timestamp_ms;               // 切换时间戳
    int                 from_path;                  // 源路径索引
    int                 to_path;                    // 目标路径索引
    int                 from_type;                  // 源路径类型
    int                 to_type;                    // 目标路径类型
    uint32_t            from_rtt_ms;                // 源路径 RTT
    uint32_t            to_rtt_ms;                  // 目标路径 RTT
    float               from_loss_rate;             // 源路径丢包率
    float               to_loss_rate;               // 目标路径丢包率
    const char*         reason;                     // 切换原因
} path_switch_record_t;

/* TURN 路径配置 */
typedef struct {
    bool                enabled;                    // 是否启用 TURN
    bool                use_as_last_resort;         // 仅作为最终备份（默认 true）
} turn_config_t;

/*
 * 路径切换阈值配置（按路径类型独立配置）
 *
 * 阈值越小，切换越积极（如 LAN 出现时快速切回）；
 * 阈值越大，切换越保守（如 SIGNALING 转发作为最终降级）。
 * thresholds 数组以 p2p_path_t 枚举值为下标：
 *   [P2P_PATH_LAN]       小阈值，快速切回 LAN
 *   [P2P_PATH_PUNCH]     标准阈值
 *   [P2P_PATH_RELAY]     保守阈值，避免不必要的 TURN 切换
 *   [P2P_PATH_SIGNALING] 最大阈值，SIGNALING 转发作为最终手段
 */
typedef struct {
    uint32_t            rtt_threshold_ms;           // RTT 阈值：新路径需比当前快至少此值才触发切换
    float               loss_threshold;             // 丢包率阈值：超过则触发切换
    uint64_t            cooldown_ms;                // 切换冷却时间
    uint32_t            stability_window_ms;        // 稳定窗口：切向此类型路径前需观察的时长
} path_threshold_config_t;

/*
 * 路径索引特殊值（内部索引，不是 p2p_path_t 类型枚举）
 * 
 * 说明：路径管理器使用整数索引标识路径在数据结构中的位置：
 *   - active_path >= 0：路径在 remote_cands[active_path] 数组中
 *   - active_path == -1：特殊路径（SIGNALING），在 path_mgr.signaling 结构中，不在数组里
 *   - active_path == -2：无有效路径
 * 
 * 通过 path_idx_to_type() 转换为公共 API 的 p2p_path_t 枚举类型。
 */
#define PATH_IDX_NONE      (-2)  // 无有效路径
#define PATH_IDX_SIGNALING (-1)  // 信令转发(SIGNALING)路径索引（不在候选数组中，非 TURN relay）

/*
 * 路径管理
 * TURN vs SIGNALING：
 *   - TURN：标准 ICE Relay 候选（P2P_PATH_RELAY），在 remote_cands 中
 *   - SIGNALING：信令服务器转发（P2P_PATH_SIGNALING），非标准降级方案，需特殊字段
 */
typedef struct {
    /* 路径选择策略 */
    p2p_path_strategy_t strategy;                   // 路径选择策略
    
    /* 切换状态 */
    uint64_t            last_switch_time;           // 最后切换时间
    
    /* 健康检查参数 */
    uint32_t            probe_interval_ms;          // 探测间隔
    uint32_t            health_check_interval_ms;   // 健康检查间隔
    uint64_t            last_health_check_ms;       // 最后健康检查时间
    uint64_t            last_reselect_ms;           // 最后路径重选时间
    
    /* 统计信息 */
    uint64_t            total_switches;             // 总切换次数
    uint64_t            total_failovers;            // 总故障转移次数
    uint64_t            start_time_ms;              // 管理器启动时间
    
    /* 数据包跟踪（环形队列，用于 RTT 测量）*/
    packet_track_t      pending_packets[MAX_PENDING_PACKETS];
    int                 pending_head;               // 队首索引
    int                 pending_count;              // 队列长度
    
    /* 切换历史与防抖动 */
    path_switch_record_t switch_history[MAX_SWITCH_HISTORY]; // 切换历史环形缓冲区
    int                 switch_history_idx;         // 当前索引
    int                 switch_history_count;       // 有效记录数
    
    /* 防抖动 */
    uint64_t            debounce_timer_ms;          // 防抖动计时器
    int                 pending_switch_path;        // 待确认的切换目标（-1=无）
    
    /* TURN 策略配置（TURN 是候选，此配置用于策略调整） */
    turn_config_t       turn_config;                // TURN 配置

    /* 按路径类型的切换阈值（下标为 p2p_path_t：0=NONE 1=LAN 2=PUNCH 3=RELAY 4=SIGNALING） */
    path_threshold_config_t thresholds[5];
} path_manager_t;

/*
 * 辅助工具：检查路径是否可选（ACTIVE、DEGRADED 或 RECOVERING）
 *   等价于 state >= PATH_STATE_ACTIVE
 */
static inline bool path_is_selectable(path_state_t state) {
    return state >= PATH_STATE_ACTIVE;
}

/* ============================================================================
 * 初始化
 * ============================================================================ */

/*
 * 初始化路径管理器
 *
 * @param s         会话指针
 * @param strategy  路径选择策略
 * @return          0=成功，-1=失败
 */
int path_manager_init(struct p2p_session *s, p2p_path_strategy_t strategy);

/*
 * 重置路径管理器运行时状态
 *
 * 清除切换历史、待跟踪包、统计计数等运行时状态，保留策略、阈值等配置。
 * 同时重置 active_path 为 PATH_IDX_NONE。
 *
 * @param s  会话指针
 */
void path_manager_reset(struct p2p_session *s);

/*
 * 初始化路径统计（候选路径使用）
 *
 * 设置合理默认值避免 memset(0) 导致的问题：
 *   - rtt_ms=100（初始估计），rtt_min=9999（等待首次测量）
 *   - 所有测量源（probe_rtt_direct/cross, data_rtt）初始为 0（未测量）
 *   - quality=FAIR, quality_score=0.5, state=INIT
 *
 * @param st         统计结构指针
 * @param cost_score 成本分数（0=LAN/PUNCH, 5=RELAY, 8+=TURN）
 */
void path_stats_init(path_stats_t *st, int cost_score);

/*
 * 启用信令转发(SIGNALING)路径
 *
 * 设置地址、初始化统计并将路径状态置为 ACTIVE，调用后即可用于数据转发。
 * 注意：此路径是信令服务器转发（P2P_PATH_SIGNALING），不是 TURN RELAY。
 *
 * @param s         会话指针
 * @param addr      信令服务器地址
 * @return          0=成功，-1=失败
 */
int path_manager_enable_signaling(struct p2p_session *s, struct sockaddr_in *addr);

/*
 * 配置 TURN 支持
 *
 * @param s                 会话指针
 * @param enabled           是否启用 TURN
 * @param last_resort_only  是否仅作为最终备份
 * @return                  0=成功，-1=失败
 */
int path_manager_configure_turn(struct p2p_session *s,
                                 bool enabled,
                                 bool last_resort_only);

/* ============================================================================
 * 状态驱动（外部事件 → 驱动内部状态机）
 *
 * 两组统计接口：
 *   Group 1 — 探测层（probe, 面向所有路径）：
 *     path_manager_on_packet_recv / path_manager_on_packet_send / path_manager_on_packet_ack
 *     用于控制包（PUNCH/REACH/ALIVE）在各路径上的 RTT 测量和丢包检测。
 *     丢包由 path_manager_tick 扫描 pending 队列超时判定。
 *
 *   Group 2 — 数据层（data, 面向当前活跃路径）：
 *     path_manager_on_packet_recv / path_manager_on_data_rtt / path_manager_on_data_loss_rate
 *     用于 DATA 传输包的接收统计、RTT 同步和数据层丢包率上报。
 *     RTT 来自 reliable 层 SRTT 或高级传输层 get_stats。
 *     丢包率来自高级传输层 get_stats（基础 reliable 层无聚合丢包计数）。
 * ============================================================================ */

 /*
 * 记录数据包接收（更新路径活跃时间和流量统计）
 *
 * @param s         会话指针
 * @param path_idx  接收路径索引
 * @param now_ms    当前时间（毫秒）
 * @param size      包大小（字节），0表示不计入流量统计
 * @return          0=成功，-1=失败
 */
int path_manager_on_packet_recv(struct p2p_session *s, int path_idx, uint64_t now_ms, uint32_t size);

/* ---- Group 1: 探测层统计（控制包 PUNCH/REACH/ALIVE 的 per-path RTT 测量） ---- */

/*
 * 记录探测包发送（开始 per-path RTT 测量）
 *
 * @param s         会话指针
 * @param path_idx  发送路径索引
 * @param seq       数据包序列号
 * @param now_ms    当前时间（毫秒）
 * @param size      包大小（字节），0表示不计入流量统计（仅用于RTT测量）
 * @return          0=成功，-1=失败
 */
int path_manager_on_packet_send(struct p2p_session *s, int path_idx, uint32_t seq, uint64_t now_ms, uint32_t size);

/*
 * 记录探测包确认（完成 per-path RTT 测量）
 *
 * 混合模式 RTT 测量策略：
 *   1. 原路返回（send_path == recv_path）：
 *      - 记录到 probe_rtt_direct（精确测量）
 *      - 用于 PUNCHING/CONNECTED 阶段的路径选择
 *      - 这是最准确的单路径 RTT
 * 
 *   2. 跨路径返回（send_path != recv_path）：
 *      - 记录到 probe_rtt_cross（估算值，仅参考）
 *      - 仅用于 PUNCHING 阶段冷打洞的初步排序
 *      - RTT = 发送路径延迟 + 接收路径延迟（非单路径真实延迟）
 *      - 路径选择时会降权处理（+30% 惩罚）
 * 
 *   3. 使用场景：
 *      - PUNCHING 阶段：接受跨路径测量（单向网络场景，无其他数据）
 *      - CONNECTED 阶段：应该都是原路返回（对称路径已建立）
 * 
 * @param s            会话指针
 * @param seq          确认的序列号
 * @param recv_path    接收路径索引（REACH 包来源路径）
 * @param now_ms       当前时间（毫秒）
 * @return             测量的 RTT（毫秒），或 -1（未找到对应发送记录）
 * 
 * @note 调用方需通过 target_addr 判断 send_path，然后比较 send_path == recv_path
 *       来确定是否原路返回，这样才能正确更新对应的 RTT 字段
 */
int path_manager_on_packet_ack(struct p2p_session *s, uint32_t seq, int recv_path, uint64_t now_ms);

/* ---- Group 2: 数据层统计（活跃路径 DATA 包） ---- */

/*
 * 更新活跃路径的 RTT（来自 reliable 层 SRTT 或高级传输层 get_stats）
 *
 * @param s         会话指针
 * @param path_idx  路径索引（-1=SIGNALING, >=0=候选索引）
 * @param rtt_ms    往返延迟（毫秒）
 * @return          0=成功，-1=失败
 *
 * @note 质量评估会自动触发（有节流保护），无需单独调用刷新函数
 */
int path_manager_on_data_rtt(struct p2p_session *s, int path_idx, uint32_t rtt_ms);

/*
 * 上报数据层丢包率（来自高级传输层 get_stats）
 *
 * 基础 reliable 层不暴露聚合丢包计数，因此该接口仅由 SCTP/DTLS 等
 * 高级传输层调用。路径管理器会取 probe 丢包率和 data 丢包率的最大值
 * 作为综合 loss_rate，用于质量评估和路径选择。
 *
 * @param s         会话指针
 * @param path_idx  路径索引（-1=SIGNALING, >=0=候选索引）
 * @param loss_rate 数据层丢包率（0.0-1.0）
 * @return          0=成功，-1=失败
 */
int path_manager_on_data_loss_rate(struct p2p_session *s, int path_idx, float loss_rate);

/* ---- 辅助函数 ---- */

/*
 * 获取路径的有效 RTT（按优先级选择最优测量值）
 *
 * 优先级策略（混合模式）：
 *   1. data_rtt > 0                    → 使用数据层 SRTT（最可靠，CONNECTED 阶段）
 *   2. probe_rtt_direct_srtt > 0       → 使用探测层原路 EWMA 平滑值（精确且抗抖动）
 *   3. probe_rtt_cross > 0             → 使用跨路径测量 * 1.3（降权，冷打洞阶段）
 *   4. 否则                            → 返回 UINT32_MAX（未测量，不可选）
 *
 * 跨路径测量降权原因：
 *   - 跨路径 RTT = 发送路径延迟 + 接收路径延迟（非单路径真实延迟）
 *   - 仅用于冷打洞阶段的初步排序，不应作为最终路径选择依据
 *   - 增加 30% 惩罚，确保有原路测量的路径被优先选择
 *
 * @param p  路径统计指针
 * @return   有效 RTT（毫秒），或 UINT32_MAX（未测量）
 *
 * @note 此函数用于路径选择和质量评估，确保使用最准确的 RTT 值
 */
static inline uint32_t get_effective_rtt(const path_stats_t *p) {
    if (p->data_rtt > 0) return p->data_rtt;                          // 最优：数据层 SRTT
    if (p->probe_rtt_direct_srtt > 0) return p->probe_rtt_direct_srtt; // 次优：原路 EWMA 平滑
    if (p->probe_rtt_cross > 0) {
        // 跨路径测量降权：增加 30% 惩罚（避免混淆单路径真实延迟）
        uint64_t penalized = (uint64_t)p->probe_rtt_cross * 13 / 10;
        return penalized > UINT32_MAX ? UINT32_MAX : (uint32_t)penalized;
    }
    return UINT32_MAX;  // 未测量
}

//-----------------------------------------------------------------------------

/*
 * 路径管理器周期性维护（驱动超时检测与故障恢复）
 *
 * - 检查所有路径的超时状态，将失效路径标记为 FAILED
 * - 尝试恢复 RECOVERING 状态的路径
 * - 内置节流控制（默认 500ms 间隔）
 *
 * @param s         会话指针
 * @param now_ms    当前时间（毫秒）
 */
void path_manager_tick(struct p2p_session *s, uint64_t now_ms);

/* ============================================================================
 * 操作执行
 * ============================================================================ */

/*
 * 计算获取最佳路径
 * + 基于当前指定的策略模式，根据路径质量评估并返回最佳路径（索引）
 *
 * @param s         会话指针
 * @return          最佳路径索引（-1=SIGNALING, >=0=候选索引），或 -2（无可用路径）
 */
int path_manager_select_best_path(struct p2p_session *s);

/*
 * 切换到指定目标路径
 * + 支持防抖逻辑策略
 *
 * @param s             会话指针
 * @param target_path   目标路径索引
 * @param reason        切换原因（用于日志）
 * @param now_ms        当前时间（毫秒）
 * @return              0=成功切换，1=防抖延迟，-1=失败
 */
int path_manager_switch_path(struct p2p_session *s, int target_path,
                              const char *reason, uint64_t now_ms);

/* ============================================================================
 * 属性/状态访问与设置
 * ============================================================================ */

/*
 * 将路径索引转换为路径类型枚举
 *
 * @param s         会话指针
 * @param path_idx  路径索引（-1=SIGNALING, >=0=候选索引）
 * @return          P2P_PATH_LAN / P2P_PATH_PUNCH / P2P_PATH_RELAY / P2P_PATH_SIGNALING / P2P_PATH_NONE
 */
p2p_path_t path_manager_get_path_type(struct p2p_session *s, int path_idx);

/*
 * 检查是否有可用路径
 *
 * @param s         会话指针
 * @return          true=有可用路径，false=无
 */
bool path_manager_has_active_path(struct p2p_session *s);

/*
 * 设置指定路径的状态
 *
 * @param s         会话指针
 * @param path_idx  路径索引（-1=SIGNALING, >=0=候选索引）
 * @param state     新状态
 * @return          0=成功，-1=失败
 */
int path_manager_set_path_state(struct p2p_session *s, int path_idx, path_state_t state);

/*
 * 设置不同类型路径的切换阈值
 *
 * @param s             会话指针
 * @param path_type     路径类型（P2P_PATH_LAN/PUNCH/RELAY/SIGNALING）
 * @param rtt_ms        RTT 阈值（毫秒）
 * @param loss_rate     丢包率阈值（0.0-1.0）
 * @param cooldown_ms   切换冷却时间（毫秒）
 * @param stability_ms  稳定窗口（毫秒）
 * @return              0=成功，-1=失败
 */
int path_manager_set_threshold(struct p2p_session *s, int path_type,
                               uint32_t rtt_ms, float loss_rate,
                               uint64_t cooldown_ms, uint32_t stability_ms);

/*
 * 获取路径质量等级
 *
 * @param s         会话指针
 * @param path_idx  路径索引
 * @return          质量等级，或 -1（失败）
 */
path_quality_t path_manager_get_quality(struct p2p_session *s, int path_idx);

/*
 * 获取路径质量评分（0.0-1.0）
 *
 * @param s         会话指针
 * @param path_idx  路径索引
 * @return          质量评分，或 -1.0（失败）
 */
float path_manager_get_quality_score(struct p2p_session *s, int path_idx);

/*
 * 获取路径质量趋势
 *
 * @param s         会话指针
 * @param path_idx  路径索引
 * @return          趋势值（-1.0=恶化, 0=稳定, 1.0=改善），或 NaN（失败）
 */
float path_manager_get_quality_trend(struct p2p_session *s, int path_idx);

/*
 * 获取切换历史记录
 *
 * @param s             会话指针
 * @param records       输出缓冲区（调用者分配）
 * @param max_count     最大记录数
 * @return              实际返回的记录数
 */
int path_manager_get_switch_history(struct p2p_session *s, path_switch_record_t *records, int max_count);

/*
 * 获取路径切换频率
 *
 * @param s             会话指针
 * @param window_ms     统计时间窗口（毫秒）
 * @return              窗口内的切换次数
 */
int path_manager_get_switch_frequency(struct p2p_session *s, uint64_t window_ms);

/*
 * 获取 TURN 路径统计
 *
 * @param s                 会话指针
 * @param total_bytes_sent  输出：总发送字节数
 * @param total_bytes_recv  输出：总接收字节数
 * @param avg_rtt_ms        输出：平均 RTT（毫秒）
 * @return                  0=成功，-1=失败（无 TURN 路径）
 */
int path_manager_get_turn_stats(struct p2p_session *s,
                                 uint64_t *total_bytes_sent,
                                 uint64_t *total_bytes_recv,
                                 uint32_t *avg_rtt_ms);

/* 工具函数：枚举值转字符串（用于日志） */
const char* path_state_str(path_state_t state);
const char* path_quality_str(path_quality_t quality);

///////////////////////////////////////////////////////////////////////////////
#endif /* P2P_PATH_MANAGER_H */
