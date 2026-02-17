/*
 * SIMPLE 模式信令（UDP, 缓存配对机制 + 公网地址探测）
 *
 * ============================================================================
 * 协议概述
 * ============================================================================
 *
 * 实现简单的 UDP 信令协议，用于交换对端地址信息，包括双方的公网地址：
 *   - REGISTER:      向服务器注册自己的 ID 和初始候选地址
 *   - REGISTER_ACK:  服务器确认，返回对端状态和缓存能力
 *   - PEER_INFO:     序列化候选同步包（服务器转发 seq=1，后续 P2P 传输）
 *   - PEER_INFO_ACK: 候选接收确认，用于可靠传输控制
 *
 * ============================================================================
 * 候选列表序列化同步机制
 * ============================================================================
 *
 * 由于 UDP 包大小限制，候选列表需要分批传输。本实现通过序列化的
 * PEER_INFO 包完成可靠同步：
 *
 *   1. 注册阶段（仅发送一次）：
 *      - 客户端发送 REGISTER（含 UDP 包可容纳的最大候选列表）
 *      - 服务器回复 REGISTER_ACK（告知缓存能力 max_candidates）
 *        · max_candidates = 0: 不支持缓存
 *        · max_candidates > 0: 支持缓存，最大缓存数量
 *      - 收到 ACK 后停止 REGISTER，进入 REGISTERED 状态
 *
 *   2. 候选同步阶段（序列化 + 确认）：
 *      - 双方上线后，服务器发送 PEER_INFO(seq=1)，包含缓存的对端候选
 *      - 客户端收到后发送 PEER_INFO_ACK(seq=1) 确认
 *      - 客户端通过 PEER_INFO(seq=2,3,...) 继续同步剩余候选
 *      - 对端通过 PEER_INFO_ACK 确认，未确认则重发
 *
 *   3. 离线缓存流程：
 *
 *      Alice (在线)           Server                    Bob (离线)
 *        |                       |                          |
 *        |--- REGISTER --------->|                          |
 *        |<-- REGISTER_ACK ------|  (peer_online=0, max=5)
 *        |   [进入 REGISTERED]   |                          |
 *        |                       |  (缓存 Alice 的候选)      |
 *        |    ... Bob 上线 ...                              |
 *        |                       |<-- REGISTER ------------|
 *        |                       |--- REGISTER_ACK -------->|  (peer_online=1, max=5)
 *        |<-- PEER_INFO(seq=1) --|--- PEER_INFO(seq=1) --->|  (包含缓存的 5 个候选)
 *        |--- PEER_INFO_ACK(1) ->|<-- PEER_INFO_ACK(1) ----|
 *        |                       |                          |
 *        |<=============== P2P PEER_INFO 序列化同步 ========>|
 *        |--- PEER_INFO(seq=2, base=5) ----------------->  |  (从第 6 个候选开始)
 *        |<-- PEER_INFO_ACK(2) -------------------------    |
 *        |--- PEER_INFO(seq=3, base=10) ---------------->  |
 *        |<-- PEER_INFO_ACK(3) -------------------------    |
 *        |--- PEER_INFO(seq=4, count=0, FIN) ----------->  |  (结束标识)
 *        |<-- PEER_INFO_ACK(4) -------------------------    |
 *
 * ============================================================================
 * 状态机
 * ============================================================================
 *
 *   IDLE ──→ REGISTERING ──→ REGISTERED ──→ READY
 *                     │                           │
 *                     └───────────────────────────┘
 *                         (收到 PEER_INFO seq=1)
 *
 *   - IDLE:        未启动
 *   - REGISTERING: 已发送 REGISTER，等待 REGISTER_ACK
 *   - REGISTERED:  已收到 ACK，等待服务器 PEER_INFO(seq=1)
 *   - READY:       已收到 PEER_INFO，开始打洞并继续同步剩余候选
 *
 * 候选列表统一存储在 p2p_session 中，本模块只负责序列化和发送。
 */

#ifndef P2P_SIGNAL_SIMPLE_H
#define P2P_SIGNAL_SIMPLE_H

#include <p2p.h>
#include <netinet/in.h>

/* 前向声明 */
struct p2p_session;

