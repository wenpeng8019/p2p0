/*
 * 信令协议序列化/反序列化实现
 *
 * ============================================================================
 * 概述
 * ============================================================================
 *
 * 本模块实现 P2P 信令数据的二进制序列化和反序列化。
 * 用于在信令通道（Relay/PubSub）中传输 ICE 候选等信息。
 *
 * 主要功能：
 *   - p2p_signal_pack():   将 p2p_signaling_payload_t 序列化为二进制
 *   - p2p_signal_unpack(): 将二进制数据反序列化为 p2p_signaling_payload_t
 *
 * ============================================================================
 * 数据结构：p2p_signaling_payload_t
 * ============================================================================
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │                      p2p_signaling_payload_t                            │
 * ├─────────────────────────────────────────────────────────────────────────┤
 * │ sender[32]        │ 发送方 local_peer_id（字符串）
 * │ target[32]        │ 目标方 local_peer_id（字符串）
 * │ timestamp         │ 时间戳（用于排序和去重）
 * │ delay_trigger     │ 延迟触发器（预留字段）
 * │ candidate_count   │ ICE 候选数量（0-8）
 * │ candidates[8]     │ ICE 候选数组
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 * 二进制格式（网络字节序）
 * ============================================================================
 *
 *  偏移量    大小       字段
 *  ─────────────────────────────────────────
 *  0         32        sender (原始字节)
 *  32        32        target (原始字节)
 *  64        4         timestamp (大端序)
 *  68        4         delay_trigger (大端序)
 *  72        4         candidate_count (大端序)
 *  76+       N×32      candidates (每个候选 32 字节)
 *
 * 每个 p2p_candidate_t 的格式（32 字节）：
 *  ─────────────────────────────────────────
 *  偏移      大小       字段
 *  ─────────────────────────────────────────
 *  0         4         type (候选类型)
 *  4         4         addr.sin_family
 *  8         4         addr.sin_port
 *  12        4         addr.sin_addr (网络序)
 *  16        4         base_addr.sin_family
 *  20        4         base_addr.sin_port
 *  24        4         base_addr.sin_addr (网络序)
 *  28        4         priority (优先级)
 *
 * ============================================================================
 * 字节序处理说明
 * ============================================================================
 *
 * 网络传输使用大端序（网络字节序），本地存储使用主机字节序。
 *
 * 特殊处理：
 *   - IP 地址 (sin_addr.s_addr) 本身已经是网络字节序，不需要转换
 *   - 其他整数字段使用 htonl/ntohl 转换
 *
 * 为什么使用 htonl 而不是直接 memcpy？
 *   - 确保在不同架构（大端/小端）之间的兼容性
 *   - 经过 DES 加密后仍能正确解析
 */

#include "p2p_signal_protocol.h"
#include <string.h>
#include <arpa/inet.h>

/*
 * ============================================================================
 * 序列化信令数据
 * ============================================================================
 *
 * 将 p2p_signaling_payload_t 结构体序列化为二进制数据。
 * 所有整数字段转换为网络字节序（大端序）。
 *
 * @param p    输入：信令数据结构
 * @param buf  输出：序列化后的二进制数据
 * @param len  缓冲区大小
 * @return     序列化后的数据长度，-1=失败
 */
