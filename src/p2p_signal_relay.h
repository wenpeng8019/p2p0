/*
 * RELAY 模式信令（TCP, 离线缓存 + Trickle ICE）
 *
 * ============================================================================
 * 协议概述
 * ============================================================================
 *
 * 基于中央服务器的信令交换模块，通过 TCP 长连接提供可靠的消息传递。
 *
 * 核心改进：异步 ICE 候选交换
 *
 * 标准 WebRTC/ICE 信令假设双方在线（RFC 5245/8445）：
 *   - Offer/Answer 通过 SIP/XMPP 等信令协议实时交换
 *   - 双方必须同时在线才能完成 ICE 协商
 *   - 不考虑用户登录/离线状态
 *
 * RELAY 模式扩展支持异步场景：
 *   - 服务器维护用户在线状态（LOGIN/LOGOUT）
 *   - 对端离线时，服务器缓存 ICE 候选
 *   - 对端上线后，服务器主动推送缓存的候选（OFFER）
 *   - 支持完整的 Trickle ICE 增量候选交换
 *   - 适用于移动网络、弱网环境、异步通信场景
 *
 * 实际收益：
 *   - Alice 在办公室发起连接，Bob 在地铁离线
 *   - 服务器缓存 Alice 的候选
 *   - Bob 到站连上网络后自动收到缓存候选
 *   - P2P 连接自动建立，无需 Alice 重新发起
 *
 * 注：与其他信令模式的对比请参考 doc/ARCHITECTURE.md
 *
 * ============================================================================
 * Trickle ICE 候选交换机制
 * ============================================================================
 *
 * 支持增量候选交换（Trickle ICE），允许候选在收集过程中即时发送，
 * 而非等待所有候选收集完成。
 *
 *   1. 登录阶段（建立连接）：
 *      - 客户端发送 LOGIN(peer_name)
 *      - 服务器回复 LOGIN_ACK(result)
 *        · result=0: 成功登录
 *        · result≠0: 失败（名称冲突、服务器满等）
 *      - 进入 CONNECTED 状态
 *
 *   2. 发起连接（CONNECT）：
 *      - 客户端发送 CONNECT(target_name, payload)
 *        · payload 包含：p2p_signaling_payload_hdr_t + N × p2p_candidate_t
 *        · N 可以是 1 到 8 个候选（支持批量和单个发送）
 *      - 服务器返回 CONNECT_ACK：
 *        · status=0: 对端在线，已转发为 P2P_RLY_OFFER
 *        · status=1: 对端离线，已缓存（等待对端上线后推送）
 *        · status=2: 缓存已满（停止发送，等待对端上线或超时）
 *        · candidates_acked: 本次确认的候选数量
 *      - 客户端根据 candidates_acked 更新 next_candidate_index
 *      - status=2 时：停止发送新候选，等待收到 OFFER（对端上线）
 *      - status=1 且所有候选已发送完：等待收到 OFFER（对端上线）
 *      - 两种等待状态均设置超时（建议 60 秒），超时后放弃连接（对端上线）
 *
 *   3. 对端在线场景（直接转发）：
 *
 *      Alice                 Server                    Bob
 *        |                     |                          |
 *        |--- CONNECT(3 cands)->|                          |
 *        |                     |--- OFFER(3 cands) ------>|  (立即转发)
 *        |<-- CONNECT_ACK -----|  (status=0, acked=3)     |
 *        |                     |<-- ANSWER(N cands) ------|
 *        |<-- FORWARD(N cands)-|                          |
 *        |                     |                          |
 *        |--- ANSWER(2 cands) ----------------------->    |  (继续 Trickle ICE)
 *        |                     |--- FORWARD(2 cands) ---->|
 *        |<=============== P2P ICE 连通性检查 ==============>|
 *
 *   4. 对端离线场景（候选缓存）：
 *
 *      Alice (在线)         Server                    Bob (离线)
 *        |                     |                          |
 *        |--- LOGIN ---------->|                          |
 *        |<-- LOGIN_ACK -------|                          |
 *        |                     |                          |
 *        |--- CONNECT(3 cands)->|  (解析并逐个缓存候选)    |
 *        |<-- CONNECT_ACK -----|  (status=1, acked=3)     |
 *        |    [等待 5s]        |                          |
 *        |--- CONNECT(2 cands)->|  (继续缓存)              |
 *        |<-- CONNECT_ACK -----|  (status=1, acked=2)     |
 *        |                     |  (Alice 已发送 5 个候选)  |
 *        |    ... Bob 上线 ...                            |
 *        |                     |<-- LOGIN -----------------|
 *        |                     |--- LOGIN_ACK ----------->|
 *        |                     |--- OFFER(5 cands) ------>|  (推送 Alice 缓存的候选)
 *        |                     |<-- ANSWER(N cands) ------|  (Bob 的候选)
 *        |<-- FORWARD(N cands)-|                          |  (转发给 Alice)
 *        |--- ANSWER(more) -------------------------------->|  (Alice 继续发送剩余候选)
 *        |                     |--- FORWARD(more) -------->|
 *        |<=============== P2P ICE 连通性检查 ==============>|
 *
 *   5. 缓存已满场景（停止发送）：
 *
 *      Alice (在线)         Server（缓存满）          Bob (离线)
 *        |                     |                          |
 *        |--- CONNECT(3 cands)->|  (尝试缓存)              |
 *        |<-- CONNECT_ACK -----|  (status=2, acked=0)     |
 *        |    [停止发送]       |  (缓存已满，拒绝新候选)   |
 *        |    [等待 60s]       |                          |
 *        |                     |                          |
 *        |    ... Bob 上线 ...                            |
 *        |                     |<-- LOGIN -----------------|
 *        |                     |--- LOGIN_ACK ----------->|
 *        |                     |--- OFFER(旧缓存) -------->|  (推送之前缓存的候选)
 *        |                     |<-- ANSWER(N cands) ------|  (Bob 的候选)
 *        |<-- FORWARD(N cands)-|                          |  (转发给 Alice，证明 Bob 已上线)
 *        |--- ANSWER(3 cands) -------------------------------->|  (Alice 继续发送剩余候选)
 *        |                     |--- FORWARD(3 cands) ----->|
 *        |<=============== P2P ICE 连通性检查 ==============>|
 *
 *      注：收到 status=2 后，Alice 停止发送并启动 60 秒超时计时器
 *          - Bob 上线时，服务器推送 OFFER 给 Bob（Alice 的缓存候选）
 *          - Bob 回应 ANSWER，服务器转发 FORWARD 给 Alice
 *          - Alice 收到 FORWARD 证明 Bob 已上线，用 ANSWER 继续发送剩余候选
 *          - 超时后仍未收到 FORWARD，则放弃连接
 *
 *   6. 不支持缓存场景（服务器配置缓存=0）：
 *
 *      Alice (在线)         Server（无缓存）          Bob (离线)
 *        |                     |                          |
 *        |--- CONNECT(3 cands)->|  (检查对端状态)          |
 *        |<-- CONNECT_ACK -----|  (status=2, acked=0)     |
 *        |    [停止发送]       |  (不缓存候选，但记录连接意图) |
 *        |    [等待 60s]       |                          |
 *        |                     |                          |
 *        |    ... Bob 上线 ...                            |
 *        |                     |<-- LOGIN -----------------|
 *        |                     |--- LOGIN_ACK ----------->|
 *        |                     |--- OFFER(empty) -------->|  (推送空 OFFER，告知 Alice 想连接)
 *        |                     |<-- ANSWER(N cands) ------|  (Bob 发送自己的候选)
 *        |<-- FORWARD(N cands)-|                          |  (转发给 Alice，证明 Bob 已上线)
 *        |--- ANSWER(3 cands) -------------------------------->|  (Alice 继续发送候选)
 *        |                     |--- FORWARD(3 cands) ----->|
 *        |<=============== P2P ICE 连通性检查 ==============>|
 *
 *      注：不支持缓存时，服务器仍记录"连接意图"（不保存候选数据）
 *          - Alice 收到 status=2，停止发送候选，等待 FORWARD
 *          - Bob 上线时，服务器推送空 OFFER（candidates=0，sender=Alice）
 *          - Bob 收到空 OFFER 知道"Alice 想连接"，发送 ANSWER（包含 Bob 的候选）
 *          - Alice 收到 FORWARD（Bob 的候选），用 ANSWER 继续发送自己的候选
 *          - 超时（60 秒）未收到 FORWARD，则放弃连接
 *          - 服务器实现：pending_intent_t 轻量级队列（sender + target，64B）
 *
 *   7. Trickle ICE 流程控制：
 *      - 客户端维护 next_candidate_index（下一个待发送候选的索引）
 *      - p2p_update() 每 5 秒检查是否有新候选需要发送
 *      - 收到 CONNECT_ACK 后，根据 candidates_acked 更新索引：
 *        · next_candidate_index += candidates_acked
 *      - 收到 OFFER 时，表示对端上线，继续发送剩余候选
 *      - 无需 FIN 标志，收集到的候选即发即连（完全 Trickle 模式）
 *
 * ============================================================================
 * 状态机
 * ============================================================================
 *
 *   DISCONNECTED ──→ CONNECTING ──→ CONNECTED ──→ ERROR
 *         ↑                               │             │
 *         └───────────────────────────────┴─────────────┘
 *                 (连接断开或错误时重连)
 *
 *   - DISCONNECTED: 未连接或已断开
 *   - CONNECTING:   正在建立 TCP 连接
 *   - CONNECTED:    已登录，可以发送/接收消息
 *   - ERROR:        发生错误（网络异常、协议错误等）
 *
 * ============================================================================
 * 消息格式详解
 * ============================================================================
 *
 * 所有消息格式：[p2p_relay_hdr_t: 9B][payload: N bytes]
 *
 * 包头结构（p2p_relay_hdr_t）：
 *   - magic:  0x50325030 ("P2P0")，用于帧同步
 *   - type:   消息类型（见下方枚举）
 *   - length: payload 长度（不包括包头）
 *
 * LOGIN:
 *   payload: [name(32)]
 *   - name: 客户端 peer ID（字符串，最大 32 字节）
 *
 * LOGIN_ACK:
 *   payload: []
 *   - 无 payload，仅通过 TCP 连接成功/失败表示结果
 *   - 成功：保持连接，进入 CONNECTED 状态
 *   - 失败：服务器关闭连接
 *
 * CONNECT:
 *   payload: [target_name(32)][signaling_payload(variable)]
 *   - target_name: 目标 peer ID（字符串，32 字节）
 *   - signaling_payload: ICE 候选数据
 *     · [p2p_signaling_payload_hdr_t: 76B][candidates: N × 32B]
 *     · hdr.sender: 发送方 ID（将被服务器覆盖为登录时的 name）
 *     · hdr.target: 目标 ID（必须与 target_name 一致）
 *     · hdr.candidate_count: 本次包含的候选数量（1-8）
 *
 * CONNECT_ACK:
 *   payload: [p2p_relay_connect_ack_t: 4B]
 *   - status: 
 *     · 0 = 成功转发（对端在线）
 *     · 1 = 已缓存（对端离线）
 *     · 2 = 缓存已满（无法继续缓存，等待对端上线）
 *   - candidates_acked: 服务器确认的候选数量（0-255）
 *     · status=0（在线）: 等于 hdr.candidate_count（全部转发）
 *     · status=1（离线）: 等于实际缓存数量（可能小于请求数量）
 *     · status=2（已满）: 为 0（拒绝所有新候选）
 *   - reserved[2]: 保留字段
 *
 * 注：status=2 不是错误状态，客户端应停止发送并等待 OFFER（对端上线通知）
 *
 * OFFER:
 *   payload: [sender_name(32)][signaling_payload(variable)]
 *   - sender_name: 发起方 peer ID
 *   - signaling_payload: ICE 候选数据（格式同 CONNECT）
 *   - 场景 1: 对端在线，服务器将 CONNECT 立即转发为 OFFER
 *   - 场景 2: 对端上线（LOGIN），服务器将缓存的候选打包为 OFFER 推送
 *
 * ANSWER:
 *   payload: [target_name(32)][signaling_payload(variable)]
 *   - target_name: 目标 peer ID（OFFER 的发送方）
 *   - signaling_payload: 应答候选数据
 *   - 无 ACK（ANSWER 仅需单向转发，不缓存）
 *
 * FORWARD:
 *   payload: [sender_name(32)][signaling_payload(variable)]
 *   - sender_name: 原始发送方 peer ID
 *   - signaling_payload: 转发的候选数据
 *   - 服务器将 ANSWER 转发为 FORWARD 给发起方
 *
 * LIST:
 *   payload: []
 *   - 查询当前在线用户列表
 *
 * LIST_RES:
 *   payload: [count(4)][names(count × 32)]
 *   - count: 在线用户数量
 *   - names: 用户名列表（每个 32 字节）
 *
 * HEARTBEAT:
 *   payload: []
 *   - 客户端定期发送（如每 30 秒）
 *   - 服务器收到后更新 last_active 时间
 *   - 无响应，单向通知
 *
 * ============================================================================
 * 服务器缓存机制
 * ============================================================================
 *
 * 服务器为每个注册用户维护离线候选缓存：
 *
 * 缓存单位：
 *   - pending_candidate_t 结构（~60 字节）
 *   - 包含：sender_name(32B) + p2p_candidate_t(32B) + timestamp(8B)
 *
 * 缓存容量：
 *   - 服务器自行配置（取决于内存和并发用户数）
 *   - 客户端无需预知具体容量（通过 candidates_acked 动态感知）
 *
 * 缓存逻辑：
 *   1. 收到 CONNECT 时，服务器检查目标是否在线
 *   2. 在线：直接转发为 OFFER，返回 status=0
 *   3. 离线：
 *      - 解析 signaling_payload_hdr（76 字节）
 *      - for (i = 0; i < candidate_count; i++):
 *        · unpack_candidate(&cand, payload + 76 + i*32)
 *        · 缓存到 pending_candidates[count++]
 *      - 返回 candidates_acked = 实际缓存数量
 *   4. 对端上线（LOGIN）时：
 *      - 按发送者分组缓存的候选
 *      - 每个发送者构建一个 OFFER：
 *        · pack_signaling_payload_hdr(sender, target, ...)
 *        · for 循环 pack_candidate() 序列化候选
 *        · 发送 OFFER 给新登录用户
 *      - 清空已推送的缓存
 *
 * 缓存限制：
 *   - 超过服务器容量时，返回 status=2（candidates_acked=0）
 *   - 客户端收到 status=2 后：
 *     · 停止发送新候选（避免浪费带宽）
 *     · 等待 OFFER（对端上线后服务器推送）
 *     · 或等待超时（如 60 秒后放弃）
 *   - 可选策略：FIFO（先进先出，丢弃最早候选以腾出空间）
 *
 * ============================================================================
 * 客户端状态追踪
 * ============================================================================
 *
 * 客户端维护以下字段实现 Trickle ICE：
 *
 * total_candidates_sent:
 *   - 累计发送的候选总数（包括成功和失败的）
 *   - 用于统计和调试
 *
 * total_candidates_acked:
 *   - 服务器确认的候选总数（累加每次 CONNECT_ACK.candidates_acked）
 *   - 用于计算丢失率：loss_rate = 1 - (acked / sent)
 *
 * next_candidate_index:
 *   - 下一个待发送候选的索引（0-based）
 *   - p2p_update() 检查：if (session->local_candidate_count > next_index)
 *   - 收到 CONNECT_ACK: next_index += candidates_acked
 *   - 收到 OFFER: 继续发送剩余候选（对端已上线）
 *
 * 重传策略：
 *   - CONNECT 基于 TCP，无需应用层重传
 *   - 但需定期检查（每 5 秒）是否有新候选
 *   - 连接断开时，next_candidate_index 重置为 0
 */

