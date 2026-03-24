/*
 * ============================================================================
 * P2P 多路径管理器 (Path Manager)
 * ============================================================================
 *
 * 【架构】
 *
 *   路径信息不由本模块存储，而是绑定在 session 的候选地址上：
 *     - s->remote_cands[i].stats  — 候选路径（索引 >=0）
 *     - s->signaling.stats — 信令转发/SIGNALING（索引 -1）
 *   path_manager 仅做决策与状态驱动，所有 API 第一个参数为 p2p_session_t *s。
 *
 *   路径类型识别：基于候选的 type 字段（P2P_CAND_HOST/SRFLX/RELAY/PRFLX）。
 *   路径优先级：直连（LAN/PUNCH）> 中继候选 > TURN > 信令转发（SIGNALING，兜底）。
 *   注意：SIGNALING 虽可用，但优先级最低（最终兜底），低于 TURN。
 *
 *
 * 【状态机】
 *
 *     INIT ──→ PROBING ──→ ACTIVE ──→ DEGRADED
 *                            ↑           ↓
 *                            └───────────┘
 *                            ↑
 *     RECOVERING ────────────┘
 *           ↓
 *        FAILED ────→ RECOVERING (30s后)
 *
 *   INIT    : 远端候选添加时的默认状态。
 *   PROBING : 首次发送 PUNCH 后进入，等待响应。
 *              → p2p_nat.c: nat_send_punch()
 *   ACTIVE  : 收到 PUNCH/PUNCH_ACK，双向可达。RTT<300ms 且 loss<10%。
 *              → p2p_nat.c: nat_on_punch() / nat_on_punch_ack()
 *   DEGRADED: RTT>300ms 或 loss>10%；恢复到 RTT<250ms 且 loss<5% 回 ACTIVE。
 *              → p2p_path_manager.c: health_check_one_path()
 *   FAILED  : 连续 3 次超时（LAN 5s / 其他 10s）。清除 writable 停止 keep-alive。
 *              → p2p_path_manager.c: health_check_one_path()
 *   RECOVERING: FAILED 30 秒后进入恢复探测。恢复 writable 触发 keep-alive。
 *             10 秒窗口内收到响应且 2 秒内有活动则回 ACTIVE；超时则回 FAILED。
 *              → p2p_path_manager.c: health_check_one_path()
 *
 *
 * 【路径选择策略】
 *
 *   SIGNALING（信令服务）不是专用中转，始终作为最终兜底，优先级低于 TURN。
 *   TURN 路径仅在 use_as_last_resort 且所有直连/中继路径失效时启用。
 *
 *   1. CONNECTION_FIRST — 按成本优先级：直连 > 中继候选 > TURN > 信令转发(SIGNALING)。
 *   2. PERFORMANCE_FIRST — 加权评分 0.5×RTT + 0.3×loss + 0.2×jitter，
 *      中继/TURN ×0.8、SIGNALING ×0.5 成本惩罚，选最高分。
 *   3. HYBRID — 直连良好(RTT<100ms, loss<1%)则用直连；
 *      中继显著更优(RTT差>阈值 或 loss>阈值)才切换；否则优先直连。
 *
 *
 * 【RTT 测量 — TCP EWMA 算法】
 *
 *   err      = sample - srtt
 *   srtt    += α × err          (α = 1/8)
 *   rttvar  += β × (|err| - rttvar)  (β = 1/4)
 *
 *   数据流：on_packet_send 记录 seq→时间到 pending_packets[] 环形队列（最多32）→
 *   on_packet_ack 计算 RTT 并写入 rtt_samples[10] 滑动窗口 → update_metrics
 *   更新 EWMA。path_manager_tick 扫描超时包并递增丢包计数。
 *
 *   要点：计数器(sent/lost/timeouts) 只在 send/loss/recv 中修改，
 *         update_metrics 只做指标计算，避免双重计数。
 *
 *
 * 【防抖动机制 — 三级保护】
 *
 *   1. 冷却期：切换后 5 秒内禁止再次切换。
 *   2. 频率检测：30 秒内 ≥5 次切换，强制防抖。
 *   3. 稳定窗口：目标路径需持续 2 秒满足切换条件才执行。
 *
 *   切换历史使用环形缓冲区（MAX_SWITCH_HISTORY=20），switch_path 记录
 *   from/to 路径的 cost_score、RTT、loss_rate 等完整快照。
 *
 *
 * 【质量评估】
 *
 *   quality_score = 0.5×RTT评分 + 0.3×丢包评分 + 0.2×抖动评分  (0.0~1.0)
 *   quality_trend = RTT 样本前半段与后半段均值之差，归一化到 [-1, 1]。
 *   stability_score = 100×(1 - 变异系数)，基于 RTT 标准差/均值。
 *
 *
 * 【故障转移】
 *
 *   health_check（默认 500ms 周期）检测超时与退化 → FAILED 时自动调用
 *   failover → select_best_path 选新路径 → 更新 active_path。
 *   RECOVERING 路径也可被选中，允许恢复中的路径参与竞选。
 *
 * ============================================================================
 */

#define MOD_TAG "PATH_MGR"

#include "p2p_internal.h"

/* 前向声明 */
static void update_metrics(path_stats_t *p);
static void update_quality(path_stats_t *p);

/* 默认参数 */
#define DEFAULT_PROBE_INTERVAL_MS       1000    /* 1秒探测间隔 */
#define DEFAULT_HEALTH_CHECK_INTERVAL   500     /* 500ms 健康检查间隔 */
#define PROBE_LOSS_TIMEOUT_MS           3000    /* 3秒未ACK视为丢包 */
#define QUALITY_UPDATE_INTERVAL_MS      1000    /* 质量评估最小更新间隔 */
#define MIN_SAMPLES_FOR_TREND           5       /* 计算质量趋势所需最小样本数 */
#define MIN_SAMPLES_FOR_STABILITY       3       /* 计算稳定性评分所需最小样本数 */
#define LAN_TIMEOUT_MS                  5000    /* LAN 路径无响应超时 */
#define WAN_TIMEOUT_MS                  10000   /* 非 LAN 路径无响应超时 */
#define FAILED_TIMEOUT_COUNT            3       /* 连续超时次数达到此值判定 FAILED */
#define DEGRADED_RTT_MS                 300     /* RTT 超过此值判定退化 */
#define DEGRADED_LOSS_RATE              0.1f    /* 丢包率超过此值判定退化 */
#define RECOVER_RTT_MS                  250     /* RTT 低于此值可从退化恢复 */
#define RECOVER_LOSS_RATE               0.05f   /* 丢包率低于此值可从退化恢复 */
#define FAILED_TO_RECOVERING_MS         30000   /* FAILED 后等待此时间再探测恢复 */
#define RECOVERING_TIMEOUT_MS           10000   /* RECOVERING 窗口超时 */
#define RECOVERING_ACTIVITY_MS          2000    /* RECOVERING 中判定活跃的最近活动窗口 */
#define DEFAULT_COOLDOWN_MS             5000    /* 防抖：默认冷却期 */
#define DEFAULT_STABILITY_WINDOW_MS     2000    /* 防抖：默认稳定窗口 */
#define SWITCH_FREQ_WINDOW_MS           30000   /* 防抖：频率检测窗口 */
#define SWITCH_FREQ_THRESHOLD           5       /* 防抖：窗口内切换次数上限 */

