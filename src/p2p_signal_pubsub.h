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
 *   - 双方通过共享的 Gist 文件交换 ICE 候选
 *   - 天然支持异步场景：双方无需同时在线
 *   - 数据在 Gist 中持久化，不受网络抖动影响
 *   - 使用 DES 加密 + Base64 编码保护候选信息隐私
 *
 * 典型使用场景：
 *   - 开发/测试环境：无服务器资源时的快速 P2P 验证
 *   - 低频连接：无需长期维护服务器连接
 *   - 跨网络调试：利用公共 Gist 穿越防火墙进行诊断
 *
 * 注：与其他信令模式的对比请参考 doc/ARCHITECTURE.md
 *
 * ============================================================================
 * PUB/SUB 角色与信令流程
 * ============================================================================
 *
 * 双端通过同一个 Gist 文件的两个字段交换信息：
 *
 *   Gist 文件（p2p_signal.json）：
 *   +------------------------------------------------------------------+
 *   |  {                                                               |
 *   |    "offer":  "<PUB 的 ICE 候选，DES 加密后 Base64 编码>",       |
 *   |    "answer": "<SUB 的 ICE 候选，DES 加密后 Base64 编码>"        |
 *   |  }                                                               |
 *   +------------------------------------------------------------------+
 *
 * 角色定义：
 *   - PUB（Publisher，发起端）：主动创建信道，写入 offer，等待 answer
 *   - SUB（Subscriber，订阅端）：轮询信道，读取 offer，写入 answer
 *
 * 完整信令流程：
 *
 *   PUB                    GitHub Gist                    SUB
 *    |                          |                           |
 *    |--- PATCH offer --------->|                           |  [1]
 *    |    (PUB 的 ICE 候选)     |                           |
 *    |                          |                           |
 *    |                          |   .--- tick() 每 5s ----> |
 *    |                          |<------- GET (轮询) -------|
 *    |                          |   If-None-Match: ETag     |
 *    |                          |--- 304 Not Modified ----->|  (offer 未更新，继续等待)
 *    |                          |   .--- tick() 每 5s ----> |
 *    |                          |<------- GET (轮询) -------|
 *    |                          |   If-None-Match: ETag     |
 *    |                          |-------- 200 OK ---------->|  (offer 有新内容)
 *    |                          |   offer 字段有新内容       |
 *    |                          |                           |
 *    |                          |      [2] SUB 解密 offer   |
 *    |                          |          添加远端候选      |
 *    |                          |                           |
 *    |                          |<------- PATCH answer -----|
 *    |                          |       (SUB 的 ICE 候选)   |
 *    |                          |                           |
 *    |   .--- tick() 每 1s ---> |                           |
 *    |<------- GET (轮询) ------|                           |
 *    |   If-None-Match: ETag    |                           |
 *    |--- 304 Not Modified ---->|                           |  (answer 未更新，继续等待)
 *    |   .--- tick() 每 1s ---> |                           |
 *    |<------- GET (轮询) ------|                           |
 *    |   If-None-Match: ETag    |                           |
 *    |<-------- 200 OK ---------|                           |  (answer 有新内容)
 *    |   answer 字段有新内容    |                           |
 *    |                          |                           |
 *    |  [3] PUB 解密 answer     |                           |
 *    |      添加远端候选         |                           |
 *    |                          |                           |
 *    |<======= ICE 连通性检查（直连 / STUN 打洞）==========>|
 *
 * 步骤说明：
 *   [1] PUB 调用 p2p_signal_pubsub_send() 将加密候选写入 "offer" 字段
 *   [2] SUB 通过 p2p_signal_pubsub_tick() 轮询检测 offer 更新，
 *       解密后自动调用 p2p_signal_pubsub_send() 写入 "answer"（仅一次）
 *   [3] PUB 通过 p2p_signal_pubsub_tick() 轮询检测 answer 更新，
 *       解密后将候选注入 p2p_session
 *
 * ETag 轮询优化：
 *   - 每次 GET 请求携带 If-None-Match: <etag>
 *   - 若 Gist 内容未变化，服务器返回 304 Not Modified（节省流量）
 *   - 若内容有更新，服务器返回 200 OK 并携带新 ETag
 *   - etag 字段由上下文维护，每次成功读取后自动更新
 *
 * ============================================================================
 * 状态机
 * ============================================================================
 *
 * PUB 端状态流转：
 *
 *   IDLE --> PUBLISHING --> WAITING_ANSWER --> READY
 *                |                  |
 *                | (写入 offer)      | (轮询 answer 成功)
 *                v                  v
 *          (p2p_signal_         (process_payload
 *           pubsub_send)         更新远端候选)
 *
 * SUB 端状态流转：
 *
 *   IDLE --> POLLING --> RECEIVING_OFFER --> ANSWERING --> READY
 *               |               |                 |
 *               | (轮询 offer)  | (解密成功)       | (写入 answer)
 *               v               v                 v
 *         (tick: GET)    (process_payload    (p2p_signal_
 *                         添加远端候选)       pubsub_send,
 *                                            answered=1)
 *
 * 状态变量说明：
 *   - answered：SUB 专用，防止重复写入 answer（SUB 收到 offer 后仅回应一次）
 *   - last_poll：上次轮询时间戳（毫秒），控制轮询间隔（PUB 默认 P2P_PUBSUB_PUB_POLL_MS，SUB 默认 P2P_PUBSUB_SUB_POLL_MS）
 *   - etag：    上次读取 Gist 的 HTTP ETag，用于 304 条件请求优化
 *
 * ============================================================================
 * 数据格式
 * ============================================================================
 *
 * 加密编码流程：
 *
 *   p2p_signaling_payload_t
 *         |
 *         v  pack_signaling_payload_hdr() + pack_candidate()
 *   二进制字节流（76B header + N*32B candidates）
 *         |
 *         v  p2p_des_encrypt(key)
 *   DES 加密密文（ECB 模式，8 字节块对齐）
 *         |
 *         v  p2p_base64_encode()
 *   Base64 字符串
 *         |
 *         v  JSON 转义
 *   "offer" / "answer" 字段值
 *
 * p2p_signaling_payload_t 结构（序列化后 76 + N*32 字节）：
 *   - sender[32]:        发送方 local_peer_id（字符串）
 *   - target[32]:        目标方 local_peer_id（字符串）
 *   - candidate_count:   本次携带的候选数量（0-8）
 *   - candidates[N]:     ICE 候选（每个 32 字节）
 *     · type:            候选类型（host / srflx / relay）
 *     · addr:            地址和端口（网络序）
 *     · priority:        ICE 优先级
 *
 * DES 密钥派生：
 *   - 密钥来源：p2p_signal_pubsub_ctx_t.auth_key（由 p2p_config_t.auth_key 初始化时拷入）
 *   - 注意：DES 仅用于演示，生产环境应使用 AES-256-GCM
 *   - 若 auth_key 为空，使用默认值 0xAA*8（不安全，仅测试用）
 *
 * GitHub Gist API：
 *   - 读取：GET  https://api.github.com/gists/{gist_id}
 *           头部：Authorization: token {github_token}
 *                 If-None-Match: {etag}（条件请求）
 *   - 写入：PATCH https://api.github.com/gists/{gist_id}
 *           头部：Authorization: token {github_token}
 *                 Content-Type: application/json
 *           体：  {"files":{"p2p_signal.json":{"content":"{\"offer\":\"...\",\"answer\":\"...\"}"}}}
 *
 * 安全注意事项：
 *   - auth_token 需具备 GitHub Gist 读写权限（scope: gist）
 *   - channel_id 只允许字母、数字、连字符、下划线、点（防命令注入）
 *   - 当前使用 system() + curl，生产环境应替换为 libcurl API 调用
 */

