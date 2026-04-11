/*
 * P2P Zero transmission library (public API)
 */

#ifndef P2P_H
#define P2P_H
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#include <stddef.h>     // IWYU pragma: keep
#include <stdint.h>     // IWYU pragma: keep
#include <stdbool.h>    // IWYU pragma: keep

#ifdef __cplusplus
extern "C" {
#endif
///////////////////////////////////////////////////////////////////////////////

/* 支持的语言（已废弃，保留以兼容旧 API）
 * 新代码请使用 lang_load_fp() / lang_load_tx() 设置语言 */
typedef enum {
    P2P_LANG_EN = 0,                            // English (默认)
    P2P_LANG_CN = 1                             // 简体中文
} p2p_language_t;

/* 日志等级 */
typedef enum {
    P2P_LOG_LEVEL_NONE = 0,                     // 不输出
    P2P_LOG_LEVEL_FATAL,                        // 致命
    P2P_LOG_LEVEL_ERROR,                        // 错误：不可恢复的异常
    P2P_LOG_LEVEL_WARN,                         // 警告：可恢复的异常或降级
    P2P_LOG_LEVEL_INFO,                         // 信息：关键流程节点（默认）
    P2P_LOG_LEVEL_DEBUG,                        // 调试：内部状态变化
    P2P_LOG_LEVEL_VERBOSE,                      // 详细：极其详细的调试信息（性能敏感）
} p2p_log_level_t;

/*
 * 日志回调接口
 * level 日志等级
 * tag   模块名（可为 NULL/空）
 * txt   已格式化的日志正文
 *       这里的 txt 是可以写入变更的，但需要确保编辑的区域 <= len+1，例如增加一个 \n，或移除末尾的 \n（如果存在）
 */
typedef void (*p2p_log_callback_t)(p2p_log_level_t level, const char *tag, char *txt, int len);

/*
 * 信令模式
 */
typedef enum {
    P2P_SIGNALING_MODE_COMPACT = 0,             // 简单无状态信令（UDP，无登录，无需 STUN 服务）
    P2P_SIGNALING_MODE_RELAY,                   // ICE 有状态信令（TCP，需登录，基于 STUN/ICE 协议）
    P2P_SIGNALING_MODE_PUBSUB,                  // 发布/订阅模式（Gist 交换信令，无登录，需 STUN 服务）
    P2P_SIGNALING_MODE_ICE,                     // 标准 ICE 信令（APP，自定义，符合 RFC 5245）
} p2p_signaling_t;

/*
 * 连接状态
 */
typedef enum {
    P2P_STATE_INIT = 0,                         // 初始状态
    P2P_STATE_CLOSED,                           // 已关闭
    P2P_STATE_ERROR,                            // 错误状态（不可恢复，等价于自动关闭）
    P2P_STATE_SIGNALING,                        // 上线中（正在向信令服务器发送 ONLINE）
    P2P_STATE_WAITING,                          // 等待对端上线后开始候选同步（已在信令服务器上线）
    P2P_STATE_PUNCHING,                         // NAT 打洞中
    P2P_STATE_LOST,                             // 信号丢失（可能恢复，也可能不会，需要由应用层来决定断开或重连；NAT 和 中继都丢失了）
    P2P_STATE_CONNECTED,                        // 已连接（NAT 肯定处于 CONNECTED 状态）
    P2P_STATE_RELAY                             // 中继模式（NAT 未处于 CONNECTED 状态，且可中继）
} p2p_state_t;

/*
 * 信道外可达性探测状态
 * + 当 P2P 处于中继（RELAY）或断连（DISCONNECTED）状态时，
 *   库会通过信令服务器探测对端是否在线、信令信道是否可用。
 *   此状态反映最近一次探测的结论，与传输路径无关。
 */
typedef enum {
    // 前置状态（不探测的原因）
    P2P_PROBE_STATE_OFFLINE = 0,                // 本地离线，信令未就绪（初始状态）
    P2P_PROBE_STATE_CONNECTED,                  // P2P 已直连，无需探测
    P2P_PROBE_STATE_NO_SUPPORT,                 // 信令服务不支持探测（服务器不支持或协议不适用）
    
    // 探测流程状态（按执行顺序）
    P2P_PROBE_STATE_READY,                      // 就绪：信令已连接且服务器支持，可触发探测
    P2P_PROBE_STATE_RUNNING,                    // 探测进行中
    P2P_PROBE_STATE_TIMEOUT,                    // 探测超时（本地无法连接到服务器）
    P2P_PROBE_STATE_SUCCESS,                    // 探测成功（对端通过信令可达）
    P2P_PROBE_STATE_PEER_OFFLINE,               // 对端离线（服务器无法转发）
    P2P_PROBE_STATE_PEER_TIMEOUT                // 对端超时（服务器已转发，但对端无响应）
} p2p_probe_state_t;

/*
 * 连接路径
 */
typedef enum {
    P2P_PATH_NONE = 0,
    P2P_PATH_LAN,                               // 同一子网，直连
    P2P_PATH_PUNCH,                             // NAT 打洞
    P2P_PATH_RELAY,                             // 数据中继（TURN 服务器）
    P2P_PATH_SIGNALING                          // 信令服务器转发（最终降级方案）
} p2p_path_type_t;

/*
 * NAT 类型
 *
 * P2P 穿透难度（从易到难）：
 *   OPEN < FULL_CONE < RESTRICTED < PORT_RESTRICTED < BLOCKED
 *
 * 注：对称型 NAT (Symmetric NAT) 无法通过 STUN 单服务器检测，
 *     但 COMPACT 信令模式在服务器配置了 probe_port 时可通过两次映射对比检测出来
 * 以下类型需要两个独立 STUN 服务器对比，STUN 检测不支持：
 *   - 对称型 UDP 防火墙 (Symmetric UDP Firewall)：有公网 IP 但过滤出站方向
 */
typedef enum {
    P2P_NAT_UNKNOWN = 0,                        // 未知：检测尚未完成或未启动
    P2P_NAT_UNDETECTABLE,                       // 不支持检测：未配置 STUN 服务器；或使用 COMPACT 信令但服务器返回检测端口为 0
    P2P_NAT_BLOCKED,                            // UDP 不可达：无法联系 STUN 服务器（Test I 超时）
    P2P_NAT_ERROR,                              // 错误：检测过程中出现异常
    P2P_NAT_OPEN,                               // 无 NAT：有公网 IP，映射地址 == 本地地址
    P2P_NAT_FULL_CONE,                          // 完全锥形：最容易穿透（Test II 成功）
    P2P_NAT_RESTRICTED,                         // 受限锥形（Test III 成功，Test II 失败）
    P2P_NAT_PORT_RESTRICTED,                    // 端口受限锥形（Test I 成功，II/III 均失败）
    P2P_NAT_SYMMETRIC,                          // 对称型 NAT：不同目标端口映射到不同外部端口;
                                                // 仅 COMPACT 模式（服务器有 probe_port）可检测
} p2p_nat_type_t;

/*
 * p2p_nat_type() 的负值返回状态（检测尚在进行中时使用）
 *
 * 当返回值 < 0 时，表示检测处于瞬态，尚未得出最终结果；
 * 当返回值 >= 0 时，可直接强转为 p2p_nat_type_t 读取检测结论
 */
#define P2P_NAT_DETECTING    (-1)               // 检测进行中：已发出请求，等待服务器响应
#define P2P_NAT_TIMEOUT      (-2)               // 检测超时：注册或探测无响应，服务器不可达

/* ---------- p2p 句柄定义 ---------- */

typedef const void* p2p_handle_t;
typedef const void* p2p_session_t;

/* ---------- 事件回调类型 ---------- */

/*
 * 连接状态变化回调
 *
 * 当连接状态发生变化时触发，覆盖所有关键状态转换：
 *   - CONNECTED: 连接建立
 *   - RELAY: 切换到中继模式
 *   - LOST: 连接丢失但可能恢复（NAT 超时且无 relay 可用）
 *   - CLOSED: 连接关闭
 *
 * 参数：
 *   session: 会话对象
 *   old_state: 前一状态
 *   new_state: 当前状态
 *   userdata: 用户自定义数据
 */
typedef void (*p2p_on_state_fn)(p2p_session_t session, p2p_state_t old_state, p2p_state_t new_state, void *userdata);

/*
 * 数据到达回调（可选，如果未设置则需要主动调用 p2p_recv）
 * 参数：
 *   session: 会话对象
 *   data: 数据缓冲区
 *   len: 数据长度
 *   userdata: 用户自定义数据
 */
typedef void (*p2p_on_data_fn)(p2p_session_t session, const void *data, int len, void *userdata);

/*
 * MSG RPC 请求到达回调（B 端，服务器把 A 的 MSG_REQ 中转给 B 时触发）
 * - sid   : 序列号，B 调用 p2p_response(session, sid, ...) 时传回
 * - msg   : 消息类型（1 字节，由 A 端指定）
 *           msg=0: Echo 请求，已由底层自动回复，不会触发此回调
 *           msg>0: 应用层自定义消息类型，需调用 p2p_response() 回复
 * - data/len : 请求数据
 */
typedef void (*p2p_on_request_fn)(p2p_session_t session, uint16_t sid,
                                  uint8_t msg, const void *data, int len,
                                  void *userdata);

/*
 * MSG RPC 应答到达回调（A 端，服务器把 B 的 MSG_RESP 转回给 A 时触发）
 * - sid   : 对应的原始请求序列号
 * - msg   : 响应码（协议中称为 code，由 B 在 p2p_response 中指定）
 *           正常响应时表示状态码，失败时表示错误类型
 * - data/len : 应答数据；len=-1 表示失败，具体错误见 msg 字段：
 *              msg=原始请求msg: B 不在线（在 REQ_ACK 阶段已知，立即失败）
 *              msg=P2P_MSG_ERR_PEER_OFFLINE (0xFF): B 在等待响应期间离线
 *              msg=P2P_MSG_ERR_TIMEOUT (0xFE): 服务器向 B 转发请求超时
 */
typedef void (*p2p_on_response_fn)(p2p_session_t session, uint16_t sid,
                                   uint8_t msg, const void *data, int len,
                                   void *userdata);
#define P2P_MSG_ERR_TIMEOUT         0xFE    // 服务器转发请求超时
#define P2P_MSG_ERR_PEER_OFFLINE    0xFF    // B端在等待响应期间离线（区别于 REQ_ACK 时已知离线）

/*
 * ICE 候选收集回调（仅 P2P_SIGNALING_MODE_ICE 模式，类似 WebRTC onicecandidate）
 *
 * 当收集到新的 ICE 候选时触发，用于 Trickle ICE 场景。
 * 应用层通过自定义信令通道（如 WebSocket）将候选发送给对端。
 *
 * 参数：
 *   session: 会话对象
 *   candidate: 候选字符串（WebRTC 格式，无 "a=" 前缀和 "\r\n" 后缀）
 *              格式示例: "candidate:1 1 UDP 2130706431 192.168.1.10 54320 typ host"
 *              如果 candidate == NULL，表示候选收集完成（对应 WebRTC 的 event.candidate == null）
 *   userdata: 用户自定义数据
 *
 * 典型用法：
 *   void on_ice_candidate(p2p_session_t session, const char *candidate, void *userdata) {
 *       if (candidate) {
 *           // 通过 WebSocket 发送给对端
 *           websocket_send_candidate(candidate);
 *       } else {
 *           // 候选收集完成
 *           websocket_send_candidate_complete();
 *       }
 *   }
 */
typedef void (*p2p_on_ice_candidate_fn)(p2p_session_t session, const char *candidate, void *userdata);

/* ---------- 配置结构 ---------- */

typedef struct {
    uint16_t                bind_port;                  // 本地 UDP 端口 (0 = any)
    bool                    multi_session;              // 允许单个实例管理多个并发会话（默认关闭）
    
    /* 信令配置 */
    p2p_signaling_t         signaling_mode;             // P2P_SIGNALING_MODE_* (连接时使用的信令模式)
    const char*             server_host;                // 信令服务器主机名 (用于 COMPACT/RELAY 模式)
    uint16_t                server_port;                // 信令服务器端口
    const char*             gh_token;                   // GitHub Token (用于 Gist API)
    const char*             gist_id;                    // Gist ID (用于 PUB/SUB 模式)
    
    /* 协议选择 */
    bool                    use_ice;                    // false = (使用私有协议 PUNCH/REACH 打洞)，
                                                        // true = ICE 标准协议 (RFC 5245，使用 STUN connectivity check) 打洞
    
    /* STUN/TURN 配置 (仅当 use_ice=true 或需要高级诊断时使用) */
    const char*             stun_server;                // STUN 服务器 (例如 stun.l.google.com)
    uint16_t                stun_port;
    bool                    skip_stun_pending;          // 建立连接时，无需等待 srflx 候选地址通过 stun 收集完成
    bool                    skip_stun_test;             // 跳过 NAT 类型检测（RFC 3489 Test II/III）
                                                        // 大多数公共 STUN 服务器不支持 CHANGE-REQUEST
                                                        // 设为 true 可跳过这些会超时的测试

    const char*             turn_server;                // TURN 服务器
    uint16_t                turn_port;
    const char*             turn_user;                  // TURN 认证用户
    const char*             turn_pass;                  // TURN 认证密码

    /* TCP 选项 */
    bool                    enable_tcp;                 // 是否尝试 TCP 打洞/回退
    uint16_t                tcp_port;                   // TCP 监听端口 (0 = any)

    /* 传输层 */
    bool                    use_pseudotcp;              // 是否启用拥塞控制 (AIMD)
    bool                    use_sctp;                   // 是否启用 usrsctp (SCTP)

    /* 加密层（与传输层正交，管加密） */
    int                     dtls_backend;               // 0=disabled, 1=mbedtls, 2=openssl
    int                     dtls_role;                  // DTLS 握手角色：
                                                        //   0 = 自动（默认）：按 peer_id 字典序决定，
                                                        //       ID 较大者为 server（被动方），较小者为 client（主动方）；
                                                        //       被动监听方（remote_peer_id 为空）始终为 server
                                                        //   1 = 强制 server（握手被动方，等待 ClientHello）
                                                        //   2 = 强制 client（握手主动方，发送 ClientHello）

    /* 其他选项 */
    bool                    threaded;                   // false = 手动更新, true = 内部线程
    int                     update_interval_ms;         // 内部线程更新间隔 (默认 10)
    bool                    nagle;                      // 是否启用 Nagle 批处理 (默认 0)
    const char*             auth_key;                   // 安全握手密钥 (可选)
    
    /* 语言选项（已废弃，保留字段以兼容旧 API） */
    p2p_language_t          language;                   // 旧系统语言选项，已无效；请用 lang_load_fp() 控制语言
    
    /* 路径管理策略 */
    int                     path_strategy;              // 路径选择策略：0=直连优先，1=性能优先，2=混合模式（默认0）
    
    /* 事件回调 */
    p2p_on_state_fn         on_state;                   // 状态变化回调 (可选)
    p2p_on_data_fn          on_data;                    // 数据到达回调 (可选)
    p2p_on_request_fn       on_request;                 // MSG RPC 请求到达（B 端，服务器可选）
    p2p_on_response_fn      on_response;                // MSG RPC 应答到达（A 端，服务器可选）
    p2p_on_ice_candidate_fn on_ice_candidate;           // ICE 候选收集回调（仅 ICE 模式，类似 WebRTC onicecandidate）
    void*                   userdata;                   // 用户自定义数据，传递给回调函数

    /* 测试选项 */
    bool                    test_ice_prflx_off;         // 禁用 Prflx 候选（不收集对等方地址）
    bool                    test_ice_host_off;          // 禁用 Host 候选（不收集本地网卡地址）
    bool                    test_ice_srflx_off;         // 禁用 Srflx 候选（不收集 NAT 反射地址）
    bool                    test_ice_relay_off;         // 禁用 Relay 候选（不收集 TURN 中继地址）

} p2p_config_t;

extern p2p_log_callback_t   p2p_log_callback;
extern p2p_log_level_t      p2p_log_level;
extern bool                 p2p_log_pre_tag;
#ifndef NDEBUG
extern uint16_t             p2p_instrument_base;        // instrument 选项基址（0-65535），用于设定该模块的选项索引的起始值，避免与其他模块冲突
#endif

///////////////////////////////////////////////////////////////////////////////

/**
 * 创建一个新的 P2P 会话。
 * @param local_peer_id 本端身份标识
 * @param cfg 配置参数
 * @return 会话句柄，失败则返回 NULL
 */
p2p_handle_t
p2p_create(const char *local_peer_id, const p2p_config_t *cfg);

/**
 * 销毁会话并释放所有资源。
 * 如果运行中则停止内部线程。
 */
void
p2p_destroy(p2p_handle_t hdl);

/**
 * 驱动会话状态机 (单线程模式)
 * 必须周期性调用（例如每 10 毫秒一次）从应用程序事件循环
 * 在线程模式下，这将被内部调用
 * 如果成功则返回 0，失败则返回 -1。
 */
int
p2p_update(p2p_handle_t hdl);

/**
 * 获取本地 NAT 类型（由 STUN 检测得出，仅在 use_ice=true 时自动检测）。
 *
 * 返回值含义：
 *
 *   负值（检测瞬态，尚未完成）：
 *     P2P_NAT_DETECTING  (-1)  检测进行中，已发送请求，等待响应
 *     P2P_NAT_TIMEOUT    (-2)  检测超时，注册或探测无服务器响应
 *
 *   >= 0（检测已结束，可强转为 p2p_nat_type_t）：
 *     P2P_NAT_UNKNOWN        (0)  检测未启动
 *     P2P_NAT_OPEN           (1)  无 NAT / 公网直连
 *     P2P_NAT_FULL_CONE      (2)  完全锥形NAT
 *     P2P_NAT_RESTRICTED     (3)  受限锥形NAT
 *     P2P_NAT_PORT_RESTRICTED(4)  端口受限锥形NAT
 *     P2P_NAT_SYMMETRIC      (5)  对称型NAT（仅COMPACT+probe_port可检测）
 *     P2P_NAT_BLOCKED        (6)  UDP 不可达（STUN 服务器无响应）
 *     P2P_NAT_UNDETECTABLE   (7)  不支持检测（无STUN配置 / 信令模式不支持）
 */
int
p2p_nat_type(p2p_handle_t hdl);

//-----------------------------------------------------------------------------

/**
 * 向远程对等体发起连接
 * 
 * 参数：
 *   hdl 会话对象
 *   remote_peer_id: 远程对等体标识（可为 NULL，取决于模式）
 * 
 * 信令模式由 cfg.signaling_mode 决定，remote_peer_id 的使用规则如下：
 * 
 * 1. COMPACT 模式（简单信令，UDP）
 *    - remote_peer_id: 必须非 NULL，指定对方的明确 local_peer_id
 *    - 原理：双方各自向服务器注册 pair<local_id, remote_id> 映射，服务器匹配成功后返回对方地址，开始 NAT 打洞
 *    - cfg 配置要求：server_host, server_port
 *    - 示例：p2p_connect(s, "bob")  // 连接到 bob
 * 
 * 2. RELAY 模式（有状态信令，TCP）
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
p2p_session_t
p2p_connect(p2p_handle_t hdl, const char *remote_peer_id);

/**
 * 发起优雅关闭。
 */
void
p2p_close(p2p_session_t session);

/**
 * 获取当前连接状态
 */
p2p_state_t
p2p_state(p2p_session_t session);

/**
 * 获取信道外可达性探测状态。
 *
 * - P2P 已直连时（CONNECTED），直接返回 P2P_PROBE_STATE_CONNECTED，无需探测。
 * - 处于中继或断连状态时，返回当前探测进度或最近一次探测结论。
 * - 探测由库自动驱动，无需手动触发。
 */
p2p_probe_state_t
p2p_probe(p2p_session_t session);

///////////////////////////////////////////////////////////////////////////////

/**
 * 判断会话是否已经建立，即确定建立连接，允许进行 I/O 操作
 */
bool
p2p_is_ready(p2p_session_t session);
 
/*
 * 获取当前连接路径 (P2P_PATH_* 枚举)。
 */
int
p2p_path(p2p_session_t session);

/*
 * 发送数据 (字节流语义，类似于 TCP send)。
 * 数据被缓冲、分片并可靠地发送。
 * 返回接受的字节数（可能小于 len），或 -1 表示错误。
 */
int
p2p_send(p2p_session_t session, const void *buf, int len);

/*
 * 接收数据 (字节流语义，类似于 TCP recv)。
 * 返回读取的字节数（如果无数据则为 0），或 -1 表示错误。
 */
int
p2p_recv(p2p_session_t session, void *buf, int len);

//-----------------------------------------------------------------------------

/**
 * 通过信令服务器向对端发送 MSG 请求（A 端）。
 *
 * 仅在 COMPACT 模式、服务器支持 MSG（ONLINE_ACK flags 含 SIG_ONACK_FLAG_MSG）
 * 且当前无挂起请求时有效。发送成功后通过 on_response 回调接收应答。
 *
 * @param session   会话对象
 * @param msg       消息ID（1 字节，应用自定义）
 * @param data      请求数据（最多 P2P_MSG_DATA_MAX 字节）
 * @param len       数据长度
 * @return 0=已加入发送队列，-1=失败（不支持/已有挂起/参数错误/未注册）
 */
int
p2p_request(p2p_session_t session, uint8_t msg, const void *data, int len);

/**
 * 回复对端的 MSG 请求（B 端，在 on_request 回调中或异步调用）。
 *
 * @param session   会话对象
 * @param code      应答码（1 字节）
 * @param data      应答数据
 * @param len       数据长度
 * @return 0=成功，-1=失败（无挂起请求/参数错误）
 */
int
p2p_response(p2p_session_t session, uint8_t code, const void *data, int len);

///////////////////////////////////////////////////////////////////////////////

/**
 * 获取本地候选数量
 *
 * @param session       会话对象
 * @return              本地候选数量
 */
int
p2p_local_candidate_count(p2p_session_t session);

/**
 * 获取远程候选数量
 *
 * @param session       会话对象
 * @return              本地候选数量
 */
int
p2p_remote_candidate_count(p2p_session_t session);

//-----------------------------------------------------------------------------
// ICE / SDP 接口（用于 P2P_SIGNALING_MODE_ICE 模式）
//-----------------------------------------------------------------------------

/**
 * 导出单个 ICE 候选为 WebRTC 格式字符串（无 "a=" 前缀和 "\r\n" 后缀）
 *
 * 用于 Trickle ICE 场景，每收集一个候选立即发送给对端。
 *
 * @param session       会话对象
 * @param cand_index    候选索引（0 ~ local_cand_cnt-1）
 * @param buf           输出缓冲区（建议至少 256 字节）
 * @param buf_size      缓冲区大小
 * @return              生成的字符串长度（不含 \0），失败返回 -1
 *
 * 输出示例："candidate:1 1 UDP 2130706431 192.168.1.10 54320 typ host"
 */
int
p2p_export_ice_candidate(p2p_session_t session, int cand_index, char *buf, int buf_size);

/**
 * 导出多个 ICE 候选为 SDP 格式（带 "a=" 前缀和 "\r\n" 后缀）
 * 
 * 支持两种模式：
 * 1. 仅生成候选行（candidates_only=true）：嵌入已有 SDP
 * 2. 生成完整 SDP（candidates_only=false）：包含 v=, o=, s=, t=, m=, ice-ufrag, ice-pwd, fingerprint, candidates
 * 
 * @param session           会话对象
 * @param sdp_buf           输出缓冲区
 *                          - 仅候选：建议 2048 字节
 *                          - 完整 SDP：建议 4096 字节
 * @param buf_size          缓冲区大小
 * @param candidates_only     1=生成完整 SDP, 0=仅生成候选行
 * @param ice_ufrag         ICE 用户名片段（仅 candidates_only=false 时需要，否则传 NULL）
 * @param ice_pwd           ICE 密码（仅 candidates_only=false 时需要，否则传 NULL）
 * @param dtls_fingerprint  DTLS 指纹（可选，格式："sha-256 AB:CD:..."，可传 NULL）
 * @return                  生成的 SDP 字符串总长度，失败返回 -1
 * 
 * 使用示例：
 *   // 仅候选
 *   int len = p2p_export_ice_sdp(hdl, buf, size, true, NULL, NULL, NULL);
 * 
 *   // 完整 SDP
 *   int len = p2p_export_ice_sdp(hdl, buf, size, false,
 *                                        "aB3d", "Xy7zK9pLm3nO1qW2", 
 *                                        "sha-256 AB:CD:EF:...");
 */
int
p2p_export_ice_sdp(p2p_session_t session, char *sdp_buf, int buf_size,
                   bool candidates_only,
                   const char *ice_ufrag,
                   const char *ice_pwd,
                   const char *dtls_fingerprint);

/**
 * 从 SDP 文本解析远端 ICE 候选（支持 "a=candidate:" 行）
 * 
 * @param session       会话对象
 * @param sdp_text      SDP 文本（多行，每行一个候选）
 * @return              解析并添加的候选数量，失败返回 -1
 * 
 * SDP 格式示例：
 *   "a=candidate:1 1 UDP 2130706431 192.168.1.10 54320 typ host\r\n"
 *   "a=candidate:2 1 UDP 1694498815 203.0.113.77 54320 typ srflx raddr 192.168.1.10 rport 54320\r\n"
 */
// int
// p2p_import_ice_sdp(p2p_handle_t hdl, const char *sdp_text);
int
p2p_import_ice_sdp(p2p_session_t session, const char *sdp_text);


///////////////////////////////////////////////////////////////////////////////

void
p2p_print(p2p_session_t session);

///////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
}
#endif

#pragma ide diagnostic pop
#pragma clang diagnostic pop
#endif /* P2P_H */

