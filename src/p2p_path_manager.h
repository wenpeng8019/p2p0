/*
 * 多路径管理器 (Multi-Path Manager)
 *
 * 功能：
 *   - 并行维护多条传输路径（LAN/PUNCH/RELAY/TURN）
 *   - 根据策略自动选择最优路径
 *   - 支持路径动态切换和故障转移
 *   - 实时监控路径质量（RTT、丢包率、抖动）
 *
 * 设计原则：
 *   - 路径独立：多条路径并行工作，互不影响
 *   - 策略可配：支持直连优先、性能优先、混合模式
 *   - 平滑切换：路径切换对应用层透明
 *   - 故障恢复：失效路径自动探测恢复
 */

#ifndef P2P_PATH_MANAGER_H
#define P2P_PATH_MANAGER_H

#include <stdc.h>

/* 路径选择策略 */
typedef enum {
    P2P_PATH_STRATEGY_CONNECTION_FIRST = 0,  // P2P 直连优先（节省带宽成本）
    P2P_PATH_STRATEGY_PERFORMANCE_FIRST,     // 传输效率优先（最低延迟）
    P2P_PATH_STRATEGY_HYBRID                 // 混合模式（平衡成本和性能）
} p2p_path_strategy_t;

/* 路径类型（复用现有定义） */
#ifndef P2P_PATH_TYPES_DEFINED
#define P2P_PATH_TYPES_DEFINED
#define P2P_PATH_NONE   0
#define P2P_PATH_LAN    1   // 同一子网，直连
#define P2P_PATH_PUNCH  2   // NAT 打洞
#define P2P_PATH_RELAY  3   // 服务器中继
#define P2P_PATH_TURN   4   // TURN 中继（预留）
#endif

/* 路径质量等级 */
typedef enum {
    PATH_QUALITY_EXCELLENT = 4,    // 优秀（RTT < 50ms, 丢包 < 1%）
    PATH_QUALITY_GOOD = 3,         // 良好（RTT < 100ms, 丢包 < 3%）
    PATH_QUALITY_FAIR = 2,         // 一般（RTT < 200ms, 丢包 < 5%）
    PATH_QUALITY_POOR = 1,         // 较差（RTT < 500ms, 丢包 < 10%）
    PATH_QUALITY_BAD = 0           // 很差（RTT >= 500ms 或丢包 >= 10%）
} path_quality_t;

/* 数据包跟踪（用于 RTT 测量）*/
#define MAX_PENDING_PACKETS 32
typedef struct {
    uint32_t    seq;           // 数据包序列号
    uint64_t    sent_time_ms;  // 发送时间戳
    int         path_idx;      // 发送路径索引
} packet_track_t;

/* 路径状态 */
typedef enum {
    PATH_STATE_INIT = 0,        // 初始化
    PATH_STATE_PROBING,         // 探测中
    PATH_STATE_ACTIVE,          // 可用
    PATH_STATE_DEGRADED,        // 降级（高丢包/高延迟）
    PATH_STATE_FAILED,          // 失败
    PATH_STATE_RECOVERING       // 恢复中
} path_state_t;

