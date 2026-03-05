/*
 * ============================================================================
 * P2P 多路径管理器实现 (Path Manager)
 * ============================================================================
 * 
 * 【模块概述】
 * 
 * 多路径管理器负责在 P2P 连接中管理多条并发路径（LAN/PUNCH/RELAY/TURN），
 * 实时监控路径质量，执行智能路径选择和自动故障转移，确保连接的可靠性和性能。
 * 
 * 核心功能：
 *   1. 多路径管理：支持最多 4 条路径同时存在
 *   2. 质量监控：实时测量 RTT、丢包率、抖动、带宽等指标
 *   3. 智能选择：根据策略（连接优先/性能优先/混合）自动选择最佳路径
 *   4. 故障转移：检测路径失效并自动切换到备用路径
 *   5. 性能恢复：失效路径定期探测并在恢复后重新启用
 * 
 * 
 * ============================================================================
 * 【核心概念】
 * ============================================================================
 * 
 * 一、路径类型 (Path Type)
 * -------------------------
 * 
 *   P2P_PATH_LAN    - 局域网直连（延迟最低，免费）
 *   P2P_PATH_PUNCH  - NAT 打洞直连（延迟低，免费）
 *   P2P_PATH_RELAY  - 服务器中继（延迟中等，有成本）
 *   P2P_PATH_TURN   - TURN 中继（延迟较高，成本最高，兜底方案）
 * 
 *   优先级（连接优先策略）：LAN > PUNCH > RELAY > TURN
 *   成本评分：LAN=0, PUNCH=0, RELAY=5, TURN=8
 * 
 * 
 * 二、路径状态机 (Path State Machine)
 * -----------------------------------
 * 
 *   路径生命周期包含 6 种状态：
 * 
 *   1. INIT（初始化）
 *      - 新路径刚被添加，未进行任何测试
 *      - 初始 RTT: 100ms（合理估计），rtt_min: 9999ms（待测量）
 *      - 转换：外部调用 set_path_state() → ACTIVE/PROBING
 * 
 *   2. PROBING（探测中）
 *      - 正在进行连通性测试（预留状态，当前未使用）
 *      - 转换：测试成功 → ACTIVE，测试失败 → FAILED
 * 
 *   3. ACTIVE（激活）
 *      - 路径正常工作，性能良好
 *      - 条件：RTT < 300ms 且 loss_rate < 10%
 *      - 转换：性能下降 → DEGRADED，无响应 → FAILED
 * 
 *   4. DEGRADED（性能退化）
 *      - 路径可用但性能下降
 *      - 触发：RTT > 300ms 或 loss_rate > 10%
 *      - 恢复条件：RTT < 250ms 且 loss_rate < 5%
 *      - 转换：恢复 → ACTIVE，持续恶化 → FAILED
 * 
 *   5. FAILED（失效）
 *      - 路径完全失效，不可使用
 *      - 触发：连续 3 次超时（ACTIVE 状态下 10秒无响应）
 *      - 每 30 秒尝试恢复探测
 *      - 转换：开始探测 → RECOVERING
 * 
 *   6. RECOVERING（恢复中）
 *      - 失效路径正在尝试恢复
 *      - 探测窗口：10 秒
 *      - 成功条件：收到响应且最近 2 秒内有活动
 *      - 转换：成功 → ACTIVE，超时 → FAILED
 * 
 *   状态转换图：
 *   
 *      INIT ──→ ACTIVE ←──→ DEGRADED
 *                 ↓              ↓
 *              FAILED ←──→ RECOVERING
 * 
 * 
 * 三、路径选择策略 (Selection Strategy)
 * ------------------------------------
 * 
 *   1. CONNECTION_FIRST（连接优先）
 *      - 优先级：LAN > PUNCH > RELAY > TURN
 *      - 适用场景：追求低延迟和低成本，优先使用直连
 *      - 特点：只要直连可用就不切换到中继
 * 
 *   2. PERFORMANCE_FIRST（性能优先）
 *      - 综合评分 = 0.5×RTT评分 + 0.3×丢包率评分 + 0.2×抖动评分
 *      - 中继路径惩罚：评分 × 0.8
 *      - 适用场景：追求最佳传输质量，可接受使用中继
 *      - 特点：RTT/丢包率显著改善时切换到中继
 * 
 *   3. HYBRID（混合模式）
 *      - 平衡延迟和成本
 *      - 决策逻辑：
 *        · 直连性能良好（RTT<100ms 且 loss<1%）→ 使用直连
 *        · 中继显著更优（RTT差距>50ms 或 loss>5%）→ 使用中继
 *        · 否则 → 优先直连（节省成本）
 *      - 适用场景：大多数实际应用的推荐策略
 * 
 * 
 * ============================================================================
 * 【关键机制】
 * ============================================================================
 * 
 * 一、健康检查 (Health Check)
 * ---------------------------
 * 
 *   周期：默认 500ms（可配置）
 *   检查内容：
 *   
 *   1. 超时检测（ACTIVE 状态）
 *      - LAN 路径：5 秒无响应触发超时
 *      - 其他路径：10 秒无响应触发超时
 *      - 连续 3 次超时 → 标记 FAILED
 * 
 *   2. 性能退化检测
 *      - ACTIVE 状态下：RTT>300ms 或 loss>10% → 转 DEGRADED
 *      - DEGRADED 状态下：RTT<250ms 且 loss<5% → 恢复 ACTIVE
 * 
 *   3. 失效路径恢复探测
 *      - FAILED 状态超过 30 秒 → 启动恢复（转 RECOVERING）
 *      - 初始化 probe_seq = now_ms（存储恢复开始时间）
 *      - 初始化 last_recv_ms = now_ms（防止立即超时）
 * 
 *   4. 恢复超时检测
 *      - RECOVERING 状态超过 10 秒未恢复 → 转回 FAILED
 *      - 收到响应且最近 2 秒有活动 → 恢复成功（转 ACTIVE）
 * 
 * 
 * 二、故障转移 (Failover)
 * -----------------------
 * 
 *   触发条件：active_path 进入 FAILED 状态
 *   
 *   执行流程：
 *   1. 标记失效路径为 FAILED
 *   2. 调用 select_best_path() 选择新路径
 *   3. 更新 active_path 索引
 *   4. 递增 total_path_switches 计数器
 *   5. 返回新路径索引（-1 表示无可用路径）
 *   
 *   注意：RECOVERING 状态的路径可以被选中（允许尝试恢复中的路径）
 * 
 * 
 * 三、RTT 测量 (Round-Trip Time Measurement)
 * ------------------------------------------
 * 
 *   算法：TCP 平滑算法（EWMA）
 *   
 *   参数：
 *     ALPHA = 0.125 (1/8)   - SRTT 平滑系数
 *     BETA  = 0.25  (1/4)   - RTTVAR 平滑系数
 *   
 *   公式：
 *     err = rtt_sample - rtt_srtt
 *     rtt_srtt += ALPHA × err
 *     rtt_rttvar += BETA × (|err| - rtt_rttvar)
 *   
 *   流程：
 *   1. 发送数据包：on_packet_send(seq, now_ms)
 *      - 记录到 pending_packets[] 缓冲区（最多32个）
 *      - 递增 total_packets_sent
 *   
 *   2. 收到 ACK：on_packet_ack(seq, now_ms)
 *      - 查找 pending_packets[] 中的发送记录
 *      - 计算 RTT = now_ms - sent_time_ms
 *      - 更新 rtt_samples[] 滑动窗口（10个样本）
 *      - 调用 update_metrics(rtt, true) 更新 EWMA
 *   
 *   3. 包丢失：on_packet_loss(seq)
 *      - 递增 total_packets_lost
 *      - 递增 consecutive_timeouts
 *      - 调用 update_metrics(0, false) 重新计算 loss_rate
 *   
 *   注意：update_metrics() 只负责计算指标，不修改计数器
 * 
 * 
 * 四、丢包统计 (Packet Loss Tracking)
 * ------------------------------------
 * 
 *   计数器：
 *     total_packets_sent  - 总发送数（在 on_packet_send 递增）
 *     total_packets_lost  - 总丢包数（在 on_packet_loss 递增）
 *   
 *   计算：
 *     loss_rate = total_packets_lost / total_packets_sent
 *   
 *   重要：计数器更新和指标计算分离
 *     - on_packet_send/loss: 更新计数器
 *     - update_metrics: 只计算 loss_rate，不修改计数器
 *   
 *   这避免了双重计数 bug（早期版本在 update_metrics 中也递增计数器）
 * 
 * 
 * 五、防抖动机制 (Debounce Mechanism)
 * -----------------------------------
 * 
 *   目的：避免路径频繁抖动切换
 *   
 *   机制：
 *   1. 冷却期（Cooldown）
 *      - 路径切换后 5 秒内禁止再次切换
 *   
 *   2. 稳定窗口（Stability Window）
 *      - 检测到需要切换时，等待 2 秒确认性能稳定
 *      - 稳定窗口内路径持续满足切换条件 → 执行切换
 *      - 条件不再满足 → 取消挂起的切换
 *   
 *   3. 挂起状态
 *      - pending_switch_path: 待切换目标路径（-1 表示无挂起）
 *      - debounce_timer_ms: 稳定窗口开始时间
 * 
 * 
 * 六、TURN 兜底策略
 * ------------------
 * 
 *   配置：
 *     turn_config.enabled = true          - TURN 功能开关
 *     turn_config.use_as_last_resort = true  - 仅作为最终备份
 *     turn_config.cost_multiplier = 5     - 成本倍数（相对 RELAY）
 *   
 *   决策逻辑（should_use_turn）：
 *     - 如果 use_as_last_resort = false：总是可用
 *     - 如果 use_as_last_resort = true：
 *       → 检查是否存在其他可用路径（包括 DEGRADED/RECOVERING）
 *       → 仅当所有其他路径都失效时才使用 TURN
 * 
 * 
 * ============================================================================
 * 【数据结构】
 * ============================================================================
 * 
 * path_info_t - 单条路径信息
 *   - type: 路径类型（LAN/PUNCH/RELAY/TURN）
 *   - state: 当前状态（6种状态）
 *   - addr: 对端网络地址
 *   - rtt_ms: 当前平滑 RTT
 *   - rtt_srtt/rtt_rttvar: EWMA 算法中间值
 *   - loss_rate: 丢包率（0.0-1.0）
 *   - consecutive_timeouts: 连续超时计数
 *   - quality_score: 综合质量评分（0.0-1.0）
 *   - total_packets_sent/recv/lost: 统计计数器
 *   - last_send_ms/last_recv_ms: 最后活动时间
 *   - cost_score: 成本评分（越低越好）
 * 
 * path_manager_t - 路径管理器
 *   - strategy: 选择策略
 *   - paths[4]: 路径数组
 *   - path_count: 当前路径数量
 *   - active_path: 当前活跃路径索引
 *   - backup_path: 备用路径索引
 *   - pending_packets[32]: 待确认包跟踪缓冲区
 *   - switch_history[16]: 路径切换历史记录
 *   - turn_config: TURN 相关配置
 * 
 * 
 * ============================================================================
 * 【API 使用流程】
 * ============================================================================
 * 
 * 1. 初始化
 *    path_manager_init(&pm, P2P_PATH_STRATEGY_HYBRID);
 * 
 * 2. 添加路径
 *    int lan_idx = path_manager_add_or_update_path(&pm, P2P_PATH_LAN, &addr);
 *    path_manager_set_path_state(&pm, lan_idx, PATH_STATE_ACTIVE);
 * 
 * 3. 选择最佳路径
 *    int best = path_manager_select_best_path(&pm);
 *    path_info_t *path = &pm.paths[best];
 * 
 * 4. 发送数据包（记录）
 *    path_manager_on_packet_send(&pm, path_idx, seq, now_ms);
 * 
 * 5. 收到 ACK（更新 RTT）
 *    int rtt = path_manager_on_packet_ack(&pm, seq, now_ms);
 * 
 * 6. 收到数据包（更新接收时间）
 *    path_manager_on_packet_recv(&pm, path_idx, now_ms);
 * 
 * 7. 检测丢包
 *    path_manager_on_packet_loss(&pm, seq);
 * 
 * 8. 健康检查（周期调用）
 *    path_manager_health_check(&pm, now_ms);
 * 
 * 9. 故障转移（自动或手动触发）
 *    int new_path = path_manager_failover(&pm, failed_path_idx);
 * 
 * 
 * ============================================================================
 * 【重要注意事项】
 * ============================================================================
 * 
 * 1. 计数器更新规则
 *    - total_packets_sent: 只在 on_packet_send 递增
 *    - total_packets_lost: 只在 on_packet_loss 递增
 *    - consecutive_timeouts: 在 on_packet_loss 递增，on_packet_recv 清零
 *    - update_metrics(): 只计算指标（loss_rate, RTT），不修改计数器
 * 
 * 2. 时间戳初始化
 *    - 新路径设置为 ACTIVE 时必须初始化 last_recv_ms = now_ms
 *    - 否则会因为 last_recv_ms=0 立即触发超时检测
 * 
 * 3. RECOVERING 状态特殊处理
 *    - probe_seq 字段被复用存储 recovery_start_time
 *    - last_recv_ms 在进入 RECOVERING 时初始化为当前时间
 *    - 判断恢复成功：last_recv_ms > recovery_start_time（确保收到新数据）
 * 
 * 4. 路径索引管理
 *    - remove_path 会移动数组元素，需要同步调整 active_path/backup_path
 *    - 选择路径后，应使用 paths[best_idx] 而非 get_active_path()
 * 
 * 5. pending_packets 缓冲区
 *    - 最多 32 个待确认包
 *    - 满时采用 FIFO 策略淘汰最老记录
 *    - ACK/Loss 后及时移除记录避免内存泄漏
 * 
 * 6. 外部 RTT 同步
 *    - 支持从 reliable 层同步 RTT（p2p.c 中使用）
 *    - 调用 update_metrics(rtt, true) 只更新 RTT，不影响计数器
 * 
 * 
 * ============================================================================
 * 【版本历史】
 * ============================================================================
 * 
 * 修复的关键 Bug：
 *   - Bug #1-7:  初始化、重复路径、超时计数、状态恢复、阈值配置
 *   - Bug #8-10: RECOVERING 超时、last_recv_ms 初始化、冗余更新
 *   - Bug #11:   total_packets_sent 双重计数（loss_rate 被低估）
 *   - Bug #12:   consecutive_timeouts 双重计数（过快失效）
 *   - Bug #13:   路径选择使用旧索引（切换时用错地址）
 * 
 * ============================================================================
 */

