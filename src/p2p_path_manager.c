/*
 * ============================================================================
 * P2P 多路径管理器 (Path Manager)
 * ============================================================================
 *
 * 【架构】
 *
 *   路径信息不由本模块存储，而是绑定在 session 的候选地址上：
 *     - s->remote_cands[i].stats  — 候选路径（索引 >=0）
 *     - s->path_mgr.relay.stats   — 信令中继（索引 -1）
 *   path_manager 仅做决策与状态驱动，所有 API 第一个参数为 p2p_session_t *s。
 *
 *   路径用 cost_score 分类：0=直连(LAN/PUNCH)  5=信令RELAY  >=8=TURN。
 *
 *
 * 【状态机】
 *
 *     INIT ──→ ACTIVE ←──→ DEGRADED
 *                ↓              ↓
 *             FAILED ←──→ RECOVERING
 *
 *   ACTIVE  : RTT<300ms 且 loss<10%，正常传输。
 *   DEGRADED: RTT>300ms 或 loss>10%；恢复到 RTT<250ms 且 loss<5% 回 ACTIVE。
 *   FAILED  : 连续 3 次超时（LAN 5s / 其他 10s）。30 秒后进入恢复探测。
 *   RECOVERING: 10 秒探测窗口，收到响应且 2 秒内有活动则恢复；否则回 FAILED。
 *     — probe_seq 复用存储 recovery_start_time。
 *
 *
 * 【路径选择策略】
 *
 *   1. CONNECTION_FIRST — 按成本优先级：直连 > RELAY候选 > 信令RELAY > TURN。
 *   2. PERFORMANCE_FIRST — 加权评分 0.5×RTT + 0.3×loss + 0.2×jitter，
 *      中继路径 ×0.8 成本惩罚，选最高分。
 *   3. HYBRID — 直连良好(RTT<100ms, loss<1%)则用直连；
 *      中继显著更优(RTT差>阈值 或 loss>阈值)才切换；否则优先直连。
 *
 *   TURN 路径仅在 use_as_last_resort 且所有非 TURN 路径失效时启用。
 *
 *
 * 【RTT 测量 — TCP EWMA 算法】
 *
 *   err      = sample - srtt
 *   srtt    += α × err          (α = 1/8)
 *   rttvar  += β × (|err| - rttvar)  (β = 1/4)
 *
 *   数据流：on_packet_send 记录 seq→时间到 pending_packets[]（FIFO，最多32）→
 *   on_packet_ack 计算 RTT 并写入 rtt_samples[10] 滑动窗口 → update_metrics
 *   更新 EWMA。on_packet_loss 递增丢包计数。
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
#include "p2p_path_manager.h"
#include "p2p_common.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* 前向声明（health_check_one_path 中调用 failover） */
int path_manager_failover(p2p_session_t *s, int failed_path);
static int path_manager_on_packet_loss(p2p_session_t *s, uint32_t seq);
static bool path_manager_should_debounce_switch(p2p_session_t *s, int target_path, uint64_t now_ms);
static bool path_manager_should_use_turn(p2p_session_t *s);

/*
 * 将路径索引转换为路径类型
 *   PATH_IDX_RELAY(-1) → P2P_PATH_SIGNALING
 *   TURN 候选（cand.type==P2P_CAND_RELAY）→ P2P_PATH_RELAY
 *   LAN 候选（stats.is_lan）→ P2P_PATH_LAN
 *   其他候选 → P2P_PATH_PUNCH
 */
static p2p_path_t path_idx_to_type(p2p_session_t *s, int path_idx) {
    if (path_idx == PATH_IDX_RELAY)
        return P2P_PATH_SIGNALING;
    if (path_idx < 0 || path_idx >= s->remote_cand_cnt)
        return P2P_PATH_NONE;
    p2p_remote_candidate_entry_t *e = &s->remote_cands[path_idx];
    if (e->cand.type == P2P_CAND_RELAY)
        return P2P_PATH_RELAY;
    if (e->stats.is_lan)
        return P2P_PATH_LAN;
    return P2P_PATH_PUNCH;
}

/* 默认参数 */
#define DEFAULT_SWITCH_COOLDOWN_MS      5000    /* 5秒切换冷却时间 */
#define DEFAULT_SWITCH_RTT_THRESHOLD    50      /* RTT 改善50ms触发切换 */
#define DEFAULT_SWITCH_LOSS_THRESHOLD   0.05f   /* 丢包率5%触发切换 */
#define DEFAULT_PROBE_INTERVAL_MS       1000    /* 1秒探测间隔 */
#define DEFAULT_HEALTH_CHECK_INTERVAL   500     /* 500ms 健康检查间隔 */

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
    if (!s) return -1;
    
    path_manager_t *pm = &s->path_mgr;
    memset(pm, 0, sizeof(*pm));
    
    /* 路径选择策略 */
    pm->strategy = strategy;
    
    /* 初始化路径索引（无活跃路径） */
    pm->active_path = -2;  // -2 表示无活跃路径
    
    /* RELAY 初始化为未启用 */
    pm->relay.active = false;
    memset(&pm->relay.addr, 0, sizeof(pm->relay.addr));
    memset(&pm->relay.stats, 0, sizeof(pm->relay.stats));
    pm->relay.stats.state = PATH_STATE_INIT;
    
    /* 设置默认参数 */
    pm->path_switch_cooldown_ms = DEFAULT_SWITCH_COOLDOWN_MS;
    pm->switch_rtt_threshold_ms = DEFAULT_SWITCH_RTT_THRESHOLD;
    pm->switch_loss_threshold = DEFAULT_SWITCH_LOSS_THRESHOLD;
    pm->stability_window_ms = 2000;  // 默认稳定窗口 2 秒
    pm->probe_interval_ms = DEFAULT_PROBE_INTERVAL_MS;
    pm->health_check_interval_ms = DEFAULT_HEALTH_CHECK_INTERVAL;
    
    pm->pending_switch_path = -2;  // -2 表示无待切换路径
    
    /* TURN 配置（注：TURN 是候选，此配置仅用于策略调整） */
    pm->turn_config.enabled = true;
    pm->turn_config.cost_multiplier = 5;
    pm->turn_config.max_bandwidth_bps = 0;
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
 * 初始化路径统计默认值
 * 避免 memset(0) 导致的问题：rtt_min=0 破坏最小值跟踪，quality=BAD 等
 */
