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
 * ============================================================================
 * 模块定位：协议转换层
 * ============================================================================
 *
 * 本模块作为**协议转换层**，提供项目内部格式与 ICE 标准格式的互转能力：
 *
 *   1. 候选地址收集（内部格式）
 *      - 使用项目统一的 p2p_local_candidate_entry_t / p2p_remote_candidate_entry_t
 *      - Host / Srflx / Relay 候选收集逻辑
 *      - Trickle ICE 增量交换
 *
 *   2. 协议转换（WebRTC 互操作）★
 *      - SDP 导出：
 *        • p2p_ice_export_candidate() - 单个候选（WebRTC 格式，无前缀后缀）
 *        • p2p_ice_export_candidates() - 批量候选（SDP 格式，带前缀后缀）
 *        • p2p_ice_export_sdp() - 向后兼容别名（指向 export_candidates）
 *      - SDP 导入：p2p_import_ice_sdp() - 解析 SDP 候选为内部格式
 *      - STUN 构造：p2p_ice_build_connectivity_check() - 生成 ICE 标准 STUN 包
 *      - Priority 算法：p2p_ice_calc_priority() - RFC 5245 标准优先级计算
 *
 *      【重要】SDP 格式差异（C vs WebRTC）：
 *
 *        ┌─────────────────────────────────────────────────────────────┐
 *        │ API                        │ 输出格式                        │
 *        ├─────────────────────────────────────────────────────────────┤
 *        │ p2p_ice_export_candidate() │ "candidate:1 1 UDP ..."        │
 *        │ (单个候选，WebRTC 格式)     │ 无 "a=" 前缀，无 "\r\n" 后缀    │
 *        │                            │ ✅ 对应 event.candidate.candidate │
 *        ├─────────────────────────────────────────────────────────────┤
 *        │ p2p_ice_export_candidates()│ "a=candidate:1 1 UDP ...\r\n"   │
 *        │ (批量候选，SDP 格式)       │ 带 "a=" 前缀，带 "\r\n" 后缀     │
 *        │                            │ ✅ 对应 SDP 文本中的 a 行        │
 *        └─────────────────────────────────────────────────────────────┘
 *
 *        使用建议：
 *          - Trickle ICE: 使用 p2p_export_ice_candidate() 逐个发送
 *          - 批量模式: 使用 p2p_export_ice_sdp() 一次性导出
 *
 *        详细说明见 p2p_ice.c 文档。
 *
 *   3. NAT 穿透执行（由 NAT 模块负责）
 *      - 连通性检查由 p2p_nat.c 驱动，可选择：
 *        • 自定义协议（P2P_PKT_PUNCH / REACH）- 高效简化
 *        • ICE 标准 STUN（调用 p2p_ice_build_connectivity_check）- WebRTC 兼容
 *
 * ============================================================================
 * 设计理念
 * ============================================================================
 *
 *   【候选数据结构】项目通用格式
 *     - p2p_local_candidate_entry_t / p2p_remote_candidate_entry_t
 *     - 存储在 p2p_session_t.local_cands[] / remote_cands[]
 *     - 与 ICE 无关的其他模块也使用相同结构
 *
 *   【连通性检查】NAT 模块负责
 *     - NAT 模块遍历 remote_cands[]，发送探测包
 *     - 根据配置选择协议：自定义或 ICE-STUN
 *     - Priority 排序、超时重试、路径切换等逻辑在 NAT 模块
 *
 *   【ICE 模块职责】协议转换 + 标准算法
 *     - 提供 SDP 格式转换（与 WebRTC 互通）
 *     - 提供 RFC 5245 标准优先级算法（NAT 模块调用）
 *     - 提供 ICE 标准 STUN 包构造（NAT 模块按需使用）
 *
 * ============================================================================
 * 使用场景
 * ============================================================================
 *
 *   【场景 1】项目内部对接（自定义协议）
 *     - NAT 模块使用 P2P_PKT_PUNCH / REACH 协议
 *     - 候选通过项目自定义信令交换（COMPACT / RELAY / PUBSUB）
 *     - 不需要 SDP，不需要 ICE-STUN
 *     - ICE 模块仅提供 priority 算法（保证候选排序一致）
 *
 *   【场景 2】与 WebRTC 互操作（ICE 标准）
 *     - 候选导出为 SDP 格式：p2p_ice_export_sdp()
 *     - 通过 WebRTC 信令（如 WebSocket）发送 SDP offer/answer
 *     - NAT 模块调用 p2p_ice_build_connectivity_check() 发送 ICE-STUN 包
 *     - 完全符合 RFC 5245 规范，可与浏览器 WebRTC 互通
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
 *                   - 标记 ice_ctx.cands_pending_send = true
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