#define MOD_TAG "PATH_MGR"

#include "p2p_path_manager.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* 默认参数 */
#define DEFAULT_SWITCH_COOLDOWN_MS      5000    /* 5秒切换冷却时间 */
#define DEFAULT_SWITCH_RTT_THRESHOLD    50      /* RTT 改善50ms触发切换 */
#define DEFAULT_SWITCH_LOSS_THRESHOLD   0.05f   /* 丢包率5%触发切换 */
#define DEFAULT_PROBE_INTERVAL_MS       1000    /* 1秒探测间隔 */
#define DEFAULT_HEALTH_CHECK_INTERVAL   500     /* 500ms 健康检查间隔 */

/* RTT 平滑参数（TCP 算法） */
#define ALPHA   0.125f  /* SRTT 权重：1/8 */
#define BETA    0.25f   /* RTTVAR 权重：1/4 */

/* ============================================================================
 * 辅助函数
 * ============================================================================ */

const char* path_type_str(int type) {
    switch (type) {
        case P2P_PATH_NONE:  return "NONE";
        case P2P_PATH_LAN:   return "LAN";
        case P2P_PATH_PUNCH: return "PUNCH";
        case P2P_PATH_RELAY: return "RELAY";
        case P2P_PATH_TURN:  return "TURN";
        default:             return "UNKNOWN";
    }
}

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

