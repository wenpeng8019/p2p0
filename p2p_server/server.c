/*
 * p2p_server/server.c — P2P 信令服务器
 *
 * 支持两种模式：
 *   - RELAY 模式 (TCP): 对应 p2p_signal_relay 模块，用于 ICE 候选交换和信令转发
 *   - SIMPLE 模式 (UDP): 对应 p2p_trans_simple 模块，简单的地址交换
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
#include <p2pp.h>              /* 协议定义 */

#define MAX_PEERS               128

// SIMPLE 模式配对超时时间（秒）
#define SIMPLE_PAIR_TIMEOUT     30

// RELAY 模式心跳超时时间（秒）
// 如果客户端超过此时间未发送任何消息（包括心跳），服务器将主动断开连接
#define RELAY_CLIENT_TIMEOUT    60

// 服务端候选缓存限制（独立于客户端）
#define SIMPLE_MAX_CANDIDATES   8

// SIMPLE 模式配对记录（UDP 无状态）
// 注意：SIMPLE 模式采用"配对缓存"机制：
//   A 注册 (local=alice, remote=bob, candidates=[...])
//   B 注册 (local=bob, remote=alice, candidates=[...])
//   服务器检测到双向匹配后，同时向 A 和 B 发送对方的候选列表
// peer 指针状态：
//   NULL: 未配对
//   有效指针: 已配对，指向对端
//   (void*)-1: 对端已断开
typedef struct simple_pair_s {
    char local_peer_id[P2P_PEER_ID_MAX];                        // 本端 ID
    char remote_peer_id[P2P_PEER_ID_MAX];                       // 目标对端 ID
    struct sockaddr_in addr;                                    // 公网地址（UDP 源地址）
    p2p_simple_candidate_t candidates[SIMPLE_MAX_CANDIDATES];   // 候选列表
    int candidate_count;                                        // 候选数量
    time_t last_seen;                                           // 最后活跃时间
    bool valid;                                                 // 记录是否有效（true=有效, false=已过期或空闲）
    struct simple_pair_s *peer;                                 // 指向配对的对端
} simple_pair_t;

// RELAY 模式客户端（TCP 长连接，对应 p2p_signal_relay 模块）
typedef struct {
    int fd;
    char name[P2P_PEER_ID_MAX];
    time_t last_active;                     // 最后活跃时间（用于检测死连接）
    bool valid;                             // 客户端是否有效（true=已连接, false=已断开）
} relay_client_t;

static simple_pair_t            g_simple_pairs[MAX_PEERS];
static relay_client_t           g_relay_clients[MAX_PEERS];

