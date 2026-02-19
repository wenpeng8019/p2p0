/*
 * COMPACT 模式信令实现（UDP 无状态）
 *
 * 从 p2p_nat.c 中提取的信令相关代码。
 * 负责 REGISTER/REGISTER_ACK/PEER_INFO/PEER_INFO_ACK 协议。
 *
 * 状态机：
 *   IDLE → REGISTERING → REGISTERED → READY
 *                 ↓                       
 *                 └──────────────────────┘ (收到 PEER_INFO seq=1)
 *
 * 候选列表统一存储在 p2p_session 中，本模块只负责序列化和发送。
 *
 * 序列化同步机制：
 *   - REGISTER 仅在 REGISTERING 阶段发送，收到 ACK 后停止
 *   - PEER_INFO(seq=1) 由服务器发送，包含缓存的对端候选
 *   - PEER_INFO(seq>1) 由客户端发送，继续同步剩余候选
 *   - 每个 PEER_INFO 需要 PEER_INFO_ACK 确认，未确认则重发
 */

#include "p2p_internal.h"
#include "p2p_signal_compact.h"
#include "p2p_udp.h"
#include "p2p_log.h"
#include "p2p_lang.h"

#include <string.h>
#ifndef _WIN32
#include <arpa/inet.h>
#endif
#ifndef _WIN32
#include <sys/time.h>
#endif

#define REGISTER_INTERVAL_MS    1000    /* 注册重发间隔 */
#define PEER_INFO_INTERVAL_MS   2000    /* PEER_INFO 重发间隔 */
#define MAX_REGISTER_ATTEMPTS   10      /* 最大 REGISTER 重发次数 */
#define MAX_CANDS_PER_PACKET    10      /* 每个 PEER_INFO 包最大候选数 */

/* 获取当前时间戳（毫秒） */
static inline uint64_t compact_time_ms(void) {
    return p2p_time_ms();
}

/*
 * 初始化信令上下文
 */
void p2p_signal_compact_init(p2p_signal_compact_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = SIGNAL_COMPACT_IDLE;
}

/*
 * 构建 REGISTER 负载
 *
 * 格式: [local_peer_id(32)][remote_peer_id(32)][candidate_count(1)][candidates(N*7)]
 *
 * 从 session 的 local_cands[] 中读取候选列表
 */
static int build_register_payload(p2p_session_t *s, uint8_t *buf, int buf_sz) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    int cand_cnt = s->local_cand_cnt;
    
    int required = P2P_PEER_ID_MAX * 2 + 1 + cand_cnt * 7;
    if (buf_sz < required) return -1;
    
    int offset = 0;
    
    /* local_peer_id */
    memset(buf, 0, P2P_PEER_ID_MAX * 2);
    memcpy(buf, ctx->local_peer_id, strlen(ctx->local_peer_id));
    memcpy(buf + P2P_PEER_ID_MAX, ctx->remote_peer_id, strlen(ctx->remote_peer_id));
    offset = P2P_PEER_ID_MAX * 2;
    
    /* candidate_count */
    buf[offset++] = (uint8_t)cand_cnt;
    
    /* candidates (每个 7 字节: type + ip + port) */
    for (int i = 0; i < cand_cnt; i++) {
        buf[offset] = (uint8_t)s->local_cands[i].type;
        memcpy(buf + offset + 1, &s->local_cands[i].addr.sin_addr.s_addr, 4);
        memcpy(buf + offset + 5, &s->local_cands[i].addr.sin_port, 2);
        offset += 7;
    }
    
    return offset;
}

/*
 * 解析 PEER_INFO 负载，追加到 session 的 remote_cands[]
 *
 * 格式: [base_index(1)][candidate_count(1)][candidates(N*7)]
 * 
 * 注意：序列化接收时按 base_index 放置候选，seq=1 时清空列表
 */
