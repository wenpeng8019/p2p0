/*
 * p2pp.h — P2P 信令协议定义
 *
 * 统一定义客户端和服务器使用的协议格式，包括：
 * - SIMPLE 模式 (UDP): 轻量级 NAT 穿透
 * - RELAY 模式 (TCP): 完整 ICE/SDP 交换
 */

#ifndef P2PP_H
#define P2PP_H

#include <stdint.h>
#include <p2p.h>

/* ============================================================================
 * SIMPLE 模式协议 (UDP)
 * ============================================================================
 *
 * 所有包共享 4 字节头: [type: u8 | flags: u8 | seq: u16]
 *
 * 包类型范围:
 *   0x01-0x0F: 信令协议
 *   0x10-0x1F: 打洞协议
 *   0x20-0x2F: 保活协议
 *   0x30-0x3F: 数据传输
 *   0x40-0x4F: 中继协议
 *   0x50-0x5F: 路由探测
 *   0x60-0x6F: 安全协议
 *
 * 候选列表同步流程:
 * 1. 客户端发送 REGISTER（含 UDP 包可容纳的最大候选列表）
 * 2. 服务器回复 REGISTER_ACK（告知远程缓存的候选数量限制）
 * 3. 双方上线后，服务器向双方发送 PEER_INFO(seq=1)，包含服务器缓存的对端候选
 * 4. 双方通过 PEER_INFO(seq=2,3,...)继续向对方同步剩余候选列表
 * 5. 每个 PEER_INFO 需要 PEER_INFO_ACK 确认，未确认则重发
 *
 * 注：REGISTER 仅在注册阶段发送，收到 REGISTER_ACK 后停止（直到重连）
 */

/* 信令协议 (与信令服务器通信) */
#define P2P_PKT_REGISTER        0x01    /* 注册到信令服务器（含本地候选列表） */
#define P2P_PKT_REGISTER_ACK    0x02    /* 注册确认（告知服务器缓存能力） */
#define P2P_PKT_PEER_INFO       0x03    /* 候选列表同步包（序列化传输） */
#define P2P_PKT_PEER_INFO_ACK   0x04    /* 候选列表确认（确认指定序列号） */
#define P2P_PKT_NAT_PROBE       0x05    /* NAT 类型探测请求（发往探测端口） */
#define P2P_PKT_NAT_PROBE_ACK   0x06    /* NAT 类型探测响应（返回第二次映射地址） */

/* 打洞协议 (NAT 穿透) */
#define P2P_PKT_PUNCH           0x10    /* NAT 打洞包 */
#define P2P_PKT_PUNCH_ACK       0x11    /* NAT 打洞确认 */

/* 保活协议 */
#define P2P_PKT_PING            0x20    /* 心跳请求 */
#define P2P_PKT_PONG            0x21    /* 心跳响应 */

/* 数据传输 (peer-to-peer) */
#define P2P_PKT_DATA            0x30    /* 数据包 */
#define P2P_PKT_ACK             0x31    /* 确认包 */
#define P2P_PKT_FIN             0x32    /* 结束包 (无需应答) */

/* 中继协议 */
#define P2P_PKT_RELAY_DATA      0x40    /* 中继服务器转发的数据 */

/* 路由探测 (同一子网) */
#define P2P_PKT_ROUTE_PROBE     0x50    /* 路由探测包 */
#define P2P_PKT_ROUTE_PROBE_ACK 0x51    /* 路由探测确认 */

/* 安全协议 */
#define P2P_PKT_AUTH            0x60    /* 安全握手包 */

/* REGISTER_ACK status 码 */
#define P2P_REGACK_PEER_OFFLINE  0   /* 成功，对端离线 */
#define P2P_REGACK_PEER_ONLINE   1   /* 成功，对端在线 */

/* SIMPLE 模式包头 (4 bytes) */
typedef struct {
    uint8_t  type;
    uint8_t  flags;
    uint16_t seq;                       /* 网络字节序 */
} p2p_packet_hdr_t;

