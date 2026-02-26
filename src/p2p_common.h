/*
 * p2p_common.h — 共享序列化工具（供库内部及 p2p_server 使用）
 *
 * 包含不依赖 LANG/i18n 宏和 p2p_session_t 的轻量工具：
 *   - ICE 候选地址基础结构体 p2p_candidate_entry_t
 *   - 序列号差值 seq_diff
 *   - sockaddr ↔ wire 格式转换
 *   - 信令负载头部 / 候选的序列化与反序列化
 *
 * 包含关系：
 *   p2p_server/server.c   → ../src/p2p_common.h
 *   src/*.c               → p2p_internal.h（已含 p2p_common.h）
 */

#ifndef P2P_COMMON_H
#define P2P_COMMON_H

#include <stdint.h>
#include <string.h>             /* strncpy, memcpy */

#include <p2pp.h>               /* p2p_signaling_payload_hdr_t, p2p_candidate_t, p2p_sockaddr_t */
#include "p2p_platform.h"       /* htonl/ntohl、struct sockaddr_in、sa_family_t 等 */

/*
 * seq_diff: 计算序列号差值（处理回绕）
 *
 * 使用有符号 16 位差值处理序列号回绕问题。
 *
 * 示例：
 *   seq_diff(5, 3)     = 2   (正常情况)
 *   seq_diff(3, 5)     = -2  (正常情况)
 *   seq_diff(1, 65535) = 2   (回绕情况：1 比 65535 新)
 *   seq_diff(65535, 1) = -2  (回绕情况：65535 比 1 旧)
 */
static inline int16_t seq_diff(uint16_t a, uint16_t b) {
    return (int16_t)(a - b);
}

/* ============================================================================
 * sockaddr ↔ wire 格式转换
 * ============================================================================ */

/*
 * struct sockaddr_in → p2p_sockaddr_t
 *
 * sin_port/sin_addr 已是网络字节序；统一用 htonl 写入确保跨平台一致性。
 * sin_port（uint16_t，大端）零扩展后再 htonl，与 p2p_wire_to_sockaddr 对称。
 */
static inline void p2p_sockaddr_to_wire(const struct sockaddr_in *s, p2p_sockaddr_t *w) {
    w->family = htonl((uint32_t)s->sin_family);
    w->port   = htonl((uint32_t)s->sin_port);
    w->ip     = s->sin_addr.s_addr;     /* 已是网络字节序，直接存储 */
}

/*
 * p2p_sockaddr_t → struct sockaddr_in
 *
 * 自动清零 sin_zero[8] 及 macOS 上的 sin_len 字段。
 */
static inline void p2p_wire_to_sockaddr(const p2p_sockaddr_t *w, struct sockaddr_in *s) {
    memset(s, 0, sizeof(*s));
    s->sin_family      = (sa_family_t)ntohl(w->family);
    s->sin_port        = (in_port_t)  ntohl(w->port);
    s->sin_addr.s_addr = w->ip;
}

/*
 * pack_signaling_payload_hdr: 序列化信令负载头部到字节流
 *
 * 格式（76 字节）：[sender:32B][target:32B][timestamp:4B][delay_trigger:4B][count:4B]
 *
 * @param sender          发送方 peer_id
 * @param target          目标方 peer_id
 * @param timestamp       时间戳
 * @param delay_trigger   延迟触发（毫秒）
 * @param candidate_count 候选数量
 * @param buf             输出缓冲区（至少 76 字节）
 * @return                写入的字节数（76）
 */
static inline int pack_signaling_payload_hdr(
    const char *sender,
    const char *target,
    uint32_t timestamp,
    uint32_t delay_trigger,
    int candidate_count,
    uint8_t *buf
) {
    int n = 0, m;

    /* sender (32字节) */
    m = sizeof(((p2p_signaling_payload_hdr_t*)0)->sender) - 1;
    strncpy((char*)buf, sender, m);
    buf[m] = '\0';
    n = m + 1;

    /* target (32字节) */
    m = sizeof(((p2p_signaling_payload_hdr_t*)0)->target) - 1;
    strncpy((char*)(buf + n), target, m);
    buf[n + m] = '\0';
    n += m + 1;

    /* timestamp, delay_trigger, candidate_count */
    *(uint32_t*)&buf[n] = htonl(timestamp);                   n += 4;
    *(uint32_t*)&buf[n] = htonl(delay_trigger);               n += 4;
    *(uint32_t*)&buf[n] = htonl((uint32_t)candidate_count);   n += 4;

    return n;  /* 76 */
}

/*
 * unpack_signaling_payload_hdr: 从字节流反序列化信令负载头部
 *
 * 格式（76 字节）：[sender:32B][target:32B][timestamp:4B][delay_trigger:4B][count:4B]
 *
 * @param p    输出：信令负载结构（仅写入头部字段）
 * @param buf  输入缓冲区（至少 76 字节）
 * @return     0=成功，-1=失败（candidate_count 越界）
 */
static inline int unpack_signaling_payload_hdr(p2p_signaling_payload_hdr_t *p, const uint8_t *buf) {
    int n = 0;

    /* sender, target */
    memcpy(p->sender, buf + n, 32); n += 32;
    memcpy(p->target, buf + n, 32); n += 32;

    /* timestamp, delay_trigger, candidate_count */
    p->timestamp     = ntohl(*(uint32_t*)&buf[n]); n += 4;
    p->delay_trigger = ntohl(*(uint32_t*)&buf[n]); n += 4;
    p->candidate_count = (int)ntohl(*(uint32_t*)&buf[n]);

    /* 验证候选数量（防止恶意/畸形包） */
    if (p->candidate_count < 0 || p->candidate_count > 200) return -1;

    return 0;
}

#endif /* P2P_COMMON_H */
