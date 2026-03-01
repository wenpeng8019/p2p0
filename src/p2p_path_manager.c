/*
 * 多路径管理器实现
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
    
    /* 初始化 RTT */
    p->rtt_ms = 9999;
    p->rtt_min = 9999;
    p->rtt_max = 0;
    p->rtt_srtt = 0;
    p->rtt_rttvar = 0;
    
    /* 初始化质量评估 */
    p->quality = PATH_QUALITY_BAD;
    p->quality_score = 0.0f;
    p->quality_trend = 0.0f;
    p->stability_score = 0;
    
    return idx;
}

int path_manager_set_path_state(path_manager_t *pm, int path_idx, path_state_t state) {
    if (!pm || path_idx < 0 || path_idx >= pm->path_count) return -1;
    
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
    } else {
        /* 超时 */
        p->consecutive_timeouts++;
    }
    
    /* 更新丢包率（简单滑动窗口估算） */
    p->total_packets_sent++;
    if (!success) {
        p->total_packets_lost++;
    }
    
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
                pm->paths[j].state == PATH_STATE_ACTIVE) {
                return j;
            }
        }
    }
    
    /* Phase 5: 如果没有其他可用路径，尝试 TURN */
    if (path_manager_should_use_turn(pm)) {
        for (int j = 0; j < pm->path_count; j++) {
            if (pm->paths[j].type == P2P_PATH_TURN &&
                pm->paths[j].state == PATH_STATE_ACTIVE) {
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
        if (pm->paths[i].state != PATH_STATE_ACTIVE) continue;
        
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
        if (pm->paths[i].state != PATH_STATE_ACTIVE) continue;
        
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
        
        /* 中继性能显著更好：使用中继 */
        if (rp->rtt_ms + 50 < dp->rtt_ms || dp->loss_rate > 0.05f) {
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
                if (p->consecutive_timeouts >= 3) {
                    p->state = PATH_STATE_FAILED;
                    
                    /* 触发故障转移 */
                    if (pm->active_path == i) {
                        path_manager_failover(pm, i);
                    }
                }
            }
        }
        
        /* 性能退化检查 */
        if (p->state == PATH_STATE_ACTIVE) {
            if (p->rtt_ms > 300 || p->loss_rate > 0.1f) {
                p->state = PATH_STATE_DEGRADED;
            }
        }
        
        /* 探测恢复（失效路径定期探测） */
        if (p->state == PATH_STATE_FAILED) {
            if (now_ms - p->last_send_ms > 30000) { /* 每30秒探测一次 */
                p->state = PATH_STATE_RECOVERING;
                p->last_send_ms = now_ms;
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
    path_info_t *target = &pm->paths[target_path];
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
    
    /* 检查是否有其他可用路径 */
    for (int i = 0; i < pm->path_count; i++) {
        if (pm->paths[i].type != P2P_PATH_TURN &&
            pm->paths[i].state == PATH_STATE_ACTIVE) {
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