// 处理 RELAY 模式信令（TCP 长连接，对应 p2p_signal_relay 模块）
void handle_relay_signaling(int idx) {
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

        p2p_relay_hdr_t ack = {P2P_RLY_MAGIC, P2P_RLY_LOGIN_ACK, 0};
        send(fd, &ack, sizeof(ack), 0);
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
            printf("[TCP] Target %s not found\n", target_name);
            ack_status = 1;  /* 目标不在线 */
            /* TODO: 可以在此存储候选，等目标上线后转发 */
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

// 处理 SIMPLE 模式信令（UDP 无状态）
// 新协议格式支持候选列表：
//   注册包: [local_peer_id(32)][remote_peer_id(32)][candidate_count(1)][candidates(N*7)]
//   响应包: [hdr(4)][candidate_count(1)][candidates(N*7)]
void handle_simple_signaling(int udp_fd, uint8_t *buf, int len, struct sockaddr_in *from) {
    
    if (len < 4) return;  // 至少需要包头
    
    p2p_packet_hdr_t *hdr = (p2p_packet_hdr_t *)buf;
    uint8_t *payload = buf + 4;
    int payload_len = len - 4;
    
    char from_str[64];
    snprintf(from_str, sizeof(from_str), "%s:%d", 
             inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    
    // P2P_PKT_REGISTER: [local_peer_id(32)][remote_peer_id(32)][candidate_count(1)][candidates(N*7)]
    if (hdr->type == P2P_PKT_REGISTER && payload_len >= P2P_PEER_ID_MAX * 2 + 1) {
        
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
        p2p_simple_candidate_t candidates[SIMPLE_MAX_CANDIDATES];
        memset(candidates, 0, sizeof(candidates));
        
        int cand_offset = P2P_PEER_ID_MAX * 2;
        if (payload_len > cand_offset) {
            candidate_count = payload[cand_offset];
            if (candidate_count > SIMPLE_MAX_CANDIDATES) {
                candidate_count = SIMPLE_MAX_CANDIDATES;
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
            if (g_simple_pairs[i].valid && 
                strcmp(g_simple_pairs[i].local_peer_id, local_peer_id) == 0 &&
                strcmp(g_simple_pairs[i].remote_peer_id, remote_peer_id) == 0) {
                local_idx = i;
                break;
            }
        }
        
        // 如果配对不存在，分配一个空位
        if (local_idx == -1) {
            for (int i = 0; i < MAX_PEERS; i++) {
                if (!g_simple_pairs[i].valid) { 
                    local_idx = i; 
                    g_simple_pairs[i].peer = NULL;
                    break; 
                }
            }
        }
        
        // 检测地址是否变化
        int addr_changed = 0;
        if (local_idx >= 0) {
            if (g_simple_pairs[local_idx].valid) {
                addr_changed = (memcmp(&g_simple_pairs[local_idx].addr, from, sizeof(*from)) != 0);
            }
            
            // 记录本端的注册信息（包括候选列表）
            strncpy(g_simple_pairs[local_idx].local_peer_id, local_peer_id, P2P_PEER_ID_MAX);
            strncpy(g_simple_pairs[local_idx].remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX);
            g_simple_pairs[local_idx].addr = *from;
            g_simple_pairs[local_idx].candidate_count = candidate_count;
            memcpy(g_simple_pairs[local_idx].candidates, candidates, sizeof(candidates));
            g_simple_pairs[local_idx].last_seen = time(NULL);
            g_simple_pairs[local_idx].valid = true;
            
            if (g_simple_pairs[local_idx].peer == (simple_pair_t*)(void*)-1) {
                g_simple_pairs[local_idx].peer = NULL;
            }
        } else {
            // 无法分配槽位，发送错误 ACK
            uint8_t ack_response[8];
            p2p_packet_hdr_t *ack_hdr = (p2p_packet_hdr_t *)ack_response;
            ack_hdr->type = P2P_PKT_REGISTER_ACK;
            ack_hdr->flags = 0;
            ack_hdr->seq = 0;
            ack_response[4] = 1;  /* status = error */
            ack_response[5] = 0;  /* flags = 0 */
            ack_response[6] = 0;
            ack_response[7] = 0;
            sendto(udp_fd, ack_response, 8, 0, (struct sockaddr *)from, sizeof(*from));
            printf("[UDP] REGISTER_ACK to %s: error (no slot available)\n", from_str);
            fflush(stdout);
            return;
        }
        
        // 查找反向配对
        int remote_idx = -1;
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_simple_pairs[i].valid && 
                strcmp(g_simple_pairs[i].local_peer_id, remote_peer_id) == 0 &&
                strcmp(g_simple_pairs[i].remote_peer_id, local_peer_id) == 0) {
                remote_idx = i;
                break;
            }
        }
        
        // 构造并发送 REGISTER_ACK
        // 格式: [hdr(4)][status(1)][max_candidates(1)][public_ip(4)][public_port(2)]
        {
            uint8_t ack_response[12];
            p2p_packet_hdr_t *ack_hdr = (p2p_packet_hdr_t *)ack_response;
            ack_hdr->type = P2P_PKT_REGISTER_ACK;
            ack_hdr->flags = 0;
            ack_hdr->seq = 0;
            
            /* status: 0=成功/对端离线, 1=成功/对端在线 */
            uint8_t status = (remote_idx >= 0) ? P2P_REGACK_PEER_ONLINE : P2P_REGACK_PEER_OFFLINE;
            uint8_t max_cands = SIMPLE_MAX_CANDIDATES;  /* 服务器缓存能力（0=不支持） */
            
            ack_response[4] = status;
            ack_response[5] = max_cands;  /* max_candidates */
            
            /* 填入客户端的公网地址（服务器观察到的 UDP 源地址）*/
            memcpy(ack_response + 6, &from->sin_addr.s_addr, 4);   /* public_ip */
            memcpy(ack_response + 10, &from->sin_port, 2);          /* public_port */
            
            sendto(udp_fd, ack_response, 12, 0, (struct sockaddr *)from, sizeof(*from));
            
            printf("[UDP] REGISTER_ACK to %s: ok, peer_online=%d, max_cands=%d, public=%s:%d\n", 
                   from_str, (remote_idx >= 0) ? 1 : 0, max_cands,
                   inet_ntoa(from->sin_addr), ntohs(from->sin_port));
            fflush(stdout);
        }
        
        if (remote_idx >= 0) {
            simple_pair_t *local = &g_simple_pairs[local_idx];
            simple_pair_t *remote = &g_simple_pairs[remote_idx];
            
            int first_match = (local->peer == NULL || remote->peer == NULL);
            
            if (first_match) {
                local->peer = remote;
                remote->peer = local;
            }
            
            // 构造 P2P_PKT_PEER_INFO 响应（序列化传输首包 seq=1）
            // 格式: [hdr(4)][base_index(1)][candidate_count(1)][candidates(N*7)]
            uint8_t response[4 + 2 + SIMPLE_MAX_CANDIDATES * 7];
            p2p_packet_hdr_t *resp_hdr = (p2p_packet_hdr_t *)response;
            resp_hdr->type = P2P_PKT_PEER_INFO;
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
            if (first_match || (addr_changed && remote->peer == local && remote->peer != (simple_pair_t*)(void*)-1)) {
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

// 清理过期的 SIMPLE 模式配对记录
void cleanup_simple_pairs(void) {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_simple_pairs[i].valid && (now - g_simple_pairs[i].last_seen) > SIMPLE_PAIR_TIMEOUT) {
            printf("[UDP] Peer pair (%s → %s) timed out\n", 
                   g_simple_pairs[i].local_peer_id, g_simple_pairs[i].remote_peer_id);
            
            // 如果有配对对端，标记对端的 peer 为 (void*)-1
            if (g_simple_pairs[i].peer != NULL && g_simple_pairs[i].peer != (simple_pair_t*)(void*)-1) {
                g_simple_pairs[i].peer->peer = (simple_pair_t*)(void*)-1;
            }
            
            g_simple_pairs[i].valid = false;
            g_simple_pairs[i].peer = NULL;  // 清空指针
        }
    }
}

// 清理过期的 Relay 模式客户端连接（检测死连接）
void cleanup_relay_clients(void) {
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

int main(int argc, char *argv[]) {

    int port = 8888;
    if (argc > 1) port = atoi(argv[1]);

    // 创建 TCP 监听套接口（Relay 模式）
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 创建 UDP 套接口（SIMPLE 模式）
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
            cleanup_simple_pairs();  // 清理 SIMPLE 模式配对
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
                handle_simple_signaling(udp_fd, buf, n, &from);
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