#ifndef P2P_SIGNAL_RELAY_H
#define P2P_SIGNAL_RELAY_H

#include <stdint.h>
#include "p2p_platform.h"   /* cross-platform socket headers */
#include <p2pp.h>  /* RELAY 模式协议定义 */

/* ============================================================================
 * 信令上下文
 * ============================================================================ */

struct p2p_session;

/* 信令连接状态 */
typedef enum {
    SIGNAL_DISCONNECTED = 0,  /* 未连接 */
    SIGNAL_CONNECTING,        /* 连接中 */
    SIGNAL_CONNECTED,         /* 已连接并登录成功 */
    SIGNAL_ERROR              /* 错误状态 */
} p2p_signal_relay_state_t;

/*
 * 信令上下文结构
 *
 * 保存与信令服务器的连接状态和相关信息。
 */
typedef struct {
    p2p_socket_t fd;                             /* TCP socket 描述符 */
    char my_name[P2P_PEER_ID_MAX];               /* 本地 peer 名称 */
    char incoming_peer_name[P2P_PEER_ID_MAX];    /* 收到请求时的对端名称 */
    struct sockaddr_in server_addr;              /* 服务器地址 */
    p2p_signal_relay_state_t state;              /* 连接状态 */
    uint64_t last_connect_attempt;               /* 最后连接尝试时间（毫秒） */
    
    /* ===== Trickle ICE 候选发送追踪 ===== */
    int total_candidates_sent;                   /* 累计发送的候选总数（统计用） */
    int total_candidates_acked;                  /* 服务器已确认缓存的总数（累加 CONNECT_ACK.candidates_acked） */
    int next_candidate_index;                    /* 下次应从第几个候选开始发送（断点续传索引） */
    
    /* ===== 等待对端上线状态追踪（场景4/5/6） ===== */
    bool waiting_for_peer;                       /* 是否正在等待对端上线（status=1/2 或所有候选已发送） */
    char waiting_target[P2P_PEER_ID_MAX];        /* 等待的目标 peer 名称 */
    uint64_t waiting_start_time;                 /* 开始等待的时间戳（毫秒） */
    
    /* 注：
     * - next_candidate_index 初始为 0
     * - 每次收到 CONNECT_ACK 后：next_candidate_index += candidates_acked
     * - p2p_update() 检查是否有新候选：if (local_candidate_count > next_candidate_index)
     * - 收到 FORWARD 表示对端上线，继续发送剩余候选
     * - waiting_for_peer=true 时，超时 60 秒后放弃连接
     */
} p2p_signal_relay_ctx_t;