/* RTT 平滑参数（TCP 算法） */
#define ALPHA   0.125f  /* SRTT 权重：1/8 */
#define BETA    0.25f   /* RTTVAR 权重：1/4 */


/* ==========================================================================
 *                                    初始化
 * ========================================================================== */

/*
 * 初始化路径管理器（方案 A）
 */
int path_manager_init(p2p_session_t *s, p2p_path_strategy_t strategy) {
    path_manager_t *pm = &s->path_mgr;
    memset(pm, 0, sizeof(*pm));
    
    /* 路径选择策略 */
    pm->strategy = strategy;
    
    /* 初始化路径索引（无活跃路径） */
    s->active_path = -2;  // -2 表示无活跃路径
    
    /* SIGNALING 初始化为未启用 */
    s->signaling.active = false;
    memset(&s->signaling.addr, 0, sizeof(s->signaling.addr));
    memset(&s->signaling.stats, 0, sizeof(s->signaling.stats));
    s->signaling.stats.state = PATH_STATE_INIT;
    
    /* 设置默认参数 */
    pm->probe_interval_ms = DEFAULT_PROBE_INTERVAL_MS;
    pm->health_check_interval_ms = DEFAULT_HEALTH_CHECK_INTERVAL;
    
    pm->pending_switch_path = -2;  // -2 表示无待切换路径
    
    /* TURN 配置（注：TURN 是候选，此配置仅用于策略调整） */
    pm->turn_config.enabled = true;
    pm->turn_config.use_as_last_resort = true;

    /* 按路径类型初始化独立阈值（rtt_ms, loss, cooldown_ms, stability_ms） */
    pm->thresholds[P2P_PATH_NONE]      = (path_threshold_config_t){50,   0.05f, 5000, 2000};
    pm->thresholds[P2P_PATH_LAN]       = (path_threshold_config_t){20,   0.02f, 1000, 1000}; /* 积极切回 LAN */
    pm->thresholds[P2P_PATH_PUNCH]     = (path_threshold_config_t){50,   0.05f, 3000, 2000}; /* 标准阈值 */
    pm->thresholds[P2P_PATH_RELAY]     = (path_threshold_config_t){100,  0.10f, 5000, 3000}; /* 保守，有成本 */
    pm->thresholds[P2P_PATH_SIGNALING] = (path_threshold_config_t){200,  0.15f, 8000, 4000}; /* 最终降级，极保守 */
    
    pm->start_time_ms = P_tick_ms();
    
    return 0;
}

/*
 * 重置路径管理器运行时状态（保留配置）
 *
 * 注意：仅重置 path_manager 自身状态，不涉及 session 层
 *       （active_path、signaling 等由 p2p_session_reset 处理）
 */
void path_manager_reset(p2p_session_t *s) {
    path_manager_t *pm = &s->path_mgr;
    
    /* 重置切换状态 */
    pm->last_switch_time = 0;
    pm->last_health_check_ms = 0;
    pm->last_reselect_ms = 0;
    
    /* 重置统计信息 */
    pm->total_switches = 0;
    pm->total_failovers = 0;
    pm->start_time_ms = P_tick_ms();
    
    /* 清空待跟踪包队列 */
    memset(pm->pending_packets, 0, sizeof(pm->pending_packets));
    pm->pending_head = 0;
    pm->pending_count = 0;
    
    /* 清空切换历史 */
    memset(pm->switch_history, 0, sizeof(pm->switch_history));
    pm->switch_history_idx = 0;
    pm->switch_history_count = 0;
    
    /* 重置防抖动 */
    pm->debounce_timer_ms = 0;
    pm->pending_switch_path = -2;
}

/*
 * 初始化路径统计默认值
 * 避免 memset(0) 导致的问题：rtt_min=0 破坏最小值跟踪，quality=BAD 等
 */
void path_stats_init(path_stats_t *st, int cost_score) {
    memset(st, 0, sizeof(*st));
    
    st->state = PATH_STATE_INIT;
    st->cost_score = cost_score;
    
    /* RTT：使用合理初始值避免新路径被忽略
     * 
     * 混合模式 RTT 初始化：
     *   - 所有测量源初始为 0 表示未测量
     *   - rtt_ms 初始估计 100ms（用于路径选择排序，避免新路径被完全忽略）
     *   - 一旦有任何测量值，get_effective_rtt() 会自动选择最优源
     */
    st->probe_rtt_direct = 0;           /* 未测量：等待原路返回测量 */
    st->probe_rtt_direct_srtt = 0;      /* 未平滑：等待首次测量后初始化 */
    st->probe_rtt_direct_rttvar = 0;    /* 未测量：等待首次测量后初始化 */
    st->probe_rtt_cross = 0;            /* 未测量：等待跨路径测量 */
    st->data_rtt = 0;                   /* 未测量：等待数据层 SRTT */
    
    st->rtt_ms = 100;       /* 初始估计 100ms（避免新路径被忽略） */
    st->rtt_min = 9999;     /* 等待首次测量 */
    st->rtt_variance = 0;   /* 等待方差测量 */
    
    /* 质量：初始假设为一般 */
    st->quality = PATH_QUALITY_FAIR;
    st->quality_score = 0.5f;
}

/*
 * 设置信令转发(SIGNALING)路径
 */
int path_manager_enable_signaling(p2p_session_t *s, struct sockaddr_in *addr) {
    s->signaling.active = true;
    s->signaling.addr = *addr;
    path_stats_init(&s->signaling.stats, 5);       /* SIGNALING cost=5 */
    s->signaling.stats.state = PATH_STATE_ACTIVE;   /* SIGNALING 一设置就可用 */
    
    return 0;
}

/*
 * TURN配置
 */
int path_manager_configure_turn(p2p_session_t *s,
                                 bool enabled,
                                 bool last_resort_only) {
    path_manager_t *pm = &s->path_mgr;
    pm->turn_config.enabled = enabled;
    pm->turn_config.use_as_last_resort = last_resort_only;
    return 0;
}

/* ==========================================================================
 *                                    操作执行
 * ========================================================================== */

/*
 * 判断是否应该使用TURN（last_resort 模式下的前置检查）
 *   遍历候选列表，检查是否所有直连/中继路径都失效
 *   SIGNALING 不阻止 TURN 启用，因为 SIGNALING 优先级低于 TURN
 */
