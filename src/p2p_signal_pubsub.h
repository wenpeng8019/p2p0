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
 * 信箱 + 发布板架构（PUB/SUB 角色）
 * ============================================================================
 *
 * 每方拥有独立的 Gist。SUB（被动方）的 Gist 初始为"信箱"，等待 PUB 投递 offer。
 * 收到 offer 后，各方的 Gist 转为发布自己候选列表的"发布板"。
 *
 * 角色由 p2p_connect(handle, remote_peer_id) 决定：
 *   - remote_peer_id != NULL → PUB（主动方，知道对方 gist_id）
 *   - remote_peer_id == NULL → SUB（被动方，等待 offer 告知对方 gist_id）
 *
 *   SUB (gist_a = 信箱)                       PUB (gist_b)
 *   ┌────────────────────┐                    ┌────────────────────┐
 *   │  Gist A            │                    │  Gist B            │
 *   │  (p2p_signal.json) │                    │  (p2p_signal.json) │
 *   │                    │                    │                    │
 *   │ 阶段1: ONLINE:<ts> │ ← PUB 读取时间戳  │                    │
 *   │ 阶段2: OFFER:gist_b│                    │                    │
 *   │ 阶段3: <A 的候选>  │ ← PUB 轮询检测 ── │                    │
 *   │                    │                    │ 阶段4: <B 的候选>  │
 *   │                    │ ── SUB 轮询读取 → │                    │
 *   └────────────────────┘                    └────────────────────┘
 *
 * 连接流程：
 *
 *   SUB (Alice)              GitHub Gists                  PUB (Bob)
 *    |                          |                            |
 *    | 1. 写入心跳时间戳        |                            |
 *    |-- PATCH gist_a --------> |                            |
 *    |   "ONLINE:<timestamp>"   |                            |
 *    |                          |                            |
 *    |                          | 2. PUB 读取 gist_a 时间戳  |
 *    |                          | <------ GET gist_a --------|
 *    |                          |    检查 SUB 是否在线       |
 *    |                          |                            |
 *    |                          | 3. PUB 投递 offer          |
 *    |                          | <-- PATCH gist_a ----------|
 *    |                          |    "OFFER:gist_b"          |
 *    |                          |                            |
 *    | 4. SUB 检测到 offer      |                            |
 *    |--- GET gist_a ---------> |                            |
 *    | → 提取 gist_b           |                            |
 *    |                          |                            |
 *    | 5. SUB 发布候选到 gist_a |                            |
 *    |--- PATCH gist_a -------> |                            |
 *    |    Base64(DES(cands))    |                            |
 *    |                          |                            |
 *    |                          | 6. PUB 检测到 SUB 候选     |
 *    |                          | <------ GET gist_a --------|
 *    |                          | → 解密 SUB 候选           |
 *    |                          |                            |
 *    |                          | 7. PUB 发布候选到 gist_b   |
 *    |                          | <------ PATCH gist_b ------|
 *    |                          |                            |
 *    | 8. SUB 轮询 gist_b       |                            |
 *    |--- GET gist_b ---------> |                            |
 *    | → 解密 PUB 候选         |                            |
 *    |                          |                            |
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
 *
 *   SUB（被动方，remote_peer_id = NULL）：
 *     WAIT_OFFER ──→ SYNCING ──→ READY
 *
 *   PUB（主动方，remote_peer_id = 对端 gist_id）：
 *     OFFERING ──→ SYNCING ──→ READY
 *
 *   - INIT:        未启动
 *   - ONLINE:      已初始化，可以发起 connect
 *   - WAIT_OFFER:  SUB 心跳模式，写入时间戳并轮询自己的 Gist 等待 offer（5s 间隔）
 *   - OFFERING:    PUB 已投递 offer 到 SUB 的 Gist，等待 SUB 响应（1s 间隔）
 *   - SYNCING:     候选同步中（发布本端候选 + 轮询对端候选，1s 间隔）
 *   - READY:       本端候选已发布，对端候选已接收
 *
 * ============================================================================
 * 数据格式
 * ============================================================================
 *
 * Gist 文件内容（p2p_signal.json）在不同阶段承载不同内容：
 *
 *   心跳：        "ONLINE:<unix_timestamp>:<peer_id>"  （SUB 上线标识，每 5 分钟刷新）
 *   Offer：      "OFFER:<pub_gist_id>:<pub_peer_id>"  （SUB 据此知道 PUB 的发布板和身份）
 *   候选列表：    Base64(DES(SDP 候选文本))            （加密候选）
 *
 * 加密编码流程（候选列表）：
 *
 *   候选数组
 *     |
 *     v  p2p_ice_export_sdp(candidates_only=true)
 *   SDP 文本（a=candidate:... 行）
 *     |
 *     v  p2p_des_encrypt(key)
 *   DES 加密密文（ECB 模式，8 字节块对齐）
 *     |
 *     v  p2p_base64_encode()
 *   Base64 字符串 → 直接作为 Gist 文件内容
 *
 * 解码流程（候选列表）：
 *
 *   Base64 字符串
 *     |
 *     v  p2p_base64_decode()
 *   DES 密文
 *     |
 *     v  p2p_des_decrypt(key)
 *   SDP 文本
 *     |
 *     v  p2p_ice_import_sdp()
 *   远端候选数组 → 注入会话
 *
 * GitHub Gist API：
 *   - 读取：GET  https://api.github.com/gists/{gist_id}
 *           头部：Authorization: token {github_token}
 *   - 写入：PATCH https://api.github.com/gists/{gist_id}
 *           头部：Authorization: token {github_token}
 *                 Content-Type: application/json
 *           体：  {"files":{"p2p_signal.json":{"content":"<内容>"}}}
 */