#include "predefine.h"
#include "p2pp.h"

struct p2p_session;

/* ============================================================================
 * 候选地址类型（RFC 5245 Section 4.1.1）
 * ============================================================================ */
typedef enum {
    P2P_ICE_CAND_HOST = 0,              // 本地网卡地址（Host Candidate）
    P2P_ICE_CAND_SRFLX,                 // STUN 反射地址（Server Reflexive Candidate）
    P2P_ICE_CAND_RELAY,                 // TURN 中继地址（Relayed Candidate）
    P2P_ICE_CAND_PRFLX                  // 对端反射地址（Peer Reflexive Candidate）
} p2p_ice_cand_type_t;

typedef struct p2p_local_candidate_entry   p2p_local_candidate_entry_t;
typedef struct p2p_remote_candidate_entry  p2p_remote_candidate_entry_t;

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

/* ============================================================================
 * 协议转换层 API（WebRTC 互操作）
 * ============================================================================
 *
 * 以下 API 用于将内部候选格式转换为 ICE 标准格式（SDP），以及构建 ICE
 * 标准的 STUN 包，实现与 WebRTC 等标准 ICE 实现的互操作。
 *
 * 【API 设计理念】
 *
 *   1. p2p_ice_export_candidate() - 单个候选（WebRTC 格式）
 *      输出: "candidate:1 1 UDP 2130706431 192.168.1.10 54320 typ host"
 *      用途: Trickle ICE 模式，逐个发送给 WebRTC
 *      对应: event.candidate.candidate 字符串
 *
 *   2. p2p_ice_export_candidates() - 批量候选（SDP 格式）
 *      输出: "a=candidate:1 1 UDP 2130706431...\r\n" × N
 *      用途: 批量模式，嵌入完整 SDP 文本
 *      对应: SDP 文本中的 a=candidate 行
 *
 *   3. p2p_ice_export_sdp() - 向后兼容别名
 *      等价于 p2p_ice_export_candidates()
 *      保持现有代码兼容性
 *
 * 【典型使用场景】
 *
 *   场景 1: Trickle ICE（推荐）
 *     - 每收集一个候选立即发送
 *     - 使用 p2p_export_ice_candidate() → WebSocket → WebRTC
 *     - 降低 50-80% 首包延迟
 *
 *   场景 2: 批量传输
 *     - 等待收集完成后一次性发送
 *     - 使用 p2p_ice_export_candidates() → 分割 → 逐个 addIceCandidate
 *     - 减少信令轮次
 *
 *   场景 3: 完整 SDP 构建（信令模块负责）
 *     - 构建完整 SDP offer/answer
 *     - 使用 p2p_export_ice_sdp() 生成候选部分
 *     - 由 p2p_signal_relay / p2p_signal_pubsub 模块组装其他字段
 */

/*
 * 导出单个候选为 WebRTC 格式（无 a= 前缀和 \r\n 后缀）
 *
 * 对应 WebRTC event.candidate.candidate 字符串格式。
 *
 * 输出格式 (RFC 5245)：
 *   candidate:<foundation> <component> <protocol> <priority> <ip> <port> 
 *             typ <type> [raddr <rel-addr>] [rport <rel-port>]
 *
 * 示例：
 *   "candidate:1 1 UDP 2130706431 192.168.1.10 54320 typ host"
 *   "candidate:2 1 UDP 1694498815 203.0.113.77 54320 typ srflx raddr 192.168.1.10 rport 54320"
 *
 * 用途：Trickle ICE 模式，每收集一个候选立即发送给 WebRTC 端。
 *
 * @param cand      单个本地候选
 * @param buf       输出缓冲区（建议至少 256 字节）
 * @param buf_size  缓冲区大小
 * @return          生成的字符串长度（不含 \0），失败返回 -1
 */
int p2p_ice_export_candidate_entry(const p2p_local_candidate_entry_t *cand, char *buf, int buf_size);