static bool should_use_turn(p2p_session_t *s) {
    path_manager_t *pm = &s->path_mgr;
    if (!pm->turn_config.enabled) return false;
    
    // 策略上，如果 TURN 不是作为最后手段，则总是可用
    if (!pm->turn_config.use_as_last_resort) return true;
    
    /* 检查是否存在非 TURN 的可用直连/中继路径（不含 SIGNALING） */
    for (int i = 0; i < s->remote_cand_cnt; i++) {
        path_stats_t *st = &s->remote_cands[i].stats;
        p2p_path_type_t type = p2p_get_path_type(s, i);
        if (type == P2P_PATH_RELAY) continue;  // 跳过 TURN/RELAY
        if (path_is_selectable(st->state)) {
            return false; // 存在直连/中继备选
        }
    }
    
    return true; // 所有直连/中继路径失效，可以启用 TURN
}

/*
 * 策略 1: CONNECTION_FIRST（P2P 直连优先）
 *   优先级：直连 > 中继候选 > TURN > 信令转发(SIGNALING)
 */
static int select_path_connection_first(p2p_session_t *s) {
    path_manager_t *pm = &s->path_mgr;
    int best_direct = -2;       /* 最佳直连候选 */
    int best_relay_cand = -2;   /* 最佳中继类候选（非 TURN） */
    int best_turn = -2;         /* TURN 候选 */
    uint32_t best_direct_rtt = UINT32_MAX;
    uint32_t best_relay_rtt = UINT32_MAX;
    uint32_t best_turn_rtt = UINT32_MAX;

    /* 遍历所有候选 */
    for (int i = 0; i < s->remote_cand_cnt; i++) {
        path_stats_t *st = &s->remote_cands[i].stats;
        if (!path_is_selectable(st->state)) continue;

        p2p_path_type_t type = p2p_get_path_type(s, i);
        
        if (type == P2P_PATH_RELAY) {
            /* TURN 路径：仅在 use_as_last_resort 时跳过 */
            if (pm->turn_config.use_as_last_resort) continue;
            if (st->rtt_ms < best_turn_rtt) {
                best_turn_rtt = st->rtt_ms;
                best_turn = i;
            }
        } else if (type == P2P_PATH_LAN || type == P2P_PATH_PUNCH) {
            if (st->rtt_ms < best_direct_rtt) {
                best_direct_rtt = st->rtt_ms;
                best_direct = i;
            }
        } else {
            /* 中继类候选（既非 TURN 也非直连） */
            if (st->rtt_ms < best_relay_rtt) {
                best_relay_rtt = st->rtt_ms;
                best_relay_cand = i;
            }
        }
    }

    /* 检查信令转发(SIGNALING) */
    bool signaling_ok = s->signaling.active && path_is_selectable(s->signaling.stats.state);

    /* 优先级：直连 > 中继候选 > TURN > 信令转发(SIGNALING) */
    if (best_direct >= 0) return best_direct;
    if (best_relay_cand >= 0) return best_relay_cand;

    /* TURN：非 last_resort 模式下在主循环中已收集 */
    if (best_turn >= 0) return best_turn;
    if (pm->turn_config.use_as_last_resort) {
        /* last_resort 模式：重新扫描找RTT最低的TURN */
        int turn_fallback = -2;
        uint32_t min_turn_rtt = UINT32_MAX;
        for (int i = 0; i < s->remote_cand_cnt; i++) {
            path_stats_t *st = &s->remote_cands[i].stats;
            p2p_path_type_t type = p2p_get_path_type(s, i);
            if (type == P2P_PATH_RELAY && path_is_selectable(st->state)) {
                if (st->rtt_ms < min_turn_rtt) {
                    min_turn_rtt = st->rtt_ms;
                    turn_fallback = i;
                }
            }
        }
        if (turn_fallback >= 0) return turn_fallback;
    }

    /* 信令转发(SIGNALING)：最终兜底 */
    if (signaling_ok) return PATH_IDX_SIGNALING;

    return -2; /* 无可用路径 */
}

/*
 * 策略 2: PERFORMANCE_FIRST（传输效率优先）
 *   综合评分：0.5×RTT + 0.3×丢包 + 0.2×抖动，中继成本打折
 */
static int select_path_performance_first(p2p_session_t *s) {
    path_manager_t *pm = &s->path_mgr;
    int best_path = -2;
    float best_score = -1.0f;

    /* 评估所有候选 */
    for (int i = 0; i < s->remote_cand_cnt; i++) {
        path_stats_t *st = &s->remote_cands[i].stats;
        if (!path_is_selectable(st->state)) continue;

        p2p_path_type_t type = p2p_get_path_type(s, i);
        
        /* TURN last_resort 检查 */
        if (type == P2P_PATH_RELAY && pm->turn_config.use_as_last_resort &&
            !should_use_turn(s)) continue;

        float rtt_score = 1.0f - fminf((float)st->rtt_ms / 500.0f, 1.0f);
        float loss_score = 1.0f - st->loss_rate;
        float jitter_score = 1.0f - fminf((float)st->rtt_variance / 100.0f, 1.0f);
        float score = 0.5f * rtt_score + 0.3f * loss_score + 0.2f * jitter_score;

        /* 成本惩罚：中继/TURN 路径打八折 */
        if (type != P2P_PATH_LAN && type != P2P_PATH_PUNCH) score *= 0.8f;

        if (score > best_score) {
            best_score = score;
            best_path = i;
        }
    }

    /* 如果找到可用候选路径，直接返回（SIGNALING 作为最终兜底，不参与评分竞争） */
    if (best_path >= 0) return best_path;

    /* 信令转发(SIGNALING)：最终兜底（只在无其他可用路径时使用） */
    if (s->signaling.active && path_is_selectable(s->signaling.stats.state))
        return PATH_IDX_SIGNALING;

    return -2;
}

/*
 * 策略 3: HYBRID（混合模式）
 *   平衡延迟和成本：直连良好用直连，中继显著更优时才切换
 */
