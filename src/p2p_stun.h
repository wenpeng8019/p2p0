/*
 * STUN 协议支持与 NAT 类型检测
 *
 * STUN (Session Traversal Utilities for NAT) 协议
 * RFC 5389 (STUN)  - 基础 STUN 协议
 * RFC 3489 (Classic STUN) - NAT 类型检测
 *
 * ============================================================================
 * STUN 协议的两大核心功能
 * ============================================================================
 *
 * 1. 获取公网映射地址（Server Reflexive Address - Srflx）
 *    - 客户端向 STUN 服务器发送 Binding Request
 *    - 服务器返回 NAT 映射后的公网 IP:Port（XOR-MAPPED-ADDRESS）
 *    - 用于 P2P 连接中的候选地址收集和交换
 *
 * 2. NAT 类型检测（RFC 3489 Classic STUN）
 *    - 通过多轮测试（Test I, II, III）判断 NAT 行为特征
 *    - 识别 NAT 类型：完全锥形、受限锥形、端口受限、对称等
 *    - 不同 NAT 类型决定 P2P 穿透难度和策略选择
 *
 * ============================================================================
 * STUN 消息格式 (RFC 5389)
 * ============================================================================
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |0 0|     STUN Message Type     |         Message Length        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                     Magic Cookie (固定 0x2112A442)            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * |                   Transaction ID (96 bits / 12 bytes)         |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        Attributes (TLV 格式)                  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * 消息头（20 字节固定长度）：
 *   - Type (2字节):   消息类型，高2位必须为00
 *   - Length (2字节): 消息体长度（不含头部20字节），必须是4的倍数
 *   - Magic (4字节):  固定值 0x2112A442，用于区分 STUN 和其他协议
 *   - Tsx ID (12字节): 事务ID，请求/响应匹配的唯一标识
 *
 * ============================================================================
 * STUN 属性格式 (TLV)
 * ============================================================================
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |           Type (属性类型)     |          Length (值长度)      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         Value (填充到4字节边界)               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * 属性类型范围：
 *   0x0000-0x7FFF: 理解必需属性（不认识则报错）
 *   0x8000-0xFFFF: 理解可选属性（不认识可忽略）
 *
 * ============================================================================
 * XOR-MAPPED-ADDRESS 格式
 * ============================================================================
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |0 0 0 0 0 0 0 0|    Family     |         X-Port (XOR)          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                 X-Address (XOR 加密的 IP 地址)                |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * XOR 加密方式：
 *   - X-Port = Port XOR 0x2112 (Magic Cookie 高16位)
 *   - X-Address = Address XOR 0x2112A442 (完整 Magic Cookie)
 *
 * 使用 XOR 的原因：防止某些 NAT 设备篡改明文 IP 地址
 */
#ifndef P2P_STUN_H
#define P2P_STUN_H

#include "predefine.h"
#include <p2p.h>            /* p2p_nat_type_t */

/*
 * STUN Magic Cookie (RFC 5389)
 * 固定值 0x2112A442，用于：
 * 1. 区分 RFC 5389 和旧版 RFC 3489 协议
 * 2. XOR 映射地址的加密密钥
 */
#define STUN_MAGIC 0x2112A442

/*
 * STUN 消息头结构 (20 字节)
 */
typedef struct {
    uint16_t type;       /* 消息类型 */
    uint16_t length;     /* 消息体长度（不含头部） */
    uint32_t magic;      /* Magic Cookie (0x2112A442) */
    uint8_t  tsx_id[12]; /* 事务 ID (用于请求/响应匹配) */
} stun_hdr_t;

/*
 * STUN 消息类型
 *
 * 类型编码规则：
 *   - 第0/1位: 类的低2位 (00=请求, 01=指示, 10=成功响应, 11=错误响应)
 *   - 其余位: 方法 (Binding=0x001)
 *
 * 即：type = method | class, 其中 class 编码在特定位置
 */
#define STUN_BINDING_REQUEST   0x0001  /* Binding 请求 (00 | 0x001) */
#define STUN_BINDING_RESPONSE  0x0101  /* Binding 成功响应 (10 | 0x001) */

/*
 * STUN 属性类型
 *
 * 地址类属性：
 *   - MAPPED-ADDRESS (0x0001): 原始映射地址（旧版，明文）
 *   - XOR-MAPPED-ADDRESS (0x0020): XOR 加密的映射地址（推荐）
 *   - CHANGED-ADDRESS (0x0005): 备用服务器地址（NAT 检测用）
 *
 * 认证类属性：
 *   - USERNAME (0x0006): 用户名（短期凭证）
 *   - MESSAGE-INTEGRITY (0x0008): HMAC-SHA1 消息完整性校验
 *   - FINGERPRINT (0x8028): CRC32 指纹（可选）
 *
 * NAT 检测属性：
 *   - CHANGE-REQUEST (0x0003): 请求服务器改变源 IP/Port
 *
 * ICE 专用属性 (RFC 5245 / RFC 8445)：
 *   - PRIORITY (0x0024): 候选优先级（32位，网络字节序）
 *   - USE-CANDIDATE (0x0025): 提名标志（无值，controlling 端使用）
 *   - ICE-CONTROLLING (0x802A): Controlling 角色标识（64位 tie-breaker）
 *   - ICE-CONTROLLED (0x8029): Controlled 角色标识（64位 tie-breaker）
 */
