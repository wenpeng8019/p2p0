/*
 * SIMPLE 模式信令实现（UDP 无状态）
 *
 * 从 p2p_nat.c 中提取的信令相关代码。
 * 负责 REGISTER/REGISTER_ACK/PEER_INFO/ICE_CANDIDATES 协议。
 *
 * 状态机：
 *   IDLE → REGISTERING → REGISTERED → READY
 *                 ↓          ↓
 *                 └──────────┘ (收到 PEER_INFO)
 *
 * 候选列表统一存储在 p2p_session 中，本模块只负责序列化和发送。
 */

#include "p2p_internal.h"
#include "p2p_signal_simple.h"
#include "p2p_udp.h"

#include <string.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/time.h>

#define REGISTER_INTERVAL_MS    1000    /* 注册重发间隔 */
#define CANDS_INTERVAL_MS       3000    /* 候选上报间隔 */
#define MAX_REGISTER_ATTEMPTS   10      /* 最大 REGISTER 重发次数 */

/* 获取当前时间戳（毫秒） */
static inline uint64_t simple_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/*
 * 初始化信令上下文
 */
void signal_simple_init(signal_simple_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = SIGNAL_SIMPLE_IDLE;
}

/*
 * 构建 REGISTER 负载
 *
 * 格式: [local_peer_id(32)][remote_peer_id(32)][candidate_count(1)][candidates(N*7)]
 *
 * 从 session 的 local_cands[] 中读取候选列表
 */
