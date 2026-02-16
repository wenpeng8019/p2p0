/*
 * SIMPLE 模式信令（UDP 无状态）
 *
 * ============================================================================
 * 协议概述
 * ============================================================================
 *
 * 实现简单的 UDP 信令协议，用于交换对端地址信息：
 *   - REGISTER:     向服务器注册自己的 ID 和候选地址
 *   - REGISTER_ACK: 服务器确认，返回对端状态和缓存能力
 *   - PEER_INFO:    从服务器接收对端的候选地址
 *   - ICE_CANDIDATES: 增量上报候选地址（当对端离线但服务器支持缓存时）
 *
 * ============================================================================
 * 与标准 ICE 的差异：离线候选缓存
 * ============================================================================
 *
 * 标准 ICE (RFC 5245) 假设双方同时在线进行候选交换。
 * 本实现支持对端离线时的服务器缓存：
 *
 *   1. REGISTER_ACK 返回对端状态：
 *      - peer_online = 1: 对端在线，立即收到候选
 *      - peer_online = 0, can_cache = 1: 对端离线，服务器缓存候选
 *      - peer_online = 0, can_cache = 0: 不支持，连接失败
 *
 *   2. 离线缓存流程：
 *
 *      Alice (在线)           Server                    Bob (离线)
 *        |                       |                          |
 *        |--- REGISTER --------->|                          |
 *        |<-- REGISTER_ACK ------|  (peer_online=0, can_cache=1)
 *        |                       |                          |
 *        |--- ICE_CANDIDATES --->|  (服务器缓存)
 *        |    ... 持续上报 ...   |                          |
 *        |                       |                          |
 *        |    ... Bob 上线 ...                              |
 *        |                       |                          |
 *        |<-- PEER_INFO ---------|<-- Bob's candidates -----|
 *        |                       |--- push Alice's cands -->|
 *
 * ============================================================================
 * 状态机
 * ============================================================================
 *
 *   IDLE ──→ REGISTERING ──→ REGISTERED ──→ READY
 *                │               │
 *                └───────────────┘
 *                    (收到 PEER_INFO)
 *
 *   - IDLE:        未启动
 *   - REGISTERING: 已发送 REGISTER，等待 REGISTER_ACK
 *   - REGISTERED:  已收到 ACK，对端离线，持续发送 ICE_CANDIDATES
 *   - READY:       已收到 PEER_INFO，可以开始打洞
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
 * REGISTER / ICE_CANDIDATES:
 *   [local_peer_id(32)][remote_peer_id(32)][candidate_count(1)][candidates(N*7)]
 *
 * REGISTER_ACK:
 *   [status(1)][flags(1)][reserved(2)]
 *   flags: P2P_REGACK_PEER_ONLINE | P2P_REGACK_CAN_CACHE | P2P_REGACK_CACHE_FULL
 *
 * PEER_INFO:
 *   [candidate_count(1)][candidates(N*7)]
 */

/* 信令状态 */
enum {
    SIGNAL_SIMPLE_IDLE = 0,       /* 未启动 */
    SIGNAL_SIMPLE_REGISTERING,    /* 等待 REGISTER_ACK */
    SIGNAL_SIMPLE_REGISTERED,     /* 已注册，对端离线，持续上报候选 */
    SIGNAL_SIMPLE_READY           /* 已收到 PEER_INFO，准备打洞 */
};

/* SIMPLE 信令上下文 */
typedef struct {
    int              state;                                 /* 信令状态 */
    struct sockaddr_in server_addr;                         /* 信令服务器地址 */
    char             local_peer_id[P2P_PEER_ID_MAX];        /* 本端 ID */
    char             remote_peer_id[P2P_PEER_ID_MAX];       /* 对端 ID */
    uint64_t         last_send_time;                        /* 上次发送时间 */
    int              verbose;                               /* 是否输出详细日志 */
    
    /* REGISTER_ACK 返回的信息 */
    uint8_t          peer_online;                           /* 对端是否在线 */
    uint8_t          server_can_cache;                      /* 服务器是否支持候选缓存 */
    uint8_t          cache_full;                            /* 候选缓存是否已满 */
    
    /* 重发控制 */
    int              register_attempts;                     /* REGISTER 重发次数 */
    int              cands_sent;                            /* 已发送的候选数量 */
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
int signal_simple_start(struct p2p_session *s, const char *local_peer_id,
                        const char *remote_peer_id,
                        const struct sockaddr_in *server, int verbose);

/*
 * 周期调用，处理重发和候选上报
 *
 * - REGISTERING 状态：重发 REGISTER
 * - REGISTERED 状态：发送 ICE_CANDIDATES（如果服务器支持缓存）
 *
 * @param s   会话对象
 * @return    0 正常，-1 错误
 */
int signal_simple_tick(struct p2p_session *s);

/*
 * 处理收到的信令包
 *
 * 支持的包类型：
 * - REGISTER_ACK: 服务器确认，提取对端状态
 * - PEER_INFO: 对端候选列表
 *
 * @param s       会话对象
 * @param type    包类型
 * @param payload 负载数据
 * @param len     负载长度
 * @param from    发送方地址
 * @return        0 成功处理，-1 解析失败，1 未处理
 */
int signal_simple_on_packet(struct p2p_session *s, uint8_t type,
                            const uint8_t *payload, int len,
                            const struct sockaddr_in *from);

#endif /* P2P_SIGNAL_SIMPLE_H */