/* ============================================================================
 * API 函数
 * ============================================================================ */

/*
 * 初始化信令上下文
 *
 * 设置初始状态为 DISCONNECTED，清空所有字段。
 *
 * @param ctx  信令上下文指针
 */
void p2p_signal_relay_init(p2p_signal_relay_ctx_t *ctx);

/*
 * 登录到信令服务器
 *
 * 建立 TCP 连接并发送 LOGIN 消息。连接是非阻塞的，
 * 需要调用 p2p_signal_relay_tick() 完成握手。
 *
 * @param ctx       信令上下文
 * @param server_ip 服务器 IP 地址（字符串）
 * @param port      服务器端口
 * @param my_name   本地 peer 名称（最大 32 字节）
 * @return          0 成功发起连接，-1 失败（参数错误或 socket 创建失败）
 */
int  p2p_signal_relay_login(p2p_signal_relay_ctx_t *ctx, const char *server_ip, int port, const char *my_name);

/*
 * 周期调用，处理信令状态和消息
 *
 * 应在主循环中定期调用（如每 100ms），负责：
 * - CONNECTING 状态：检查连接是否完成，发送 LOGIN
 * - CONNECTED 状态：接收并处理服务器消息（OFFER/FORWARD/CONNECT_ACK 等）
 * - 连接断开时自动重连（间隔 3 秒）
 *
 * @param ctx  信令上下文
 * @param s    会话对象（用于处理收到的候选）
 */