/* 路径信息（单个路径的完整状态） */
typedef struct {
    int                 type;           // P2P_PATH_LAN/PUNCH/RELAY/TURN
    path_state_t        state;          // 路径状态
    struct sockaddr_in  addr;           // 目标地址
    
    /* 性能指标（用于路径选择） */
    uint32_t            rtt_ms;         // 往返延迟（毫秒）
    uint32_t            rtt_min;        // 最小 RTT（基线）
    uint32_t            rtt_variance;   // 延迟抖动
    float               loss_rate;      // 丢包率（0.0-1.0）
    uint64_t            bandwidth_bps;  // 估计带宽（bps）
    
    /* 健康检查 */
    uint64_t            last_send_ms;   // 最后发送时间
    uint64_t            last_recv_ms;   // 最后接收时间
    uint64_t            probe_seq;      // 探测序列号
    int                 consecutive_timeouts; // 连续超时次数
    
    /* 统计信息 */
    uint64_t            total_bytes_sent;
    uint64_t            total_bytes_recv;
    uint64_t            total_packets_sent;
    uint64_t            total_packets_recv;
    uint64_t            total_packets_lost;
    
    /* 成本估算（用于策略决策） */
    int                 cost_score;     // 0=免费(LAN/PUNCH), 1-10=中继成本
    
    /* RTT 平滑（EWMA: Exponentially Weighted Moving Average） */
    uint32_t            rtt_srtt;       // 平滑 RTT
    uint32_t            rtt_rttvar;     // RTT 方差
    
    /* Phase 2: 实时测量统计 */
    uint32_t            rtt_samples[10];    // RTT 样本环形缓冲区
    int                 rtt_sample_idx;     // 当前样本索引
    int                 rtt_sample_count;   // 有效样本数
    uint32_t            rtt_max;            // 最大 RTT
    uint64_t            last_quality_check_ms; // 上次质量检查时间
    
    /* Phase 3: 路径质量预测 */
    path_quality_t      quality;            // 当前质量等级
    float               quality_score;      // 质量评分（0.0-1.0）
    float               quality_trend;      // 质量趋势（-1.0=恶化, 0=稳定, 1.0=改善）
    int                 stability_score;    // 稳定性评分（0-100）
    uint64_t            rtt_trend_sum;      // RTT 趋势累积
    int                 rtt_trend_count;    // RTT 趋势样本数
} path_info_t;

/* 最大路径数量 */
#define P2P_MAX_PATHS 4

/* Phase 4: 路径切换历史记录（用于分析和防抖动） */
#define MAX_SWITCH_HISTORY 20
typedef struct {
    uint64_t    timestamp_ms;       // 切换时间戳
    int         from_path;          // 源路径索引
    int         to_path;            // 目标路径索引
    int         from_type;          // 源路径类型
    int         to_type;            // 目标路径类型
    uint32_t    from_rtt_ms;        // 源路径 RTT
    uint32_t    to_rtt_ms;          // 目标路径 RTT
    float       from_loss_rate;     // 源路径丢包率
    float       to_loss_rate;       // 目标路径丢包率
    const char* reason;             // 切换原因
} path_switch_record_t;

/* Phase 4: 动态阈值配置（根据路径类型） */
typedef struct {
    uint32_t    rtt_threshold_ms;       // RTT 阈值
    float       loss_threshold;         // 丢包率阈值
    uint64_t    cooldown_ms;            // 切换冷却时间
    uint32_t    stability_window_ms;    // 稳定窗口（防抖动）
} path_threshold_config_t;

/* Phase 5: TURN 路径配置 */
typedef struct {
    bool        enabled;                // 是否启用 TURN
    int         cost_multiplier;        // 成本倍数（相对于 RELAY）
    uint32_t    max_bandwidth_bps;      // 最大带宽限制
    bool        use_as_last_resort;     // 仅作为最终备份
} turn_config_t;