static int parse_peer_info(p2p_session_t *s, const uint8_t *payload, int len, 
                          uint16_t seq, uint8_t flags) {

    if (len < 2) return -1;
    
    uint8_t base_index = payload[0];
    uint8_t count = payload[1];
    
    /* seq=1 是第一个包（服务器发送），清空列表 */
    if (seq == 1) {
        s->remote_cand_cnt = 0;
    }
    
    /* count=0 或 FIN 标志表示对端发送完毕 */
    if (count == 0 || (flags & SIG_PEER_INFO_FIN)) {
        p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
        ctx->remote_recv_complete = 1;
        if (ctx->verbose) {
            P2P_LOG_INFO("COMPACT", "PEER_INFO(seq=%d): %s, %s=%d",
                   seq, MSG(MSG_COMPACT_RECEIVED_FIN), MSG(MSG_COMPACT_TOTAL_CANDIDATES), s->remote_cand_cnt);
        }
        return 0;
    }
    
    int offset = 2;
    
    for (int i = 0; i < count && offset + 7 <= len; i++) {
        int idx = base_index + i;
        if (idx >= P2P_MAX_CANDIDATES) {
            /* 超出最大容量，忽略剩余候选 */
            break;
        }
        
        /* 确保有足够空间 */
        if (idx >= s->remote_cand_cnt) {
            s->remote_cand_cnt = idx + 1;
        }
        
        p2p_candidate_t *c = &s->remote_cands[idx];
        c->type = (p2p_cand_type_t)payload[offset];
        c->priority = 0;  /* COMPACT 模式不使用优先级 */
        memset(&c->addr, 0, sizeof(c->addr));
        c->addr.sin_family = AF_INET;
        memcpy(&c->addr.sin_addr.s_addr, payload + offset + 1, 4);
        memcpy(&c->addr.sin_port, payload + offset + 5, 2);
        offset += 7;
    }
    
    return 0;
}

/*
 * 开始信令交换（发送 REGISTER）
 */
int p2p_signal_compact_start(struct p2p_session *s, const char *local_peer_id,
                             const char *remote_peer_id,
                             const struct sockaddr_in *server, int verbose) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;

    if (ctx->state != SIGNAL_COMPACT_IDLE) return -1;

    ctx->server_addr = *server;
    ctx->verbose = verbose;
    memset(ctx->local_peer_id, 0, sizeof(ctx->local_peer_id));
    memset(ctx->remote_peer_id, 0, sizeof(ctx->remote_peer_id));
    strncpy(ctx->local_peer_id, local_peer_id, P2P_PEER_ID_MAX - 1);
    strncpy(ctx->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX - 1);

    /* 初始化新字段 */
    ctx->peer_online = 0;
    ctx->register_attempts = 0;

    ctx->state = SIGNAL_COMPACT_REGISTERING;
    ctx->last_send_time = compact_time_ms();

    if (ctx->verbose) {
        P2P_LOG_INFO("COMPACT", "START: %s '%s' -> '%s' %s %s:%d (%d %s)",
               MSG(MSG_COMPACT_REGISTERING), local_peer_id, remote_peer_id,
               MSG(MSG_COMPACT_WITH_SERVER), inet_ntoa(server->sin_addr), ntohs(server->sin_port),
               s->local_cand_cnt, MSG(MSG_ICE_CANDIDATE_PAIRS));
    }

    /* 构造并发送带候选列表的注册包 */
    uint8_t payload[256];
    int payload_len = build_register_payload(s, payload, sizeof(payload));
    if (payload_len > 0) {
        udp_send_packet(s->sock, server, SIG_PKT_REGISTER, 0, 0, payload, payload_len);
    }

    return 0;
}

/*
 * 处理收到的信令包
 *
 * 支持的包类型：
 * - REGISTER_ACK: 服务器确认，提取对端状态
 * - PEER_INFO: 对端候选列表（序列化）
 * - PEER_INFO_ACK: 对端确认
 */
