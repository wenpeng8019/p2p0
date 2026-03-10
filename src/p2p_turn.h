/*
 * TURN (Traversal Using Relays around NAT) 协议实现
 *
 * 参考文档:
 *   - RFC 5766: TURN - Traversal Using Relays around NAT
 *   - RFC 5389: STUN - Session Traversal Utilities for NAT (基础协议)
 *
 * ============================================================================
 * TURN 协议概述
 * ============================================================================
 *
 * TURN 是 STUN 的扩展协议，用于在 NAT 穿透失败时提供中继服务。
 * 当两个对等方无法直接建立 P2P 连接（如对称 NAT 情况）时，
 * TURN 服务器作为中继转发数据。
 *
 * 工作流程:
 *   1. 客户端向 TURN 服务器发送 Allocate Request
 *   2. 服务器分配一个 Relay 地址（中继地址）
 *   3. 客户端将 Relay 地址作为候选发送给对端
 *   4. 数据通过 TURN 服务器中继转发
 *
 * ============================================================================
 * STUN/TURN 消息格式（RFC 5389 Section 6）
 * ============================================================================
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |0 0|     STUN Message Type     |         Message Length        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         Magic Cookie                          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                                |
 * |                     Transaction ID (96 bits)                   |
 * |                                                                |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * 字段说明:
 *   - Message Type (14 bits): 消息类型，包含 Method 和 Class
 *   - Message Length (16 bits): 消息体长度（不包括 20 字节头部）
 *   - Magic Cookie (32 bits): 固定值 0x2112A442
 *   - Transaction ID (96 bits): 事务标识符，用于匹配请求/响应
 *
 * Message Type 编码:
 *   Bits:  M11 M10 M9 M8 M7 C1 M6 M5 M4 C0 M3 M2 M1 M0
 *   M bits: Method (方法)
 *   C bits: Class (00=Request, 01=Indication, 10=Success, 11=Error)
 *
 * ============================================================================
 * TURN 消息类型（RFC 5766 Section 13）
 * ============================================================================
 *
 * Method          | Request  | Indication | Success  | Error
 * ----------------|----------|------------|----------|----------
 * Allocate (0x03) | 0x0003   | -          | 0x0103   | 0x0113
 * Refresh (0x04)  | 0x0004   | -          | 0x0104   | 0x0114
 * Send (0x06)     | -        | 0x0016     | -        | -
 * Data (0x07)     | -        | 0x0017     | -        | -
 * CreatePerm(0x08)| 0x0008   | -          | 0x0108   | 0x0118
 * ChannelBind(0x9)| 0x0009   | -          | 0x0109   | 0x0119
 *
 * ============================================================================
 * STUN/TURN 属性格式（RFC 5389 Section 15）
 * ============================================================================
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         Attribute Type        |          Length               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         Value (variable)                      |
 * |                         .... (padded to 4-byte boundary)      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * 常用属性类型:
 *   0x0001  MAPPED-ADDRESS       映射地址（已废弃，用 XOR-MAPPED-ADDRESS）
 *   0x0006  USERNAME             用户名
 *   0x0008  MESSAGE-INTEGRITY    消息完整性（HMAC-SHA1）
 *   0x000D  LIFETIME             分配生存时间
 *   0x0014  REALM                认证域
 *   0x0015  NONCE                随机数
 *   0x0016  XOR-RELAYED-ADDRESS  XOR 编码的中继地址（TURN）
 *   0x0019  REQUESTED-TRANSPORT  请求的传输协议
 *   0x0020  XOR-MAPPED-ADDRESS   XOR 编码的映射地址
 *
 * ============================================================================
 * TURN REQUESTED-TRANSPORT 属性（RFC 5766 Section 14.7）
 * ============================================================================
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |    Protocol   |                    RFFU                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *   - Protocol: IANA 协议号（17 = UDP, 6 = TCP）
 *   - RFFU: 保留字段，必须为 0
 *
 * ============================================================================
 * XOR-RELAYED-ADDRESS / XOR-MAPPED-ADDRESS 属性格式
 * ============================================================================
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |x x x x x x x x|    Family     |           X-Port              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         X-Address (IPv4)                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *   - Family: 0x01 = IPv4, 0x02 = IPv6
 *   - X-Port: 端口 XOR Magic Cookie 高 16 位 (0x2112)
 *   - X-Address: 地址 XOR Magic Cookie (0x2112A442)
 *
 * 注意: 旧版 TURN 使用 RELAYED-ADDRESS (0x0016) 未经 XOR
 *       新版应使用 XOR-RELAYED-ADDRESS (0x0016)
 *
 * ============================================================================
 */