/*
 * 导出多个候选为 SDP 格式（带 a= 前缀和 \r\n 后缀）
 *
 * 【重要】本函数可以生成完整的 SDP offer/answer 文本。
 *
 * 用途 1: 仅生成候选部分（嵌入到已有 SDP）
 * 用途 2: 生成完整 SDP 文本（包含 v=, o=, s=, t=, m=, ice-ufrag 等）
 *
 * 完整 SDP 示例：
 *   v=0
 *   o=- 123456789 2 IN IP4 127.0.0.1
 *   s=-
 *   t=0 0
 *   m=application 9 UDP/DTLS/SCTP webrtc-datachannel
 *   c=IN IP4 0.0.0.0
 *   a=ice-ufrag:aB3d                    ← 由调用者提供
 *   a=ice-pwd:Xy7zK9pLm3nO1qW2         ← 由调用者提供
 *   a=fingerprint:sha-256 AB:CD...      ← 可选，由 DTLS 模块提供
 *   a=setup:actpass                     ← 自动生成
 *   a=candidate:1 1 UDP 2130706431...   ← 本函数生成
 *   a=candidate:2 1 UDP 1694498815...   ← 本函数生成
 *
 * 批量模式，一次性导出所有候选，适合传统 SDP offer/answer 交换。
 *
 * SDP candidate 格式 (RFC 5245)：
 *   a=candidate:<foundation> <component> <protocol> <priority> <ip> <port> 
 *               typ <type> [raddr <rel-addr>] [rport <rel-port>]
 *
 * 用途：批量候选传输，适合嵌入完整 SDP 文本。
 *
 * @param cands             本地候选数组
 * @param cnt               候选数量
 * @param sdp_buf           输出缓冲区
 *                          - 若只生成候选：每个约 100 字节
 *                          - 若生成完整 SDP：需要额外 500-1000 字节（建议 >= 2048）
 * @param buf_size          缓冲区大小
 * @param generate_full     1=生成完整 SDP, 0=仅生成候选行（传统模式）
 * @param ice_ufrag         ICE 用户名片段（仅 generate_full=1 时需要，否则传 NULL）
 * @param ice_pwd           ICE 密码（仅 generate_full=1 时需要，否则传 NULL）
 * @param dtls_fingerprint  DTLS 指纹（可选，格式："sha-256 AB:CD:..."，可传 NULL）
 *
 * @return 生成的 SDP 字符串总长度，失败返回 -1
 *
 * 使用示例：
 *   // 方式1: 仅候选（嵌入已有 SDP）
 *   int len = p2p_ice_export_candidates(cands, cnt, buf, size, 0, NULL, NULL, NULL);
 *
 *   // 方式2: 完整 SDP（自包含格式）
 *   int len = p2p_export_ice_sdp(cands, cnt, buf, size, 1,
 *                                        "aB3d", "Xy7zK9pLm3nO1qW2", 
 *                                        "sha-256 AB:CD:EF:...");
 */
int p2p_ice_export_sdp(const p2p_local_candidate_entry_t *cands, int cnt,
                       char *sdp_buf, int buf_size, bool candidates_only,
                       const char *ice_ufrag,
                       const char *ice_pwd,
                       const char *dtls_fingerprint);

/*
 * 从 SDP 解析远端候选（a=candidate 行）
 *
 * 解析格式示例：
 *   a=candidate:1 1 UDP 2130706431 192.168.1.10 54320 typ host
 *   a=candidate:2 1 UDP 1694498815 203.0.113.77 54320 typ srflx raddr 192.168.1.10 rport 54320
 *
 * @param sdp_text   SDP 文本（多行，每行一个候选）
 * @param cands      输出：远端候选数组
 * @param max_cands  候选数组容量
 * @return           解析的候选数量，失败返回 -1
 */
int p2p_ice_import_sdp(const char *sdp_text, p2p_remote_candidate_entry_t *cands, int max_cands);

/*
 * 构建 ICE 标准的连通性检查包（包装 p2p_stun_build_ice_check）
 *
 * 用于 NAT 模块根据配置选择发送自定义 PUNCH 包或 ICE 标准 STUN 包。
 *
 * @param buf            输出缓冲区
 * @param max_len        缓冲区大小
 * @param local_ufrag    本地用户名片段
 * @param local_pwd      本地密码
 * @param remote_ufrag   远端用户名片段
 * @param remote_pwd     远端密码
 * @param priority       本地候选优先级
 * @param is_controlling 1=Controlling 角色, 0=Controlled 角色
 * @param tie_breaker    64位 tie-breaker 值（用于角色冲突解决）
 * @param use_candidate  是否携带 USE-CANDIDATE 属性
 * @return               生成的请求长度，失败返回 -1
 */
int p2p_ice_build_connectivity_check(
    uint8_t *buf, int max_len,
    const char *local_ufrag, const char *local_pwd,
    const char *remote_ufrag, const char *remote_pwd,
    uint32_t priority, int is_controlling, 
    uint64_t tie_breaker, int use_candidate
);

///////////////////////////////////////////////////////////////////////////////
#endif /* P2P_ICE_H */