void path_stats_init(path_stats_t *st, int cost_score) {
    if (!st) return;
    memset(st, 0, sizeof(*st));
    
    st->state = PATH_STATE_INIT;
    st->cost_score = cost_score;
    
    /* RTT：使用合理初始值避免新路径被忽略 */
    st->rtt_ms = 100;       /* 初始估计 100ms */
    st->rtt_min = 9999;     /* 等待首次测量 */
    st->rtt_srtt = 100;     /* EWMA 初始 100ms */
    st->rtt_rttvar = 50;    /* 方差 50ms */
    
    /* 质量：初始假设为一般 */
    st->quality = PATH_QUALITY_FAIR;
    st->quality_score = 0.5f;
}

/*
 * 设置信令 RELAY 路径
 */
int path_manager_enable_relay(p2p_session_t *s, struct sockaddr_in *addr) {
    if (!s || !addr) return -1;
    
    path_manager_t *pm = &s->path_mgr;
    pm->relay.active = true;
    pm->relay.addr = *addr;
    path_stats_init(&pm->relay.stats, 5);       /* RELAY cost=5 */
    pm->relay.stats.state = PATH_STATE_ACTIVE;   /* RELAY 一设置就可用 */
    
    return 0;
}

/*
 * TURN配置
 */
int path_manager_configure_turn(p2p_session_t *s,
                                 bool enabled,
                                 int cost_multiplier,
                                 uint32_t max_bandwidth_bps,
                                 bool last_resort_only) {
    if (!s) return -1;
    path_manager_t *pm = &s->path_mgr;
    pm->turn_config.enabled = enabled;
    pm->turn_config.cost_multiplier = cost_multiplier;
    pm->turn_config.max_bandwidth_bps = max_bandwidth_bps;
    pm->turn_config.use_as_last_resort = last_resort_only;
    return 0;
}

/*
 * 添加TURN路径 - 方案A：TURN是候选的一种类型，由外部添加到候选列表
 * 此函数仅返回提示信息
 */
int path_manager_add_turn_path(p2p_session_t *s, struct sockaddr_in *addr) {
    (void)s; (void)addr;
    /* 方案A：TURN路径通过p2p_session_t的候选机制添加，不需要单独API */
    return -1;
}


/* ==========================================================================
 *                                    操作执行
 * ========================================================================== */

/*
 * 辅助：检查路径是否可选（ACTIVE 或 RECOVERING）
 */
static bool path_is_selectable(path_state_t state) {
    return state == PATH_STATE_ACTIVE || state == PATH_STATE_RECOVERING;
}

/*
 * 辅助：判断候选路径是否为直连（cost_score == 0 为 LAN/PUNCH）
 */
static bool path_is_direct(const path_stats_t *stats) {
    return stats->cost_score == 0;
}

/*
 * 辅助：判断候选路径是否为 TURN 类型（cost_score >= 8）
 */
static bool path_is_turn(const path_stats_t *stats) {
    return stats->cost_score >= 8;
}

/*
 * 策略 1: CONNECTION_FIRST（P2P 直连优先）
 *   优先选直连(cost=0)，其次RELAY(cost=5)，最后TURN(cost>=8)
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

        if (path_is_turn(st)) {
            /* TURN 路径：仅在 use_as_last_resort 时跳过 */
            if (pm->turn_config.use_as_last_resort) continue;
            if (st->rtt_ms < best_turn_rtt) {
                best_turn_rtt = st->rtt_ms;
                best_turn = i;
            }
        } else if (path_is_direct(st)) {
            if (st->rtt_ms < best_direct_rtt) {
                best_direct_rtt = st->rtt_ms;
                best_direct = i;
            }
        } else {
            /* 中继类候选（cost > 0 且 < 8） */
            if (st->rtt_ms < best_relay_rtt) {
                best_relay_rtt = st->rtt_ms;
                best_relay_cand = i;
            }
        }
    }

    /* 检查信令 RELAY */
    bool relay_ok = pm->relay.active && path_is_selectable(pm->relay.stats.state);

    /* 优先级：直连 > 中继候选 > 信令RELAY > TURN */
    if (best_direct >= 0) return best_direct;
    if (best_relay_cand >= 0) return best_relay_cand;
    if (relay_ok) return PATH_IDX_RELAY;

    /* 最终尝试 TURN（last_resort） */
    if (best_turn >= 0) return best_turn;
    if (pm->turn_config.use_as_last_resort) {
        /* 重新扫描 TURN */
        for (int i = 0; i < s->remote_cand_cnt; i++) {
            path_stats_t *st = &s->remote_cands[i].stats;
            if (path_is_turn(st) && path_is_selectable(st->state))
                return i;
        }
    }
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

        /* TURN last_resort 检查 */
        if (path_is_turn(st) && pm->turn_config.use_as_last_resort &&
            !path_manager_should_use_turn(s)) continue;

        float rtt_score = 1.0f - fminf(st->rtt_ms / 500.0f, 1.0f);
        float loss_score = 1.0f - st->loss_rate;
        float jitter_score = 1.0f - fminf(st->rtt_variance / 100.0f, 1.0f);
        float score = 0.5f * rtt_score + 0.3f * loss_score + 0.2f * jitter_score;

        /* 成本惩罚：中继/TURN 路径打八折 */
        if (!path_is_direct(st)) score *= 0.8f;

        if (score > best_score) {
            best_score = score;
            best_path = i;
        }
    }

    /* 评估信令 RELAY */
    if (pm->relay.active && path_is_selectable(pm->relay.stats.state)) {
        path_stats_t *rst = &pm->relay.stats;
        float rtt_score = 1.0f - fminf(rst->rtt_ms / 500.0f, 1.0f);
        float loss_score = 1.0f - rst->loss_rate;
        float jitter_score = 1.0f - fminf(rst->rtt_variance / 100.0f, 1.0f);
        float score = (0.5f * rtt_score + 0.3f * loss_score + 0.2f * jitter_score) * 0.8f;

        if (score > best_score) {
            best_score = score;
            best_path = PATH_IDX_RELAY;
        }
    }

    return best_path;
}

