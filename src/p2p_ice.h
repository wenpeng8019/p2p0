/*
 * ICE 协议实现（RFC 5245 / RFC 8445）
 *
 * ============================================================================
 * 概述
 * ============================================================================
 *
 * ICE (Interactive Connectivity Establishment) 是一种 NAT 穿透框架，
 * 用于在两个位于 NAT 后的端点之间建立直接通信路径。
 *
 * 本模块实现了 RFC 5245 的核心功能：
 *   - 候选地址收集（Gathering）
 *   - 连通性检查（Connectivity Check）
 *   - Trickle ICE 增量交换
 *   - 提名与路径选择
 *
 * ============================================================================
 * 相关 RFC
 * ============================================================================
 *
 * - RFC 5245: ICE 协议原始规范
 * - RFC 8445: ICE 协议更新版（取代 RFC 5245）
 * - RFC 8838: Trickle ICE（增量候选交换）
 * - RFC 5389: STUN 协议（用于收集 Server Reflexive 候选）
 * - RFC 5766: TURN 协议（用于收集 Relay 候选）
 *
 * ============================================================================
 * 与标准 ICE 的差异：离线候选缓存
 * ============================================================================
 *
 * 【标准 ICE 的假设】
 *
 *   RFC 5245 假设双方在信令交换时都在线：
 *     - Offer/Answer 通过 SIP/XMPP 等信令协议实时交换
 *     - 双方同时在线才能完成 ICE 协商
 *     - 如果一方离线，连接建立失败
 *
 * 【本实现的改进】
 *
 *   支持对端离线时的候选缓存，适用于异步 P2P 场景：
 *
 *   1. 即时发送 + 服务器缓存
 *      - 调用 p2p_ice_send_local_candidate() 发送候选
 *      - 信令服务器负责转发（对端在线）或缓存（对端离线）
 *
 *   2. p2p_signal_relay_send_connect() 返回值含义：
 *
 *      返回值 > 0:  对端在线，候选已成功转发
 *                   - 正常 ICE 流程，对端立即收到候选
 *
 *      返回值 = 0:  对端离线，候选已缓存在服务器
 *                   - 服务器存储候选，等待对端上线后推送
 *                   - 本端继续周期性重发（p2p_update 每 5 秒）
 *                   - 对端上线后收到完整候选列表
 *
 *      返回值 < 0:  发送失败（网络错误/服务器断开）
 *                   - 标记 cands_pending_send = true
 *                   - 下次 tick 时重试
 *
 *   3. 离线场景的时序：
 *
 *      Alice (在线)                Server                     Bob (离线)
 *        |                           |                           |
 *        |--- CONNECT(candidates) -->|                           |
 *        |<-- ret=0 (cached) --------|                           |
 *        |                           |                           |
 *        |    ... Bob 上线 ...                                   |
 *        |                           |                           |
 *        |                           |--- push candidates ------>|
 *        |                           |<-- Bob's candidates ------|
 *        |<-- forward Bob's cands ---|                           |
 *        |                           |                           |
 *        |<================ ICE 连通性检查 ==================>|
 *
 *   这种设计使得 P2P 连接可以在对端稍后上线时自动建立，
 *   无需双方同时在线，更适合移动端和弱网络环境。
 *
 * ============================================================================
 * 候选地址类型
 * ============================================================================
 *
 *   类型        | 描述                    | 优先级 | 来源
 *   ------------|------------------------|--------|------------------
 *   Host        | 本地网卡地址            | 最高   | getifaddrs()
 *   Srflx       | STUN 反射地址（公网IP） | 中     | STUN Binding
 *   Relay       | TURN 中继地址          | 最低   | TURN Allocate
 *   Prflx       | 对端反射地址           | 中     | 连通性检查发现
 *
 * ============================================================================
 * ICE 状态机
 * ============================================================================
 *
 *   IDLE ──→ GATHERING ──→ GATHERING_DONE ──→ CHECKING ──→ COMPLETED
 *                                                │              │
 *                                                └──→ FAILED ←──┘
 *
 *   状态说明：
 *   - IDLE:           初始状态
 *   - GATHERING:      正在收集本地候选地址
 *   - GATHERING_DONE: 候选收集完成
 *   - CHECKING:       正在进行连通性检查
 *   - COMPLETED:      至少一条路径可用
 *   - FAILED:         所有候选对检查失败
 *
 * ============================================================================
 * 连通性检查流程
 * ============================================================================
 *
 *      Peer A                          Peer B
 *        │                               │
 *        │ ──── STUN Request ──────────→ │  连通性检查
 *        │ ←─── STUN Response ────────── │
 *        │                               │
 *        │ ←─── STUN Request ─────────── │  反向检查
 *        │ ──── STUN Response ─────────→ │
 *        │                               │
 *       ICE Completed                   ICE Completed
 *
 * ============================================================================
 * 本实现的简化
 * ============================================================================
 *
 * 为简化实现，本模块省略了以下 RFC 5245 特性：
 *   - 完整的优先级计算公式（使用固定优先级）
 *   - 候选对排序（Checklist Ordering）
 *   - Frozen/Waiting/In-Progress 状态
 *   - ICE Lite 模式
 *   - IPv6 候选
 */