static int select_path_hybrid(p2p_session_t *s) {
    path_manager_t *pm = &s->path_mgr;
    int best_direct = -2;
    int best_relay = -2;    /* 非 TURN 中继候选（不含 SIGNALING） */
    int best_turn = -2;
    uint32_t direct_rtt = UINT32_MAX;
    float direct_loss = 1.0f;
    uint32_t relay_rtt = UINT32_MAX;
    float relay_loss = 1.0f;

    uint32_t turn_rtt = UINT32_MAX;

    for (int i = 0; i < s->remote_cand_cnt; i++) {
        path_stats_t *st = &s->remote_cands[i].stats;
        if (!path_is_selectable(st->state)) continue;

        p2p_path_type_t type = p2p_get_path_type(s, i);
        
        if (type == P2P_PATH_RELAY) {
            if (st->rtt_ms < turn_rtt) {
                turn_rtt = st->rtt_ms;
                best_turn = i;
            }
        } else if (type == P2P_PATH_LAN || type == P2P_PATH_PUNCH) {
            if (st->rtt_ms < direct_rtt) {
                direct_rtt = st->rtt_ms;
                direct_loss = st->loss_rate;
                best_direct = i;
            }
        } else {
            /* 中继候选 */
            if (st->rtt_ms < relay_rtt) {
                relay_rtt = st->rtt_ms;
                relay_loss = st->loss_rate;
                best_relay = i;
            }
        }
    }

    /* 决策逻辑 */
    if (best_direct >= 0) {
        /* 直连性能良好：RTT < 100ms 且 loss < 1% → 用直连 */
        if (direct_rtt < 100 && direct_loss < 0.01f)
            return best_direct;

        /* 没有中继替代方案：仍用直连 */
        if (best_relay < 0)
            return best_direct;

        /* 根据当前活跃路径类型取对应阈值（无活跃路径时使用标准PUNCH阈值） */
        p2p_path_type_t cur_type = p2p_get_path_type(s, s->active_path);
        if (cur_type == P2P_PATH_NONE) cur_type = P2P_PATH_PUNCH;
        const path_threshold_config_t *thr = &pm->thresholds[cur_type];

        /* 中继显著更好且自身质量可接受时才切换 */
        /* 中继丢包率阈值：使用配置值的2倍（允许中继有额外开销） */
        bool relay_acceptable = relay_loss < thr->loss_threshold * 2.0f;
        if (relay_acceptable && 
            (relay_rtt + thr->rtt_threshold_ms < direct_rtt ||
             direct_loss > thr->loss_threshold))
            return best_relay;

        /* 差距不大：优先直连（节省成本） */
        return best_direct;
    }

    /* 无直连：用中继 */
    if (best_relay >= 0)
        return best_relay;

    /* TURN */
    if (best_turn >= 0 && should_use_turn(s))
        return best_turn;

    /* 信令转发(SIGNALING)：最终兜底 */
    if (s->signaling.active && path_is_selectable(s->signaling.stats.state))
        return PATH_IDX_SIGNALING;

    return -2; /* 无可用路径 */
}

/*
 * 选择最佳路径（根据策略分发）
 */
int path_manager_select_best_path(p2p_session_t *s) {
    switch (s->path_mgr.strategy) {
        default:
        case P2P_PATH_STRATEGY_CONNECTION_FIRST:
            return select_path_connection_first(s);
        case P2P_PATH_STRATEGY_PERFORMANCE_FIRST:
            return select_path_performance_first(s);
        case P2P_PATH_STRATEGY_HYBRID:
            return select_path_hybrid(s);
    }
}

/*
 * 检查是否应该防抖（避免频繁切换）
 *   三级检查：冷却期 → 频率检测 → 稳定窗口
 */
static bool should_debounce_switch(p2p_session_t *s, int target_path, uint64_t now_ms) {

    path_manager_t *pm = &s->path_mgr;
    
    // 1. 冷却期检查：根据目标路径类型取对应的冷却时间
    p2p_path_type_t target_type = p2p_get_path_type(s, target_path);
    uint64_t cooldown = pm->thresholds[target_type].cooldown_ms;
    if (cooldown == 0) cooldown = DEFAULT_COOLDOWN_MS;
    if (tick_diff(now_ms, pm->last_switch_time) < cooldown) {
        return true;
    }
    
    // 2. 频率检查：最近 30 秒内切换 >= 5 次，强制防抖
    int recent_switches = path_manager_get_switch_frequency(s, SWITCH_FREQ_WINDOW_MS);
    if (recent_switches >= SWITCH_FREQ_THRESHOLD) {
        return true;
    }
    
    // 3. 稳定窗口检查：根据目标路径类型取对应的稳定窗口
    uint32_t stability_ms = pm->thresholds[target_type].stability_window_ms > 0
                          ? pm->thresholds[target_type].stability_window_ms
                          : DEFAULT_STABILITY_WINDOW_MS;

    if (pm->pending_switch_path >= -1) {

        // 如果目标变了，重置稳定窗口
        if (pm->pending_switch_path != target_path) {
            pm->pending_switch_path = target_path;
            pm->debounce_timer_ms = now_ms;
            return true;
        }
        // 已在等待同一目标稳定
        if (tick_diff(now_ms, pm->debounce_timer_ms) >= stability_ms) {
            return false; /* 稳定期已过，可以切换 */
        }
        return true; /* 仍在稳定期 */
    }
    
    // 首次检测到需要切换，启动稳定窗口
    pm->pending_switch_path = target_path;
    pm->debounce_timer_ms = now_ms;
    return true;
}

/*
 * 记录切换历史到环形缓冲区（共享逻辑，正常切换和故障转移都用）
 */
static void record_switch(p2p_session_t *s, int from_path, int to_path,
                          const char *reason, uint64_t now_ms) {
    path_manager_t *pm = &s->path_mgr;
    int idx = pm->switch_history_idx;
    path_switch_record_t *rec = &pm->switch_history[idx];

    rec->from_path = from_path;
    rec->to_path = to_path;
    rec->timestamp_ms = now_ms;
    rec->reason = reason ? reason : "unknown";

    path_stats_t *fs = p2p_get_path_stats(s, from_path);
    if (fs) {
        rec->from_type = p2p_get_path_type(s, from_path);
        rec->from_rtt_ms = fs->rtt_ms;
        rec->from_loss_rate = fs->loss_rate;
    } else {
        rec->from_type = P2P_PATH_NONE;
        rec->from_rtt_ms = 0;
        rec->from_loss_rate = 0.0f;
    }

    path_stats_t *ts = p2p_get_path_stats(s, to_path);
    if (ts) {
        rec->to_type = p2p_get_path_type(s, to_path);
        rec->to_rtt_ms = ts->rtt_ms;
        rec->to_loss_rate = ts->loss_rate;
    } else {
        rec->to_type = P2P_PATH_NONE;
        rec->to_rtt_ms = 0;
        rec->to_loss_rate = 0.0f;
    }

    // 维护切换计数与时间戳
    pm->total_switches++;
    pm->last_switch_time = now_ms;

    // 清除挂起的切换
    pm->pending_switch_path = -2;
    pm->debounce_timer_ms = 0;

    // 添加记录到环形缓冲区
    pm->switch_history_idx = (idx + 1) % MAX_SWITCH_HISTORY;
    if (pm->switch_history_count < MAX_SWITCH_HISTORY)
        pm->switch_history_count++;
}

/*
 * 重置切换相关状态（用于立即切换后的状态同步）
 */
void path_manager_switch_reset(struct p2p_session *s, uint64_t now_ms) {
    path_manager_t *pm = &s->path_mgr;
    pm->last_switch_time = now_ms;
    pm->pending_switch_path = -2;
    pm->debounce_timer_ms = 0;
}

/*
 * 执行路径切换（带防抖和历史记录）
 *   返回: 0=成功, -1=参数错误, 1=防抖中暂不切换
 */