#ifndef P2P_SIGNAL_PUBSUB_H
#define P2P_SIGNAL_PUBSUB_H

#include <stdint.h>
#include <netinet/in.h>

/* PUB 端轮询 answer 的间隔（毫秒）：尽快获取 answer，缩短建连延迟 */
#ifndef P2P_PUBSUB_PUB_POLL_MS
#define P2P_PUBSUB_PUB_POLL_MS  1000
#endif

/* SUB 端轮询 offer 的间隔（毫秒）：offer 写入后等待时间较长，无需频繁轮询 */
#ifndef P2P_PUBSUB_SUB_POLL_MS
#define P2P_PUBSUB_SUB_POLL_MS  5000
#endif

struct p2p_session;

/*
 * P2P 信令角色
 *
 * PUB（发起端）：主动写入 offer，等待对方写入 answer
 * SUB（订阅端）：轮询 offer，收到后自动写入 answer
 */
typedef enum {
    P2P_SIGNAL_ROLE_UNKNOWN = 0,
    P2P_SIGNAL_ROLE_PUB,            /* Publisher：发起端，写 offer / 读 answer */
    P2P_SIGNAL_ROLE_SUB             /* Subscriber：订阅端，读 offer / 写 answer */
} p2p_signal_role_t;

/*
 * PUBSUB 信令上下文
 *
 * 通过 p2p_signal_pubsub_init() 初始化，
 * 通过 p2p_signal_pubsub_set_role() 设置角色后方可使用。
 */