/* 路径管理器 */
typedef struct {
    p2p_path_strategy_t strategy;       // 路径选择策略
    
    path_info_t         paths[P2P_MAX_PATHS]; // 路径数组
    int                 path_count;     // 当前路径数量
    
    int                 active_path;    // 当前活跃路径索引（-1=无）
    int                 backup_path;    // 备用路径索引（快速切换）
    
    /* 自动切换参数 */
    uint64_t            path_switch_cooldown_ms;  // 切换冷却时间（避免频繁切换）
    uint64_t            last_switch_time;         // 最后切换时间
    uint32_t            switch_rtt_threshold_ms;  // RTT 改善阈值（触发切换）
    float               switch_loss_threshold;    // 丢包率阈值（触发切换）
    
    /* 健康检查参数 */
    uint32_t            probe_interval_ms;        // 探测间隔
    uint32_t            health_check_interval_ms; // 健康检查间隔
    uint64_t            last_health_check_ms;     // 最后健康检查时间
    
    /* 统计信息 */
    uint32_t            total_path_switches;      // 总切换次数
    uint64_t            uptime_by_path[P2P_MAX_PATHS]; // 各路径在线时长（毫秒）
    uint64_t            start_time_ms;            // 管理器启动时间
    
    /* Phase 2: 数据包跟踪（用于 RTT 测量）*/
    packet_track_t      pending_packets[MAX_PENDING_PACKETS]; // 待确认数据包
    int                 pending_count;            // 待确认数量
    uint32_t            next_track_seq;           // 下一个跟踪序列号
    
    /* Phase 4: 切换历史与动态阈值 */
    path_switch_record_t switch_history[MAX_SWITCH_HISTORY]; // 切换历史环形缓冲区
    int                 switch_history_idx;       // 当前索引
    int                 switch_history_count;     // 有效记录数
    path_threshold_config_t thresholds[P2P_MAX_PATHS]; // 每条路径的动态阈值
    uint64_t            debounce_timer_ms;        // 防抖动计时器
    int                 pending_switch_path;      // 待确认的切换目标（-1=无）
    
    /* Phase 5: TURN 支持 */
    turn_config_t       turn_config;              // TURN 配置
} path_manager_t;

/* ============================================================================
 * 路径管理器 API
 * ============================================================================ */

/*
 * 初始化路径管理器
 *
 * @param pm        路径管理器指针
 * @param strategy  路径选择策略
 */
void path_manager_init(path_manager_t *pm, p2p_path_strategy_t strategy);

/*
 * 添加或更新路径
 *
 * @param pm        路径管理器指针
 * @param type      路径类型（P2P_PATH_LAN/PUNCH/RELAY/TURN）
 * @param addr      目标地址
 * @return          路径索引（>=0），或 -1（失败）
 */
int path_manager_add_or_update_path(path_manager_t *pm, int type, struct sockaddr_in *addr);

/*
 * 查找路径索引
 *
 * @param pm        路径管理器指针
 * @param type      路径类型
 * @return          路径索引（>=0），或 -1（未找到）
 */
int path_manager_find_path(path_manager_t *pm, int type);

/*
 * 更新路径性能指标
 *
 * @param pm        路径管理器指针
 * @param path_idx  路径索引
 * @param rtt_ms    往返延迟（毫秒）
 * @param success   是否成功（用于计算丢包率）
 * @return          0=成功，-1=失败
 */
int path_manager_update_metrics(path_manager_t *pm, int path_idx, uint32_t rtt_ms, bool success);

/*
 * 选择最佳路径（核心逻辑）
 *
 * @param pm        路径管理器指针
 * @return          最佳路径索引（>=0），或 -1（无可用路径）
 */
int path_manager_select_best_path(path_manager_t *pm);

/*
 * 健康检查（周期性调用）
 *
 * @param pm        路径管理器指针
 * @param now_ms    当前时间（毫秒）
 */
void path_manager_health_check(path_manager_t *pm, uint64_t now_ms);

/*
 * 故障转移（当前路径失效时）
 *
 * @param pm        路径管理器指针
 * @param failed_path 失效路径索引
 * @return          新路径索引（>=0），或 -1（无备用路径）
 */
int path_manager_failover(path_manager_t *pm, int failed_path);

/*
 * 获取当前活跃路径
 *
 * @param pm        路径管理器指针
 * @return          活跃路径指针，或 NULL（无活跃路径）
 */
path_info_t* path_manager_get_active_path(path_manager_t *pm);

/*
 * 检查是否有可用路径
 *
 * @param pm        路径管理器指针
 * @return          true=有可用路径，false=无
 */