int path_manager_switch_path(p2p_session_t *s,  int target_path, const char *reason, uint64_t now_ms) {

    if (target_path < -1) return -1; /* -1=SIGNALING, >=0=候选 */
    if (target_path == s->active_path) return 0; /* 已是目标路径 */
    
    // 防抖检查
    if (should_debounce_switch(s, target_path, now_ms)) {
        return 1; /* 防抖中，暂不切换 */
    }
    
    // 执行切换（统一更新 active_path 和 active_addr）
    int old_path = s->active_path;
    p2p_set_active_path(s, target_path);

    // 记录切换历史
    record_switch(s, old_path, target_path, reason, now_ms);
    
    return 0;
}

/* ==========================================================================
 *                                 属性/状态访问与设置
 * ========================================================================== */

/*
 * 检查是否有可用路径
 */
bool path_manager_has_active_path(p2p_session_t *s) {

    // 检查 SIGNALING 转发路径是否可用
    if (s->signaling.active &&
        path_is_selectable(s->signaling.stats.state)) {
        return true;
    }
    
    // 检查是否存在可用的候选路径
    for (int i = 0; i < s->remote_cand_cnt; i++) {
        if (path_is_selectable(s->remote_cands[i].stats.state)) {
            return true;
        }
    }
    
    return false;
}

int path_manager_set_path_state(p2p_session_t *s, int path_idx, path_state_t state) {
    
    path_stats_t *stats = p2p_get_path_stats(s, path_idx);
    if (!stats) return -1;
    
    // 设置路径为有效时，需要将 last_recv_ms 初始化为当前时刻（避免无法正确触发超时）
    if (state == PATH_STATE_ACTIVE && stats->last_recv_ms == 0) {
        stats->last_recv_ms = P_tick_ms();
    }
    
    // 如果路径变为 FAILED，记录当前时间戳（health_check 会基于此判断是否进入 RECOVERING）
    if (state == PATH_STATE_FAILED && stats->state != PATH_STATE_FAILED) {
        stats->probe_seq = P_tick_ms();
    }
    
    stats->state = state;
    return 0;
}

/*
 * 设置路径阈值 - 按路径类型独立配置切换参数
 */
int path_manager_set_threshold(p2p_session_t *s, int path_type,
                                uint32_t rtt_ms, float loss_rate,
                                uint64_t cooldown_ms, uint32_t stability_ms) {
    path_manager_t *pm = &s->path_mgr;

    // 有效类型范围：P2P_PATH_NONE(0) ~ P2P_PATH_SIGNALING(4)
    if (path_type < P2P_PATH_NONE || path_type > P2P_PATH_SIGNALING) return -1;

    pm->thresholds[path_type].rtt_threshold_ms  = rtt_ms;
    pm->thresholds[path_type].loss_threshold    = loss_rate;
    pm->thresholds[path_type].cooldown_ms       = cooldown_ms;
    pm->thresholds[path_type].stability_window_ms = stability_ms;

    return 0;
}

path_quality_t path_manager_get_quality(p2p_session_t *s, int path_idx) {
    path_stats_t *stats = p2p_get_path_stats(s, path_idx);
    if (!stats) return PATH_QUALITY_BAD;
    
    // 计算最新的路径质量
    update_quality(stats);
    
    return stats->quality;
}

float path_manager_get_quality_score(p2p_session_t *s, int path_idx) {
    path_stats_t *stats = p2p_get_path_stats(s, path_idx);
    if (!stats) return -1.0f;
    
    // 计算最新的路径质量
    update_quality(stats);
    
    return stats->quality_score;
}

float path_manager_get_quality_trend(p2p_session_t *s, int path_idx) {
    path_stats_t *stats = p2p_get_path_stats(s, path_idx);
    if (!stats) return NAN;
    
    // 计算最新的路径质量
    update_quality(stats);
    
    return stats->quality_trend;
}

/*
 * 获取路径切换历史（按时间倒序返回，最新的在前）
 */
int path_manager_get_switch_history(p2p_session_t *s, 
                                     path_switch_record_t *records,
                                     int max_count) {

    if (!records || max_count <= 0) return 0;
    path_manager_t *pm = &s->path_mgr;
    
    int count = pm->switch_history_count < max_count ? pm->switch_history_count : max_count;
    
    // 按时间倒序（最新在前）从环形缓冲区拷贝
    for (int i = 0; i < count; i++) {
        int idx = (pm->switch_history_idx - 1 - i + MAX_SWITCH_HISTORY) % MAX_SWITCH_HISTORY;
        records[i] = pm->switch_history[idx];
    }
    
    return count;
}

/*
 * 分析路径切换频率（指定时间窗口内的切换次数）
 */
int path_manager_get_switch_frequency(p2p_session_t *s, uint64_t window_ms) {
    
    if (window_ms == 0) return 0;
    path_manager_t *pm = &s->path_mgr;
    if (pm->switch_history_count == 0) return 0;
    
    uint64_t now = P_tick_ms();
    uint64_t cutoff_time = (now > window_ms) ? tick_diff(now, window_ms) : 0;
    
    int count = 0;
    for (int i = 0; i < pm->switch_history_count; i++) {
        int idx = (pm->switch_history_idx - 1 - i + MAX_SWITCH_HISTORY) % MAX_SWITCH_HISTORY;
        if (pm->switch_history[idx].timestamp_ms >= cutoff_time) {
            count++;
        } else {
            break; // 环形缓冲区按时间排序，可以提前退出
        }
    }
    
    return count;
}

/*
 * 获取TURN统计：遍历候选找到 RELAY 类型路径，汇总统计
 */
int path_manager_get_turn_stats(p2p_session_t *s,
                                 uint64_t *total_bytes_sent,
                                 uint64_t *total_bytes_recv,
                                 uint32_t *avg_rtt_ms) {
    uint64_t bytes_sent = 0, bytes_recv = 0;
    uint32_t rtt_sum = 0;
    int turn_count = 0;
    
    for (int i = 0; i < s->remote_cand_cnt; i++) {
        p2p_path_type_t type = p2p_get_path_type(s, i);
        if (type != P2P_PATH_RELAY) continue;
        
        path_stats_t *st = &s->remote_cands[i].stats;
        bytes_sent += st->total_bytes_sent;
        bytes_recv += st->total_bytes_recv;
        rtt_sum += st->rtt_ms;
        turn_count++;
    }
    
    if (turn_count == 0) return -1; /* 无TURN路径 */
    
    if (total_bytes_sent) *total_bytes_sent = bytes_sent;
    if (total_bytes_recv) *total_bytes_recv = bytes_recv;
    if (avg_rtt_ms) *avg_rtt_ms = rtt_sum / turn_count;
    
    return 0;
}


/* ==========================================================================
 *                                    工具函数
 * ========================================================================== */