typedef struct {
    p2p_signal_role_t role;         /* 本端角色（PUB / SUB）*/
    char backend_url[256];          /* GitHub Gist API 基础 URL（保留扩展字段）*/
    char auth_token[128];           /* GitHub Personal Access Token */
    char auth_key[64];              /* DES 加密密钥（来自 p2p_config_t.auth_key）*/
    char channel_id[128];           /* Gist ID（作为信令通道标识）*/
    char etag[128];                 /* 上次读取 Gist 的 HTTP ETag，用于 304 条件请求 */
    uint64_t last_poll;             /* 上次轮询时间戳（毫秒），控制轮询间隔 */
    int answered;                   /* SUB 专用：是否已发送过 answer（防重复回应）*/
} p2p_signal_pubsub_ctx_t;

/*
 * 初始化 PUBSUB 信令上下文
 *
 * 必须在 p2p_signal_pubsub_set_role() 之前调用。
 *
 * @param ctx        信令上下文（调用者分配）
 * @param token      GitHub Personal Access Token（需具备 gist 读写权限）
 * @param channel_id Gist ID（仅允许字母、数字、连字符、下划线、点）
 * @return           0=成功，-1=失败（channel_id 格式非法）
 */
int  p2p_signal_pubsub_init(p2p_signal_pubsub_ctx_t *ctx, const char *token, const char *channel_id);

/*
 * 设置本端角色（PUB / SUB）
 *
 * 必须在 p2p_signal_pubsub_tick() / p2p_signal_pubsub_send() 之前调用。
 *
 * @param ctx   信令上下文
 * @param role  P2P_SIGNAL_ROLE_PUB 或 P2P_SIGNAL_ROLE_SUB
 */
void p2p_signal_pubsub_set_role(p2p_signal_pubsub_ctx_t *ctx, p2p_signal_role_t role);

/*
 * 周期调用：轮询 Gist，处理接收到的信令数据
 *
 * 应由主循环频繁调用（建议 ≤P2P_PUBSUB_PUB_POLL_MS）。内部通过 last_poll 控制实际轮询频率：
 *   - PUB 端：间隔 P2P_PUBSUB_PUB_POLL_MS ms（尽快获取 answer，缩短建连延迟）
 *   - SUB 端：间隔 P2P_PUBSUB_SUB_POLL_MS ms（offer 写入后等待时间较长，无需频繁轮询）
 *
 * PUB 端行为：
 *   - 轮询 Gist 的 "answer" 字段，若有更新则解密并注入远端 ICE 候选
 *
 * SUB 端行为：
 *   - 轮询 Gist 的 "offer" 字段，若有更新则解密、注入远端候选，
 *     并自动调用 p2p_signal_pubsub_send() 写入 answer（仅一次）
 *
 * @param ctx  信令上下文
 * @param s    P2P 会话对象（候选存储目标）
 */
void p2p_signal_pubsub_tick(p2p_signal_pubsub_ctx_t *ctx, struct p2p_session *s);

/*
 * 发送本端 ICE 候选到 Gist
 *
 * 将 data 经 DES 加密、Base64 编码后，PATCH 到 Gist 对应字段：
 *   - PUB 角色 --> 写入 "offer" 字段
 *   - SUB 角色 --> 写入 "answer" 字段
 *
 * 注意：内部通过 system() + curl 发起 HTTP 请求，写入前会读取现有 Gist
 * 内容以保留另一字段（避免覆盖对方数据）。
 *
 * @param ctx         信令上下文
 * @param target_name 目标 peer ID（当前版本保留，未使用）
 * @param data        待发送的原始二进制数据（p2p_signaling_payload_t 序列化结果）
 * @param len         数据长度（字节）
 * @return            0=成功，-1=失败（角色未设置、curl 失败等）
 */
int  p2p_signal_pubsub_send(p2p_signal_pubsub_ctx_t *ctx, const char *target_name, const void *data, int len);

#endif /* P2P_SIGNAL_PUBSUB_H */