void p2p_signal_relay_tick(p2p_signal_relay_ctx_t *ctx, struct p2p_session *s);

/*
 * 发送 CONNECT 消息（发起连接）
 *
 * 向目标 peer 发送候选信息。支持 Trickle ICE，可多次调用发送不同批次的候选。
 *
 * @param ctx         信令上下文
 * @param target_name 目标 peer 名称（字符串，最大 32 字节）
 * @param data        ICE 候选数据（已序列化的 signaling_payload）
 *                    格式：[p2p_signaling_payload_hdr_t: 76B][candidates: N×32B]
 * @param len         数据长度（76 + N*32 字节）
 * @return            发送结果状态码
 *                    - > 0: 对端在线，返回已转发的候选数量
 *                    - = 0: 对端离线，候选已缓存（等待对端上线后由服务器推送）
 *                    - = -1: 连接断开（网络错误）
 *                    - = -2: 缓存已满（停止发送，等待对端上线或超时）
 */
int  p2p_signal_relay_send_connect(p2p_signal_relay_ctx_t *ctx, const char *target_name, const void *data, int len);

/*
 * 发送 ANSWER 消息（应答连接）
 *
 * 收到 OFFER 后，回复候选信息。同样支持 Trickle ICE，可分批发送。
 *
 * @param ctx         信令上下文
 * @param target_name 目标 peer 名称（OFFER 的发送方）
 * @param data        ICE 候选数据（格式同 send_connect）
 * @param len         数据长度
 * @return            0 成功，-1 失败（连接断开）
 */
int  p2p_signal_relay_reply_connect(p2p_signal_relay_ctx_t *ctx, const char *target_name, const void *data, int len);

/*
 * 关闭信令连接
 *
 * 关闭 TCP socket，重置状态为 DISCONNECTED。
 *
 * @param ctx  信令上下文
 */
void p2p_signal_relay_close(p2p_signal_relay_ctx_t *ctx);

#endif /* P2P_SIGNAL_RELAY_H */