const char* path_state_str(path_state_t state) {
    switch (state) {
        case PATH_STATE_INIT:       return "INIT";
        case PATH_STATE_PROBING:    return "PROBING";
        case PATH_STATE_ACTIVE:     return "ACTIVE";
        case PATH_STATE_DEGRADED:   return "DEGRADED";
        case PATH_STATE_FAILED:     return "FAILED";
        case PATH_STATE_RECOVERING: return "RECOVERING";
        default:                    return "UNKNOWN";
    }
}

const char* path_quality_str(path_quality_t quality) {
    switch (quality) {
        case PATH_QUALITY_EXCELLENT: return "EXCELLENT";
        case PATH_QUALITY_GOOD:      return "GOOD";
        case PATH_QUALITY_FAIR:      return "FAIR";
        case PATH_QUALITY_POOR:      return "POOR";
        case PATH_QUALITY_BAD:       return "BAD";
        default:                     return "UNKNOWN";
    }
}


/* ==========================================================================
 *                                    状态驱动
 * ========================================================================== */

int path_manager_on_packet_send(p2p_session_t *s, int path_idx, uint32_t seq, uint64_t now_ms, uint32_t size) {
    path_stats_t *stats = p2p_get_path_stats(s, path_idx);
    if (!stats) return -1;
    
    path_manager_t *pm = &s->path_mgr;
    
    // 发送包统计
    stats->last_send_ms = now_ms;
    stats->total_packets_sent++;
    if (size > 0) stats->total_bytes_sent += size;
    
    // 环形队列已满：丢弃队首最老包
    if (pm->pending_count >= MAX_PENDING_PACKETS) {
        packet_track_t *oldest = &pm->pending_packets[pm->pending_head];
        if (oldest->sent_time_ms > 0) {
            path_stats_t *p = p2p_get_path_stats(s, oldest->path_idx);
            if (p) {
                p->total_packets_lost++;                    // 增加路径丢包计数
                p->consecutive_timeouts++;                  // 增加路径连续超时次数
                update_metrics(p);                          // 更新综合指标（丢包率）
            }
        }
        pm->pending_head = (pm->pending_head + 1) % MAX_PENDING_PACKETS;
        pm->pending_count--;
    }
    
    // 添加到队尾
    int tail = (pm->pending_head + pm->pending_count) % MAX_PENDING_PACKETS;
    packet_track_t *track = &pm->pending_packets[tail];
    track->seq = seq;
    track->sent_time_ms = now_ms;
    track->path_idx = path_idx;
    pm->pending_count++;
    
    return 0;
}

/*
 * 记录探测包确认（完成 per-path RTT 测量）
 *
 * 混合模式 RTT 测量策略：
 *   - 原路返回（send_path == recv_path）：记录到 probe_rtt_direct（精确）
 *   - 跨路径返回（send_path != recv_path）：记录到 probe_rtt_cross（估算，降权）
 * 
 * 适用场景：
 *   - PUNCHING 阶段：冷打洞时可能出现单向网络，接受跨路径测量
 *   - CONNECTED 阶段：应该都是原路返回（对称路径已建立）
 */
int path_manager_on_packet_ack(p2p_session_t *s, uint32_t seq, int recv_path, uint64_t now_ms) {
    path_manager_t *pm = &s->path_mgr;
    
    // 在环形队列中查找对应的发送记录（seq=0 为特殊值，也允许测量）
    for (int k = 0; k < pm->pending_count; k++) {
        int idx = (pm->pending_head + k) % MAX_PENDING_PACKETS;
        packet_track_t *track = &pm->pending_packets[idx];
        if (track->sent_time_ms == 0) continue;             // 跳过已消费（应答）的槽位
        if (track->seq != seq) continue;
        
        int send_path = track->path_idx;                    // PUNCH 发送路径
        uint64_t sent_ms = track->sent_time_ms;
        track->sent_time_ms = 0;                            // 标记为已消费（应答）

        // 窗口滑动，推进队首跳过连续空洞
        while (pm->pending_count > 0 &&
               pm->pending_packets[pm->pending_head].sent_time_ms == 0) {
            pm->pending_head = (pm->pending_head + 1) % MAX_PENDING_PACKETS;
            pm->pending_count--;
        }
        
        // 计算 RTT（round trip time）
        uint32_t rtt = (uint32_t)tick_diff(now_ms, sent_ms);
        
        // ========== 关键：判断是否原路返回 ==========
        bool is_roundtrip = (send_path == recv_path);
        
        // 更新发送路径的 RTT 统计（按原路/跨路径分别记录）
        path_stats_t *send_stats = p2p_get_path_stats(s, send_path);
        if (send_stats) {
            if (is_roundtrip) {
                // ========== 原路返回：精确测量，进行 EWMA 平滑 ==========
                
                // 记录原始测量值
                send_stats->probe_rtt_direct = rtt;
                
                // EWMA 平滑算法（TCP-style smoothed RTT）
                if (send_stats->probe_rtt_direct_srtt == 0) {
                    // 首次测量：初始化
                    send_stats->probe_rtt_direct_srtt = rtt;
                    send_stats->probe_rtt_direct_rttvar = rtt / 2;
                } else {
                    // 后续测量：应用 EWMA
                    // SRTT = (1-α) * SRTT + α * RTT
                    // RTTVAR = (1-β) * RTTVAR + β * |SRTT - RTT|
                    int32_t err = (int32_t)rtt - (int32_t)send_stats->probe_rtt_direct_srtt;
                    int32_t new_srtt = (int32_t)send_stats->probe_rtt_direct_srtt + (int32_t)(ALPHA * (float)err);
                    send_stats->probe_rtt_direct_srtt = new_srtt > 0 ? (uint32_t)new_srtt : send_stats->probe_rtt_direct_srtt;
                    
                    int32_t var_err = abs(err) - (int32_t)send_stats->probe_rtt_direct_rttvar;
                    int32_t new_rttvar = (int32_t)send_stats->probe_rtt_direct_rttvar + (int32_t)(BETA * (float)var_err);
                    send_stats->probe_rtt_direct_rttvar = new_rttvar > 0 ? (uint32_t)new_rttvar : 1;  // 最小值1
                }
                
                // 更新 RTT 样本缓冲区（用于统计分析）
                send_stats->rtt_samples[send_stats->rtt_sample_idx] = rtt;
                send_stats->rtt_sample_idx = (send_stats->rtt_sample_idx + 1) % RTT_SAMPLE_COUNT;
                if (send_stats->rtt_sample_count < RTT_SAMPLE_COUNT) {
                    send_stats->rtt_sample_count++;
                }
                
                // 更新 min/max RTT
                if (rtt < send_stats->rtt_min) send_stats->rtt_min = rtt;
                if (rtt > send_stats->rtt_max) send_stats->rtt_max = rtt;
                
                // 重置连续超时计数
                send_stats->consecutive_timeouts = 0;
                
                // 更新综合指标（rtt_ms, rtt_variance, loss_rate）
                update_metrics(send_stats);
                
                // 调试日志
                print("V:", "path_manager: RTT path[%d] = %u ms (direct, srtt=%u, var=%u, seq=%u)",
                      send_path, rtt, send_stats->probe_rtt_direct_srtt, 
                      send_stats->probe_rtt_direct_rttvar, seq);
            } else {
                // ========== 跨路径返回：估算值（仅参考），不平滑 ==========
                // 注意：这个 RTT 不是单条路径的真实延迟，而是 send_path + recv_path 的组合延迟
                send_stats->probe_rtt_cross = rtt;
                
                // 更新综合指标（可能会用跨路径值作为初步参考）
                update_metrics(send_stats);
                
                // 调试日志：标记为跨路径测量
                print("V:", "path_manager: RTT path[%d] = %u ms (cross-path from recv_path=%d, seq=%u)",
                      send_path, rtt, recv_path, seq);
                
                // 提示：跨路径测量仅作为冷打洞阶段的初步参考
                // 在路径选择时，get_effective_rtt() 会对其降权处理（+30% 惩罚）
            }
        }
        
        return (int)rtt;
    }
    
    return -1; /* 未找到对应发送记录 */
}

