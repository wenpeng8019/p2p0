/*
 * P2P Zero transmission library (public API)
 */

#ifndef P2P_H
#define P2P_H

#include <stddef.h>     // IWYU pragma: keep
#include <stdint.h>     // IWYU pragma: keep
#include <stdbool.h>    // IWYU pragma: keep

#ifdef __cplusplus
extern "C" {
#endif
///////////////////////////////////////////////////////////////////////////////


/* ---------- 连接模式 ---------- */

enum {
    P2P_SIGNALING_MODE_SIMPLE = 0,              // 简单无状态信令（UDP，无登录）
    P2P_SIGNALING_MODE_ICE,                     // ICE 有状态信令（TCP，需登录）
    P2P_SIGNALING_MODE_PUBSUB                   // 发布/订阅模式（Gist/KV 存储，角色由 remote_peer_id 决定）
};

/* ---------- 连接状态 ---------- */

enum {
    P2P_STATE_IDLE = 0,                         // 初始状态
    P2P_STATE_REGISTERING,                      // 注册到信令服务器
    P2P_STATE_PUNCHING,                         // NAT 打洞中
    P2P_STATE_CONNECTED,                        // 已连接
    P2P_STATE_RELAY,                            // 中继模式
    P2P_STATE_CLOSING,                          // 关闭中
    P2P_STATE_CLOSED,                           // 已关闭
    P2P_STATE_ERROR                             // 错误状态
};

/* ---------- 连接路径 (如何通信) ---------- */

enum {
    P2P_PATH_NONE = 0,
    P2P_PATH_LAN,                               // 同一子网，直连
    P2P_PATH_PUNCH,                             // NAT 打洞
    P2P_PATH_RELAY                              // 服务器中继（fallback）
};

/* ---------- 配置 ---------- */

/* 最大 peer ID 长度 */
#define P2P_PEER_ID_MAX  32

/* 前向声明 */
typedef struct p2p_session p2p_session_t;

/* ---------- 事件回调类型 ---------- */

/*
 * 连接建立回调
 * 参数：
 *   session: 会话对象
 *   userdata: 用户自定义数据
 */
typedef void (*p2p_on_connected_fn)(p2p_session_t *session, void *userdata);

/*
 * 连接断开回调
 * 参数：
 *   session: 会话对象
 *   userdata: 用户自定义数据
 */
typedef void (*p2p_on_disconnected_fn)(p2p_session_t *session, void *userdata);

/*
 * 数据到达回调（可选，如果未设置则需要主动调用 p2p_recv）
 * 参数：
 *   session: 会话对象
 *   data: 数据缓冲区
 *   len: 数据长度
 *   userdata: 用户自定义数据
 */
typedef void (*p2p_on_data_fn)(p2p_session_t *session, const void *data, int len, void *userdata);

/* ---------- 配置结构 ---------- */

typedef struct {
    uint16_t                bind_port;                  // 本地 UDP 端口 (0 = any)
    char                    peer_id[P2P_PEER_ID_MAX];   // 本端身份
    
    /* 信令配置 */
    int                     signaling_mode;             // P2P_SIGNALING_MODE_* (连接时使用的信令模式)
    const char*             server_host;                // 信令服务器主机名 (用于 SIMPLE/ICE 模式)
    uint16_t                server_port;                // 信令服务器端口
    const char*             gh_token;                   // GitHub Token (用于 Gist API)
    const char*             gist_id;                    // Gist ID (用于 PUB/SUB 模式)
    
    /* 协议选择 */
    bool                    use_ice;                    // false = SIMPLE (私有协议), true = ICE (RFC 5245)
    
    /* STUN/TURN 配置 (仅当 use_ice=true 或需要高级诊断时使用) */
    const char*             stun_server;                // STUN 服务器 (例如 stun.l.google.com)
    uint16_t                stun_port;
    const char*             turn_server;                // TURN 服务器
    uint16_t                turn_port;
    const char*             turn_user;                  // TURN 认证用户
    const char*             turn_pass;                  // TURN 认证密码

    /* TCP 选项 */
    bool                    enable_tcp;                 // 是否尝试 TCP 打洞/回退
    uint16_t                tcp_port;                   // TCP 监听端口 (0 = any)

    /* 传输质量 */
    bool                    use_pseudotcp;              // 是否启用拥塞控制 (AIMD)
    bool                    use_dtls;                   // 是否启用 MbedTLS DTLS
    bool                    use_openssl;                // 是否启用 OpenSSL DTLS
    bool                    use_sctp;                   // 是否启用 usrsctp (SCTP)
    bool                    dtls_server;                // 是否作为 DTLS 服务端 (握手被动方)

    /* 其他选项 */
    bool                    threaded;                   // false = 手动更新, true = 内部线程
    int                     update_interval_ms;         // 内部线程更新间隔 (默认 10)
    bool                    nagle;                      // 是否启用 Nagle 批处理 (默认 0)
    const char*             auth_key;                   // 安全握手密钥 (可选)
    
    /* 测试选项 */
    bool                    disable_lan_shortcut;       // 是否禁止同子网直连优化 (用于测试 NAT 打洞)
    bool                    verbose_nat_punch;          // 是否输出详细的 NAT 打洞流程日志
    
    /* 事件回调 */
    p2p_on_connected_fn     on_connected;               // 连接建立回调 (可选)
    p2p_on_disconnected_fn  on_disconnected;            // 连接断开回调 (可选)
    p2p_on_data_fn          on_data;                    // 数据到达回调 (可选)
    void*                   userdata;                   // 用户自定义数据，传递给回调函数
} p2p_config_t;

///////////////////////////////////////////////////////////////////////////////

/*
 * 创建一个新的 P2P 会话。
 * 如果失败则返回 NULL。
 */
p2p_session_t *p2p_create(const p2p_config_t *cfg);

/*
 * 销毁会话并释放所有资源。
 * 如果运行中则停止内部线程。
 */
void p2p_destroy(p2p_session_t *s);

/*
 * 向远程对等体发起连接
 * 
 * 参数：
 *   s: session 会话对象
 *   remote_peer_id: 远程对等体标识（可为 NULL，取决于模式）
 * 
 * 信令模式由 cfg.signaling_mode 决定，remote_peer_id 的使用规则如下：
 * 
 * 1. SIMPLE 模式（简单信令，UDP）
 *    - remote_peer_id: 必须非 NULL，指定对方的明确 peer_id
 *    - 原理：双方各自向服务器注册 pair<local_id, remote_id> 映射，服务器匹配成功后返回对方地址，开始 NAT 打洞
 *    - cfg 配置要求：server_host, server_port
 *    - 示例：p2p_connect(s, "bob")  // 连接到 bob
 * 
 * 2. ICE 模式（有状态信令，TCP）
 *    - remote_peer_id: 可为 NULL 或非 NULL
 *      * 非 NULL: 主动发起，连接到指定目标（发送 offer）
 *      * NULL: 被动等待，接受来自任意对等方的连接（等待 offer），等价于登录信令服务器
 *    - 原理：通过 TCP 长连接登录信令服务器，服务器转发 ICE 候选者，支持一对多场景（服务器可转发多个对等方的消息）
 *    - cfg 配置要求：server_host, server_port
 *    - 示例：
 *      p2p_connect(s, "bob")  // 主动连接 bob
 *      p2p_connect(s, NULL)   // 被动等待任意连接
 * 
 * 3. PUBSUB 模式（公共信令，Gist/KV 存储）
 *    - remote_peer_id: 可为 NULL 或非 NULL，决定角色
 *      * 非 NULL: PUB 角色（发布者），主动发布 offer 到 Gist
 *      * NULL: SUB 角色（订阅者），监听 Gist 中的 offer 并自动回复 answer
 *    - 原理：使用 GitHub Gist 或类似的 KV 存储作为信令中介，发布者写入 offer，订阅者读取并回复 answer
 *    - cfg 配置要求：gh_token, gist_id
 *    - 示例：
 *      p2p_connect(s, "bob")  // PUB 模式，发布 offer 等待 bob 的 answer
 *      p2p_connect(s, NULL)   // SUB 模式，监听任意 offer 并回复 answer
 * 
 * 返回：0 = 成功，-1 = 失败
 */
int p2p_connect(p2p_session_t *s, const char *remote_peer_id);

/*
 * 发起优雅关闭。
 */
void p2p_close(p2p_session_t *s);

/*
 * 驱动会话状态机 (单线程模式)
 * 必须周期性调用（例如每 10 毫秒一次）从应用程序事件循环
 * 在线程模式下，这将被内部调用
 * 如果成功则返回 0，失败则返回 -1。
 */
int p2p_update(p2p_session_t *s);

///////////////////////////////////////////////////////////////////////////////

/*
 * 发送数据 (字节流语义，类似于 TCP send)。
 * 数据被缓冲、分片并可靠地发送。
 * 返回接受的字节数（可能小于 len），或 -1 表示错误。
 */
int p2p_send(p2p_session_t *s, const void *buf, int len);

/*
 * 接收数据 (字节流语义，类似于 TCP recv)。
 * 返回读取的字节数（如果无数据则为 0），或 -1 表示错误。
 */
int p2p_recv(p2p_session_t *s, void *buf, int len);

/*
 * 获取当前连接状态 (P2P_STATE_* 枚举)。
 */
int p2p_state(const p2p_session_t *s);

/*
 * 获取当前连接路径 (P2P_PATH_* 枚举)。
 */
int p2p_path(const p2p_session_t *s);

/*
 * 判断会话是否已经建立，即确定建立连接，允许进行 I/O 操作
 */
int  p2p_is_ready(p2p_session_t *sa);


///////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
}
#endif

#endif /* P2P_H */
