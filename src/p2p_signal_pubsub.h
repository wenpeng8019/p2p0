/*
 * PUBSUB 模式信令（HTTP Polling，GitHub Gist，DES 加密）
 *
 * ============================================================================
 * 协议概述
 * ============================================================================
 *
 * 基于 HTTP 存储的无服务器信令交换模块，使用 GitHub Gist 作为共享信道。
 *
 * 核心特点：无需专用信令服务器
 *
 * 标准 P2P 信令方案均依赖专用服务器（COMPACT 模式的 UDP 服务器、
 * RELAY 模式的 TCP 长连接服务器）。PUBSUB 模式通过第三方 HTTP 存储
 * 服务（GitHub Gist）实现去中心化的信令交换：
 *
 *   - 无需部署或维护专用服务器
 *   - 双方通过各自独立的 Gist 发布板交换 ICE 候选
 *   - 天然支持异步场景：双方无需同时在线
 *   - 数据在 Gist 中持久化，不受网络抖动影响
 *   - 使用 DES 加密 + Base64 编码保护候选信息隐私
 *
 * ============================================================================
 * 独立发布板架构
 * ============================================================================
 *
 * 每方拥有独立的 Gist 作为自己的"发布板"，避免共享 Gist 的读写一致性问题。
 * 各方的 Gist ID 即为其 peer_id。
 *
 *   Peer A (gist_a)                            Peer B (gist_b)
 *   ┌────────────────────┐                    ┌────────────────────┐
 *   │  Gist A 发布板      │                    │  Gist B 发布板      │
 *   │  (p2p_signal.json) │                    │  (p2p_signal.json) │
 *   │  {                 │  ← B 轮询读取 ──── │                    │
 *   │    "candidates":   │                    │  {                 │
 *   │      "<A 的候选>"  │                    │    "candidates":   │
 *   │  }                 │ ──── A 轮询读取 → │      "<B 的候选>"  │
 *   └────────────────────┘                    │  }                 │
 *                                              └────────────────────┘
 *
 * 连接流程：
 *
 *   Peer A                  GitHub Gists                  Peer B
 *    |                          |                           |
 *    | 1. 写候选到自己的发布板  |                           |
 *    |--- PATCH gist_a ------->|                           |
 *    |                          |                           |
 *    |                          |  2. B 轮询 A 的发布板     |
 *    |                          |<------ GET gist_a --------|
 *    |                          |------- 200 OK ----------->|
 *    |                          |                           |
 *    |                          |  3. B 解密 A 的候选       |
 *    |                          |     B 写候选到自己的发布板 |
 *    |                          |<------ PATCH gist_b ------|
 *    |                          |                           |
 *    |  4. A 轮询 B 的发布板   |                           |
 *    |------- GET gist_b ----->|                           |
 *    |<------- 200 OK ---------|                           |
 *    |                          |                           |
 *    |  5. A 解密 B 的候选     |                           |
 *    |                          |                           |
 *    |<========= ICE 连通性检查（直连 / STUN 打洞）=========>|
 *
 * ============================================================================
 * 状态机（对齐 RELAY 模式两阶段设计）
 * ============================================================================
 *
 * 阶段1: 实例级（online）
 *   INIT ──→ ONLINE
 *
 * 阶段2: 会话级（connect）
 *   WAIT_STUN ──→ SYNCING ──→ READY
 *
 *   - INIT:       未启动
 *   - ONLINE:     已初始化，可以发起 connect
 *   - WAIT_STUN:  等待本地 STUN 收集完成
 *   - SYNCING:    候选同步中（写入本端发布板 + 轮询对端发布板）
 *   - READY:      本端候选已发布，对端候选已接收
 *
 * ============================================================================
 * 数据格式
 * ============================================================================
 *
 * Gist 文件内容（p2p_signal.json）：
 *   <DES 加密 + Base64 编码的候选列表>
 *
 * 加密编码流程：
 *
 *   候选数组
 *     |
 *     v  pack_candidate() × N
 *   二进制字节流（N × sizeof(p2p_candidate_t)）
 *     |
 *     v  p2p_des_encrypt(key)
 *   DES 加密密文（ECB 模式，8 字节块对齐）
 *     |
 *     v  p2p_base64_encode()
 *   Base64 字符串 → 直接作为 Gist 文件内容
 *
 * GitHub Gist API：
 *   - 读取：GET  https://api.github.com/gists/{gist_id}
 *           头部：Authorization: token {github_token}
 *   - 写入：PATCH https://api.github.com/gists/{gist_id}
 *           头部：Authorization: token {github_token}
 *                 Content-Type: application/json
 *           体：  {"files":{"p2p_signal.json":{"content":"<Base64密文>"}}}
 */