/* ============================================================================
 * SIMPLE 模式消息格式
 * ============================================================================
 *
 * 候选地址使用 p2p_simple_candidate_t（定义在 p2pp.h），每个 7 字节。
 *
 * REGISTER:
 *   [local_peer_id(32)][remote_peer_id(32)][candidate_count(1)][candidates(N*7)]
 *
 * REGISTER_ACK:
 *   [status(1)][max_candidates(1)][public_ip(4)][public_port(2)]
 *   status: 0=成功/对端离线, 1=成功/对端在线, >=2=错误码
 *   max_candidates: 服务器为对端缓存的最大候选数量（0=不支持缓存）
 *   public_ip/port: 客户端的公网地址（服务器观察到的 UDP 源地址）
 *
 * PEER_INFO (seq 字段在包头 hdr.seq):
 *   [base_index(1)][candidate_count(1)][candidates(N*7)]
 *   - base_index: 本批候选的起始索引（0-based）
 *   - candidate_count: 本批候选数量，0 表示结束标识（FIN）
 *   - seq=1: 服务器发送，base_index=0，包含缓存的候选
 *   - seq>1: 客户端发送，base_index 递增
 *   - flags: 可包含 FIN 标志（0x01）表示候选列表发送完毕
 *
 * PEER_INFO_ACK:
 *   [ack_seq(2)][reserved(2)]
 *   - ack_seq: 确认的 PEER_INFO 序列号
 */

/* PEER_INFO flags */
#define P2P_PEER_INFO_FIN  0x01     /* 候选列表发送完毕 */

/* 信令状态 */
enum {
    SIGNAL_SIMPLE_IDLE = 0,         /* 未启动 */
    SIGNAL_SIMPLE_REGISTERING,      /* 等待 REGISTER_ACK */
    SIGNAL_SIMPLE_REGISTERED,       /* 已注册，等待 PEER_INFO(seq=1) */
    SIGNAL_SIMPLE_READY             /* 已收到 PEER_INFO，开始打洞并同步剩余候选 */
};

/* SIMPLE 信令上下文 */
typedef struct {
    int                 state;                              /* 信令状态 */
    struct sockaddr_in  server_addr;                        /* 信令服务器地址 */
    char                local_peer_id[P2P_PEER_ID_MAX];     /* 本端 ID */
    char                remote_peer_id[P2P_PEER_ID_MAX];    /* 对端 ID */
    uint64_t            last_send_time;                     /* 上次发送时间 */
    int                 verbose;                            /* 是否输出详细日志 */
    
    /* REGISTER_ACK 返回的信息 */
    uint8_t             peer_online;                        /* 对端是否在线 */
    uint8_t             max_remote_candidates;              /* 服务器为对端缓存的最大候选数（0=不支持） */
    struct sockaddr_in  public_addr;                        /* 本端的公网地址（服务器探测到的）*/
    
    /* REGISTER 重发控制（仅 REGISTERING 状态） */
    int                 register_attempts;                  /* REGISTER 重发次数 */
    
    /* PEER_INFO 序列化同步控制 */
    int                 candidates_per_packet;              /* 每个 PEER_INFO 包的候选数量 */
    int                 candidates_sent;                    /* 已发送的候选总数（base_index） */
    uint16_t            next_send_seq;                      /* 下一个要发送的 PEER_INFO 序列号 */
    uint16_t            last_acked_seq;                     /* 对方已确认的最大序列号 */
    uint8_t             remote_recv_complete;               /* 对端已接收完所有候选 */
    uint16_t            last_recv_seq;                      /* 已收到的最大 PEER_INFO 序列号 */
    uint8_t             local_send_complete;                /* 本端已发送完所有候选 */
} signal_simple_ctx_t;

/*
 * 初始化信令上下文
 */
void signal_simple_init(signal_simple_ctx_t *ctx);

/*
 * 开始信令交换（发送 REGISTER）
 *
 * @param s             会话对象（包含候选列表）
 * @param local_peer_id 本端 ID
 * @param remote_peer_id 对端 ID
 * @param server        服务器地址
 * @param verbose       是否输出详细日志
 * @return              0 成功，-1 失败
 */
int signal_simple_start(struct p2p_session *s, const char *local_peer_id, const char *remote_peer_id,
                        const struct sockaddr_in *server, int verbose);

/*
 * 周期调用，处理重发和候选同步
 *
 * - REGISTERING 状态：重发 REGISTER（直到收到 ACK）
 * - READY 状态：序列化发送剩余候选（PEER_INFO seq>1），处理重传
 *
 * @param s   会话对象
 * @return    0 正常，-1 错误
 */
int signal_simple_tick(struct p2p_session *s);

/*
 * 处理收到的信令包
 *
 * 支持的包类型：
 * - REGISTER_ACK:   服务器确认，提取对端状态和缓存能力
 * - PEER_INFO:      对端候选列表（序列化传输）
 * - PEER_INFO_ACK:  对端确认（停止重传对应序列号的包）
 *
 * @param s       会话对象
 * @param type    包类型
 * @param seq     包序列号（从包头提取）
 * @param flags   包标志位（从包头提取）
 * @param payload 负载数据
 * @param len     负载长度
 * @param from    发送方地址
 * @return        0 成功处理，-1 解析失败，1 未处理
 */
int signal_simple_on_packet(struct p2p_session *s, uint8_t type, uint16_t seq, uint8_t flags,
                            const uint8_t *payload, int len,
                            const struct sockaddr_in *from);

#endif /* P2P_SIGNAL_SIMPLE_H */