#define STUN_ATTR_MAPPED_ADDR       0x0001  /* 映射地址（明文，已废弃） */
#define STUN_ATTR_CHANGE_REQUEST    0x0003  /* 请求改变源地址/端口 */
#define STUN_ATTR_CHANGED_ADDR      0x0005  /* 备用服务器地址 */
#define STUN_ATTR_USERNAME          0x0006  /* 用户名 */
#define STUN_ATTR_MESSAGE_INTEGRITY 0x0008  /* HMAC-SHA1 完整性校验 */
#define STUN_ATTR_XOR_MAPPED_ADDR   0x0020  /* XOR 映射地址（推荐） */
#define STUN_ATTR_PRIORITY          0x0024  /* ICE: 候选优先级 */
#define STUN_ATTR_USE_CANDIDATE     0x0025  /* ICE: 提名标志 */
#define STUN_ATTR_FINGERPRINT       0x8028  /* CRC32 指纹 */
#define STUN_ATTR_ICE_CONTROLLED    0x8029  /* ICE: Controlled 角色 */
#define STUN_ATTR_ICE_CONTROLLING   0x802A  /* ICE: Controlling 角色 */

/*
 * CHANGE-REQUEST 属性标志位
 * 用于 NAT 类型检测 (RFC 3489)
 */
#define STUN_FLAG_CHANGE_IP         0x04  /* 请求服务器从不同 IP 响应 */
#define STUN_FLAG_CHANGE_PORT       0x02  /* 请求服务器从不同端口响应 */

struct p2p_session;

/*
 * ============================================================================
 * STUN 公开接口
 * ============================================================================
 */

/*
 * 构建 STUN Binding 请求
 *
 * @param buf      输出缓冲区
 * @param max_len  缓冲区大小
 * @param tsx_id   事务 ID（12字节，NULL 则自动生成）
 * @param username 用户名（可选，用于 ICE 短期凭证）
 * @param password 密码（可选，用于计算 MESSAGE-INTEGRITY）
 * @return         生成的请求长度，失败返回 -1
 */
int p2p_stun_build_binding_request(uint8_t *buf, int max_len, uint8_t tsx_id[12],
                                   const char *username, const char *password);

/*
 * 构建 STUN Binding Response（用于回复对端的 ICE connectivity check）
 *
 * @param buf      输出缓冲区
 * @param max_len  缓冲区大小
 * @param tsx_id   事务 ID（从请求包偏移 8 处复制，12字节）
 * @param mapped   对端的观测地址（from addr，写入 XOR-MAPPED-ADDRESS）
 * @param password ICE 本地密码（用于 MESSAGE-INTEGRITY，NULL 则略过）
 * @return         生成的响应长度，失败返回 -1
 */
int p2p_stun_build_binding_response(uint8_t *buf, int max_len, const uint8_t tsx_id[12],
                                    const struct sockaddr_in *mapped, const char *password);

/*
 * 构建 ICE 标准的连通性检查包（STUN Binding Request + ICE 属性）
 *
 * 用于 ICE 标准模式下的连通性检查，包含以下属性：
 *   - USERNAME: 格式为 "remote_ufrag:local_ufrag"（如提供）
 *   - PRIORITY: 本地候选优先级（32位）
 *   - ICE-CONTROLLING / ICE-CONTROLLED: 角色标识（64位 tie-breaker）
 *   - USE-CANDIDATE: 提名标志（仅 controlling 端且提名时）
 *   - MESSAGE-INTEGRITY: HMAC-SHA1（使用 remote_pwd）
 *   - FINGERPRINT: CRC32
 *
 * @param buf            输出缓冲区
 * @param max_len        缓冲区大小
 * @param tsx_id         事务 ID（12字节，NULL 则自动生成）
 * @param username       ICE 用户名（格式: "remote_ufrag:local_ufrag"，NULL 表示不包含）
 * @param password       ICE 远端密码（用于 MESSAGE-INTEGRITY，NULL 表示不包含）
 * @param priority       本地候选优先级（0 表示不包含 PRIORITY 属性）
 * @param is_controlling 1=Controlling 角色, 0=Controlled 角色
 * @param tie_breaker    64位 tie-breaker 值（用于角色冲突解决）
 * @param use_candidate  是否携带 USE-CANDIDATE 属性（1=携带，0=不携带）
 * @return               生成的请求长度，失败返回 -1
 */
int p2p_stun_build_ice_check(uint8_t *buf, int max_len, uint8_t tsx_id[12],
                              const char *username, const char *password,
                              uint32_t priority, int is_controlling, 
                              uint64_t tie_breaker, int use_candidate);

/*
 * 检查 STUN 包是否包含 ICE 属性（用于 connectivity check 识别）
 *
 * 用于在包派发阶段判断一个 STUN 包是否为 ICE connectivity check 相关：
 *   - PRIORITY: 候选优先级（connectivity check 必含）
 *   - ICE-CONTROLLING / ICE-CONTROLLED: 角色标识
 *   - USE-CANDIDATE: 提名标志
 *
 * @param buf   STUN 包数据
 * @param len   包长度
 * @return      1=包含 ICE 属性（应由 NAT 模块处理），0=普通 STUN 包（NAT 检测用）
 */
int p2p_stun_has_ice_attrs(const uint8_t *buf, int len);


/*
 * 处理 STUN 响应包
 * 功能：解析响应，提取映射地址（Srflx），推进 NAT 检测状态机
 */
void p2p_stun_handle_packet(struct p2p_session *s, const uint8_t *buf, int len, const struct sockaddr_in *from);

/*
 * STUN NAT 检测状态机 tick
 * 仅处理 STUN 路径（需配置 stun_server），COMPACT 路径由 compact_nat_detect_tick 处理
 */
void p2p_stun_nat_detect_tick(struct p2p_session *s);

#endif /* P2P_STUN_H */