int path_manager_on_packet_recv(p2p_session_t *s, int path_idx, uint64_t now_ms, uint32_t size) {
    path_stats_t *p = p2p_get_path_stats(s, path_idx);
    if (!p) return -1;

    // 接收包统计
    p->last_recv_ms = now_ms;
    p->total_packets_recv++;
    if (size > 0) p->total_bytes_recv += size;
    
    // 重置路径连续超时次数
    p->consecutive_timeouts = 0;
    
    return 0;
}

int path_manager_on_data_rtt(p2p_session_t *s, int path_idx, uint32_t rtt_ms) {
    path_stats_t *p = p2p_get_path_stats(s, path_idx);
    if (!p) return -1;

    // 记录数据层 RTT（最可靠的测量值，来自 reliable 层 SRTT，已平滑）
    p->data_rtt = rtt_ms;

    // 更新综合指标
    update_metrics(p);
    
    // 定期更新路径质量评估，有节流保护（1秒最多更新一次）
    update_quality(p);
    
    return 0;
}

int path_manager_on_data_loss_rate(p2p_session_t *s, int path_idx, float loss_rate) {
    path_stats_t *p = p2p_get_path_stats(s, path_idx);
    if (!p) return -1;

    p->data_loss_rate = loss_rate;

    // 综合丢包率 = max(探测层, 数据层)
    float probe_loss = (p->total_packets_sent > 0)
        ? (float)p->total_packets_lost / (float)p->total_packets_sent
        : 0.0f;
    p->loss_rate = fmaxf(probe_loss, p->data_loss_rate);
    
    update_quality(p);
    return 0;
}

/* 内部函数：实时更新路径综合指标（RTT/丢包率）
 * 
 * 此函数负责更新路径的综合 RTT 和丢包率，用于路径选择和质量评估。
 * 注意：各测量源的 EWMA 平滑在各自的记录函数中完成。
 */
static void update_metrics(path_stats_t *p) {
    // ========== 选择最优 RTT 作为综合 RTT ==========
    // 优先级：data_rtt > probe_rtt_direct_srtt > probe_rtt_cross * 1.3
    uint32_t effective_rtt = get_effective_rtt(p);
    if (effective_rtt != UINT32_MAX) {
        p->rtt_ms = effective_rtt;
    }
    // 否则保持 rtt_ms 初始值（100ms），避免新路径被完全忽略
    
    // ========== 使用 direct 的方差作为综合抖动 ==========
    // 只有原路返回测量才有可靠的抖动指标
    p->rtt_variance = p->probe_rtt_direct_rttvar;
    
    // ========== 重新计算综合丢包率 ==========
    // 综合丢包率 = max(探测层, 数据层)
    if (p->total_packets_sent > 0) {
        float probe_loss = (float)p->total_packets_lost / (float)p->total_packets_sent;
        p->loss_rate = fmaxf(probe_loss, p->data_loss_rate);
    } else if (p->data_loss_rate > 0.0f) {
        p->loss_rate = p->data_loss_rate;
    }
}

/* 内部函数：定期更新路径质量评估（有节流保护，1秒最多更新一次） */
static void update_quality(path_stats_t *p) {

    // 节流保护，避免过度计算（每秒最多更新一次） 
    uint64_t now_ms = P_tick_ms();
    if (tick_diff(now_ms, p->last_quality_check_ms) < QUALITY_UPDATE_INTERVAL_MS) return;
    p->last_quality_check_ms = now_ms;
    
    // 计算质量评分（0.0-1.0）
    float rtt_score    = 1.0f - fminf((float)p->rtt_ms / 500.0f, 1.0f);
    float jitter_score = 1.0f - fminf((float)p->rtt_variance / 100.0f, 1.0f);
    float loss_score   = 1.0f - fminf(p->loss_rate / 0.2f, 1.0f);
    
    // 加权计算综合质量评分
    p->quality_score = 0.5f * rtt_score +      /* RTT 权重50% */
                       0.3f * loss_score +     /* 丢包率30% */
                       0.2f * jitter_score;    /* 抖动20% */
    
    // 确定质量等级
    if (p->rtt_ms < 50 && p->loss_rate < 0.01f) {
        p->quality = PATH_QUALITY_EXCELLENT;
    } else if (p->rtt_ms < 100 && p->loss_rate < 0.03f) {
        p->quality = PATH_QUALITY_GOOD;
    } else if (p->rtt_ms < 200 && p->loss_rate < 0.05f) {
        p->quality = PATH_QUALITY_FAIR;
    } else if (p->rtt_ms < 500 && p->loss_rate < 0.1f) {
        p->quality = PATH_QUALITY_POOR;
    } else {
        p->quality = PATH_QUALITY_BAD;
    }
    
    // 计算质量趋势（基于 RTT 样本环形缓冲区，按时间序遍历）
    if (p->rtt_sample_count >= MIN_SAMPLES_FOR_TREND) {

        int mid = p->rtt_sample_count / 2;
        int oldest = (p->rtt_sample_idx - p->rtt_sample_count + RTT_SAMPLE_COUNT) % RTT_SAMPLE_COUNT;
        
        uint32_t first_half_sum = 0, second_half_sum = 0;
        for (int i = 0; i < mid; i++) {
            first_half_sum += p->rtt_samples[(oldest + i) % RTT_SAMPLE_COUNT];
        }
        for (int i = mid; i < p->rtt_sample_count; i++) {
            second_half_sum += p->rtt_samples[(oldest + i) % RTT_SAMPLE_COUNT];
        }
        
        float first_half_avg = (float)first_half_sum / (float)mid;
        float second_half_avg = (float)second_half_sum / (float)(p->rtt_sample_count - mid);
        
        // 负值＝改善，正值＝恶化
        float rtt_change = second_half_avg - first_half_avg;
        
        // 归一化到 -1.0 ~ 1.0
        if (first_half_avg > 0) {
            p->quality_trend = -fminf(fmaxf(rtt_change / first_half_avg, -1.0f), 1.0f);
        } else {
            p->quality_trend = 0.0f;
        }
    }
    
    // 计算稳定性评分（基于 RTT 方差）
    if (p->rtt_sample_count >= MIN_SAMPLES_FOR_STABILITY) {
        
        int oldest = (p->rtt_sample_idx - p->rtt_sample_count + RTT_SAMPLE_COUNT) % RTT_SAMPLE_COUNT;

        uint32_t sum = 0;
        for (int i = 0; i < p->rtt_sample_count; i++) {
            sum += p->rtt_samples[(oldest + i) % RTT_SAMPLE_COUNT];
        }
        float mean = (float)sum / (float)p->rtt_sample_count;
        
        float variance_sum = 0;
        for (int i = 0; i < p->rtt_sample_count; i++) {
            float diff = (float)p->rtt_samples[(oldest + i) % RTT_SAMPLE_COUNT] - mean;
            variance_sum += diff * diff;
        }
        float std_dev = sqrtf(variance_sum / (float)p->rtt_sample_count);
        
        // 稳定性评分：标准差越小越稳定（变异系数）
        float cv = (mean > 0) ? (std_dev / mean) : 1.0f;
        p->stability_score = (int)(100.0f * fmaxf(0.0f, 1.0f - fminf(cv, 1.0f)));
    }
}

