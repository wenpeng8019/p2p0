/*
 * P2P 信令服务器
 *
 * 支持两种信令模式：
 *
 * 1. COMPACT 模式 (UDP)
 *    - 对应客户端 p2p_signal_compact 模块
 *    - 无状态信令，基于 UDP 数据包交换
 *    - 紧凑集成：信令交换 + NAT端口检测 + 候选交换 + 数据中继，一个完整的 P2P 统一协议实现
 *
 * 2. RELAY 模式 (TCP)
 *    - 对应客户端 p2p_signal_relay 模块
 *    - 有状态信令，基于 TCP 长连接
 *    - 支持在线状态查询、以及基本数据中转功能，用于支持 ICE/STUN/TURN 协议架构实现的信令服务器
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <stdbool.h>
#include <p2p.h>
#include <p2pp.h>

#define MAX_PEERS                   128

// COMPACT 模式配对超时时间（秒）
#define COMPACT_PAIR_TIMEOUT        30

// RELAY 模式心跳超时时间（秒）
// 如果客户端超过此时间未发送任何消息（包括心跳），服务器将主动断开连接
#define RELAY_CLIENT_TIMEOUT        60

// 服务端候选缓存限制（独立于客户端）
#define COMPACT_MAX_CANDIDATES      8

// COMPACT 模式配对记录（UDP 无状态）
/* 注意：COMPACT 模式采用"配对缓存"机制：
 *   A 注册 (local=alice, remote=bob, candidates=[...])
 *   B 注册 (local=bob, remote=alice, candidates=[...])
 *   服务器检测到双向匹配后，同时向 A 和 B 发送对方的候选列表
 */
typedef struct compact_pair {
    bool                    valid;                              // 记录是否有效（无效意味着未分配或已回收）
    char                    local_peer_id[P2P_PEER_ID_MAX];     // 本端 ID
    char                    remote_peer_id[P2P_PEER_ID_MAX];    // 目标对端 ID
    struct sockaddr_in      addr;                               // 公网地址（UDP 源地址）
    p2p_compact_candidate_t candidates[COMPACT_MAX_CANDIDATES]; // 候选列表
    int                     candidate_count;                    // 候选数量
    struct compact_pair*    peer;                               // 指向配对的对端。(void*)-1 表示对端已断开
    time_t                  last_active;                        // 最后活跃时间
} compact_pair_t;

// RELAY 模式待转发缓存项
typedef struct {
    char     sender_name[P2P_PEER_ID_MAX];  // 发送者名称
    uint8_t  payload[2048];                 // 序列化的候选数据
    uint32_t length;                        // 负载长度
    time_t   timestamp;                     // 缓存时间戳
} pending_offer_t;

#define RELAY_MAX_PENDING_OFFERS  16        // 每个用户最多缓存 16 个待转发 OFFER

// RELAY 模式客户端（TCP 长连接）
typedef struct relay_client {
    bool                    valid;                              // 客户端是否有效（无效意味着未分配或已回收）
    char                    name[P2P_PEER_ID_MAX];
    int                     fd;
    time_t                  last_active;                        // 最后活跃时间（用于检测死连接）
    
    // ===== 离线缓存队列 =====
    pending_offer_t         pending_offers[RELAY_MAX_PENDING_OFFERS];  // 待转发的 OFFER 队列
    int                     pending_count;                              // 已缓存的 OFFER 数量
} relay_client_t;

static compact_pair_t       g_compact_pairs[MAX_PEERS];
static relay_client_t       g_relay_clients[MAX_PEERS];

// 服务器配置（运行时参数）
static int                  g_probe_port = 0;                   // compact 模式 NAT 探测端口（0=不支持探测）
static bool                 g_relay_enabled = false;            // compact 模式是否支持中继功能

///////////////////////////////////////////////////////////////////////////////

