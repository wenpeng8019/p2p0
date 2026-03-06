# 方案 A：统计信息嵌入候选（最终版）

## 核心理念

**TURN 是候选，RELAY 不是**：
- TURN (ICE Relay 候选)：在 remote_cands 中，type=P2P_ICE_CAND_RELAY
- RELAY (信令服务器中继)：不是候选，需要特殊处理

## 核心设计

### 1. 提取统计字段到独立结构

```c
/* 路径统计信息（可复用于候选和特殊路径）*/
typedef struct {
    path_state_t        state;                  // 路径状态
    
    /* RTT 统计 */
    uint32_t            rtt_ms;                 // 当前 RTT
    uint32_t            rtt_min;                // 最小 RTT
    uint32_t            rtt_max;                // 最大 RTT
    uint32_t            rtt_variance;           // 延迟抖动
    uint32_t            rtt_srtt;               // 平滑 RTT (EWMA)
    uint32_t            rtt_rttvar;             // RTT 方差
    uint32_t            rtt_samples[10];        // RTT 样本环形缓冲区
    int                 rtt_sample_idx;         // 当前样本索引
    int                 rtt_sample_count;       // 有效样本数
    
    /* 丢包统计 */
    float               loss_rate;              // 丢包率 (0.0-1.0)
    uint64_t            total_packets_sent;
    uint64_t            total_packets_recv;
    uint64_t            total_packets_lost;
    
    /* 带宽评估 */
    uint64_t            bandwidth_bps;          // 估计带宽
    uint64_t            total_bytes_sent;
    uint64_t            total_bytes_recv;
    
    /* 健康检查 */
    uint64_t            last_send_ms;           // 最后发送时间
    uint64_t            last_recv_ms;           // 最后接收时间
    uint64_t            probe_seq;              // 探测序列号
    int                 consecutive_timeouts;   // 连续超时次数
    uint64_t            last_quality_check_ms;  // 上次质量检查时间
    
    /* 质量评估 */
    path_quality_t      quality;                // 当前质量等级
    float               quality_score;          // 质量评分 (0.0-1.0)
    float               quality_trend;          // 质量趋势
    int                 stability_score;        // 稳定性评分 (0-100)
    uint64_t            rtt_trend_sum;          // RTT 趋势累积
    int                 rtt_trend_count;        // RTT 趋势样本数
    
    /* 成本 */
    int                 cost_score;             // 成本评分
} path_stats_t;
```

### 2. 候选结构包含统计

```c
struct p2p_remote_candidate_entry {
    p2p_candidate_entry_t cand;                 // 基础候选字段
    uint64_t              last_punch_send_ms;   // 打洞时间
    bool                  reachable;            // 可达性
    uint64_t              last_reachable_ms;    // 最后可达时间
    
    /* 嵌入路径统计 */
    path_stats_t          stats;                // 路径统计信息
};
```

### 3. path_manager 简化为决策器

```c
/* 特殊路径索引 */
#define PATH_IDX_RELAY  (-1)  // 信令服务器中继（非候选）
// >= 0: remote_cands 索引（包含 Host/Srflx/TURN/Prflx 所有 ICE 候选）

struct path_manager {
    p2p_session_t      *session;                // 回指会话（访问 remote_cands）
    p2p_path_strategy_t strategy;               // 路径选择策略
    
    /* 当前激活路径（候选索引或特殊索引）*/
    int                 active_path_idx;        // -1=RELAY, >=0=候选索引
    int                 backup_path_idx;        // 备用路径
    
    /* RELAY 路径（信令服务器，非 ICE 候选）*/
    struct {
        bool            active;
        struct sockaddr_in addr;
        path_stats_t    stats;
    } relay;
    
    /* TURN 不需要特殊处理！
     * TURN 是标准 ICE 候选，在 remote_cands 中
     * type = P2P_ICE_CAND_RELAY
     * 统计存在候选的 stats 字段中 */
    
    /* 切换控制 */
    uint64_t            path_switch_cooldown_ms;
    uint64_t            last_switch_time;
    uint32_t            switch_rtt_threshold_ms;
    float               switch_loss_threshold;
    
    /* 待确认数据包跟踪（RTT 测量用）*/
    packet_track_t      pending_packets[32];
    int                 pending_count;
    
    /* 切换历史 */
    path_switch_record_t switch_history[MAX_SWITCH_HISTORY];
    int                  switch_history_count;
    
    /* 动态阈值（按路径类型）*/
    path_threshold_config_t thresholds[4];      // LAN/PUNCH/RELAY/TURN
    uint64_t            debounce_timer_ms;
    int                 pending_switch_path;
    
    /* 健康检查 */
    uint32_t            probe_interval_ms;
    uint32_t            health_check_interval_ms;
    uint64_t            last_health_check_ms;
    
    /* 统计 */
    uint64_t            total_switches;
    uint64_t            total_failovers;
};
```

### 4. 路径类型映射

```
P2P_PATH_LAN    → remote_cands[i].type = Host (同子网)
P2P_PATH_PUNCH  → remote_cands[i].type = Host/Srflx/Prflx (NAT打洞)
P2P_PATH_TURN   → remote_cands[i].type = P2P_ICE_CAND_RELAY (TURN候选)
P2P_PATH_RELAY  → path_manager.relay (信令服务器，特殊处理)
```

注意：
- TURN 是 ICE 标准候选，完全在 remote_cands 中管理
- RELAY 是非标准降级方案，需要特殊字段

```c
/* 初始化（回指会话）*/
void path_manager_init(path_manager_t *pm, p2p_session_t *session, p2p_path_strategy_t strategy);

/* 更新统计（通过候选索引）*/
int path_manager_update_stats(path_manager_t *pm, int cand_idx, uint32_t rtt_ms, bool success);

/* 选择最佳路径（返回候选索引或特殊索引）*/
int path_manager_select_best_path(path_manager_t *pm);

/* 设置活跃路径 */
int path_manager_set_active(path_manager_t *pm, int path_idx);

/* 获取路径地址（统一接口）*/
const struct sockaddr_in* path_manager_get_addr(path_manager_t *pm, int path_idx);

/* 获取路径统计（统一接口）*/
path_stats_t* path_manager_get_stats(path_manager_t *pm, int path_idx);

/* RELAY/TURN 特殊处理 */
int path_manager_add_relay(path_manager_t *pm, struct sockaddr_in *addr);
int path_manager_add_turn(path_manager_t *pm, struct sockaddr_in *addr);
```

## 优势

1. **完全消除冗余**：地址只存一份（在 remote_cands 或 relay/turn 字段）
2. **数据局部性好**：统计信息和候选紧邻
3. **职责清晰**：
   - `remote_cands`：地址管理 + 统计存储
   - `path_manager`：策略决策 + 路径选择
4. **易于维护**：候选删除时，统计自动删除

## 改动范围

- `p2p_internal.h`：修改 `p2p_remote_candidate_entry`
- `p2p_path_manager.h`：提取 `path_stats_t`，简化 `path_manager_t`
- `p2p_path_manager.c`：重写大部分 API
- `p2p_nat.c`：调整调用方式
- `p2p.c`：调整调用方式

## 风险评估

- **改动量大**：几乎重写 path_manager
- **测试工作量**：需要全面测试路径选择逻辑
- **兼容性**：API 变化较大

准备好就开始实施吗？