/*
 * 策略 3: HYBRID（混合模式）
 *   平衡延迟和成本：直连良好用直连，中继显著更优时才切换
 */
static int select_path_hybrid(p2p_session_t *s) {
    path_manager_t *pm = &s->path_mgr;
    int best_direct = -2;
    int best_relay = -2;    /* 包括候选中继和信令 RELAY */
    int best_turn = -2;
    uint32_t direct_rtt = UINT32_MAX;
    float direct_loss = 1.0f;
    uint32_t relay_rtt = UINT32_MAX;
    float relay_loss = 1.0f;

    for (int i = 0; i < s->remote_cand_cnt; i++) {
        path_stats_t *st = &s->remote_cands[i].stats;
        if (!path_is_selectable(st->state)) continue;

        if (path_is_turn(st)) {
            best_turn = i;
        } else if (path_is_direct(st)) {
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

    /* 考虑信令 RELAY */
    if (pm->relay.active && path_is_selectable(pm->relay.stats.state)) {
        path_stats_t *rst = &pm->relay.stats;
        if (rst->rtt_ms < relay_rtt) {
            relay_rtt = rst->rtt_ms;
            relay_loss = rst->loss_rate;
            best_relay = PATH_IDX_RELAY;
        }
    }

    /* 决策逻辑 */
    if (best_direct >= 0) {
        /* 直连性能良好：RTT < 100ms 且 loss < 1% → 用直连 */
        if (direct_rtt < 100 && direct_loss < 0.01f)
            return best_direct;

        /* 没有中继替代方案：仍用直连 */
        if (best_relay < -1)
            return best_direct;

        /* 根据当前活跃路径类型取对应阈值 */
        p2p_path_t cur_type = path_idx_to_type(s, pm->active_path);
        const path_threshold_config_t *thr = &pm->thresholds[cur_type];

        /* 中继显著更好（使用当前路径类型的可配阈值）则切换 */
        if (relay_rtt + thr->rtt_threshold_ms < direct_rtt ||
            direct_loss > thr->loss_threshold)
            return best_relay;

        /* 差距不大：优先直连（节省成本） */
        return best_direct;
    }

    /* 无直连：用中继 */
    if (best_relay >= -1)
        return best_relay;

    /* TURN 兜底 */
    if (best_turn >= 0 && path_manager_should_use_turn(s))
        return best_turn;

    return -2; /* 无可用路径 */
}

/*
 * 判断是否应该使用TURN
 *   遍历候选列表，检查是否所有非TURN路径都失效
 */
static bool path_manager_should_use_turn(p2p_session_t *s) {
    if (!s) return false;
    path_manager_t *pm = &s->path_mgr;
    if (!pm->turn_config.enabled) return false;
    
    /* 如果TURN不是最后手段，总是可用 */
    if (!pm->turn_config.use_as_last_resort) return true;
    
    /* 检查是否存在非TURN的可用路径 */
    
    /* 信令RELAY可用？ */
    if (pm->relay.active && 
        (pm->relay.stats.state == PATH_STATE_ACTIVE ||
         pm->relay.stats.state == PATH_STATE_DEGRADED ||
         pm->relay.stats.state == PATH_STATE_RECOVERING)) {
        return false; /* 有非TURN备选，不需要TURN */
    }
    
    /* 候选中是否有非TURN可用路径？ */
    for (int i = 0; i < s->remote_cand_cnt; i++) {
        path_stats_t *st = &s->remote_cands[i].stats;
        if (path_is_turn(st)) continue; /* 跳过TURN自身 */
        if (st->state == PATH_STATE_ACTIVE ||
            st->state == PATH_STATE_DEGRADED ||
            st->state == PATH_STATE_RECOVERING) {
            return false; /* 有非TURN备选 */
        }
    }
    
    return true; /* 所有非TURN路径失效，需要启用TURN */
}

/*
 * 选择最佳路径（根据策略分发）
 */
int path_manager_select_best_path(p2p_session_t *s) {
    if (!s) return -2;
    
    switch (s->path_mgr.strategy) {
        case P2P_PATH_STRATEGY_CONNECTION_FIRST:
            return select_path_connection_first(s);
        case P2P_PATH_STRATEGY_PERFORMANCE_FIRST:
            return select_path_performance_first(s);
        case P2P_PATH_STRATEGY_HYBRID:
            return select_path_hybrid(s);
        default:
            return select_path_connection_first(s);
    }
}

/*
 * 检查是否应该防抖（避免频繁切换）
 *   三级检查：冷却期 → 频率检测 → 稳定窗口
 */
static bool path_manager_should_debounce_switch(p2p_session_t *s, 
                                          int target_path,
                                          uint64_t now_ms) {
    if (!s) return false;
    path_manager_t *pm = &s->path_mgr;
    
    /* 1. 冷却期检查 */
    if (now_ms - pm->last_switch_time < pm->path_switch_cooldown_ms) {
        return true;
    }
    
    /* 2. 频率检查：最近 30 秒内切换 >= 5 次，强制防抖 */
    int recent_switches = path_manager_get_switch_frequency(s, 30000);
    if (recent_switches >= 5) {
        return true;
    }
    
    /* 3. 稳定窗口检查：根据目标路径类型取对应的稳定窗口 */
    p2p_path_t target_type = path_idx_to_type(s, target_path);
    uint32_t stability_ms = pm->thresholds[target_type].stability_window_ms > 0
                          ? pm->thresholds[target_type].stability_window_ms
                          : 2000;

    if (pm->pending_switch_path >= -1) {
        if (pm->pending_switch_path != target_path) {
            /* 目标变了，重置稳定窗口 */
            pm->pending_switch_path = target_path;
            pm->debounce_timer_ms = now_ms;
            return true;
        }
        /* 已在等待同一目标稳定 */
        if (now_ms - pm->debounce_timer_ms >= stability_ms) {
            return false; /* 稳定期已过，可以切换 */
        }
        return true; /* 仍在稳定期 */
    }
    
    /* 首次检测到需要切换，启动稳定窗口 */
    pm->pending_switch_path = target_path;
    pm->debounce_timer_ms = now_ms;
    return true;
}

/*
 * 执行路径切换（带防抖和历史记录）
 *   返回: 0=成功, -1=参数错误, 1=防抖中暂不切换
 */
int path_manager_switch_path(p2p_session_t *s, 
                              int target_path,
                              const char *reason,
                              uint64_t now_ms) {
    if (!s) return -1;
    path_manager_t *pm = &s->path_mgr;
    if (target_path < -1) return -1; /* -1=RELAY, >=0=候选 */
    if (target_path == pm->active_path) return 0; /* 已是目标路径 */
    
    /* 防抖检查 */
    if (path_manager_should_debounce_switch(s, target_path, now_ms)) {
        return 1; /* 防抖中，暂不切换 */
    }
    
    int old_path = pm->active_path;
    
    /* 执行切换 */
    pm->active_path = target_path;
    pm->last_switch_time = now_ms;
    pm->total_switches++;
    
    /* 清除挂起的切换 */
    pm->pending_switch_path = -2;
    pm->debounce_timer_ms = 0;
    
    /* 记录切换历史（环形缓冲区） */
    int idx = pm->switch_history_idx;
    path_switch_record_t *record = &pm->switch_history[idx];
    
    record->from_path = old_path;
    record->to_path = target_path;
    record->timestamp_ms = now_ms;
    record->reason = reason ? reason : "unknown";
    
    /* 填充来源路径详情 */
    path_stats_t *from_stats = path_manager_get_stats(s, old_path);
    if (from_stats) {
        record->from_type = path_idx_to_type(s, old_path);
        record->from_rtt_ms = from_stats->rtt_ms;
        record->from_loss_rate = from_stats->loss_rate;
    } else {
        record->from_type = P2P_PATH_NONE;
        record->from_rtt_ms = 0;
        record->from_loss_rate = 0.0f;
    }
    
    /* 填充目标路径详情 */
    path_stats_t *to_stats = path_manager_get_stats(s, target_path);
    if (to_stats) {
        record->to_type = path_idx_to_type(s, target_path);
        record->to_rtt_ms = to_stats->rtt_ms;
        record->to_loss_rate = to_stats->loss_rate;
    } else {
        record->to_type = P2P_PATH_NONE;
        record->to_rtt_ms = 0;
        record->to_loss_rate = 0.0f;
    }
    
    /* 推进环形缓冲区指针 */
    pm->switch_history_idx = (idx + 1) % MAX_SWITCH_HISTORY;
    if (pm->switch_history_count < MAX_SWITCH_HISTORY) {
        pm->switch_history_count++;
    }
    
    return 0;
}


/* ==========================================================================
 *                                 属性/状态访问与设置
 * ========================================================================== */

/*
 * 获取路径统计（统一接口）
 */
path_stats_t* path_manager_get_stats(p2p_session_t *s, int path_idx) {
    if (!s) return NULL;
    
    if (path_idx == PATH_IDX_RELAY) {
        /* RELAY 路径 */
        return s->path_mgr.relay.active ? &s->path_mgr.relay.stats : NULL;
    } else if (path_idx >= 0 && path_idx < s->remote_cand_cnt) {
        /* 候选路径 */
        return &s->remote_cands[path_idx].stats;
    }
    
    return NULL;  // 无效索引
}

/*
 * 获取路径地址（统一接口）
 */
const struct sockaddr_in* path_manager_get_addr(p2p_session_t *s, int path_idx) {
    if (!s) return NULL;
    
    if (path_idx == PATH_IDX_RELAY) {
        /* RELAY 路径 */
        return s->path_mgr.relay.active ? &s->path_mgr.relay.addr : NULL;
    } else if (path_idx >= 0 && path_idx < s->remote_cand_cnt) {
        /* 候选路径 */
        return &s->remote_cands[path_idx].cand.addr;
    }
    
    return NULL;  // 无效索引
}

/*
 * 通过地址查找候选索引
 */
int path_manager_find_by_addr(p2p_session_t *s, const struct sockaddr_in *addr) {
    if (!s || !addr) return -2;
    
    /* 检查 RELAY */
    if (s->path_mgr.relay.active && sockaddr_equal(&s->path_mgr.relay.addr, addr)) {
        return PATH_IDX_RELAY;
    }
    
    /* 检查候选 */
    for (int i = 0; i < s->remote_cand_cnt; i++) {
        if (sockaddr_equal(&s->remote_cands[i].cand.addr, addr)) {
            return i;
        }
    }
    
    return -2;  // 未找到
}

/*
 * 获取活跃路径索引
 */
int path_manager_get_active_idx(p2p_session_t *s) {
    return s ? s->path_mgr.active_path : -2;
}

/*
 * 检查是否有可用路径（遍历所有路径检查实际状态）
 */
bool path_manager_has_active_path(p2p_session_t *s) {
    if (!s) return false;
    
    /* 检查 RELAY */
    if (s->path_mgr.relay.active &&
        path_is_selectable(s->path_mgr.relay.stats.state)) {
        return true;
    }
    
    /* 检查所有候选 */
    for (int i = 0; i < s->remote_cand_cnt; i++) {
        if (path_is_selectable(s->remote_cands[i].stats.state)) {
            return true;
        }
    }
    
    return false;
}

/*
 * 设置活跃路径
 */
int path_manager_set_active(p2p_session_t *s, int path_idx) {
    if (!s) return -1;
    s->path_mgr.active_path = path_idx;
    return 0;
}

int path_manager_set_path_state(p2p_session_t *s, int path_idx, path_state_t state) {
    if (!s) return -1;
    
    path_stats_t *stats = path_manager_get_stats(s, path_idx);
    if (!stats) return -1;
    
    /* 如果设置为 ACTIVE 且 last_recv_ms 未初始化，初始化为当前时间避免永不超时 */
    if (state == PATH_STATE_ACTIVE && stats->last_recv_ms == 0) {
        stats->last_recv_ms = P_tick_ms();
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
    if (!s) return -1;
    path_manager_t *pm = &s->path_mgr;

    /* 有效类型范围：P2P_PATH_NONE(0) ~ P2P_PATH_SIGNALING(4) */
    if (path_type < P2P_PATH_NONE || path_type > P2P_PATH_SIGNALING) return -1;

    pm->thresholds[path_type].rtt_threshold_ms  = rtt_ms;
    pm->thresholds[path_type].loss_threshold    = loss_rate;
    pm->thresholds[path_type].cooldown_ms       = cooldown_ms;
    pm->thresholds[path_type].stability_window_ms = stability_ms;

    /* 同步更新全局字段（供旧代码直接访问 pm->switch_rtt_threshold_ms 等） */
    if (path_type == P2P_PATH_PUNCH) {
        pm->switch_rtt_threshold_ms = rtt_ms;
        pm->switch_loss_threshold   = loss_rate;
        pm->path_switch_cooldown_ms = cooldown_ms;
        pm->stability_window_ms     = stability_ms;
    }

    return 0;
}

path_quality_t path_manager_get_quality(p2p_session_t *s, int path_idx) {
    if (!s) return PATH_QUALITY_BAD;
    
    path_stats_t *stats = path_manager_get_stats(s, path_idx);
    if (!stats) return PATH_QUALITY_BAD;
    
    /* 确保质量是最新的 */
    path_manager_update_quality(s, path_idx);
    
    return stats->quality;
}

float path_manager_get_quality_score(p2p_session_t *s, int path_idx) {
    if (!s) return -1.0f;
    
    path_stats_t *stats = path_manager_get_stats(s, path_idx);
    if (!stats) return -1.0f;
    
    /* 确保质量是最新的 */
    path_manager_update_quality(s, path_idx);
    
    return stats->quality_score;
}

float path_manager_get_quality_trend(p2p_session_t *s, int path_idx) {
    if (!s) return NAN;
    
    path_stats_t *stats = path_manager_get_stats(s, path_idx);
    if (!stats) return NAN;
    
    /* 确保质量是最新的 */
    path_manager_update_quality(s, path_idx);
    
    return stats->quality_trend;
}

/*
 * 获取路径切换历史（按时间倒序返回，最新的在前）
 */
int path_manager_get_switch_history(p2p_session_t *s, 
                                     path_switch_record_t *records,
                                     int max_count) {
    if (!s || !records || max_count <= 0) return 0;
    path_manager_t *pm = &s->path_mgr;
    
    int count = pm->switch_history_count < max_count ? pm->switch_history_count : max_count;
    
    /* 按时间倒序（最新在前）从环形缓冲区拷贝 */
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
    if (!s || window_ms == 0) return 0;
    path_manager_t *pm = &s->path_mgr;
    if (pm->switch_history_count == 0) return 0;
    
    uint64_t now = P_tick_ms();
    uint64_t cutoff_time = (now > window_ms) ? (now - window_ms) : 0;
    
    int count = 0;
    for (int i = 0; i < pm->switch_history_count; i++) {
        int idx = (pm->switch_history_idx - 1 - i + MAX_SWITCH_HISTORY) % MAX_SWITCH_HISTORY;
        if (pm->switch_history[idx].timestamp_ms >= cutoff_time) {
            count++;
        } else {
            break; /* 环形缓冲区按时间排序，可以提前退出 */
        }
    }
    
    return count;
}

/*
 * 获取TURN统计：遍历候选找到cost_score>=8的TURN路径，汇总统计
 */
int path_manager_get_turn_stats(p2p_session_t *s,
                                 uint64_t *total_bytes_sent,
                                 uint64_t *total_bytes_recv,
                                 uint32_t *avg_rtt_ms) {
    if (!s) return -1;
    
    uint64_t bytes_sent = 0, bytes_recv = 0;
    uint32_t rtt_sum = 0;
    int turn_count = 0;
    
    for (int i = 0; i < s->remote_cand_cnt; i++) {
        path_stats_t *st = &s->remote_cands[i].stats;
        if (!path_is_turn(st)) continue;
        
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

int path_manager_on_packet_send(p2p_session_t *s, int path_idx, uint32_t seq, uint64_t now_ms) {
    if (!s) return -1;
    
    path_stats_t *stats = path_manager_get_stats(s, path_idx);
    if (!stats) return -1;
    
    path_manager_t *pm = &s->path_mgr;
    
    /* 更新发送统计 */
    stats->last_send_ms = now_ms;
    stats->total_packets_sent++;
    
    /* 缓冲区已满：最老的未 ACK 包视为丢包（on_packet_loss 内部会移除该记录） */
    if (pm->pending_count >= MAX_PENDING_PACKETS) {
        path_manager_on_packet_loss(s, pm->pending_packets[0].seq);
    }
    
    /* 添加新的跟踪记录 */
    packet_track_t *track = &pm->pending_packets[pm->pending_count++];
    track->seq = seq;
    track->sent_time_ms = now_ms;
    track->path_idx = path_idx;
    
    return 0;
}

int path_manager_on_packet_ack(p2p_session_t *s, uint32_t seq, uint64_t now_ms) {
    if (!s) return -1;
    
    path_manager_t *pm = &s->path_mgr;
    
    /* 查找对应的发送记录（seq=0 为特殊值，也允许测量）*/
    for (int i = 0; i < pm->pending_count; i++) {
        if (pm->pending_packets[i].seq == seq) {
            packet_track_t *track = &pm->pending_packets[i];
            int path_idx = track->path_idx;
            
            path_stats_t *p = path_manager_get_stats(s, path_idx);
            if (!p) {
                return -1;
            }
            
            /* 计算 RTT */
            uint32_t rtt = (uint32_t)(now_ms - track->sent_time_ms);
            
            /* 更新 RTT 样本缓冲区 */
            p->rtt_samples[p->rtt_sample_idx] = rtt;
            p->rtt_sample_idx = (p->rtt_sample_idx + 1) % 10;
            if (p->rtt_sample_count < 10) {
                p->rtt_sample_count++;
            }
            
            /* 更新 RTT 统计 */
            if (rtt < p->rtt_min) p->rtt_min = rtt;
            if (rtt > p->rtt_max) p->rtt_max = rtt;
            
            /* 使用 path_manager_update_metrics 更新 EWMA RTT */
            path_manager_update_metrics(s, path_idx, rtt, true);
            
            /* 移除跟踪记录 */
            for (int j = i; j < pm->pending_count - 1; j++) {
                pm->pending_packets[j] = pm->pending_packets[j + 1];
            }
            pm->pending_count--;
            
            return (int)rtt;
        }
    }
    
    return -1; /* 未找到对应发送记录 */
}

int path_manager_on_packet_recv(p2p_session_t *s, int path_idx, uint64_t now_ms) {
    if (!s) return -1;
    
    path_stats_t *p = path_manager_get_stats(s, path_idx);
    if (!p) return -1;
    p->last_recv_ms = now_ms;
    p->total_packets_recv++;
    
    /* 重置连续超时计数 */
    p->consecutive_timeouts = 0;
    
    return 0;
}

static int path_manager_on_packet_loss(p2p_session_t *s, uint32_t seq) {
    if (!s) return -1;
    
    path_manager_t *pm = &s->path_mgr;
    
    /* 查找对应的发送记录 */
    for (int i = 0; i < pm->pending_count; i++) {
        if (pm->pending_packets[i].seq == seq) {
            packet_track_t *track = &pm->pending_packets[i];
            int path_idx = track->path_idx;
            
            path_stats_t *p = path_manager_get_stats(s, path_idx);
            if (!p) {
                return -1;
            }
            
            /* 更新丢包统计 */
            p->total_packets_lost++;
            p->consecutive_timeouts++;
            
            /* 使用 path_manager_update_metrics 更新丢包率 */
            path_manager_update_metrics(s, path_idx, 0, false);
            
            /* 移除跟踪记录 */
            for (int j = i; j < pm->pending_count - 1; j++) {
                pm->pending_packets[j] = pm->pending_packets[j + 1];
            }
            pm->pending_count--;
            
            return 0;
        }
    }
    
    return -1; /* 未找到对应发送记录 */
}

int path_manager_update_metrics(p2p_session_t *s, int path_idx, uint32_t rtt_ms, bool success) {
    if (!s) return -1;
    
    path_stats_t *p = path_manager_get_stats(s, path_idx);
    if (!p) return -1;
    
    /* 更新 RTT（使用 TCP 平滑算法） */
    if (success) {
        if (p->rtt_srtt == 0) {
            /* 首次测量 */
            p->rtt_srtt = rtt_ms;
            p->rtt_rttvar = rtt_ms / 2;
        } else {
            /* EWMA 平滑（使用有符号算术避免 UB） */
            int32_t err = (int32_t)rtt_ms - (int32_t)p->rtt_srtt;
            p->rtt_srtt = (uint32_t)((int32_t)p->rtt_srtt + (int32_t)(ALPHA * err));
            int32_t var_err = abs(err) - (int32_t)p->rtt_rttvar;
            p->rtt_rttvar = (uint32_t)((int32_t)p->rtt_rttvar + (int32_t)(BETA * var_err));
        }
        
        p->rtt_ms = p->rtt_srtt;
        p->rtt_variance = p->rtt_rttvar;
        
        /* 更新最小 RTT */
        if (rtt_ms < p->rtt_min) {
            p->rtt_min = rtt_ms;
        }
        
        p->consecutive_timeouts = 0;
    }
    
    /* 重新计算丢包率（基于已有计数器，计数器在 on_packet_send/loss 中更新） */
    if (p->total_packets_sent > 0) {
        p->loss_rate = (float)p->total_packets_lost / (float)p->total_packets_sent;
    }
    
    return 0;
}

int path_manager_update_quality(p2p_session_t *s, int path_idx) {
    if (!s) return -1;
    
    path_stats_t *p = path_manager_get_stats(s, path_idx);
    if (!p) return -1;
    
    /* 频率限制：避免过度计算（每秒最多更新一次） */
    uint64_t now_ms = P_tick_ms();
    if (now_ms - p->last_quality_check_ms < 1000) {
        return 0;  /* 跳过更新，使用缓存值 */
    }
    p->last_quality_check_ms = now_ms;
    
    /* 计算质量评分（0.0-1.0） */
    float rtt_score = 1.0f - fminf(p->rtt_ms / 500.0f, 1.0f);
    float loss_score = 1.0f - fminf(p->loss_rate / 0.2f, 1.0f);
    float jitter_score = 1.0f - fminf(p->rtt_variance / 100.0f, 1.0f);
    
    /* 加权计算综合质量评分 */
    p->quality_score = 0.5f * rtt_score +      /* RTT 权重50% */
                       0.3f * loss_score +      /* 丢包率30% */
                       0.2f * jitter_score;     /* 抖动20% */
    
    /* 确定质量等级 */
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
    
    /* 计算质量趋势（基于 RTT 样本环形缓冲区，按时间序遍历） */
    if (p->rtt_sample_count >= 5) {
        int mid = p->rtt_sample_count / 2;
        uint32_t first_half_sum = 0, second_half_sum = 0;
        int oldest = (p->rtt_sample_idx - p->rtt_sample_count + 10) % 10;
        
        for (int i = 0; i < mid; i++) {
            first_half_sum += p->rtt_samples[(oldest + i) % 10];
        }
        for (int i = mid; i < p->rtt_sample_count; i++) {
            second_half_sum += p->rtt_samples[(oldest + i) % 10];
        }
        
        float first_half_avg = (float)first_half_sum / mid;
        float second_half_avg = (float)second_half_sum / (p->rtt_sample_count - mid);
        
        /* 负值＝改善，正值＝恶化 */
        float rtt_change = second_half_avg - first_half_avg;
        
        /* 归一化到 -1.0 ~ 1.0 */
        if (first_half_avg > 0) {
            p->quality_trend = -fminf(fmaxf(rtt_change / first_half_avg, -1.0f), 1.0f);
        } else {
            p->quality_trend = 0.0f;
        }
    }
    
    /* 计算稳定性评分（基于 RTT 方差） */
    if (p->rtt_sample_count >= 3) {
        uint32_t sum = 0;
        for (int i = 0; i < p->rtt_sample_count; i++) {
            sum += p->rtt_samples[i];
        }
        float mean = (float)sum / p->rtt_sample_count;
        
        float variance_sum = 0;
        for (int i = 0; i < p->rtt_sample_count; i++) {
            float diff = (float)p->rtt_samples[i] - mean;
            variance_sum += diff * diff;
        }
        float std_dev = sqrtf(variance_sum / p->rtt_sample_count);
        
        /* 稳定性评分：标准差越小越稳定（变异系数） */
        float cv = (mean > 0) ? (std_dev / mean) : 1.0f;
        p->stability_score = (int)(100.0f * fmaxf(0.0f, 1.0f - fminf(cv, 1.0f)));
    }
    
    return 0;
}

/*
 * 对单条路径执行健康检查（共享逻辑，RELAY和候选都用）
 *   is_lan: 该路径是否为 LAN（超时更短）
 *   path_chn: 路径索引（-1=RELAY, >=0=候选），用于故障转移
 */
static void health_check_one_path(p2p_session_t *s, path_stats_t *p,
                                   int path_idx, uint64_t now_ms) {
    path_manager_t *pm = &s->path_mgr;

    /* ---- 1. ACTIVE 超时检测 ---- */
    if (p->state == PATH_STATE_ACTIVE) {
        uint64_t timeout = p->is_lan ? 5000 : 10000;

        if (p->last_recv_ms > 0 && now_ms - p->last_recv_ms > timeout) {
            p->consecutive_timeouts = (int)((now_ms - p->last_recv_ms) / timeout);
            if (p->consecutive_timeouts >= 3) {
                p->state = PATH_STATE_FAILED;
                /* 触发故障转移 */
                if (pm->active_path == path_idx) {
                    path_manager_failover(s, path_idx);
                }
            }
        } else {
            p->consecutive_timeouts = 0;
        }
    }

    /* ---- 2. 性能退化 / 恢复 ---- */
    if (p->state == PATH_STATE_ACTIVE) {
        if (p->rtt_ms > 300 || p->loss_rate > 0.1f)
            p->state = PATH_STATE_DEGRADED;
    } else if (p->state == PATH_STATE_DEGRADED) {
        if (p->rtt_ms < 250 && p->loss_rate < 0.05f)
            p->state = PATH_STATE_ACTIVE;
    }

    /* ---- 3. FAILED → RECOVERING：30 秒后探测恢复 ---- */
    if (p->state == PATH_STATE_FAILED) {
        if (p->last_send_ms > 0 && now_ms - p->last_send_ms > 30000) {
            p->state = PATH_STATE_RECOVERING;
            p->consecutive_timeouts = 0;
            /* 复用 probe_seq 存储恢复开始时间 */
            p->probe_seq = now_ms;
            /* 初始化 last_recv_ms 避免使用旧值 */
            p->last_recv_ms = now_ms;
        }
    }

    /* ---- 4. RECOVERING 超时或恢复成功 ---- */
    if (p->state == PATH_STATE_RECOVERING) {
        uint64_t recovering_start = p->probe_seq;
        uint64_t elapsed = now_ms - recovering_start;

        if (elapsed > 10000) {
            /* 10 秒未恢复 → 回到 FAILED */
            p->state = PATH_STATE_FAILED;
        } else if (p->last_recv_ms > recovering_start &&
                   now_ms - p->last_recv_ms < 2000) {
            /* 收到数据且最近 2 秒内有活动 → 恢复成功 */
            p->state = PATH_STATE_ACTIVE;
        }
    }
}

void path_manager_health_check(p2p_session_t *s, uint64_t now_ms) {
    if (!s) return;
    
    path_manager_t *pm = &s->path_mgr;
    
    /* 节流：避免过于频繁的健康检查 */
    if (now_ms - pm->last_health_check_ms < pm->health_check_interval_ms) {
        return;
    }
    pm->last_health_check_ms = now_ms;

    /* 扫描超时未 ACK 的探测包，视为丢包
     * 阈值：3 倍 PUNCH_INTERVAL（覆盖 2 次重传才判定丢失），上限 3000ms。
     * 从后向前遍历，on_packet_loss 移除 i 处记录后不影响 0..i-1。 */
#define PROBE_LOSS_TIMEOUT_MS 3000
    for (int i = pm->pending_count - 1; i >= 0; i--) {
        if (now_ms - pm->pending_packets[i].sent_time_ms > PROBE_LOSS_TIMEOUT_MS) {
            path_manager_on_packet_loss(s, pm->pending_packets[i].seq);
        }
    }

    /* 检查 RELAY 路径 */
    if (pm->relay.active) {
        health_check_one_path(s, &pm->relay.stats, PATH_IDX_RELAY, now_ms);
    }
    
    /* 检查所有候选路径 */
    for (int i = 0; i < s->remote_cand_cnt; i++) {
        path_stats_t *stats = &s->remote_cands[i].stats;
        health_check_one_path(s, stats, i, now_ms);
    }
}

int path_manager_failover(p2p_session_t *s, int failed_path) {
    if (!s) return -1;
    
    path_manager_t *pm = &s->path_mgr;
    
    /* 标记失效路径 */
    path_stats_t *failed_stats = path_manager_get_stats(s, failed_path);
    if (failed_stats) {
        failed_stats->state = PATH_STATE_FAILED;
    }
    
    /* 选择新路径 */
    int new_path = path_manager_select_best_path(s);
    
    if (new_path >= -1 && new_path != failed_path) {
        int old_path = pm->active_path;
        pm->active_path = new_path;
        pm->total_switches++;
        pm->total_failovers++;
        
        /* 记录故障转移到切换历史（与 switch_path 共用历史缓冲区） */
        uint64_t now_ms = P_tick_ms();
        int idx = pm->switch_history_idx;
        path_switch_record_t *rec = &pm->switch_history[idx];
        rec->from_path = old_path;
        rec->to_path = new_path;
        rec->timestamp_ms = now_ms;
        rec->reason = "failover";
        
        path_stats_t *fs = path_manager_get_stats(s, old_path);
        if (fs) {
            rec->from_type = path_idx_to_type(s, old_path);
            rec->from_rtt_ms = fs->rtt_ms;
            rec->from_loss_rate = fs->loss_rate;
        } else {
            rec->from_type = P2P_PATH_NONE;
            rec->from_rtt_ms = 0;
            rec->from_loss_rate = 0.0f;
        }
        
        path_stats_t *ts = path_manager_get_stats(s, new_path);
        if (ts) {
            rec->to_type = path_idx_to_type(s, new_path);
            rec->to_rtt_ms = ts->rtt_ms;
            rec->to_loss_rate = ts->loss_rate;
        } else {
            rec->to_type = P2P_PATH_NONE;
            rec->to_rtt_ms = 0;
            rec->to_loss_rate = 0.0f;
        }
        
        pm->switch_history_idx = (idx + 1) % MAX_SWITCH_HISTORY;
        if (pm->switch_history_count < MAX_SWITCH_HISTORY)
            pm->switch_history_count++;
        
        return new_path;
    }
    
    return -1; /* 无可用备用路径 */
}