bool path_manager_has_active_path(path_manager_t *pm);

/*
 * 标记路径状态
 *
 * @param pm        路径管理器指针
 * @param path_idx  路径索引
 * @param state     新状态
 * @return          0=成功，-1=失败
 */
int path_manager_set_path_state(path_manager_t *pm, int path_idx, path_state_t state);

/*
 * 移除路径
 *
 * @param pm        路径管理器指针
 * @param type      路径类型
 * @return          0=成功，-1=失败
 */
int path_manager_remove_path(path_manager_t *pm, int type);

/*
 * 获取路径类型字符串（用于日志）
 */
const char* path_type_str(int type);

/*
 * 获取路径状态字符串（用于日志）
 */
const char* path_state_str(path_state_t state);

/* ============================================================================
 * Phase 2: RTT 测量与丢包统计 API
 * ============================================================================ */

/*
 * 记录数据包发送（开始 RTT 测量）
 *
 * @param pm        路径管理器指针
 * @param path_idx  发送路径索引
 * @param seq       数据包序列号
 * @param now_ms    当前时间（毫秒）
 * @return          0=成功，-1=失败
 */
int path_manager_on_packet_send(path_manager_t *pm, int path_idx, uint32_t seq, uint64_t now_ms);

/*
 * 记录数据包确认（完成 RTT 测量）
 *
 * @param pm        路径管理器指针
 * @param seq       确认的序列号
 * @param now_ms    当前时间（毫秒）
 * @return          测量的 RTT（毫秒），或 -1（未找到对应发送记录）
 */
int path_manager_on_packet_ack(path_manager_t *pm, uint32_t seq, uint64_t now_ms);

/*
 * 记录数据包接收（用于更新路径活跃时间）
 *
 * @param pm        路径管理器指针
 * @param path_idx  接收路径索引
 * @param now_ms    当前时间（毫秒）
 * @return          0=成功，-1=失败
 */
int path_manager_on_packet_recv(path_manager_t *pm, int path_idx, uint64_t now_ms);

/*
 * 记录数据包丢失
 *
 * @param pm        路径管理器指针
 * @param seq       丢失的序列号
 * @return          0=成功，-1=失败（未找到对应发送记录）
 */
int path_manager_on_packet_loss(path_manager_t *pm, uint32_t seq);

/* ============================================================================
 * Phase 3: 路径质量预测 API
 * ============================================================================ */

/*
 * 更新路径质量评估
 *
 * @param pm        路径管理器指针
 * @param path_idx  路径索引
 * @return          0=成功，-1=失败
 */
int path_manager_update_quality(path_manager_t *pm, int path_idx);

/*
 * 获取路径质量等级
 *
 * @param pm        路径管理器指针
 * @param path_idx  路径索引
 * @return          质量等级，或 -1（失败）
 */
path_quality_t path_manager_get_quality(path_manager_t *pm, int path_idx);

/*
 * 获取路径质量评分（0.0-1.0）
 *
 * @param pm        路径管理器指针
 * @param path_idx  路径索引
 * @return          质量评分，或 -1.0（失败）
 */
float path_manager_get_quality_score(path_manager_t *pm, int path_idx);

/*
 * 预测路径质量趋势
 *
 * @param pm        路径管理器指针
 * @param path_idx  路径索引
 * @return          趋势值（-1.0=恶化, 0=稳定, 1.0=改善），或 NaN（失败）
 */
float path_manager_predict_trend(path_manager_t *pm, int path_idx);

/*
 * 获取路径质量字符串（用于日志）
 */
const char* path_quality_str(path_quality_t quality);

/* ============================================================================
 * Phase 4: 动态阈值与防抖动 API
 * ============================================================================ */