#ifndef P2P_ICE_H
#define P2P_ICE_H

#include <stdint.h>
#include "../stdc/stdc.h"   /* cross-platform utilities */
#include "p2pp.h"

/* ============================================================================
 * 候选地址类型（RFC 5245 Section 4.1.1）
 * ============================================================================ */
typedef enum {
    P2P_ICE_CAND_HOST = 0,              // 本地网卡地址（Host Candidate）
    P2P_ICE_CAND_SRFLX,                 // STUN 反射地址（Server Reflexive Candidate）
    P2P_ICE_CAND_RELAY,                 // TURN 中继地址（Relayed Candidate）
    P2P_ICE_CAND_PRFLX                  // 对端反射地址（Peer Reflexive Candidate）
} p2p_ice_cand_type_t;

typedef struct p2p_candidate_entry        p2p_candidate_entry_t;
typedef struct p2p_remote_candidate_entry p2p_remote_candidate_entry_t;

/* ============================================================================
 * ICE 状态机（RFC 5245 Section 7）
 * ============================================================================ */
typedef enum {
    P2P_ICE_STATE_INIT = 0,             // 初始状态
    P2P_ICE_STATE_GATHERING,            // 正在收集候选地址
    P2P_ICE_STATE_GATHERING_DONE,       // 候选收集完成
    P2P_ICE_STATE_CHECKING,             // 正在进行连通性检查
    P2P_ICE_STATE_COMPLETED,            // 连接建立成功
    P2P_ICE_STATE_FAILED                // 连接建立失败
} p2p_ice_state_t;

/* ============================================================================
 * ICE 模块 API
 * ============================================================================ */

struct p2p_session;
struct p2p_signaling_payload;

/* 收集本地候选地址（Host + Srflx + Relay） */
int p2p_ice_gather_candidates(struct p2p_session *s);

/* ICE 状态机定时处理（发送连通性检查） */
void p2p_ice_tick(struct p2p_session *s);

/* 处理 Trickle ICE 候选（增量添加） */
void p2p_ice_on_remote_candidates(struct p2p_session *s, const uint8_t *payload, int len);

/* 连通性检查成功回调 */
void p2p_ice_on_check_success(struct p2p_session *s, const struct sockaddr_in *from);

/* Trickle ICE: 发送单个本地候选 */
int  p2p_ice_send_local_candidate(struct p2p_session *s, p2p_candidate_entry_t *c);

/* ============================================================================
 * 优先级计算（RFC 5245 Section 4.1.2）
 * ============================================================================ */

/*
 * 计算候选优先级
 *
 * RFC 5245 优先级公式：
 *   priority = (2^24) * type_preference +
 *              (2^8)  * local_preference +
 *              (2^0)  * (256 - component_id)
 *
 * 类型偏好值 (type_preference)：
 *   - Host:  126 (本地地址最优先)
 *   - Prflx: 110 (对端反射)
 *   - Srflx: 100 (服务器反射)
 *   - Relay:   0 (中继最低优先级)
 *
 * @param type        候选类型
 * @param local_pref  本地偏好值 (0-65535，用于区分同类型多个候选)
 * @param component   组件 ID (RTP=1, RTCP=2)
 * @return            32 位优先级值
 */
uint32_t p2p_ice_calc_priority(p2p_ice_cand_type_t type, uint16_t local_pref, uint8_t component);

/*
 * 计算候选对优先级
 *
 * RFC 5245 Section 5.7.2:
 *   pair_priority = 2^32 * MIN(G, D) + 2 * MAX(G, D) + (G > D ? 1 : 0)
 *
 * @param controlling_prio  controlling 端候选优先级
 * @param controlled_prio   controlled 端候选优先级
 * @param is_controlling    本端是否为 controlling 角色
 * @return                  64 位候选对优先级
 */
uint64_t p2p_ice_calc_pair_priority(uint32_t controlling_prio, uint32_t controlled_prio, int is_controlling);

#endif /* P2P_ICE_H */