// 处理 RELAY 模式信令（TCP 长连接，对应 p2p_signal_relay 模块）
static void handle_relay_signaling(int idx) {

    p2p_relay_hdr_t hdr;
    int fd = g_relay_clients[idx].fd;

    // 更新最后活跃时间（收到任何数据都表示连接活跃）
    g_relay_clients[idx].last_active = time(NULL);

    int n = recv(fd, &hdr, sizeof(hdr), 0);
    if (n <= 0) {
        printf("[TCP] Peer %s disconnected\n", g_relay_clients[idx].name);
        close(fd);
        g_relay_clients[idx].valid = false;
        return;
    }

    // Debug: print received bytes
    printf("[DEBUG] Received %d bytes: magic=0x%08X, type=%d, length=%d (expected magic=0x%08X)\n",
           n, hdr.magic, hdr.type, hdr.length, P2P_RLY_MAGIC);

    if (hdr.magic != P2P_RLY_MAGIC) {
        printf("[TCP] Invalid magic from peer\n");
        close(fd);
        g_relay_clients[idx].valid = false;
        return;
    }

    // 用户请求登录
    if (hdr.type == P2P_RLY_LOGIN) {
        p2p_relay_login_t login;
        recv(fd, &login, sizeof(login), 0);
        strncpy(g_relay_clients[idx].name, login.name, P2P_PEER_ID_MAX);
        g_relay_clients[idx].valid = true;
        printf("[TCP] Peer '%s' logged in (fd: %d)\n", g_relay_clients[idx].name, fd);
        fflush(stdout);

        // 发送登录确认
        p2p_relay_hdr_t ack = {P2P_RLY_MAGIC, P2P_RLY_LOGIN_ACK, 0};
        send(fd, &ack, sizeof(ack), 0);
        
        // 检查是否有待转发的缓存 OFFER
        if (g_relay_clients[idx].pending_count > 0) {
            relay_client_t *client = &g_relay_clients[idx];
            
            printf("[TCP] Flushing %d pending OFFERs to '%s'...\n", 
                   client->pending_count, client->name);
            fflush(stdout);
            
            for (int i = 0; i < client->pending_count; i++) {
                pending_offer_t *offer = &client->pending_offers[i];
                
                // 转发为 P2P_RLY_OFFER
                p2p_relay_hdr_t relay_hdr = {
                    P2P_RLY_MAGIC,
                    P2P_RLY_OFFER,
                    (uint32_t)(P2P_PEER_ID_MAX + offer->length)
                };
                
                send(fd, &relay_hdr, sizeof(relay_hdr), 0);
                send(fd, offer->sender_name, P2P_PEER_ID_MAX, 0);  // 发送者名称
                send(fd, offer->payload, offer->length, 0);        // 负载数据
                
                printf("[TCP]   → Forwarded OFFER from '%s' (%u bytes, cached at %ld)\n",
                       offer->sender_name, offer->length, offer->timestamp);
                fflush(stdout);
            }
            
            // 清空缓存
            client->pending_count = 0;
            printf("[TCP] All pending OFFERs flushed to '%s'\n", client->name);
            fflush(stdout);
        }
    }
    // 信令转发：P2P_RLY_CONNECT → P2P_RLY_OFFER，P2P_RLY_ANSWER → P2P_RLY_FORWARD
    else if (hdr.type == P2P_RLY_CONNECT || hdr.type == P2P_RLY_ANSWER) {

        // 接收目标对端名称
        char target_name[P2P_PEER_ID_MAX];
        if (recv(fd, target_name, P2P_PEER_ID_MAX, 0) != P2P_PEER_ID_MAX) {
            printf("[TCP] Failed to receive target name from %s\n", g_relay_clients[idx].name);
            close(fd);
            g_relay_clients[idx].valid = false;
            return;
        }
        
        // 接收信令负载数据
        uint32_t payload_len = hdr.length - P2P_PEER_ID_MAX;
        if (payload_len > 65536) {  // 防止过大的负载
            printf("[TCP] Payload too large (%u bytes) from %s\n", payload_len, g_relay_clients[idx].name);
            close(fd);
            g_relay_clients[idx].valid = false;
            return;
        }
        
        uint8_t *payload = malloc(payload_len);
        if (recv(fd, payload, payload_len, 0) != (int)payload_len) {
            printf("[TCP] Failed to receive payload from %s\n", g_relay_clients[idx].name);
            free(payload);
            close(fd);
            g_relay_clients[idx].valid = false;
            return;
        }

        printf("[TCP] Relaying %s from %s to %s (%u bytes)\n", 
               hdr.type == P2P_RLY_CONNECT ? "CONNECT" : "ANSWER",
               g_relay_clients[idx].name, target_name, payload_len);
        fflush(stdout);

        /*
         * 从负载中提取候选数量
         * 负载格式（p2p_signaling_payload_t 序列化后）：
         *   [sender: 32B][target: 32B][timestamp: 4B][delay_trigger: 4B][candidate_count: 4B][...]
         * candidate_count 位于偏移量 32+32+4+4 = 72
         */
        uint8_t candidates_in_payload = 0;
        if (payload_len >= 76) {  /* 至少包含到 candidate_count */
            uint32_t count;
            memcpy(&count, payload + 72, 4);
            candidates_in_payload = (count > 255) ? 255 : (uint8_t)count;
        }

        // 查找目标客户端并转发
        bool found = false;
        uint8_t ack_status = 0;
        uint8_t candidates_stored = candidates_in_payload;  /* 默认全部转发成功 */

        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_relay_clients[i].valid && strcmp(g_relay_clients[i].name, target_name) == 0) {

                // 根据消息类型选择转发类型
                // P2P_RLY_CONNECT → P2P_RLY_OFFER（通知对方有人要连接）
                // P2P_RLY_ANSWER → P2P_RLY_FORWARD（转发应答）
                uint8_t relay_type = (hdr.type == P2P_RLY_CONNECT) ? P2P_RLY_OFFER : P2P_RLY_FORWARD;
                
                p2p_relay_hdr_t relay_hdr = {P2P_RLY_MAGIC, 
                    relay_type,
                    (uint32_t)(P2P_PEER_ID_MAX + payload_len)
                };
                send(g_relay_clients[i].fd, &relay_hdr, sizeof(relay_hdr), 0);
                send(g_relay_clients[i].fd, g_relay_clients[idx].name, P2P_PEER_ID_MAX, 0);  // 源客户端名称
                send(g_relay_clients[i].fd, payload, payload_len, 0);
                found = true;
                ack_status = 0;  /* 成功转发 */
                break;
            }
        }
        if (!found) {
            printf("[TCP] Target %s not found (offline)\n", target_name);
            
            // 查找或创建目标用户槽位（用于缓存）
            int target_idx = -1;
            for (int i = 0; i < MAX_PEERS; i++) {
                if (g_relay_clients[i].valid && strcmp(g_relay_clients[i].name, target_name) == 0) {
                    target_idx = i;
                    break;
                }
            }
            
            // 如果目标从未登录，分配一个离线槽位
            if (target_idx == -1) {
                for (int i = 0; i < MAX_PEERS; i++) {
                    if (!g_relay_clients[i].valid) {
                        target_idx = i;
                        g_relay_clients[i].valid = true;
                        g_relay_clients[i].fd = -1;  // 离线标识
                        strncpy(g_relay_clients[i].name, target_name, P2P_PEER_ID_MAX);
                        g_relay_clients[i].pending_count = 0;
                        g_relay_clients[i].last_active = time(NULL);
                        break;
                    }
                }
            }
            
            // 尝试缓存候选
            if (target_idx >= 0) {
                relay_client_t *target = &g_relay_clients[target_idx];
                
                if (target->pending_count < RELAY_MAX_PENDING_OFFERS) {
                    // 缓存 OFFER
                    pending_offer_t *slot = &target->pending_offers[target->pending_count];
                    strncpy(slot->sender_name, g_relay_clients[idx].name, P2P_PEER_ID_MAX);
                    slot->length = payload_len;
                    memcpy(slot->payload, payload, payload_len);
                    slot->timestamp = time(NULL);
                    target->pending_count++;
                    
                    ack_status = 1;  /* 目标不在线（已缓存） */
                    candidates_stored = candidates_in_payload;  /* 全部缓存成功 */
                    
                    printf("[TCP] Cached OFFER for offline user '%s' (%d/%d), from '%s', %d candidates\n",
                           target_name, target->pending_count, RELAY_MAX_PENDING_OFFERS,
                           g_relay_clients[idx].name, candidates_in_payload);
                    fflush(stdout);
                } else {
                    ack_status = 2;  /* 缓存已满 */
                    candidates_stored = 0;
                    printf("[TCP] Cannot cache OFFER for '%s': storage full (%d/%d)\n",
                           target_name, RELAY_MAX_PENDING_OFFERS, RELAY_MAX_PENDING_OFFERS);
                    fflush(stdout);
                }
            } else {
                ack_status = 3;  /* 服务器错误（无法分配槽位） */
                candidates_stored = 0;
                printf("[TCP] Cannot allocate slot for offline user '%s'\n", target_name);
                fflush(stdout);
            }
        }

        /* 仅对 P2P_RLY_CONNECT 发送 ACK（P2P_RLY_ANSWER 不需要确认） */
        if (hdr.type == P2P_RLY_CONNECT) {
            p2p_relay_hdr_t ack_hdr = {P2P_RLY_MAGIC, P2P_RLY_CONNECT_ACK, sizeof(p2p_relay_connect_ack_t)};
            p2p_relay_connect_ack_t ack_payload = {ack_status, candidates_stored, {0, 0}};
            send(fd, &ack_hdr, sizeof(ack_hdr), 0);
            send(fd, &ack_payload, sizeof(ack_payload), 0);
            printf("[TCP] Sent CONNECT_ACK to %s (status=%d, candidates=%d)\n", 
                   g_relay_clients[idx].name, ack_status, candidates_stored);
        }

        free(payload);
    }
    // 获取在线用户列表
    else if (hdr.type == P2P_RLY_LIST) {

        // 构造在线用户列表（逗号分隔）
        char list_buf[1024] = {0};
        int offset = 0;
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_relay_clients[i].valid && g_relay_clients[i].fd != fd) {
                int remaining = sizeof(list_buf) - offset;
                if (remaining < P2P_PEER_ID_MAX + 2) {  // 确保有足够空间
                    printf("[TCP] User list truncated (too many users)\n");
                    break;
                }
                int n = snprintf(list_buf + offset, remaining, "%s,", g_relay_clients[i].name);
                if (n >= remaining) {  // 检查是否被截断
                    break;
                }
                offset += n;
            }
        }
        
        p2p_relay_hdr_t res_hdr = {P2P_RLY_MAGIC, P2P_RLY_LIST_RES, (uint32_t)offset};
        send(fd, &res_hdr, sizeof(res_hdr), 0);
        if (offset > 0) {
            send(fd, list_buf, offset, 0);
        }
    }
    // 处理心跳
    else if (hdr.type == P2P_RLY_HEARTBEAT) {
        // 心跳的作用：
        // 1. 检测死连接（对方崩溃、网络断开等 TCP 无法检测的情况）
        // 2. 保持 NAT 映射（防止中间设备超时关闭）
        // 3. last_active 已在函数开头更新，这里无需额外处理
        
        // 可选：回复心跳响应（让客户端也能检测服务器状态）
        // p2p_relay_hdr_t ack = {P2P_RLY_MAGIC, P2P_RLY_HEARTBEAT, 0};
        // send(fd, &ack, sizeof(ack), 0);
    }
    // 未知消息类型
    else {
        printf("[TCP] Unknown message type %d from %s\n", hdr.type, g_relay_clients[idx].name);
    }
}