int p2p_signal_pack(const p2p_signaling_payload_t *p, uint8_t *buf, size_t len) {
    size_t offset = 0;
    
    /* 计算所需缓冲区大小 */
    size_t required = sizeof(p->sender) + sizeof(p->target) + 
                      sizeof(uint32_t) * 3 +                        // 时间戳, 延迟触发器, 候选数量
                      p->candidate_count * (sizeof(uint32_t) * 8);  // 每个候选 = 8 个 uint32_t
    
    if (len < required) return -1;
    
    /*
     * 第一部分：sender 和 target（原始字节，无需转换）
     */
    memcpy(buf + offset, p->sender, sizeof(p->sender));
    offset += sizeof(p->sender);
    memcpy(buf + offset, p->target, sizeof(p->target));
    offset += sizeof(p->target);
    
    /*
     * 第二部分：整数字段（转换为网络字节序）
     */
    uint32_t tmp = htonl(p->timestamp);
    memcpy(buf + offset, &tmp, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    tmp = htonl(p->delay_trigger);
    memcpy(buf + offset, &tmp, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    tmp = htonl((uint32_t)p->candidate_count);
    memcpy(buf + offset, &tmp, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    /*
     * 第三部分：ICE 候选数组
     * 每个候选包含 8 个 uint32_t 字段
     */
    for (int i = 0; i < p->candidate_count; i++) {
        const p2p_candidate_t *c = &p->candidates[i];
        
        // 候选类型 (Host=1, Srflx=2, Relay=3)
        tmp = htonl((uint32_t)c->type);
        memcpy(buf + offset, &tmp, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        
        // 主地址 (addr): sin_family, sin_port, sin_addr
        tmp = htonl((uint32_t)c->addr.sin_family);
        memcpy(buf + offset, &tmp, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        
        tmp = htonl((uint32_t)c->addr.sin_port);
        memcpy(buf + offset, &tmp, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        
        // 注意：IP 地址已经是网络字节序，直接复制
        tmp = (uint32_t)c->addr.sin_addr.s_addr;
        memcpy(buf + offset, &tmp, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        
        // 基地址 (base_addr): 用于 Srflx/Relay 类型追溯本地地址
        tmp = htonl((uint32_t)c->base_addr.sin_family);
        memcpy(buf + offset, &tmp, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        
        tmp = htonl((uint32_t)c->base_addr.sin_port);
        memcpy(buf + offset, &tmp, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        
        tmp = (uint32_t)c->base_addr.sin_addr.s_addr;  // 网络字节序
        memcpy(buf + offset, &tmp, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        
        // 优先级：ICE 候选排序用
        tmp = htonl(c->priority);
        memcpy(buf + offset, &tmp, sizeof(uint32_t));
        offset += sizeof(uint32_t);
    }
    
    return (int)offset;
}

/*
 * ============================================================================
 * 反序列化信令数据
 * ============================================================================
 *
 * 将二进制数据反序列化为 p2p_signaling_payload_t 结构体。
 * 所有整数字段从网络字节序转换为主机字节序。
 *
 * @param p    输出：信令数据结构
 * @param buf  输入：二进制数据
 * @param len  数据长度
 * @return     0=成功，-1=失败
 */
int p2p_signal_unpack(p2p_signaling_payload_t *p, const uint8_t *buf, size_t len) {
    size_t offset = 0;
    uint32_t tmp;
    
    /* 检查最小长度：sender + target + 3 个整数字段 */
    if (len < sizeof(p->sender) + sizeof(p->target) + sizeof(uint32_t) * 3) return -1;
    
    /*
     * 第一部分：sender 和 target（原始字节）
     */
    memcpy(p->sender, buf + offset, sizeof(p->sender));
    offset += sizeof(p->sender);
    memcpy(p->target, buf + offset, sizeof(p->target));
    offset += sizeof(p->target);
    
    /*
     * 第二部分：整数字段（从网络字节序转换）
     */
    memcpy(&tmp, buf + offset, sizeof(uint32_t));
    p->timestamp = ntohl(tmp);
    offset += sizeof(uint32_t);
    
    memcpy(&tmp, buf + offset, sizeof(uint32_t));
    p->delay_trigger = ntohl(tmp);
    offset += sizeof(uint32_t);
    
    memcpy(&tmp, buf + offset, sizeof(uint32_t));
    p->candidate_count = (int)ntohl(tmp);
    offset += sizeof(uint32_t);
    
    // 验证候选数量（防止缓冲区溢出）
    if (p->candidate_count < 0 || p->candidate_count > 8) return -1;
    
    // 检查是否有足够的数据包含所有候选
    size_t required = offset + p->candidate_count * (sizeof(uint32_t) * 8);
    if (len < required) return -1;
    
    /*
     * 第三部分：ICE 候选数组
     */
    for (int i = 0; i < p->candidate_count; i++) {
        p2p_candidate_t *c = &p->candidates[i];
        
        // 候选类型
        memcpy(&tmp, buf + offset, sizeof(uint32_t));
        c->type = (p2p_cand_type_t)ntohl(tmp);
        offset += sizeof(uint32_t);
        
        /* 主地址 (addr) */
        memcpy(&tmp, buf + offset, sizeof(uint32_t));
        c->addr.sin_family = (sa_family_t)ntohl(tmp);
        offset += sizeof(uint32_t);
        
        memcpy(&tmp, buf + offset, sizeof(uint32_t));
        c->addr.sin_port = (in_port_t)ntohl(tmp);
        offset += sizeof(uint32_t);
        
        // IP 地址保持网络字节序
        memcpy(&tmp, buf + offset, sizeof(uint32_t));
        c->addr.sin_addr.s_addr = tmp;
        offset += sizeof(uint32_t);
        
        // 基地址 (base_addr)
        memcpy(&tmp, buf + offset, sizeof(uint32_t));
        c->base_addr.sin_family = (sa_family_t)ntohl(tmp);
        offset += sizeof(uint32_t);
        
        memcpy(&tmp, buf + offset, sizeof(uint32_t));
        c->base_addr.sin_port = (in_port_t)ntohl(tmp);
        offset += sizeof(uint32_t);
        
        memcpy(&tmp, buf + offset, sizeof(uint32_t));
        c->base_addr.sin_addr.s_addr = tmp;  // 保持网络字节序
        offset += sizeof(uint32_t);
        
        // 优先级
        memcpy(&tmp, buf + offset, sizeof(uint32_t));
        c->priority = ntohl(tmp);
        offset += sizeof(uint32_t);
    }
    
    return 0;
}