int p2p_signal_compact_on_packet(struct p2p_session *s, uint8_t type, uint16_t seq, uint8_t flags,
                                 const uint8_t *payload, int len,
                                 const struct sockaddr_in *from) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    (void)flags;  /* 暂时未使用 */
    
    switch (type) {
    
    case SIG_PKT_REGISTER_ACK:
        /* 解析 REGISTER_ACK: [status(1)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)] */
        if (len < 10) return -1;
        
        uint8_t status = payload[0];
        if (status >= 2) {
            if (ctx->verbose) {
                P2P_LOG_ERROR("COMPACT", "REGISTER_ACK: %s (status=%d)", MSG(MSG_COMPACT_SERVER_ERROR), status);
            }
            return -1;
        }

        uint8_t max_cands = payload[1];
        uint8_t relay_flags_value = flags;  /* 从 header 的 flags 字段读取 */
        
        ctx->peer_online = (status == SIG_REGACK_PEER_ONLINE) ? 1 : 0;
        ctx->max_remote_candidates = max_cands;
        ctx->relay_support = (relay_flags_value & SIG_REGACK_FLAG_RELAY) ? 1 : 0;
        
        /* 解析公网地址（服务器主端口探测到的 UDP 源地址）*/
        memset(&ctx->public_addr, 0, sizeof(ctx->public_addr));
        ctx->public_addr.sin_family = AF_INET;
        memcpy(&ctx->public_addr.sin_addr.s_addr, payload + 2, 4);
        memcpy(&ctx->public_addr.sin_port, payload + 6, 2);
        
        /* 解析 NAT 探测端口 */
        memcpy(&ctx->probe_port, payload + 8, 2);
        ctx->probe_port = ntohs(ctx->probe_port);
        
        /* 根据服务器缓存能力确定每个包的候选数量 */
        if (max_cands > 0 && max_cands < MAX_CANDS_PER_PACKET) {
            ctx->candidates_per_packet = max_cands;
        } else {
            ctx->candidates_per_packet = MAX_CANDS_PER_PACKET;
        }
        
        if (ctx->verbose) {
            P2P_LOG_INFO("COMPACT", "REGISTER_ACK: peer_online=%d, max_cands=%d (%s=%s), %s=%s, public_addr=%s:%d, probe_port=%d",
                   ctx->peer_online, max_cands, MSG(MSG_COMPACT_CACHE), max_cands > 0 ? MSG(MSG_ICE_YES) : MSG(MSG_ICE_NO_CACHED),
                   MSG(MSG_COMPACT_RELAY), ctx->relay_support ? MSG(MSG_ICE_YES) : MSG(MSG_ICE_NO_CACHED),
                   inet_ntoa(ctx->public_addr.sin_addr), ntohs(ctx->public_addr.sin_port),
                   ctx->probe_port);
        }
        
        /* 如果已经收到 PEER_INFO 进入 READY 状态，忽略延迟到达的 ACK */
        if (ctx->state == SIGNAL_COMPACT_READY) {
            if (ctx->verbose) {
                P2P_LOG_WARN("COMPACT", "%s", MSG(MSG_COMPACT_ALREADY_READY));
            }
            return 0;
        }
        
        /* 仅在 REGISTERING 状态处理状态转换 */
        if (ctx->state == SIGNAL_COMPACT_REGISTERING) {
            /* 收到 ACK 后进入 REGISTERED 状态，停止发送 REGISTER */
            ctx->state = SIGNAL_COMPACT_REGISTERED;
            
            if (ctx->verbose) {
                const char *status_msg = ctx->peer_online ? 
                    MSG(MSG_COMPACT_PEER_ONLINE) :
                    MSG(MSG_COMPACT_PEER_OFFLINE);
                P2P_LOG_INFO("COMPACT", "%s: %s", MSG(MSG_COMPACT_ENTERED_REGISTERED), status_msg);
            }
        }
        
        return 0;
    
    case SIG_PKT_PEER_INFO:

        if (parse_peer_info(s, payload, len, seq, flags) < 0) {
            return -1;
        }
        
        /* 更新接收状态 */
        if (seq > ctx->last_recv_seq) {
            ctx->last_recv_seq = seq;
        }
        
        if (ctx->verbose && !ctx->remote_recv_complete) {
            uint8_t base_idx = (len >= 1) ? payload[0] : 0;
            uint8_t count = (len >= 2) ? payload[1] : 0;
            P2P_LOG_INFO("COMPACT", "PEER_INFO(seq=%u, %s=%u): %s %u %s (%s: %d)", 
                   seq, MSG(MSG_COMPACT_BASE), base_idx, MSG(MSG_RELAY_FORWARD_RECEIVED), count, MSG(MSG_ICE_CANDIDATE_PAIRS), MSG(MSG_COMPACT_TOTAL_CANDIDATES), s->remote_cand_cnt);
        }
        
        /* 发送 PEER_INFO_ACK 确认 */
        uint8_t ack_payload[4];
        ack_payload[0] = (uint8_t)(seq >> 8);
        ack_payload[1] = (uint8_t)(seq & 0xFF);
        ack_payload[2] = 0;  /* reserved */
        ack_payload[3] = 0;  /* reserved */
        
        udp_send_packet(s->sock, from, SIG_PKT_PEER_INFO_ACK, 0, 0, ack_payload, sizeof(ack_payload));
        
        if (ctx->verbose) {
            P2P_LOG_INFO("COMPACT", "%s PEER_INFO_ACK(seq=%u)", MSG(MSG_RELAY_ANSWER_SENT), seq);
        }
        
        /* seq=1 是服务器发送的首包，收到后从 REGISTERED 进入 READY 状态 */
        if (seq == 1 && ctx->state == SIGNAL_COMPACT_REGISTERED) {
            ctx->state = SIGNAL_COMPACT_READY;
            /* 初始化发送序列号（从 2 开始，1 是服务器发的） */
            ctx->next_send_seq = 2;
            
            if (ctx->verbose) {
                P2P_LOG_INFO("COMPACT", "%s", MSG(MSG_COMPACT_ENTERED_READY));
            }
        }
        
        return 0;
    
    case SIG_PKT_PEER_INFO_ACK:
        /* 解析 PEER_INFO_ACK: [ack_seq(2)][reserved(2)] */
        if (len < 4) return -1;
        
        uint16_t ack_seq = ((uint16_t)payload[0] << 8) | payload[1];
        
        /* 更新已确认的序列号 */
        if (ack_seq > ctx->last_acked_seq) {
            ctx->last_acked_seq = ack_seq;
            
            if (ctx->verbose) {
                P2P_LOG_INFO("COMPACT", "PEER_INFO_ACK(seq=%u): %s", ack_seq, MSG(MSG_RELAY_CONNECT_ACK));
            }
        }
        
        return 0;
    
    default:
        return 1;  /* 未处理 */
    }
}

