
#include "p2p_nat.h"
#include "p2p_udp.h"
#include "p2p_internal.h"

#include <string.h>
#include <arpa/inet.h>
#include <stddef.h>

#define REGISTER_INTERVAL_MS    1000        // 请求连接间隔
#define PUNCH_INTERVAL_MS       500         // 打洞间隔
#define PUNCH_TIMEOUT_MS        5000        // 打洞超时
#define PING_INTERVAL_MS        15000       // 心跳间隔，1.5s
#define PONG_TIMEOUT_MS         30000       // 心跳超时，3s

///////////////////////////////////////////////////////////////////////////////

// NAT 状态机周期维护
int nat_tick(nat_ctx_t *n, int sock) {

    uint64_t now = time_ms();

    switch (n->state) {

    // 当前处于（注册）发起连接阶段
    case NAT_REGISTERING:

        // 由于 UDP 不可靠，或者服务器响应慢，所以在确认收到服务器返回的 PEER_INFO 前，定期重发注册请求
        if (now - n->last_send_time >= REGISTER_INTERVAL_MS) {
            // P2P_PKT_REGISTER payload 格式：[local_peer_id (32)] [remote_peer_id (32)] = 64 bytes
            uint8_t payload[P2P_PEER_ID_MAX * 2];
            memset(payload, 0, sizeof(payload));
            memcpy(payload, n->local_peer_id, strlen(n->local_peer_id));
            memcpy(payload + P2P_PEER_ID_MAX, n->remote_peer_id, strlen(n->remote_peer_id));
            udp_send_packet(sock, &n->server_addr,
                            P2P_PKT_REGISTER, 0, 0, payload, P2P_PEER_ID_MAX * 2);
            n->last_send_time = now;
            
            if (n->verbose) {
                printf("[NAT_PUNCH] REGISTERING: Retrying registration (waiting for PEER_INFO)...\n");
                fflush(stdout);
            }
        }
        break;

    // 当前处于连接打洞阶段
    case NAT_PUNCHING:

        if (now - n->punch_start >= PUNCH_TIMEOUT_MS) {
            if (n->verbose) {
                printf("[NAT_PUNCH] TIMEOUT: Punch failed after %d attempts, switching to RELAY\n",
                       n->punch_attempts);
                fflush(stdout);
            }
            n->state = NAT_RELAY;
            return 0;
        }

        if (now - n->last_send_time >= PUNCH_INTERVAL_MS) {
            udp_send_packet(sock, &n->peer_pub_addr,
                            P2P_PKT_PUNCH, 0, 0, NULL, 0);
            n->punch_attempts++;
            n->last_send_time = now;
            
            if (n->verbose) {
                printf("[NAT_PUNCH] PUNCHING: Attempt #%d to %s:%d\n",
                       n->punch_attempts,
                       inet_ntoa(n->peer_pub_addr.sin_addr),
                       ntohs(n->peer_pub_addr.sin_port));
                fflush(stdout);
            }
        }
        break;

    // 当前处于已连接状态
    case NAT_CONNECTED:

        // 发送心跳保活包
        if (now - n->last_send_time >= PING_INTERVAL_MS) {
            udp_send_packet(sock, &n->peer_pub_addr, P2P_PKT_PING, 0, 0, NULL, 0);
            n->last_send_time = now;
        }

        // 超时检查
        if (n->last_recv_time > 0 &&
            now - n->last_recv_time >= PONG_TIMEOUT_MS) {

            n->state = NAT_IDLE;        // 标记连接 lost。可以尝试重新打洞，或者标记失败
            return -1;
        }
        break;

    // 当前处于中继转发模式
    // + 即使走服务器中转，也可以偶尔尝试直接发包给 Peer，万一网络状况变化（或 NAT 映射生效），可能恢复直连
    case NAT_RELAY:

        if (now - n->last_send_time >= PUNCH_INTERVAL_MS * 4) {
            udp_send_packet(sock, &n->peer_pub_addr, P2P_PKT_PUNCH, 0, 0, NULL, 0);
            n->last_send_time = now;
        }
        break;

    default:
        break;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////

void nat_init(nat_ctx_t *n) {
    memset(n, 0, sizeof(*n));
    n->state = NAT_IDLE;
}

// 向信令（Signaling）服务器发起连接注册。即 <自己的 ID + 请求连接的目标 ID>
int nat_start(nat_ctx_t *n, const char *local_peer_id, const char *remote_peer_id,
              int sock, const struct sockaddr_in *server, int verbose) {
    if (n->state != NAT_IDLE) return -1;

    n->server_addr = *server;
    n->verbose = verbose;
    memset(n->local_peer_id, 0, sizeof(n->local_peer_id));
    memset(n->remote_peer_id, 0, sizeof(n->remote_peer_id));
    strncpy(n->local_peer_id, local_peer_id, P2P_PEER_ID_MAX - 1);
    strncpy(n->remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX - 1);

    n->state = NAT_REGISTERING;
    n->last_send_time = time_ms();
    n->punch_attempts = 0;

    if (n->verbose) {
        printf("[NAT_PUNCH] START: Registering '%s' -> '%s' with server %s:%d\n",
               local_peer_id, remote_peer_id,
               inet_ntoa(server->sin_addr), ntohs(server->sin_port));
        fflush(stdout);
    }

    // 构造并发送数据包
    // payload 格式：[local_peer_id (32 bytes)] [remote_peer_id (32 bytes)]
    // 服务器通过 local_peer_id 建立映射，通过 remote_peer_id 查找目标地址
    uint8_t payload[P2P_PEER_ID_MAX * 2] = {0};
    memcpy(payload, local_peer_id, strlen(local_peer_id));
    memcpy(payload + P2P_PEER_ID_MAX, remote_peer_id, strlen(remote_peer_id));
    udp_send_packet(sock, server, P2P_PKT_REGISTER, 0, 0, payload, P2P_PEER_ID_MAX * 2);

    return 0;
}

// 解析从信令（Signaling）服务器下发的 PEER_INFO 包
// + 格式: [ pub_ip: 4 | pub_port: 2 | priv_ip: 4 | priv_port: 2 ] = 12 bytes
static int parse_peer_info(const uint8_t *payload, int len,
                           struct sockaddr_in *pub, struct sockaddr_in *priv) {
    if (len < 12) return -1;

    memset(pub, 0, sizeof(*pub));
    pub->sin_family = AF_INET;
    memcpy(&pub->sin_addr.s_addr, payload, 4);
    memcpy(&pub->sin_port, payload + 4, 2);  /* already network order */

    memset(priv, 0, sizeof(*priv));
    priv->sin_family = AF_INET;
    memcpy(&priv->sin_addr.s_addr, payload + 6, 4);
    memcpy(&priv->sin_port, payload + 10, 2);

    return 0;
}

// 处理从 NAT 链路层接收到的数据包（可能来自信令服务器，也可以能来自对端）
int nat_on_packet(nat_ctx_t *n, uint8_t type, const uint8_t *payload, int len,
                  const struct sockaddr_in *from, int sock) {
    uint64_t now = time_ms();

    switch (type) {

    // 信令服务器下发的对方地址信息
    // 支持三种场景：
    //   1. 首次匹配：收到对方地址，进入打洞阶段
    //   2. 重复请求：已配对后对方再次注册，更新地址信息
    //   3. 地址变化：对方公网地址变化，服务器主动推送更新
    case P2P_PKT_PEER_INFO: {
        struct sockaddr_in new_pub, new_priv;
        if (parse_peer_info(payload, len, &new_pub, &new_priv) < 0)
            return -1;

        if (n->verbose) {
            printf("[NAT_PUNCH] PEER_INFO: Received peer address\n");
            printf("            Public:  %s:%d\n",
                   inet_ntoa(new_pub.sin_addr), ntohs(new_pub.sin_port));
            printf("            Private: %s:%d\n",
                   inet_ntoa(new_priv.sin_addr), ntohs(new_priv.sin_port));
            fflush(stdout);
        }

        // 检测地址是否变化
        int addr_changed = (n->peer_pub_addr.sin_addr.s_addr != new_pub.sin_addr.s_addr ||
                           n->peer_pub_addr.sin_port != new_pub.sin_port);

        // 更新对端地址
        n->peer_pub_addr = new_pub;
        n->peer_priv_addr = new_priv;

        switch (n->state) {
        case NAT_REGISTERING:
            // 首次收到对方地址，进入打洞阶段
            if (n->verbose) {
                printf("[NAT_PUNCH] STATE: REGISTERING -> PUNCHING\n");
                fflush(stdout);
            }
            n->state = NAT_PUNCHING;
            n->punch_start = now;
            n->punch_attempts = 0;
            n->last_send_time = 0;  // 立即开始打洞
            break;

        case NAT_PUNCHING:
            // 打洞中收到地址更新，重置打洞参数
            if (addr_changed) {
                if (n->verbose) {
                    printf("[NAT_PUNCH] Peer address changed during punching, restarting...\n");
                    fflush(stdout);
                }
                n->punch_start = now;
                n->punch_attempts = 0;
                n->last_send_time = 0;
            }
            break;

        case NAT_CONNECTED:
            // 已连接状态下收到地址更新（对方地址变化）
            if (addr_changed) {
                if (n->verbose) {
                    printf("[NAT_PUNCH] Peer address changed, re-punching...\n");
                    fflush(stdout);
                }
                // 尝试向新地址打洞验证
                udp_send_packet(sock, &n->peer_pub_addr, P2P_PKT_PUNCH, 0, 0, NULL, 0);
                n->last_send_time = now;
            }
            break;

        case NAT_RELAY:
            // 中继模式下收到地址更新，尝试直连打洞
            if (addr_changed) {
                if (n->verbose) {
                    printf("[NAT_PUNCH] STATE: RELAY -> PUNCHING (peer address changed)\n");
                    fflush(stdout);
                }
                n->state = NAT_PUNCHING;
                n->punch_start = now;
                n->punch_attempts = 0;
                n->last_send_time = 0;
            }
            break;

        default:
            break;
        }
        return 0;
    }

    // 对方发来的打洞请求包
    case P2P_PKT_PUNCH:

        if (n->verbose) {
            printf("[NAT_PUNCH] PUNCH: Received from %s:%d, sending ACK\n",
                   inet_ntoa(from->sin_addr), ntohs(from->sin_port));
            fflush(stdout);
        }

        // 回复应答包（向对方公网地址发送）
        udp_send_packet(sock, from, P2P_PKT_PUNCH_ACK, 0, 0, NULL, 0);

    // 对方回复的打洞应答包
    case P2P_PKT_PUNCH_ACK: {
        
        if (n->verbose && type == P2P_PKT_PUNCH_ACK) {
            printf("[NAT_PUNCH] PUNCH_ACK: Received from %s:%d\n",
                   inet_ntoa(from->sin_addr), ntohs(from->sin_port));
            fflush(stdout);
        }
        
        // 如果开启 ICE，通知 ICE 层路径通了
        struct p2p_session *s = (struct p2p_session *)((char *)n - offsetof(struct p2p_session, nat));
        if (s->cfg.use_ice) {
            p2p_ice_on_check_success(s, from);
        }

        // 标记状态为 "已连接"
        if (n->state == NAT_PUNCHING || n->state == NAT_RELAY) {
            n->peer_pub_addr = *from;   // 记录（更新）对方的公网地址
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
    }

    case P2P_PKT_PING:

        // 回复应答包 PONG
        udp_send_packet(sock, from, P2P_PKT_PONG, 0, 0, NULL, 0);

    case P2P_PKT_PONG:
        
        // 心跳响应: 更新最后接收时间
        n->last_recv_time = now;
        return 0;

    default:
        return 0;
    }
}