/*
 * SIMPLE 模式精简候选结构 (7 bytes)
 * 用于 UDP 信令传输，省略了 priority 和 base_addr。
 * 
 * 布局: [type: 1B][ip: 4B][port: 2B]
 */
typedef struct {
    uint8_t  type;                      /* 候选类型 (0=Host, 1=Srflx, 2=Relay, 3=Prflx) */
    uint32_t ip;                        /* IP 地址（网络字节序） */
    uint16_t port;                      /* 端口（网络字节序） */
} __attribute__((packed)) p2p_simple_candidate_t;

/*
 * SIMPLE 模式消息格式:
 *
 * REGISTER:
 *   [local_peer_id(32)][remote_peer_id(32)][candidate_count(1)][candidates(N*7)]
 *
 * REGISTER_ACK:
 *   [status(1)][flags(1)][max_candidates(1)][reserved(1)]
 *   - flags: P2P_REGACK_PEER_ONLINE
 *   - max_candidates: 服务器为该对端缓存的最大候选数量（0=不支持缓存）
 *
 * PEER_INFO (seq 字段在包头 p2p_packet_hdr_t.seq):
 *   [base_index(1)][candidate_count(1)][candidates(N*7)]
 *   - base_index: 本批候选的起始索引（0-based）
 *   - candidate_count: 本批候选数量，0 表示结束标识
 *   - seq=1: 服务器发送，包含缓存的对端候选
 *   - seq>1: 客户端发送，继续同步剩余候选
 *   - flags: 可包含 P2P_PEER_INFO_FIN (0x01) 表示候选列表发送完毕
 *
 * PEER_INFO_ACK:
 *   [ack_seq(2)][reserved(2)]
 *   - ack_seq: 确认的 PEER_INFO 序列号
 */

/* PEER_INFO 标志位 */
#define P2P_PEER_INFO_FIN  0x01    /* 候选列表发送完毕 */

/* ============================================================================
 * RELAY 模式协议 (TCP)
 * ============================================================================
 *
 * 包头: [magic: 4B][type: 1B][length: 4B]
 * magic = 0x50325030 ("P2P0")
 */

#define P2P_RLY_MAGIC 0x50325030     /* "P2P0" */

/* RELAY 模式消息类型 */
typedef enum {
    P2P_RLY_LOGIN = 1,          /* 登录请求: Client -> Server */
    P2P_RLY_LOGIN_ACK,          /* 登录确认: Server -> Client */
    P2P_RLY_LIST,               /* 查询在线用户: Client -> Server */
    P2P_RLY_LIST_RES,           /* 在线用户列表: Server -> Client */
    P2P_RLY_CONNECT,            /* 发起连接: Client -> Server (携带 SDP/ICE) */
    P2P_RLY_OFFER,             /* 转发连接请求: Server -> Target */
    P2P_RLY_ANSWER,         /* 返回应答: Target -> Server */
    P2P_RLY_FORWARD,       /* 转发应答: Server -> Client */
    P2P_RLY_HEARTBEAT,          /* 心跳: Client -> Server */
    P2P_RLY_CONNECT_ACK         /* 连接确认: Server -> Client */
} p2p_relay_type_t;

/* RELAY 模式包头 (9 bytes) */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint8_t  type;
    uint32_t length;
} p2p_relay_hdr_t;

/* RELAY 模式登录消息 */
typedef struct {
    char name[P2P_PEER_ID_MAX];
} p2p_relay_login_t;

/*
 * RELAY 模式连接确认 (P2P_RLY_CONNECT_ACK)
 *
 * status:
 *   0 = 成功转发给目标
 *   1 = 目标不在线（已存储等待转发）
 *   2 = 存储失败（容量不足）
 *   3 = 服务器错误
 */
typedef struct {
    uint8_t  status;
    uint8_t  candidates_stored;
    uint8_t  reserved[2];
} p2p_relay_connect_ack_t;
#pragma pack(pop)

#endif /* P2PP_H */