/* ============================================================================
 * 基础 API 实现
 * ============================================================================ */

void path_manager_init(path_manager_t *pm, p2p_path_strategy_t strategy) {
    memset(pm, 0, sizeof(*pm));
    
    pm->strategy = strategy;
    pm->active_path = -1;
    pm->backup_path = -1;
    
    /* 设置默认参数 */
    pm->path_switch_cooldown_ms = DEFAULT_SWITCH_COOLDOWN_MS;
    pm->switch_rtt_threshold_ms = DEFAULT_SWITCH_RTT_THRESHOLD;
    pm->switch_loss_threshold = DEFAULT_SWITCH_LOSS_THRESHOLD;
    pm->probe_interval_ms = DEFAULT_PROBE_INTERVAL_MS;
    pm->health_check_interval_ms = DEFAULT_HEALTH_CHECK_INTERVAL;
    
    /* Phase 4: 初始化动态阈值（根据路径类型） */
    for (int i = 0; i < P2P_MAX_PATHS; i++) {
        pm->thresholds[i].rtt_threshold_ms = 50;        // 默认 50ms
        pm->thresholds[i].loss_threshold = 0.05f;       // 默认 5%
        pm->thresholds[i].cooldown_ms = 5000;           // 默认 5s
        pm->thresholds[i].stability_window_ms = 2000;   // 默认 2s 稳定窗口
    }
    pm->pending_switch_path = -1;
    
    /* Phase 5: 初始化 TURN 配置 */
    pm->turn_config.enabled = true;
    pm->turn_config.cost_multiplier = 5;                // TURN 成本 = 5x RELAY
    pm->turn_config.max_bandwidth_bps = 0;              // 无限制
    pm->turn_config.use_as_last_resort = true;          // 仅作为最终备份
    
    /* 初始化路径成本 */
    // LAN 和 PUNCH 免费，RELAY/TURN 有成本
}