/*
 * 周期调用，处理 REGISTER 重发和 PEER_INFO 序列化发送
 *
 * REGISTERING 状态：快速重发（1秒），等待 ACK 确认，有超时限制
 * READY 状态：序列化发送剩余候选（2秒重传间隔），确认后停止
 */
int p2p_signal_compact_tick(struct p2p_session *s) {

    p2p_signal_compact_ctx_t *ctx = &s->sig_compact_ctx;
    uint64_t now = compact_time_ms();

    /* REGISTERING 状态：重发 REGISTER */
    if (ctx->state == SIGNAL_COMPACT_REGISTERING) {
        if (now - ctx->last_send_time < REGISTER_INTERVAL_MS) {
            return 0;
        }
        
        /* 检查超时 */
        if (++ctx->register_attempts > MAX_REGISTER_ATTEMPTS) {
            if (ctx->verbose) {
                P2P_LOG_ERROR("COMPACT", "TIMEOUT: %s (%d)",
                       MSG(MSG_COMPACT_MAX_ATTEMPTS), MAX_REGISTER_ATTEMPTS);
            }
            return -1;
        }
        
        /* 构建并发送 REGISTER 包 */
        uint8_t payload[256];
        int payload_len = build_register_payload(s, payload, sizeof(payload));
        if (payload_len > 0) {
            udp_send_packet(s->sock, &ctx->server_addr, SIG_PKT_REGISTER, 0, 0, payload, payload_len);
            
            if (ctx->verbose) {
                P2P_LOG_INFO("COMPACT", "REGISTERING: %s #%d (%d %s)...",
                       MSG(MSG_COMPACT_ATTEMPT), ctx->register_attempts, s->local_cand_cnt, MSG(MSG_ICE_CANDIDATE_PAIRS));
            }
        }
        ctx->last_send_time = now;
        return 0;
    }
    
    /* READY 状态：序列化发送剩余候选 */
    if (ctx->state == SIGNAL_COMPACT_READY) {
        /* 如果已发送完毕，无需继续 */
        if (ctx->local_send_complete) {
            return 0;
        }
        
        /* 检查是否需要重发或发送下一批 */
        if (now - ctx->last_send_time < PEER_INFO_INTERVAL_MS) {
            return 0;
        }
        
        /* 如果上一个序列号还未确认，重发 */
        uint16_t seq_to_send;
        if (ctx->next_send_seq > 2 && ctx->last_acked_seq < ctx->next_send_seq - 1) {
            /* 重发未确认的包 */
            seq_to_send = ctx->last_acked_seq + 1;
        } else {
            /* 发送新包 */
            seq_to_send = ctx->next_send_seq;
            ctx->next_send_seq++;
        }
        
        /* 构建 PEER_INFO 包: [base_index(1)][candidate_count(1)][candidates(N*7)] */
        uint8_t payload[256];
        int offset = 0;
        
        /* 计算这个序列号应该发送哪些候选 */
        /* seq=2 对应 base_index = max_remote_candidates (服务器已缓存的数量) */
        int base_idx = ctx->max_remote_candidates + (seq_to_send - 2) * ctx->candidates_per_packet;
        int end_idx = base_idx + ctx->candidates_per_packet;
        if (end_idx > s->local_cand_cnt) end_idx = s->local_cand_cnt;
        
        int count = end_idx - base_idx;
        uint8_t flags = 0;
        
        /* 检查是否已发送完所有候选 */
        if (count <= 0 || end_idx >= s->local_cand_cnt) {
            /* 发送 FIN 标志 */
            flags = SIG_PEER_INFO_FIN;
            count = 0;
            base_idx = s->local_cand_cnt;  /* 指向列表末尾 */
        }
        
        payload[offset++] = (uint8_t)base_idx;
        payload[offset++] = (uint8_t)count;
        
        /* 序列化候选 (每个 7 字节: type + ip + port) */
        for (int i = base_idx; i < end_idx; i++) {
            payload[offset] = (uint8_t)s->local_cands[i].type;
            memcpy(payload + offset + 1, &s->local_cands[i].addr.sin_addr.s_addr, 4);
            memcpy(payload + offset + 5, &s->local_cands[i].addr.sin_port, 2);
            offset += 7;
        }
        
        /* 直接向对端发送（P2P 模式，不通过服务器） */
        /* 使用第一个远端候选作为目标地址 */
        if (s->remote_cand_cnt > 0) {
            udp_send_packet(s->sock, &s->remote_cands[0].addr, SIG_PKT_PEER_INFO, flags, seq_to_send, 
                           payload, offset);
            
            if (ctx->verbose) {
                if (flags & SIG_PEER_INFO_FIN) {
                    P2P_LOG_INFO("COMPACT", "READY: %s PEER_INFO(seq=%u) %s FIN (%s: %d)",
                           MSG(MSG_RELAY_ANSWER_SENT), seq_to_send, MSG(MSG_COMPACT_WITH), MSG(MSG_COMPACT_TOTAL_SENT), s->local_cand_cnt);
                } else {
                    P2P_LOG_INFO("COMPACT", "READY: %s PEER_INFO(seq=%u, %s=%d) %s %d %s [%d-%d]",
                           MSG(MSG_RELAY_ANSWER_SENT), seq_to_send, MSG(MSG_COMPACT_BASE), base_idx, MSG(MSG_COMPACT_WITH), count, MSG(MSG_ICE_CANDIDATE_PAIRS), base_idx, end_idx - 1);
                }
            }
            
            /* 如果发送了 FIN，标记完成 */
            if (flags & SIG_PEER_INFO_FIN) {
                ctx->local_send_complete = 1;
            }
        }
        
        ctx->last_send_time = now;
        return 0;
    }
    
    /* REGISTERED 状态：等待 PEER_INFO(seq=1)，无需发送 */
    return 0;
}

