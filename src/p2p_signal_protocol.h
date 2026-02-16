/*
 * 信令协议数据结构和序列化接口
 *
 * 定义 P2P 信令交换的数据格式，以及序列化/反序列化函数。
 * 用于 Relay 和 PubSub 两种信令模式。
 */

#ifndef P2P_SIGNAL_PROTOCOL_H
#define P2P_SIGNAL_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include "p2p_ice.h"

/*
 * 信令负载结构
 *
 * 包含 ICE 候选信息，用于在对等方之间交换。
 * 可通过 Relay 服务器或 PubSub (GitHub Gist) 传输。
 */
typedef struct p2p_signaling_payload {
    char sender[32];              /* 发送方 peer_id */
    char target[32];              /* 目标方 peer_id */
    uint32_t timestamp;           /* 时间戳（用于排序和去重） */
    uint32_t delay_trigger;       /* 延迟触发打洞（毫秒） */
    int candidate_count;          /* ICE 候选数量（0-8） */
    p2p_candidate_t candidates[8]; /* ICE 候选数组 */
} p2p_signaling_payload_t;

/* 序列化：将结构体打包为二进制（网络字节序） */
int p2p_signal_pack(const p2p_signaling_payload_t *p, uint8_t *buf, size_t len);

/* 反序列化：将二进制解包为结构体 */
int p2p_signal_unpack(p2p_signaling_payload_t *p, const uint8_t *buf, size_t len);

#endif /* P2P_SIGNAL_PROTOCOL_H */