#ifndef P2P_SIGNAL_PUBSUB_H
#define P2P_SIGNAL_PUBSUB_H

#include "predefine.h"

/* 轮询对端发布板的间隔（毫秒）*/
#ifndef P2P_PUBSUB_POLL_MS
#define P2P_PUBSUB_POLL_MS  3000
#endif

struct p2p_session;
struct p2p_instance;

/* ============================================================================
 * PUBSUB 实例上下文（instance 级别）
 * ============================================================================ */

typedef enum {
    SIG_PUBSUB_INIT = 0,                               /* 未启动 */
    SIG_PUBSUB_ERROR,                                   /* 错误状态 */
    SIG_PUBSUB_ONLINE,                                  /* 已上线 */
} p2p_pubsub_st;

typedef struct {
    /* 基础状态 */
    p2p_pubsub_st       state;                          /* 信令状态 */

    /* 身份标识 */
    char                local_peer_id[P2P_PEER_ID_MAX]; /* 本端名称 */
    char                auth_token[128];                /* GitHub Personal Access Token */
    char                auth_key[64];                   /* DES 加密密钥 */
    char                local_gist_id[128];             /* 本端发布板 Gist ID */

} p2p_signal_pubsub_ctx_t;

/* ============================================================================
 * PUBSUB 会话上下文（session 级别：与对端的关系）
 * ============================================================================ */

typedef enum {
    SIG_PUBSUB_SESS_WAIT_ONLINE = 0,                   /* 执行 connect() 但实例未就绪 */
    SIG_PUBSUB_SESS_WAIT_STUN,                         /* 等待本地 STUN 收集完成 */
    SIG_PUBSUB_SESS_SYNCING,                            /* 候选同步中 */
    SIG_PUBSUB_SESS_READY,                              /* 本端已发布 + 对端候选已接收 */
} p2p_pubsub_sess_st;

typedef struct {
    p2p_pubsub_sess_st  state;                          /* 会话状态 */

    char                remote_gist_id[128];            /* 对端发布板 Gist ID（= remote_peer_id）*/

    /* 发布状态 */
    bool                local_published;                /* 本端候选是否已写入发布板 */
    int                 local_published_cnt;            /* 已发布的候选数量 */

    /* 轮询状态 */
    uint64_t            last_poll;                      /* 上次轮询对端发布板的时间戳 */
    bool                remote_received;                /* 是否已成功接收对端候选 */

} p2p_pubsub_session_t;

/* ============================================================================
 * PUBSUB 信令 API（对齐 RELAY/COMPACT 风格）
 * ============================================================================ */

/*
 * 初始化 PUBSUB 信令上下文
 */
void p2p_signal_pubsub_init(p2p_signal_pubsub_ctx_t *ctx);

/*
 * 信令接收维护（拉取阶段）
 *
 * 轮询对端发布板，接收远端候选。
 * 在 p2p_update() 的阶段 2（信令拉取）中调用。
 */
void p2p_signal_pubsub_tick_recv(struct p2p_instance *inst, uint64_t now);

/*
 * 信令发送维护（推送阶段）
 *
 * 将本端候选写入自己的发布板。
 * 在 p2p_update() 的阶段 8（信令推送）中调用。
 */
void p2p_signal_pubsub_tick_send(struct p2p_instance *inst, uint64_t now);

//----------------------------------------------------------------------------

/*
 * 实例上线（配置 token 和 gist_id）
 *
 * @param inst          P2P 实例
 * @param local_peer_id 本端名称
 * @param token         GitHub Personal Access Token
 * @param gist_id       本端发布板 Gist ID
 * @return              E_NONE=成功
 */
ret_t p2p_signal_pubsub_online(struct p2p_instance *inst, const char *local_peer_id,
                                const char *token, const char *gist_id);

/*
 * 实例下线
 */
ret_t p2p_signal_pubsub_offline(struct p2p_instance *inst);

//-----------------------------------------------------------------------------

/*
 * STUN 候选收集完成后，将 WAIT_STUN 会话转入 SYNCING
 */
void p2p_signal_pubsub_stun_ready(struct p2p_session *s);

/*
 * Trickle ICE：本地候选异步补发入口
 */
void p2p_signal_pubsub_trickle_candidate(struct p2p_session *s);

//-----------------------------------------------------------------------------

/*
 * 建立与对端的会话
 *
 * @param s              P2P 会话
 * @param remote_peer_id 对端发布板 Gist ID
 * @return               E_NONE=成功
 */
ret_t p2p_signal_pubsub_connect(struct p2p_session *s, const char *remote_peer_id);

/*
 * 断开当前会话
 */
ret_t p2p_signal_pubsub_disconnect(struct p2p_session *s);

#endif /* P2P_SIGNAL_PUBSUB_H */