static int build_register_payload(p2p_session_t *s, uint8_t *buf, int buf_sz) {

    signal_simple_ctx_t *ctx = &s->sig_simple_ctx;
    int cand_cnt = s->local_cand_cnt;
    
    int required = P2P_PEER_ID_MAX * 2 + 1 + cand_cnt * 7;
    if (buf_sz < required) return -1;
    
    int offset = 0;
    
    /* peer_id */
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
 * 解析 PEER_INFO 负载，写入 session 的 remote_cands[]
 *
 * 格式: [candidate_count(1)][candidates(N*7)]
 */
static int parse_peer_info(p2p_session_t *s, const uint8_t *payload, int len) {

    /* 清空远端候选列表 */
    s->remote_cand_cnt = 0;
    
    if (len < 1) return -1;
    
    int count = payload[0];
    if (count > P2P_MAX_CANDIDATES) count = P2P_MAX_CANDIDATES;
    
    int offset = 1;
    for (int i = 0; i < count && offset + 7 <= len; i++) {
        p2p_candidate_t *c = &s->remote_cands[s->remote_cand_cnt++];
        c->type = (p2p_cand_type_t)payload[offset];
        c->priority = 0;  /* SIMPLE 模式不使用优先级 */
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
int signal_simple_start(p2p_session_t *s, const char *local_peer_id,
                        const char *remote_peer_id,
                        const struct sockaddr_in *server, int verbose) {

    signal_simple_ctx_t *ctx = &s->sig_simple_ctx;

    if (ctx->state != SIGNAL_SIMPLE_IDLE) return -1;

    ctx->server_addr = *server;
    ctx->verbose = verbose;
    memset(ctx->local_peer_id, 0, sizeof(ctx->local_peer_id));
    memset(ctx->remote_peer_id, 0, sizeof(ctx->remote_peer_id));
    strncpy(ctx->local_peer_id, local_peer_id, P2P_PEER_ID_MAX - 1);
    strncpy(ctx->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX - 1);

    /* 初始化新字段 */
    ctx->peer_online = 0;
    ctx->server_can_cache = 0;
    ctx->cache_full = 0;
    ctx->register_attempts = 0;
    ctx->cands_sent = 0;

    ctx->state = SIGNAL_SIMPLE_REGISTERING;
    ctx->last_send_time = simple_time_ms();

    if (ctx->verbose) {
        printf("[SIGNAL_SIMPLE] START: Registering '%s' -> '%s' with server %s:%d (%d candidates)\n",
               local_peer_id, remote_peer_id,
               inet_ntoa(server->sin_addr), ntohs(server->sin_port),
               s->local_cand_cnt);
        fflush(stdout);
    }

    /* 构造并发送带候选列表的注册包 */
    uint8_t payload[256];
    int payload_len = build_register_payload(s, payload, sizeof(payload));
    if (payload_len > 0) {
        udp_send_packet(s->sock, server, P2P_PKT_REGISTER, 0, 0, payload, payload_len);
    }

    return 0;
}

/*
 * 处理收到的信令包
 *
 * 支持的包类型：
 * - REGISTER_ACK: 服务器确认，提取对端状态
 * - PEER_INFO: 对端候选列表
 */
int signal_simple_on_packet(p2p_session_t *s, uint8_t type,
                            const uint8_t *payload, int len,
                            const struct sockaddr_in *from) {

    signal_simple_ctx_t *ctx = &s->sig_simple_ctx;
    (void)from;  /* 暂时未使用 */
    
    switch (type) {
    
    case P2P_PKT_REGISTER_ACK:
        /* 解析 REGISTER_ACK: [status(1)][flags(1)][reserved(2)] */
        if (len < 4) return -1;
        
        uint8_t status = payload[0];
        if (status != 0) {
            if (ctx->verbose) {
                printf("[SIGNAL_SIMPLE] REGISTER_ACK: Server error (status=%d)\n", status);
                fflush(stdout);
            }
            return -1;
        }

        uint8_t flags = payload[1];
        ctx->peer_online = (flags & P2P_REGACK_PEER_ONLINE) ? 1 : 0;
        ctx->server_can_cache = (flags & P2P_REGACK_CAN_CACHE) ? 1 : 0;
        ctx->cache_full = (flags & P2P_REGACK_CACHE_FULL) ? 1 : 0;
        
        if (ctx->verbose) {
            printf("[SIGNAL_SIMPLE] REGISTER_ACK: peer_online=%d, can_cache=%d, cache_full=%d\n",
                   ctx->peer_online, ctx->server_can_cache, ctx->cache_full);
            fflush(stdout);
        }
        
        /* 如果已经收到 PEER_INFO 进入 READY 状态，忽略延迟到达的 ACK */
        if (ctx->state == SIGNAL_SIMPLE_READY) {
            if (ctx->verbose) {
                printf("[SIGNAL_SIMPLE] Already READY, ignoring delayed REGISTER_ACK\n");
                fflush(stdout);
            }
            return 0;
        }
        
        /* 仅在 REGISTERING 状态处理状态转换 */
        if (ctx->state == SIGNAL_SIMPLE_REGISTERING) {
            if (ctx->peer_online) {
                /* 对端在线，等待 PEER_INFO（可能很快就到） */
                /* 保持 REGISTERING 状态，因为 PEER_INFO 可能在下一个包 */
            } else {
                /* 对端离线，切换到 REGISTERED 状态 */
                ctx->state = SIGNAL_SIMPLE_REGISTERED;
                ctx->last_send_time = 0;  /* 立即开始上报候选 */
                
                if (ctx->verbose) {
                    printf("[SIGNAL_SIMPLE] Peer offline, entering REGISTERED state\n");
                    fflush(stdout);
                }
            }
        }
        /* REGISTERED 状态可能收到重发的 ACK，更新 flags 但不改变状态 */
        
        return 0;
    
    case P2P_PKT_PEER_INFO:

        if (parse_peer_info(s, payload, len) < 0) {
            return -1;
        }
        
        if (ctx->verbose) {
            printf("[SIGNAL_SIMPLE] PEER_INFO: Received %d remote candidates\n", 
                   s->remote_cand_cnt);
            for (int i = 0; i < s->remote_cand_cnt; i++) {
                const char *type_str = "Unknown";
                switch (s->remote_cands[i].type) {
                    case P2P_CAND_HOST: type_str = "Host"; break;
                    case P2P_CAND_SRFLX: type_str = "Srflx"; break;
                    case P2P_CAND_PRFLX: type_str = "Prflx"; break;
                    case P2P_CAND_RELAY: type_str = "Relay"; break;
                }
                printf("            [%d] %s: %s:%d\n", i, type_str,
                       inet_ntoa(s->remote_cands[i].addr.sin_addr),
                       ntohs(s->remote_cands[i].addr.sin_port));
            }
            fflush(stdout);
        }
        
        /* 标记状态为已就绪 */
        ctx->state = SIGNAL_SIMPLE_READY;
        return 0;
    
    default:
        return 1;  /* 未处理 */
    }
}

/*
 * 周期调用，处理 REGISTER 重发
 *
 * REGISTERING 状态：快速重发（1秒），等待 ACK 确认，有超时限制
 * REGISTERED 状态：慢速重发（3秒），更新候选缓存，无超时限制
 */
int signal_simple_tick(p2p_session_t *s) {

    signal_simple_ctx_t *ctx = &s->sig_simple_ctx;
    uint64_t now = simple_time_ms();

    /* 确定重发间隔和条件 */
    uint64_t interval_ms = 0;
    int should_send = 0;
    
    if (ctx->state == SIGNAL_SIMPLE_REGISTERING) {
        interval_ms = REGISTER_INTERVAL_MS;
        should_send = 1;  /* 总是重发，等待 ACK */
    } else if (ctx->state == SIGNAL_SIMPLE_REGISTERED) {
        interval_ms = CANDS_INTERVAL_MS;
        should_send = (ctx->server_can_cache && !ctx->cache_full);  /* 仅当服务器支持缓存时 */
    } else {
        return 0;  /* 其他状态不需要周期发送 */
    }

    /* 检查是否到达重发时间 */
    if (!should_send || now - ctx->last_send_time < interval_ms) {
        return 0;
    }

    /* REGISTERING 状态检查超时 */
    if (ctx->state == SIGNAL_SIMPLE_REGISTERING) {
        if (++ctx->register_attempts > MAX_REGISTER_ATTEMPTS) {
            if (ctx->verbose) {
                printf("[SIGNAL_SIMPLE] TIMEOUT: Max register attempts reached (%d)\n",
                       MAX_REGISTER_ATTEMPTS);
                fflush(stdout);
            }
            return -1;  /* 超时失败 */
        }
    }

    /* 构建并发送 REGISTER 包 */
    uint8_t payload[256];
    int payload_len = build_register_payload(s, payload, sizeof(payload));
    if (payload_len > 0) {
        udp_send_packet(s->sock, &ctx->server_addr, P2P_PKT_REGISTER, 0, 0, payload, payload_len);
        if (ctx->state == SIGNAL_SIMPLE_REGISTERED) {
            ctx->cands_sent++;
        }
    }
    ctx->last_send_time = now;

    /* 日志输出 */
    if (ctx->verbose) {
        if (ctx->state == SIGNAL_SIMPLE_REGISTERING) {
            printf("[SIGNAL_SIMPLE] REGISTERING: Attempt #%d (%d candidates)...\n",
                   ctx->register_attempts, s->local_cand_cnt);
        } else {
            printf("[SIGNAL_SIMPLE] REGISTERED: Re-registering with %d candidates (attempt #%d)\n",
                   s->local_cand_cnt, ctx->cands_sent);
        }
        fflush(stdout);
    }

    return 0;
}