// 清理过期的 Relay 模式客户端连接（检测死连接）
static void cleanup_relay_clients(void) {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_relay_clients[i].valid) {
            // 检查是否超时（超过 RELAY_CLIENT_TIMEOUT 秒未收到任何消息）
            if ((now - g_relay_clients[i].last_active) > RELAY_CLIENT_TIMEOUT) {
                printf("[TCP] Client '%s' (fd:%d) timed out (no activity for %ld seconds)\n",
                       g_relay_clients[i].name, g_relay_clients[i].fd,
                       now - g_relay_clients[i].last_active);
                
                close(g_relay_clients[i].fd);
                g_relay_clients[i].valid = false;
            }
        }
    }
}

// 处理 COMPACT 模式信令（UDP 无状态）
static void handle_compact_signaling(int udp_fd, uint8_t *buf, int len, struct sockaddr_in *from) {
    
    if (len < 4) return;  // 至少需要包头
    
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)buf;
    uint8_t *payload = buf + 4;
    int payload_len = len - 4;
    
    char from_str[64];
    snprintf(from_str, sizeof(from_str), "%s:%d", 
             inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    
    // SIG_PKT_REGISTER: [local_peer_id(32)][remote_peer_id(32)][candidate_count(1)][candidates(N*7)]
    if (hdr->type == SIG_PKT_REGISTER && payload_len >= P2P_PEER_ID_MAX * 2 + 1) {
        
        // 解析 local_peer_id 和 remote_peer_id
        char local_peer_id[P2P_PEER_ID_MAX + 1] = {0};
        char remote_peer_id[P2P_PEER_ID_MAX + 1] = {0};
        memcpy(local_peer_id, payload, P2P_PEER_ID_MAX);
        memcpy(remote_peer_id, payload + P2P_PEER_ID_MAX, P2P_PEER_ID_MAX);
        
        // 确保字符串以 \0 结尾
        local_peer_id[P2P_PEER_ID_MAX] = '\0';
        remote_peer_id[P2P_PEER_ID_MAX] = '\0';
        
        // 解析候选列表（新格式）
        int candidate_count = 0;
        p2p_compact_candidate_t candidates[COMPACT_MAX_CANDIDATES];
        memset(candidates, 0, sizeof(candidates));
        
        int cand_offset = P2P_PEER_ID_MAX * 2;
        if (payload_len > cand_offset) {
            candidate_count = payload[cand_offset];
            if (candidate_count > COMPACT_MAX_CANDIDATES) {
                candidate_count = COMPACT_MAX_CANDIDATES;
            }
            cand_offset++;
            
            // 解析每个候选 (7 字节: type + ip + port)
            for (int i = 0; i < candidate_count && cand_offset + 7 <= payload_len; i++) {
                candidates[i].type = payload[cand_offset];
                memcpy(&candidates[i].ip, payload + cand_offset + 1, 4);
                memcpy(&candidates[i].port, payload + cand_offset + 5, 2);
                cand_offset += 7;
            }
        }
        
        printf("[UDP] REGISTER from %s: local='%s', remote='%s', candidates=%d\n", 
               from_str, local_peer_id, remote_peer_id, candidate_count);
        for (int i = 0; i < candidate_count; i++) {
            struct in_addr ip;
            ip.s_addr = candidates[i].ip;
            printf("      [%d] type=%d, %s:%d\n", i, candidates[i].type, 
                   inet_ntoa(ip), ntohs(candidates[i].port));
        }
        fflush(stdout);
        
        // 查找本端槽位
        int local_idx = -1;
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_compact_pairs[i].valid && 
                strcmp(g_compact_pairs[i].local_peer_id, local_peer_id) == 0 &&
                strcmp(g_compact_pairs[i].remote_peer_id, remote_peer_id) == 0) {
                local_idx = i;
                break;
            }
        }
        
        // 如果配对不存在，分配一个空位
        if (local_idx == -1) {
            for (int i = 0; i < MAX_PEERS; i++) {
                if (!g_compact_pairs[i].valid) { 
                    local_idx = i; 
                    g_compact_pairs[i].peer = NULL;
                    break; 
                }
            }
        }
        
        // 检测地址是否变化
        int addr_changed = 0;
        if (local_idx >= 0) {
            if (g_compact_pairs[local_idx].valid) {
                addr_changed = (memcmp(&g_compact_pairs[local_idx].addr, from, sizeof(*from)) != 0);
            }
            
            // 记录本端的注册信息（包括候选列表）
            strncpy(g_compact_pairs[local_idx].local_peer_id, local_peer_id, P2P_PEER_ID_MAX);
            strncpy(g_compact_pairs[local_idx].remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX);
            g_compact_pairs[local_idx].addr = *from;
            g_compact_pairs[local_idx].candidate_count = candidate_count;
            memcpy(g_compact_pairs[local_idx].candidates, candidates, sizeof(candidates));
            g_compact_pairs[local_idx].last_active = time(NULL);
            g_compact_pairs[local_idx].valid = true;
            
            if (g_compact_pairs[local_idx].peer == (compact_pair_t*)(void*)-1) {
                g_compact_pairs[local_idx].peer = NULL;
            }
        } else {
            // 无法分配槽位，发送错误 ACK
            uint8_t ack_response[14];
            p2p_packet_hdr_t *ack_hdr = (p2p_packet_hdr_t *)ack_response;
            ack_hdr->type = SIG_PKT_REGISTER_ACK;
            ack_hdr->flags = 0;
            ack_hdr->seq = 0;
            ack_response[4] = 2;  /* status = error */
            memset(ack_response + 5, 0, 9);  /* 其余字段全部置 0 */
            sendto(udp_fd, ack_response, 14, 0, (struct sockaddr *)from, sizeof(*from));
            printf("[UDP] REGISTER_ACK to %s: error (no slot available)\n", from_str);
            fflush(stdout);
            return;
        }
        
        // 查找反向配对
        int remote_idx = -1;
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_compact_pairs[i].valid && 
                strcmp(g_compact_pairs[i].local_peer_id, remote_peer_id) == 0 &&
                strcmp(g_compact_pairs[i].remote_peer_id, local_peer_id) == 0) {
                remote_idx = i;
                break;
            }
        }
        
        // 构造并发送 REGISTER_ACK
        // 格式: [hdr(4)][status(1)][max_candidates(1)][public_ip(4)][public_port(2)][probe_port(2)] = 14字节
        {
            uint8_t ack_response[14];
            p2p_packet_hdr_t *ack_hdr = (p2p_packet_hdr_t *)ack_response;
            ack_hdr->type = SIG_PKT_REGISTER_ACK;
            ack_hdr->flags = g_relay_enabled ? SIG_REGACK_FLAG_RELAY : 0;
            ack_hdr->seq = 0;
            
            /* status: 0=成功/对端离线, 1=成功/对端在线 */
            uint8_t status = (remote_idx >= 0) ? SIG_REGACK_PEER_ONLINE : SIG_REGACK_PEER_OFFLINE;
            uint8_t max_cands = COMPACT_MAX_CANDIDATES;  /* 服务器缓存能力（0=不支持） */
            
            ack_response[4] = status;
            ack_response[5] = max_cands;    /* max_candidates */
            
            /* 填入客户端的公网地址（服务器主端口观察到的 UDP 源地址）*/
            memcpy(ack_response + 6, &from->sin_addr.s_addr, 4);   /* public_ip */
            memcpy(ack_response + 10, &from->sin_port, 2);          /* public_port */
            
            /* NAT 探测端口（根据配置填入，0=不支持探测）*/
            uint16_t probe_port_net = htons(g_probe_port);
            memcpy(ack_response + 12, &probe_port_net, 2);         /* probe_port */
            
            sendto(udp_fd, ack_response, 14, 0, (struct sockaddr *)from, sizeof(*from));
            
            printf("[UDP] REGISTER_ACK to %s: ok, peer_online=%d, max_cands=%d, relay=%s, public=%s:%d, probe_port=%d\n", 
                   from_str, (remote_idx >= 0) ? 1 : 0, max_cands,
                   g_relay_enabled ? "yes" : "no",
                   inet_ntoa(from->sin_addr), ntohs(from->sin_port), g_probe_port);
            fflush(stdout);
        }
        
        if (remote_idx >= 0) {
            compact_pair_t *local = &g_compact_pairs[local_idx];
            compact_pair_t *remote = &g_compact_pairs[remote_idx];
            
            int first_match = (local->peer == NULL || remote->peer == NULL);
            
            if (first_match) {
                local->peer = remote;
                remote->peer = local;
            }
            
            // 构造 SIG_PKT_PEER_INFO 响应（序列化传输首包 seq=1）
            // 格式: [hdr(4)][base_index(1)][candidate_count(1)][candidates(N*7)]
            uint8_t response[4 + 2 + COMPACT_MAX_CANDIDATES * 7];
            p2p_packet_hdr_t *resp_hdr = (p2p_packet_hdr_t *)response;
            resp_hdr->type = SIG_PKT_PEER_INFO;
            resp_hdr->flags = 0;
            resp_hdr->seq = htons(1);  /* seq=1 表示服务器发送的首包 */
            
            // 向当前请求方发送对方的候选列表
            response[4] = 0;  /* base_index = 0 (从第一个候选开始) */
            response[5] = (uint8_t)remote->candidate_count;
            int resp_len = 6;
            for (int i = 0; i < remote->candidate_count; i++) {
                response[resp_len] = remote->candidates[i].type;
                memcpy(response + resp_len + 1, &remote->candidates[i].ip, 4);
                memcpy(response + resp_len + 5, &remote->candidates[i].port, 2);
                resp_len += 7;
            }
            
            sendto(udp_fd, response, resp_len, 0, 
                   (struct sockaddr *)from, sizeof(*from));
            
            printf("[UDP] Sent PEER_INFO(seq=1, base=0) to %s (local='%s') with %d candidates%s\n", 
                   from_str, local_peer_id, remote->candidate_count,
                   first_match ? " [FIRST MATCH]" : "");
            fflush(stdout);
            
            // 首次匹配或地址变化：也通知对方本端的候选列表
            if (first_match || (addr_changed && remote->peer == local && remote->peer != (compact_pair_t*)(void*)-1)) {
                response[4] = 0;  /* base_index = 0 */
                response[5] = (uint8_t)local->candidate_count;
                resp_len = 6;
                for (int i = 0; i < local->candidate_count; i++) {
                    response[resp_len] = local->candidates[i].type;
                    memcpy(response + resp_len + 1, &local->candidates[i].ip, 4);
                    memcpy(response + resp_len + 5, &local->candidates[i].port, 2);
                    resp_len += 7;
                }
                
                sendto(udp_fd, response, resp_len, 0,
                       (struct sockaddr *)&remote->addr, sizeof(remote->addr));
                
                printf("[UDP] Sent PEER_INFO(seq=1, base=0) to %s:%d (local='%s') with %d candidates%s\n",
                       inet_ntoa(remote->addr.sin_addr), ntohs(remote->addr.sin_port),
                       remote_peer_id, local->candidate_count,
                       first_match ? " [BILATERAL]" : " [ADDR_CHANGED]");
                fflush(stdout);
            }
        } else {
            printf("[UDP] Target pair (%s → %s) not found (waiting for peer registration)\n", 
                   remote_peer_id, local_peer_id);
            fflush(stdout);
        }
    }
}