int path_manager_find_path(path_manager_t *pm, int type) {
    for (int i = 0; i < pm->path_count; i++) {
        if (pm->paths[i].type == type) {
            return i;
        }
    }
    return -1;
}

int path_manager_add_or_update_path(path_manager_t *pm, int type, struct sockaddr_in *addr) {
    if (!pm || !addr) return -1;
    
    /* 查找是否已存在 */
    int idx = path_manager_find_path(pm, type);
    
    if (idx >= 0) {
        /* 更新现有路径 */
        pm->paths[idx].addr = *addr;
        return idx;
    }
    
    /* 添加新路径 */
    if (pm->path_count >= P2P_MAX_PATHS) {
        return -1; /* 路径数量已满 */
    }
    
    idx = pm->path_count++;
    path_info_t *p = &pm->paths[idx];
    
    memset(p, 0, sizeof(*p));
    p->type = type;
    p->state = PATH_STATE_INIT;
    p->addr = *addr;
    
    /* 设置成本分数 */
    switch (type) {
        case P2P_PATH_LAN:   p->cost_score = 0; break; /* 免费 */
        case P2P_PATH_PUNCH: p->cost_score = 0; break; /* 免费 */
        case P2P_PATH_RELAY: p->cost_score = 5; break; /* 中等成本 */
        case P2P_PATH_TURN:  p->cost_score = 8; break; /* 较高成本 */
    }
    
    /* 初始化 RTT - 使用合理的默认值避免新路径被忽略 */
    p->rtt_ms = 100;      /* 假设 100ms 作为初始估计 */
    p->rtt_min = 9999;    /* 保持 9999 等待首次测量 */
    p->rtt_max = 0;
    p->rtt_srtt = 100;    /* EWMA 也使用 100ms */
    p->rtt_rttvar = 50;   /* 方差 50ms */
    
    /* 初始化质量评估 */
    p->quality = PATH_QUALITY_FAIR;  /* 初始假设为一般质量 */
    p->quality_score = 0.5f;         /* 初始评分 0.5 */
    p->quality_trend = 0.0f;
    p->stability_score = 0;
    
    return idx;
}

int path_manager_set_path_state(path_manager_t *pm, int path_idx, path_state_t state) {
    if (!pm || path_idx < 0 || path_idx >= pm->path_count) return -1;
    
    path_info_t *p = &pm->paths[path_idx];
    
    /* 如果设置为 ACTIVE 且 last_recv_ms 未初始化，初始化为当前时间避免永不超时 */
    if (state == PATH_STATE_ACTIVE && p->last_recv_ms == 0) {
        p->last_recv_ms = P_tick_ms();
    }
    
    pm->paths[path_idx].state = state;
    return 0;
}