#ifndef P2P_SIGNAL_PUBSUB_H
#define P2P_SIGNAL_PUBSUB_H

#include "predefine.h"

/* 轮询间隔（毫秒）*/
#ifndef P2P_PUBSUB_POLL_MAILBOX_MS
#define P2P_PUBSUB_POLL_MAILBOX_MS  5000    /* SUB 等待 offer 的信箱轮询 */
#endif
#ifndef P2P_PUBSUB_POLL_SYNC_MS
#define P2P_PUBSUB_POLL_SYNC_MS     1000    /* 活跃同步轮询 */
#endif
#ifndef P2P_PUBSUB_POLL_CONFIRM_MS
#define P2P_PUBSUB_POLL_CONFIRM_MS  300     /* offer 确认轮询（心跳竞争期） */
#endif
#ifndef P2P_PUBSUB_TRICKLE_BATCH_MS
#define P2P_PUBSUB_TRICKLE_BATCH_MS 1000    /* trickle 攒批窗口（毫秒）*/
#endif
#ifndef P2P_PUBSUB_HEARTBEAT_SEC
#define P2P_PUBSUB_HEARTBEAT_SEC    300     /* SUB 心跳刷新间隔（秒） */
#endif

struct p2p_session;
struct p2p_instance;

/* ============================================================================
 * PUBSUB 实例上下文（instance 级别）
 * ============================================================================ */

typedef enum {
    SIG_PUBSUB_INIT = 0,                                /* 未启动 */
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
    SIG_PUBSUB_SESS_IDLE = 0,                           /* 未初始化 */
    SIG_PUBSUB_SESS_WAIT_OFFER,                         /* SUB: 心跳模式，写入时间戳并轮询自己的 Gist 等待 offer */
    SIG_PUBSUB_SESS_OFFERING,                           /* PUB: offer 已投递，轮询 SUB 的 Gist 等待响应 */
    SIG_PUBSUB_SESS_SYNCING,                            /* 候选同步中 */
    SIG_PUBSUB_SESS_READY,                              /* 本端候选同步完成（ver=0 已发布）*/
} p2p_pubsub_sess_st;

typedef struct {
    bool                is_pub;                         /* true=PUB（主动方） false=SUB（被动方）*/
    p2p_pubsub_sess_st  state;                          /* 会话状态 */
    uint64_t            last_poll;                      /* 上次轮询时间戳 */

    uint64_t            last_sub;                       /* SUB: 上次写入订阅的时间 (now_ms)，0=未发送 */
    int                 offer_sent;                     /* PUB: 0=未发送, 1=已写入(待确认), 2=已确认 */

    char                remote_gist_id[128];            /* 对端发布板 Gist ID（PUB: connect 传入; SUB: offer 提取）*/
    char                remote_peer_id[P2P_PEER_ID_MAX]; /* 对端 peer_id（从 ONLINE/OFFER 协议提取）*/

    /* 发布状态 */
    int                 local_sync_ver;                 /* 本端发布版本 (>=1 trickle, 0=final) */
    int                 candidate_synced_count;         /* 已发布的候选数量 */
    uint64_t            last_sync;                      /* 上次发布候选的时间 (now_ms) */

    /* 轮询状态 */
    int                 remote_sync_ver;                /* 对端最后处理版本 (-1=未收到, 0=全部完成) */

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
ret_t p2p_signal_pubsub_connect(struct p2p_session *s, const char *remote_gist_id);

/*
 * 断开当前会话
 */
void p2p_signal_pubsub_disconnect(struct p2p_session *s);

#endif /* P2P_SIGNAL_PUBSUB_H */