// 清理过期的 COMPACT 模式配对记录
static void cleanup_compact_pairs(void) {

    time_t now = time(NULL);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_compact_pairs[i].valid && (now - g_compact_pairs[i].last_active) > COMPACT_PAIR_TIMEOUT) {
            printf("[UDP] Peer pair (%s → %s) timed out\n", 
                   g_compact_pairs[i].local_peer_id, g_compact_pairs[i].remote_peer_id);
            
            // 如果有配对对端，标记对端的 peer 为 (void*)-1
            if (g_compact_pairs[i].peer != NULL && g_compact_pairs[i].peer != (compact_pair_t*)(void*)-1) {
                g_compact_pairs[i].peer->peer = (compact_pair_t*)(void*)-1;
            }
            
            g_compact_pairs[i].valid = false;
            g_compact_pairs[i].peer = NULL;  // 清空指针
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {

    int port = 8888;
    if (argc > 1) port = atoi(argv[1]);
    if (argc > 2) g_probe_port = atoi(argv[2]);
    if (argc > 3 && strcmp(argv[3], "relay") == 0) g_relay_enabled = true;
    
    printf("[SERVER] Starting P2P signal server on port %d\n", port);
    printf("[SERVER] NAT probe: %s (port %d)\n", g_probe_port > 0 ? "enabled" : "disabled", g_probe_port);
    printf("[SERVER] Relay support: %s\n", g_relay_enabled ? "enabled" : "disabled");
    fflush(stdout);

    // 创建 TCP 监听套接口（Relay 模式）
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 创建 UDP 套接口（COMPACT 模式）
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 绑定监听端口（TCP 和 UDP 同一端口）
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("TCP bind");
        return 1;
    }
    
    if (bind(udp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("UDP bind");
        return 1;
    }

    // 启动 TCP 监听
    listen(listen_fd, 10);
    printf("P2P Signaling Server listening on port %d (TCP + UDP)...\n", port);
    fflush(stdout);

    // 服务主过程
    fd_set read_fds;
    time_t last_cleanup = time(NULL);
    
    while (1) {

        // 定期清理过期记录
        time_t now = time(NULL);
        if (now - last_cleanup >= 10) {
            cleanup_compact_pairs();  // 清理 COMPACT 模式配对
            cleanup_relay_clients();   // 清理 RELAY 模式死连接
            last_cleanup = now;
        }

        // 构造监听集合：TCP listen + TCP clients + UDP
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        FD_SET(udp_fd, &read_fds);
        int max_fd = (listen_fd > udp_fd) ? listen_fd : udp_fd;

        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_relay_clients[i].valid) {
                FD_SET(g_relay_clients[i].fd, &read_fds);
                if (g_relay_clients[i].fd > max_fd) max_fd = g_relay_clients[i].fd;
            }
        }

        // 执行端口监听（超时 1 秒，用于定期清理）
        struct timeval tv = {1, 0};
        if (select(max_fd + 1, &read_fds, NULL, NULL, &tv) < 0) {
            perror("select");
            break;
        }

        // 收到新的 TCP 连接请求
        if (FD_ISSET(listen_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
            
            int i = 0;
            for (i = 0; i < MAX_PEERS; i++) {

                // 找一个空闲位置记录新连接
                if (!g_relay_clients[i].valid) {
                    g_relay_clients[i].fd = client_fd;
                    g_relay_clients[i].valid = true;
                    g_relay_clients[i].last_active = time(NULL);  // 初始化活跃时间
                    g_relay_clients[i].pending_count = 0;         // 初始化缓存计数
                    strncpy(g_relay_clients[i].name, "unknown", P2P_PEER_ID_MAX);
                    printf("[TCP] New connection from %s:%d\n", 
                           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    fflush(stdout);
                    break;
                }
            }
            if (i == MAX_PEERS) {
                printf("[TCP] Max peers reached, rejecting connection\n");
                close(client_fd);
            }
        }
        
        // 收到 UDP 数据包
        if (FD_ISSET(udp_fd, &read_fds)) {
            uint8_t buf[2048];
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            
            int n = recvfrom(udp_fd, buf, sizeof(buf), 0, 
                            (struct sockaddr *)&from, &from_len);
            if (n > 0) {
                handle_compact_signaling(udp_fd, buf, n, &from);
            }
        }

        // 处理 Relay 模式信令请求
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_relay_clients[i].valid && FD_ISSET(g_relay_clients[i].fd, &read_fds)) {
                handle_relay_signaling(i);
            }
        }
    }

    close(listen_fd);
    close(udp_fd);
    return 0;
}
