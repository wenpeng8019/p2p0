
/*
 * NAT 穿透实现（纯打洞逻辑）
 *
 * 只负责 PUNCH/ACK/PING/PONG 的发送和接收。
 * 候选列表统一存储在 p2p_session 中，本模块从 session 读取远端候选进行打洞。
 */

#include "p2p_internal.h"
#include "p2p_nat.h"
#include "p2p_udp.h"

#include <string.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <stdio.h>

#define PUNCH_INTERVAL_MS       500         /* 打洞间隔 */
#define PUNCH_TIMEOUT_MS        5000        /* 打洞超时 */
#define PING_INTERVAL_MS        15000       /* 心跳间隔 */
#define PONG_TIMEOUT_MS         30000       /* 心跳超时 */

///////////////////////////////////////////////////////////////////////////////

/*
 * 初始化打洞上下文
 */
void nat_init(nat_ctx_t *n) {
    memset(n, 0, sizeof(*n));
    n->state = NAT_IDLE;
}

/*
 * 开始打洞（从 session 的 remote_cands[] 读取目标）
 */
int nat_start_punch(p2p_session_t *s, int verbose) {
    nat_ctx_t *n = &s->nat;
    
    if (s->remote_cand_cnt == 0) {
        if (verbose) {
            printf("[NAT_PUNCH] ERROR: No remote candidates to punch\n");
            fflush(stdout);
        }
        return -1;
    }
    
    n->verbose = verbose;
    n->state = NAT_PUNCHING;
    n->punch_start = time_ms();
    n->punch_attempts = 0;
    n->last_send_time = 0;  /* 立即开始打洞 */
    n->peer_addr = s->remote_cands[0].addr;
    
    if (n->verbose) {
        printf("[NAT_PUNCH] START: Punching to %d candidates\n", s->remote_cand_cnt);
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
    
    /* 立即向第一个候选发送打洞包 */
    udp_send_packet(s->sock, &n->peer_addr, P2P_PKT_PUNCH, 0, 0, NULL, 0);
    n->last_send_time = time_ms();
    n->punch_attempts = 1;
    
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 处理打洞相关数据包
 */
int nat_on_packet(p2p_session_t *s, uint8_t type, const uint8_t *payload, int len,
                  const struct sockaddr_in *from) {
    nat_ctx_t *n = &s->nat;
    (void)payload;
    (void)len;
    
    uint64_t now = time_ms();

    switch (type) {

    case P2P_PKT_PUNCH:
        if (n->verbose) {
            printf("[NAT_PUNCH] PUNCH: Received from %s:%d, sending ACK\n",
                   inet_ntoa(from->sin_addr), ntohs(from->sin_port));
            fflush(stdout);
        }
        /* 回复应答包 */
        udp_send_packet(s->sock, from, P2P_PKT_PUNCH_ACK, 0, 0, NULL, 0);
        /* fall through - 收到 PUNCH 也算成功 */

    case P2P_PKT_PUNCH_ACK:
        if (n->verbose && type == P2P_PKT_PUNCH_ACK) {
            printf("[NAT_PUNCH] PUNCH_ACK: Received from %s:%d\n",
                   inet_ntoa(from->sin_addr), ntohs(from->sin_port));
            fflush(stdout);
        }
        
        /* 通知 ICE 层（如果启用） */
        if (s->cfg.use_ice) {
            p2p_ice_on_check_success(s, from);
        }

        /* 标记连接成功 */
        if (n->state == NAT_PUNCHING || n->state == NAT_RELAY) {
            n->peer_addr = *from;
            n->state = NAT_CONNECTED;
            n->last_recv_time = now;
            
            if (n->verbose) {
                printf("[NAT_PUNCH] SUCCESS: Hole punched! Connected to %s:%d\n",
                       inet_ntoa(from->sin_addr), ntohs(from->sin_port));
                printf("            Attempts: %d, Time: %llu ms\n",
                       n->punch_attempts, now - n->punch_start);
                fflush(stdout);
            }
        }
        return 0;

    case P2P_PKT_PING:
        /* 回复 PONG */
        udp_send_packet(s->sock, from, P2P_PKT_PONG, 0, 0, NULL, 0);
        /* fall through */

    case P2P_PKT_PONG:
        n->last_recv_time = now;
        return 0;

    default:
        return 0;
    }
}

///////////////////////////////////////////////////////////////////////////////

/*
 * 周期调用，发送打洞包和心跳
 */
int nat_tick(p2p_session_t *s) {
    nat_ctx_t *n = &s->nat;
    uint64_t now = time_ms();

    switch (n->state) {

        case NAT_PUNCHING:
            /* 超时判断 */
            if (now - n->punch_start >= PUNCH_TIMEOUT_MS) {
                if (n->verbose) {
                    printf("[NAT_PUNCH] TIMEOUT: Punch failed after %d attempts, switching to RELAY\n",
                           n->punch_attempts);
                    fflush(stdout);
                }
                n->state = NAT_RELAY;
                return 0;
            }

            /* 定期发送打洞包 */
            if (now - n->last_send_time >= PUNCH_INTERVAL_MS) {
                /* 向所有远端候选发送打洞包（并行打洞） */
                for (int i = 0; i < s->remote_cand_cnt; i++) {
                    udp_send_packet(s->sock, &s->remote_cands[i].addr,
                                    P2P_PKT_PUNCH, 0, 0, NULL, 0);
                }
                n->punch_attempts++;
                n->last_send_time = now;

                if (n->verbose) {
                    printf("[NAT_PUNCH] PUNCHING: Attempt #%d to %d candidates\n",
                           n->punch_attempts, s->remote_cand_cnt);
                    fflush(stdout);
                }
            }
            break;

        case NAT_CONNECTED:
            /* 发送心跳保活包 */
            if (now - n->last_send_time >= PING_INTERVAL_MS) {
                udp_send_packet(s->sock, &n->peer_addr, P2P_PKT_PING, 0, 0, NULL, 0);
                n->last_send_time = now;
            }

            /* 超时检查 */
            if (n->last_recv_time > 0 && now - n->last_recv_time >= PONG_TIMEOUT_MS) {
                if (n->verbose) {
                    printf("[NAT_PUNCH] TIMEOUT: Connection lost (no pong for %d ms)\n",
                           PONG_TIMEOUT_MS);
                    fflush(stdout);
                }
                n->state = NAT_IDLE;
                return -1;
            }
            break;

        case NAT_RELAY:
            /* 中继模式下周期性尝试直连 */
            if (now - n->last_send_time >= PUNCH_INTERVAL_MS * 4) {
                for (int i = 0; i < s->remote_cand_cnt; i++) {
                    udp_send_packet(s->sock, &s->remote_cands[i].addr,
                                    P2P_PKT_PUNCH, 0, 0, NULL, 0);
                }
                n->last_send_time = now;
            }
            break;

        default:
            break;
    }

    return 0;
}