int path_manager_update_metrics(path_manager_t *pm, int path_idx, uint32_t rtt_ms, bool success) {
    if (!pm || path_idx < 0 || path_idx >= pm->path_count) return -1;
    
    path_info_t *p = &pm->paths[path_idx];
    
    /* 更新 RTT（使用 TCP 平滑算法） */
    if (success) {
        if (p->rtt_srtt == 0) {
            /* 首次测量 */
            p->rtt_srtt = rtt_ms;
            p->rtt_rttvar = rtt_ms / 2;
        } else {
            /* EWMA 平滑 */
            int32_t err = rtt_ms - p->rtt_srtt;
            p->rtt_srtt += (uint32_t)(ALPHA * err);
            p->rtt_rttvar += (uint32_t)(BETA * (abs(err) - p->rtt_rttvar));
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

path_info_t* path_manager_get_active_path(path_manager_t *pm) {
    if (!pm || pm->active_path < 0 || pm->active_path >= pm->path_count) {
        return NULL;
    }
    return &pm->paths[pm->active_path];
}

bool path_manager_has_active_path(path_manager_t *pm) {
    if (!pm) return false;
    
    for (int i = 0; i < pm->path_count; i++) {
        if (pm->paths[i].state == PATH_STATE_ACTIVE) {
            return true;
        }
    }
    return false;
}

int path_manager_remove_path(path_manager_t *pm, int type) {
    if (!pm) return -1;
    
    int idx = path_manager_find_path(pm, type);
    if (idx < 0) return -1;
    
    /* 移除路径（通过移动数组） */
    for (int i = idx; i < pm->path_count - 1; i++) {
        pm->paths[i] = pm->paths[i + 1];
    }
    pm->path_count--;
    
    /* 调整活跃路径索引 */
    if (pm->active_path == idx) {
        pm->active_path = -1; /* 活跃路径被移除，需要重新选择 */
    } else if (pm->active_path > idx) {
        pm->active_path--;
    }
    
    if (pm->backup_path == idx) {
        pm->backup_path = -1;
    } else if (pm->backup_path > idx) {
        pm->backup_path--;
    }
    
    return 0;
}

/* ============================================================================
 * 路径选择策略实现（核心逻辑）
 * ============================================================================ */

/* 策略 1: CONNECTION_FIRST（P2P 直连优先） */
static int select_path_connection_first(path_manager_t *pm) {
    /* 优先级：LAN > PUNCH > RELAY > TURN（如果允许） */
    
    static const int priority_order[] = {
        P2P_PATH_LAN, P2P_PATH_PUNCH, P2P_PATH_RELAY, P2P_PATH_TURN
    };
    
    for (int i = 0; i < 4; i++) {
        int path_type = priority_order[i];
        
        /* Phase 5: TURN 仅作为最终备份检查 */
        if (path_type == P2P_PATH_TURN && 
            pm->turn_config.use_as_last_resort) {
            continue;  /* 跳过 TURN，先尝试其他路径 */
        }
        
        for (int j = 0; j < pm->path_count; j++) {
            if (pm->paths[j].type == path_type &&
                (pm->paths[j].state == PATH_STATE_ACTIVE ||
                 pm->paths[j].state == PATH_STATE_RECOVERING)) {  /* 允许恢复中的路径 */
                return j;
            }
        }
    }
    
    /* Phase 5: 如果没有其他可用路径，尝试 TURN */
    if (path_manager_should_use_turn(pm)) {
        for (int j = 0; j < pm->path_count; j++) {
            if (pm->paths[j].type == P2P_PATH_TURN &&
                (pm->paths[j].state == PATH_STATE_ACTIVE ||
                 pm->paths[j].state == PATH_STATE_RECOVERING)) {  /* 允许恢复中的路径 */
                return j;
            }
        }
    }
    
    return -1;
}

/* 策略 2: PERFORMANCE_FIRST（传输效率优先） */
static int select_path_performance_first(path_manager_t *pm) {
    int best_path = -1;
    float best_score = -1.0f;
    
    for (int i = 0; i < pm->path_count; i++) {
        if (pm->paths[i].state != PATH_STATE_ACTIVE &&
            pm->paths[i].state != PATH_STATE_RECOVERING) continue;  /* 允许恢复中的路径 */
        
        /* Phase 5: TURN 仅作为最终备份检查 */
        if (pm->paths[i].type == P2P_PATH_TURN &&
            pm->turn_config.use_as_last_resort &&
            !path_manager_should_use_turn(pm)) {
            continue;  /* TURN 仅作为最终备份，跳过 */
        }
        
        path_info_t *p = &pm->paths[i];
        
        /* 归一化指标（0-1，越高越好） */
        float rtt_score = 1.0f - fminf(p->rtt_ms / 500.0f, 1.0f);
        float loss_score = 1.0f - p->loss_rate;
        float jitter_score = 1.0f - fminf(p->rtt_variance / 100.0f, 1.0f);
        
        /* 加权计算综合得分 */
        float score = 0.5f * rtt_score +      /* RTT 权重50% */
                      0.3f * loss_score +      /* 丢包率30% */
                      0.2f * jitter_score;     /* 抖动20% */
        
        /* Phase 5: 成本惩罚（TURN 路径降低得分） */
        if (p->type == P2P_PATH_TURN || p->type == P2P_PATH_RELAY) {
            score *= 0.8f;  /* 中继路径得分打八折 */
        }
        
        if (score > best_score) {
            best_score = score;
            best_path = i;
        }
    }
    
    return best_path;
}

/* 策略 3: HYBRID（混合模式） */
static int select_path_hybrid(path_manager_t *pm) {
    int direct_path = -1;   /* 最佳直连路径 */
    int relay_path = -1;    /* 最佳中继路径 */
    int turn_path = -1;     /* TURN 路径 */
    
    /* 找出最佳直连、最佳中继和 TURN */
    for (int i = 0; i < pm->path_count; i++) {
        if (pm->paths[i].state != PATH_STATE_ACTIVE &&
            pm->paths[i].state != PATH_STATE_RECOVERING) continue;  /* 允许恢复中的路径 */
        
        if (pm->paths[i].type == P2P_PATH_LAN || pm->paths[i].type == P2P_PATH_PUNCH) {
            if (direct_path == -1 || pm->paths[i].rtt_ms < pm->paths[direct_path].rtt_ms) {
                direct_path = i;
            }
        }
        
        if (pm->paths[i].type == P2P_PATH_RELAY) {
            if (relay_path == -1 || pm->paths[i].rtt_ms < pm->paths[relay_path].rtt_ms) {
                relay_path = i;
            }
        }
        
        /* Phase 5: 记录 TURN 路径 */
        if (pm->paths[i].type == P2P_PATH_TURN) {
            turn_path = i;
        }
    }
    
    /* 决策逻辑 */
    if (direct_path != -1) {
        path_info_t *dp = &pm->paths[direct_path];
        
        /* 直连性能良好：使用直连 */
        if (dp->rtt_ms < 100 && dp->loss_rate < 0.01f) {
            return direct_path;
        }
        
        /* 没有中继：仍使用直连 */
        if (relay_path == -1) {
            return direct_path;
        }
        
        /* 比较性能差距 */
        path_info_t *rp = &pm->paths[relay_path];
        
        /* 中继性能显著更好：使用中继（使用可配置阈值）*/
        if (rp->rtt_ms + pm->switch_rtt_threshold_ms < dp->rtt_ms || 
            dp->loss_rate > pm->switch_loss_threshold) {
            return relay_path;
        }
        
        /* 性能差距不大：优先直连（节省成本） */
        return direct_path;
    }
    
    /* 无直连路径：使用中继 */
    if (relay_path != -1) {
        return relay_path;
    }
    
    /* Phase 5: 无直连和中继，尝试 TURN */
    if (turn_path >= 0 && path_manager_should_use_turn(pm)) {
        return turn_path;
    }
    
    return -1;
}

int path_manager_select_best_path(path_manager_t *pm) {
    if (!pm || pm->path_count == 0) return -1;
    
    switch (pm->strategy) {
        case P2P_PATH_STRATEGY_CONNECTION_FIRST:
            return select_path_connection_first(pm);
        
        case P2P_PATH_STRATEGY_PERFORMANCE_FIRST:
            return select_path_performance_first(pm);
        
        case P2P_PATH_STRATEGY_HYBRID:
            return select_path_hybrid(pm);
        
        default:
            return select_path_connection_first(pm);
    }
}

/* ============================================================================
 * 健康检查与故障转移
 * ============================================================================ */

void path_manager_health_check(path_manager_t *pm, uint64_t now_ms) {
    if (!pm) return;
    
    /* 节流：避免过于频繁的健康检查 */
    if (now_ms - pm->last_health_check_ms < pm->health_check_interval_ms) {
        return;
    }
    pm->last_health_check_ms = now_ms;
    
    for (int i = 0; i < pm->path_count; i++) {
        path_info_t *p = &pm->paths[i];
        
        /* 超时检查 */
        if (p->state == PATH_STATE_ACTIVE) {
            uint64_t timeout = (p->type == P2P_PATH_LAN) ? 5000 : 10000;
            
            if (p->last_recv_ms > 0 && now_ms - p->last_recv_ms > timeout) {
                p->consecutive_timeouts++;  /* 增加超时计数 */
                
                if (p->consecutive_timeouts >= 3) {
                    p->state = PATH_STATE_FAILED;
                    
                    /* 触发故障转移 */
                    if (pm->active_path == i) {
                        path_manager_failover(pm, i);
                    }
                }
            } else {
                /* 收到数据则重置超时计数 */
                p->consecutive_timeouts = 0;
            }
        }
        
        /* 性能退化检查与恢复 */
        if (p->state == PATH_STATE_ACTIVE) {
            if (p->rtt_ms > 300 || p->loss_rate > 0.1f) {
                p->state = PATH_STATE_DEGRADED;
            }
        } else if (p->state == PATH_STATE_DEGRADED) {
            /* 性能恢复：RTT < 250ms 且丢包 < 5% */
            if (p->rtt_ms < 250 && p->loss_rate < 0.05f) {
                p->state = PATH_STATE_ACTIVE;
            }
        }
        
        /* 探测恢复（失效路径定期探测）*/
        if (p->state == PATH_STATE_FAILED) {
            if (now_ms - p->last_send_ms > 30000) { /* 每30秒探测一次 */
                p->state = PATH_STATE_RECOVERING;
                p->consecutive_timeouts = 0;  /* 重置计数 */
                /* 复用 probe_seq 字段记录恢复开始时间 */
                p->probe_seq = now_ms;
                /* 初始化 last_recv_ms 为当前时间，避免使用旧值 */
                p->last_recv_ms = now_ms;
            }
        }
        
        /* RECOVERING 状态超时转回 FAILED 或恢夏成功转 ACTIVE */
        if (p->state == PATH_STATE_RECOVERING) {
            uint64_t recovering_start = p->probe_seq;  /* probe_seq 存储恢复开始时间 */
            uint64_t elapsed = now_ms - recovering_start;
            
            if (elapsed > 10000) {
                /* 10秒内未恢复，回到 FAILED */
                p->state = PATH_STATE_FAILED;
            } else if (p->last_recv_ms > recovering_start && 
                       now_ms - p->last_recv_ms < 2000) {
                /* 收到数据且最近 2秒内有活动，恢复成功 */
                p->state = PATH_STATE_ACTIVE;
            }
        }
    }
}

int path_manager_failover(path_manager_t *pm, int failed_path) {
    if (!pm || failed_path < 0 || failed_path >= pm->path_count) return -1;
    
    /* 标记失效路径 */
    pm->paths[failed_path].state = PATH_STATE_FAILED;
    
    /* 选择新路径 */
    int new_path = path_manager_select_best_path(pm);
    
    if (new_path >= 0 && new_path != failed_path) {
        pm->active_path = new_path;
        pm->total_path_switches++;
        return new_path;
    }
    
    return -1; /* 无可用备用路径 */
}

/* ============================================================================
 * Phase 2: RTT 测量与丢包统计实现
 * ============================================================================ */

int path_manager_on_packet_send(path_manager_t *pm, int path_idx, uint32_t seq, uint64_t now_ms) {
    if (!pm || path_idx < 0 || path_idx >= pm->path_count) return -1;
    
    path_info_t *p = &pm->paths[path_idx];
    
    /* 更新发送统计 */
    p->last_send_ms = now_ms;
    p->total_packets_sent++;
    
    /* 如果跟踪缓冲区已满，移除最老的记录 */
    if (pm->pending_count >= MAX_PENDING_PACKETS) {
        // 移除最老的记录（FIFO）
        for (int i = 0; i < pm->pending_count - 1; i++) {
            pm->pending_packets[i] = pm->pending_packets[i + 1];
        }
        pm->pending_count--;
    }
    
    /* 添加新的跟踪记录 */
    packet_track_t *track = &pm->pending_packets[pm->pending_count++];
    track->seq = seq;
    track->sent_time_ms = now_ms;
    track->path_idx = path_idx;
    
    return 0;
}

int path_manager_on_packet_ack(path_manager_t *pm, uint32_t seq, uint64_t now_ms) {
    if (!pm) return -1;
    
    /* 查找对应的发送记录 */
    for (int i = 0; i < pm->pending_count; i++) {
        if (pm->pending_packets[i].seq == seq) {
            packet_track_t *track = &pm->pending_packets[i];
            int path_idx = track->path_idx;
            
            if (path_idx < 0 || path_idx >= pm->path_count) {
                return -1;
            }
            
            path_info_t *p = &pm->paths[path_idx];
            
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
            path_manager_update_metrics(pm, path_idx, rtt, true);
            
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

int path_manager_on_packet_recv(path_manager_t *pm, int path_idx, uint64_t now_ms) {
    if (!pm || path_idx < 0 || path_idx >= pm->path_count) return -1;
    
    path_info_t *p = &pm->paths[path_idx];
    p->last_recv_ms = now_ms;
    p->total_packets_recv++;
    
    /* 重置连续超时计数 */
    p->consecutive_timeouts = 0;
    
    return 0;
}

int path_manager_on_packet_loss(path_manager_t *pm, uint32_t seq) {
    if (!pm) return -1;
    
    /* 查找对应的发送记录 */
    for (int i = 0; i < pm->pending_count; i++) {
        if (pm->pending_packets[i].seq == seq) {
            packet_track_t *track = &pm->pending_packets[i];
            int path_idx = track->path_idx;
            
            if (path_idx < 0 || path_idx >= pm->path_count) {
                return -1;
            }
            
            path_info_t *p = &pm->paths[path_idx];
            
            /* 更新丢包统计 */
            p->total_packets_lost++;
            p->consecutive_timeouts++;
            
            /* 使用 path_manager_update_metrics 更新丢包率 */
            path_manager_update_metrics(pm, path_idx, 0, false);
            
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

/* ============================================================================
 * Phase 3: 路径质量预测实现
 * ============================================================================ */

int path_manager_update_quality(path_manager_t *pm, int path_idx) {
    if (!pm || path_idx < 0 || path_idx >= pm->path_count) return -1;
    
    path_info_t *p = &pm->paths[path_idx];
    
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
    
    /* 计算质量趋势（基于 RTT 样本） */
    if (p->rtt_sample_count >= 5) {
        /* 计算前半部分和后半部分的平均 RTT */
        int mid = p->rtt_sample_count / 2;
        uint32_t first_half_sum = 0, second_half_sum = 0;
        
        for (int i = 0; i < mid; i++) {
            first_half_sum += p->rtt_samples[i];
        }
        for (int i = mid; i < p->rtt_sample_count; i++) {
            second_half_sum += p->rtt_samples[i];
        }
        
        float first_half_avg = (float)first_half_sum / mid;
        float second_half_avg = (float)second_half_sum / (p->rtt_sample_count - mid);
        
        /* 计算趋势：负值表示改善，正值表示恶化 */
        float rtt_change = second_half_avg - first_half_avg;
        
        /* 归一化到 -1.0 到 1.0 */
        if (first_half_avg > 0) {
            p->quality_trend = -fminf(fmaxf(rtt_change / first_half_avg, -1.0f), 1.0f);
        } else {
            p->quality_trend = 0.0f;
        }
    }
    
    /* 计算稳定性评分（基于 RTT 方差） */
    if (p->rtt_sample_count >= 3) {
        /* 计算标准差 */
        uint32_t sum = 0;
        for (int i = 0; i < p->rtt_sample_count; i++) {
            sum += p->rtt_samples[i];
        }
        float mean = (float)sum / p->rtt_sample_count;
        
        float variance_sum = 0;
        for (int i = 0; i < p->rtt_sample_count; i++) {
            float diff = p->rtt_samples[i] - mean;
            variance_sum += diff * diff;
        }
        float std_dev = sqrtf(variance_sum / p->rtt_sample_count);
        
        /* 稳定性评分：标准差越小越稳定 */
        float cv = (mean > 0) ? (std_dev / mean) : 1.0f; // 变异系数
        p->stability_score = (int)(100.0f * fmaxf(0.0f, 1.0f - fminf(cv, 1.0f)));
    }
    
    return 0;
}

path_quality_t path_manager_get_quality(path_manager_t *pm, int path_idx) {
    if (!pm || path_idx < 0 || path_idx >= pm->path_count) {
        return PATH_QUALITY_BAD;
    }
    
    /* 确保质量是最新的 */
    path_manager_update_quality(pm, path_idx);
    
    return pm->paths[path_idx].quality;
}

float path_manager_get_quality_score(path_manager_t *pm, int path_idx) {
    if (!pm || path_idx < 0 || path_idx >= pm->path_count) {
        return -1.0f;
    }
    
    /* 确保质量是最新的 */
    path_manager_update_quality(pm, path_idx);
    
    return pm->paths[path_idx].quality_score;
}

float path_manager_predict_trend(path_manager_t *pm, int path_idx) {
    if (!pm || path_idx < 0 || path_idx >= pm->path_count) {
        return NAN;
    }
    
    /* 确保质量是最新的 */
    path_manager_update_quality(pm, path_idx);
    
    return pm->paths[path_idx].quality_trend;
}

/* ============================================================================
 * Phase 4: 动态阈值与防抖动实现
 * ============================================================================ */

int path_manager_set_threshold(path_manager_t *pm, int path_type,
                                uint32_t rtt_ms, float loss_rate,
                                uint64_t cooldown_ms, uint32_t stability_ms) {
    if (!pm) return -1;
    
    /* 找到对应路径类型的索引 */
    int idx = path_manager_find_path(pm, path_type);
    if (idx < 0) {
        /* 如果路径还不存在，设置默认阈值 */
        idx = 0; /* 使用第一个槽位存储默认配置 */
    }
    
    if (idx >= P2P_MAX_PATHS) return -1;
    
    pm->thresholds[idx].rtt_threshold_ms = rtt_ms;
    pm->thresholds[idx].loss_threshold = loss_rate;
    pm->thresholds[idx].cooldown_ms = cooldown_ms;
    pm->thresholds[idx].stability_window_ms = stability_ms;
    
    return 0;
}

int path_manager_get_switch_history(path_manager_t *pm, 
                                     path_switch_record_t *records,
                                     int max_count) {
    if (!pm || !records || max_count <= 0) return 0;
    
    int count = pm->switch_history_count < max_count ? 
                pm->switch_history_count : max_count;
    
    /* 按时间倒序返回（最新的在前面） */
    for (int i = 0; i < count; i++) {
        int idx = (pm->switch_history_idx - 1 - i + MAX_SWITCH_HISTORY) % MAX_SWITCH_HISTORY;
        records[i] = pm->switch_history[idx];
    }
    
    return count;
}

int path_manager_analyze_switch_frequency(path_manager_t *pm, uint64_t window_ms) {
    if (!pm || window_ms == 0) return 0;
    
    if (pm->switch_history_count == 0) return 0;
    
    /* 获取当前时间（使用最新记录的时间戳） */
    int latest_idx = (pm->switch_history_idx - 1 + MAX_SWITCH_HISTORY) % MAX_SWITCH_HISTORY;
    uint64_t now = pm->switch_history[latest_idx].timestamp_ms;
    uint64_t cutoff_time = now - window_ms;
    
    int count = 0;
    for (int i = 0; i < pm->switch_history_count; i++) {
        int idx = (pm->switch_history_idx - 1 - i + MAX_SWITCH_HISTORY) % MAX_SWITCH_HISTORY;
        if (pm->switch_history[idx].timestamp_ms >= cutoff_time) {
            count++;
        } else {
            break; /* 记录是按时间排序的 */
        }
    }
    
    return count;
}

bool path_manager_should_debounce_switch(path_manager_t *pm, 
                                          int target_path,
                                          uint64_t now_ms) {
    if (!pm || target_path < 0 || target_path >= pm->path_count) {
        return false;
    }
    
    /* 检查是否在冷却时间内 */
    if (now_ms - pm->last_switch_time < pm->path_switch_cooldown_ms) {
        return true;  /* 冷却期，防抖 */
    }
    
    /* 检查最近是否频繁切换（抖动检测） */
    int recent_switches = path_manager_analyze_switch_frequency(pm, 30000); /* 最近30秒 */
    if (recent_switches >= 5) {
        return true;  /* 频繁切换，防抖 */
    }
    
    /* 检查目标路径的稳定性窗口 */

    uint32_t stability_window = pm->thresholds[target_path].stability_window_ms;
    
    if (stability_window > 0) {
        /* 需要目标路径在稳定窗口内持续表现良好 */
        if (pm->pending_switch_path == target_path) {
            /* 已经在等待稳定 */
            if (now_ms - pm->debounce_timer_ms >= stability_window) {
                return false;  /* 稳定期已过，可以切换 */
            }
            return true;  /* 仍在稳定期 */
        } else {
            /* 首次检测到需要切换，启动稳定窗口 */
            pm->pending_switch_path = target_path;
            pm->debounce_timer_ms = now_ms;
            return true;  /* 开始防抖 */
        }
    }
    
    return false;  /* 无需防抖 */
}

int path_manager_switch_path(path_manager_t *pm, 
                              int target_path,
                              const char *reason,
                              uint64_t now_ms) {
    if (!pm || target_path < 0 || target_path >= pm->path_count) {
        return -1;
    }
    
    /* 检查是否需要防抖 */
    if (path_manager_should_debounce_switch(pm, target_path, now_ms)) {
        return 1;  /* 防抖延迟，稍后重试 */
    }
    
    int old_path = pm->active_path;
    
    /* 记录切换历史 */
    path_switch_record_t *record = &pm->switch_history[pm->switch_history_idx];
    record->timestamp_ms = now_ms;
    record->from_path = old_path;
    record->to_path = target_path;
    record->from_type = (old_path >= 0) ? pm->paths[old_path].type : P2P_PATH_NONE;
    record->to_type = pm->paths[target_path].type;
    record->from_rtt_ms = (old_path >= 0) ? pm->paths[old_path].rtt_ms : 0;
    record->to_rtt_ms = pm->paths[target_path].rtt_ms;
    record->from_loss_rate = (old_path >= 0) ? pm->paths[old_path].loss_rate : 0.0f;
    record->to_loss_rate = pm->paths[target_path].loss_rate;
    record->reason = reason;
    
    pm->switch_history_idx = (pm->switch_history_idx + 1) % MAX_SWITCH_HISTORY;
    if (pm->switch_history_count < MAX_SWITCH_HISTORY) {
        pm->switch_history_count++;
    }
    
    /* 执行切换 */
    pm->active_path = target_path;
    pm->last_switch_time = now_ms;
    pm->total_path_switches++;
    pm->pending_switch_path = -1;  /* 清除待切换状态 */
    
    return 0;  /* 成功切换 */
}

/* ============================================================================
 * Phase 5: TURN 路径支持实现
 * ============================================================================ */

int path_manager_configure_turn(path_manager_t *pm,
                                 bool enabled,
                                 int cost_multiplier,
                                 uint32_t max_bandwidth_bps,
                                 bool last_resort_only) {
    if (!pm) return -1;
    
    pm->turn_config.enabled = enabled;
    pm->turn_config.cost_multiplier = cost_multiplier;
    pm->turn_config.max_bandwidth_bps = max_bandwidth_bps;
    pm->turn_config.use_as_last_resort = last_resort_only;
    
    /* 更新已存在的 TURN 路径成本 */
    int turn_idx = path_manager_find_path(pm, P2P_PATH_TURN);
    if (turn_idx >= 0) {
        pm->paths[turn_idx].cost_score = cost_multiplier;
    }
    
    return 0;
}

int path_manager_add_turn_path(path_manager_t *pm, struct sockaddr_in *addr) {
    if (!pm || !addr) return -1;
    
    if (!pm->turn_config.enabled) {
        return -1;  /* TURN 未启用 */
    }
    
    /* 使用现有的添加路径函数 */
    int idx = path_manager_add_or_update_path(pm, P2P_PATH_TURN, addr);
    
    if (idx >= 0) {
        /* 应用 TURN 配置 */
        pm->paths[idx].cost_score = pm->turn_config.cost_multiplier;
        
        /* 如果是仅作为最终备份，初始状态设为 INIT */
        if (pm->turn_config.use_as_last_resort) {
            pm->paths[idx].state = PATH_STATE_INIT;
        }
    }
    
    return idx;
}

bool path_manager_should_use_turn(path_manager_t *pm) {
    if (!pm || !pm->turn_config.enabled) {
        return false;
    }
    
    /* 如果不是仅作为最终备份，总是可以使用 */
    if (!pm->turn_config.use_as_last_resort) {
        return true;
    }
    
    /* 检查是否有其他可用路径（包括 DEGRADED 性能较差但仍可用的路径）*/
    for (int i = 0; i < pm->path_count; i++) {
        if (pm->paths[i].type != P2P_PATH_TURN &&
            (pm->paths[i].state == PATH_STATE_ACTIVE ||
             pm->paths[i].state == PATH_STATE_DEGRADED ||
             pm->paths[i].state == PATH_STATE_RECOVERING)) {
            return false;  /* 有其他可用路径 */
        }
    }
    
    /* 所有其他路径都失效，需要使用 TURN */
    return true;
}

int path_manager_get_turn_stats(path_manager_t *pm,
                                 uint64_t *total_bytes_sent,
                                 uint64_t *total_bytes_recv,
                                 uint32_t *avg_rtt_ms) {
    if (!pm) return -1;
    
    int turn_idx = path_manager_find_path(pm, P2P_PATH_TURN);
    if (turn_idx < 0) {
        return -1;  /* 无 TURN 路径 */
    }
    
    path_info_t *turn = &pm->paths[turn_idx];
    
    if (total_bytes_sent) *total_bytes_sent = turn->total_bytes_sent;
    if (total_bytes_recv) *total_bytes_recv = turn->total_bytes_recv;
    if (avg_rtt_ms) *avg_rtt_ms = turn->rtt_ms;
    
    return 0;
}