/*
 * 设置路径类型的切换阈值
 *
 * @param pm            路径管理器指针
 * @param path_type     路径类型（P2P_PATH_LAN/PUNCH/RELAY/TURN）
 * @param rtt_ms        RTT 阈值（毫秒）
 * @param loss_rate     丢包率阈值（0.0-1.0）
 * @param cooldown_ms   切换冷却时间（毫秒）
 * @param stability_ms  稳定窗口（毫秒，防抖动）
 * @return              0=成功，-1=失败
 */
int path_manager_set_threshold(path_manager_t *pm, int path_type,
                                uint32_t rtt_ms, float loss_rate,
                                uint64_t cooldown_ms, uint32_t stability_ms);

/*
 * 获取切换历史记录
 *
 * @param pm            路径管理器指针
 * @param records       输出缓冲区（调用者分配）
 * @param max_count     最大记录数
 * @return              实际返回的记录数
 */
int path_manager_get_switch_history(path_manager_t *pm, 
                                     path_switch_record_t *records,
                                     int max_count);

/*
 * 分析路径切换频率（检测抖动）
 *
 * @param pm            路径管理器指针
 * @param window_ms     分析时间窗口（毫秒）
 * @return              窗口内的切换次数
 */
int path_manager_analyze_switch_frequency(path_manager_t *pm, uint64_t window_ms);

/*
 * 检查路径切换是否应该防抖（延迟执行）
 *
 * @param pm            路径管理器指针
 * @param target_path   目标路径索引
 * @param now_ms        当前时间（毫秒）
 * @return              true=需要防抖，false=可以立即切换
 */
bool path_manager_should_debounce_switch(path_manager_t *pm, 
                                          int target_path,
                                          uint64_t now_ms);

/*
 * 执行路径切换（包含防抖逻辑）
 *
 * @param pm            路径管理器指针
 * @param target_path   目标路径索引
 * @param reason        切换原因（用于日志）
 * @param now_ms        当前时间（毫秒）
 * @return              0=成功切换，1=防抖延迟，-1=失败
 */
int path_manager_switch_path(path_manager_t *pm, 
                              int target_path,
                              const char *reason,
                              uint64_t now_ms);

/* ============================================================================
 * Phase 5: TURN 路径支持 API
 * ============================================================================ */

/*
 * 配置 TURN 支持
 *
 * @param pm                路径管理器指针
 * @param enabled           是否启用 TURN
 * @param cost_multiplier   成本倍数（相对于 RELAY，1-10）
 * @param max_bandwidth_bps 最大带宽限制（bps，0=无限制）
 * @param last_resort_only  是否仅作为最终备份
 * @return                  0=成功，-1=失败
 */
int path_manager_configure_turn(path_manager_t *pm,
                                 bool enabled,
                                 int cost_multiplier,
                                 uint32_t max_bandwidth_bps,
                                 bool last_resort_only);

/*
 * 添加 TURN 路径
 *
 * @param pm        路径管理器指针
 * @param addr      TURN 服务器地址（已分配的中继地址）
 * @return          路径索引（>=0），或 -1（失败）
 */
int path_manager_add_turn_path(path_manager_t *pm, struct sockaddr_in *addr);

/*
 * 检查是否需要启用 TURN（所有其他路径失效）
 *
 * @param pm        路径管理器指针
 * @return          true=需要启用 TURN，false=不需要
 */
bool path_manager_should_use_turn(path_manager_t *pm);

/*
 * 获取 TURN 路径统计（成本、流量等）
 *
 * @param pm                路径管理器指针
 * @param total_bytes_sent  输出：总发送字节数
 * @param total_bytes_recv  输出：总接收字节数
 * @param avg_rtt_ms        输出：平均 RTT（毫秒）
 * @return                  0=成功，-1=失败（无 TURN 路径）
 */
int path_manager_get_turn_stats(path_manager_t *pm,
                                 uint64_t *total_bytes_sent,
                                 uint64_t *total_bytes_recv,
                                 uint32_t *avg_rtt_ms);

#endif /* P2P_PATH_MANAGER_H */