/*
 * 对单条路径执行健康检查（共享逻辑，SIGNALING 和候选都用）
 */
static void health_check_one_path(p2p_session_t *s, path_stats_t *p,
                                   int path_idx, uint64_t now_ms) {
    path_manager_t *pm = &s->path_mgr;

    /* ---- 1. ACTIVE / DEGRADED 超时检测 ---- */
    if (p->state == PATH_STATE_ACTIVE || p->state == PATH_STATE_DEGRADED) {
        uint64_t timeout = p->is_lan ? LAN_TIMEOUT_MS : WAN_TIMEOUT_MS;

        if (p->last_recv_ms > 0 && tick_diff(now_ms, p->last_recv_ms) > timeout) {
            int silent_timeouts = (int)(tick_diff(now_ms, p->last_recv_ms) / timeout);
            if (silent_timeouts >= FAILED_TIMEOUT_COUNT) {
                p->state = PATH_STATE_FAILED;

                // 记录进入 FAILED 的时间（用于 30s 后触发 RECOVERING）
                p->probe_seq = now_ms;

                // 故障转移：当前活跃路径失效时，选择新路径
                if (s->active_path == path_idx) {
                    int new_path = path_manager_select_best_path(s);
                    if (new_path >= -1 && new_path != path_idx) {

                        // 增加故障转移计数
                        pm->total_failovers++;

                        // 执行切换（不带防抖，立即切换到新路径）
                        int old_path = s->active_path;
                        p2p_set_active_path(s, new_path);

                        // 记录切换历史
                        record_switch(s, old_path, new_path, "failover", now_ms);
                    }
                }
            }
        }
    }

    /* ---- 2. 性能退化 / 恢复 ---- */
    if (p->state == PATH_STATE_ACTIVE) {
        if (p->rtt_ms > DEGRADED_RTT_MS || p->loss_rate > DEGRADED_LOSS_RATE)
            p->state = PATH_STATE_DEGRADED;
    } else if (p->state == PATH_STATE_DEGRADED) {
        if (p->rtt_ms < RECOVER_RTT_MS && p->loss_rate < RECOVER_LOSS_RATE)
            p->state = PATH_STATE_ACTIVE;
    }

    /* ---- 3. FAILED → RECOVERING：30 秒后探测恢复 ---- */
    if (p->state == PATH_STATE_FAILED) {
        if (p->probe_seq > 0 && tick_diff(now_ms, p->probe_seq) > FAILED_TO_RECOVERING_MS) {
            p->state = PATH_STATE_RECOVERING;

            p->consecutive_timeouts = 0;                        // 重置连续超时计数
            p->probe_seq = now_ms;                              // 恢复开始时间（复用 probe_seq 存储）
            p->last_recv_ms = now_ms;                           // 初始化 last_recv_ms 为当前时刻
        }
    }

    /* ---- 4. RECOVERING 超时或恢复成功 ---- */
    if (p->state == PATH_STATE_RECOVERING) {
        uint64_t recovering_start = p->probe_seq;
        uint64_t elapsed = tick_diff(now_ms, recovering_start);

        // RECOVERING_TIMEOUT_MS 内未恢复 → 回到 FAILED，重新等待
        if (elapsed > RECOVERING_TIMEOUT_MS) {
            p->state = PATH_STATE_FAILED;
            p->probe_seq = now_ms;
        } 
        // 收到数据且最近 2 秒内有活动 → 恢复成功
        else if (p->last_recv_ms > recovering_start &&
                   tick_diff(now_ms, p->last_recv_ms) < RECOVERING_ACTIVITY_MS) {
            
            p->state = PATH_STATE_ACTIVE;
        }
    }
}

void path_manager_tick(p2p_session_t *s, uint64_t now_ms) {
    path_manager_t *pm = &s->path_mgr;
    
    // 周期 health check
    if (tick_diff(now_ms, pm->last_health_check_ms) < pm->health_check_interval_ms) return;
    pm->last_health_check_ms = now_ms;

    // 扫描未 ACK 的探测包，如果超时视为丢包
    for (int k = 0; k < pm->pending_count; k++) {
        int idx = (pm->pending_head + k) % MAX_PENDING_PACKETS;
        packet_track_t *track = &pm->pending_packets[idx];
        if (track->sent_time_ms == 0) continue;     // 跳过已消费（应答）的槽位

        if (tick_diff(now_ms, track->sent_time_ms) > PROBE_LOSS_TIMEOUT_MS) {
            path_stats_t *p = p2p_get_path_stats(s, track->path_idx);
            if (p) {
                p->total_packets_lost++;            // 增加路径丢包计数
                p->consecutive_timeouts++;          // 增加路径连续超时次数
                update_metrics(p);                  // 更新综合指标（丢包率）
            }
            track->sent_time_ms = 0;
        }
    }
    // 窗口滑动，推进队首跳过连续空洞
    while (pm->pending_count > 0 &&
           pm->pending_packets[pm->pending_head].sent_time_ms == 0) {
        pm->pending_head = (pm->pending_head + 1) % MAX_PENDING_PACKETS;
        pm->pending_count--;
    }

    // 检查信令中转路径（如果路径有效）
    if (s->signaling.active) {
        health_check_one_path(s, &s->signaling.stats, PATH_IDX_SIGNALING, now_ms);
    }
    // 检查所有候选路径
    for (int i = 0; i < s->remote_cand_cnt; i++) {
        health_check_one_path(s, &s->remote_cands[i].stats, i, now_ms);
    }
}