#ifndef P2P_TURN_H
#define P2P_TURN_H

#include <stdc.h>

struct p2p_session;

/* ============================================================================
 * TURN 状态机
 * ============================================================================
 *
 *  TURN_IDLE ──→ TURN_ALLOCATING ──→ (401 Unauthorized?)
 *                                          │
 *                                          ↓
 *                                    TURN_AUTHENTICATING ──→ TURN_ALLOCATED
 *                                          │                      │
 *                                          ↓                      ↓
 *                                     TURN_FAILED          (定期 Refresh)
 */
typedef enum {
    TURN_IDLE = 0,           // 未启动
    TURN_ALLOCATING,         // 首次 Allocate 已发送（无认证）
    TURN_AUTHENTICATING,     // 收到 401 后带认证重发中
    TURN_ALLOCATED,          // 分配成功
    TURN_FAILED              // 分配失败
} turn_state_t;

/* 最大中继权限数 */
#define TURN_MAX_PERMISSIONS 16

/* ============================================================================
 * TURN 会话上下文
 * ============================================================================ */
typedef struct {
    struct sockaddr_in  server_addr;                    // 已解析的 TURN 服务器地址
    struct sockaddr_in  relay_addr;                     // 已分配的中继地址
    uint8_t             tsx_id[12];                     // 当前事务 ID
    char                realm[128];                     // 认证域（401 响应中获取）
    char                nonce[128];                     // 随机数（401 响应中获取）
    uint8_t             key[16];                        // 长期凭证密钥 MD5(user:realm:pass)

    turn_state_t        state;                          // 当前状态
    bool                has_key;                        // 密钥是否已计算
    uint32_t            lifetime;                       // 分配生存时间（秒）
    uint64_t            alloc_time_ms;                  // 分配成功时间
    uint64_t            last_refresh_ms;                // 上次 Refresh 时间

    struct sockaddr_in  perms[TURN_MAX_PERMISSIONS];    // 已授权的对端地址（仅 IP 部分有意义）
    int                 perm_count;                     // 已授权数量
    int                 perm_cand_synced;               // 已同步 CreatePermission 的远端候选数
    uint64_t            last_perm_ms;                   // 上次 Permission 创建/刷新时间
} turn_ctx_t;

/* ============================================================================
 * API
 * ============================================================================ */

/* 初始化 TURN 上下文 */
void p2p_turn_init(turn_ctx_t *t);

/* 释放 TURN 分配（发送 Refresh lifetime=0）并清零上下文 */
void p2p_turn_reset(struct p2p_session *s);

/* 定时维护（权限同步、Refresh 续期） */
void p2p_turn_tick(struct p2p_session *s, uint64_t now_ms);

//-----------------------------------------------------------------------------

/* 发起 TURN Allocate 请求（首次无认证） */
int  p2p_turn_allocate(struct p2p_session *s);

/* 通过 TURN 中继发送数据（Send Indication，无需认证） */
int  p2p_turn_send_indication(struct p2p_session *s, const struct sockaddr_in *peer_addr,
                              const uint8_t *data, int len);

//-----------------------------------------------------------------------------

/*
 * 处理来自 TURN 服务器的响应
 *
 * 返回值:
 *   0 = 控制消息已处理（Allocate/Refresh/CreatePermission 响应）
 *   1 = Data Indication: 中继数据已提取到 out_data/out_len, 对端地址在 out_peer
 *  -1 = 非 TURN 消息（未处理）
 */
int  p2p_turn_handle_packet(struct p2p_session *s, const uint8_t *buf, int len,
                            const struct sockaddr_in *from,
                            const uint8_t **out_data, int *out_len,
                            struct sockaddr_in *out_peer);

///////////////////////////////////////////////////////////////////////////////
#endif /* P2P_TURN_H */
