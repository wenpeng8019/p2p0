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
#include "protocol.h"

#define MAX_PEERS               128

// SIMPLE 模式配对超时时间（秒）
#define SIMPLE_PAIR_TIMEOUT     30

// RELAY 模式心跳超时时间（秒）
// 如果客户端超过此时间未发送任何消息（包括心跳），服务器将主动断开连接
#define RELAY_CLIENT_TIMEOUT    60

// SIMPLE 模式协议包类型
#define SIMPLE_PKT_REGISTER     0x01     // 注册请求
#define SIMPLE_PKT_PEER_INFO    0x02     // 对端信息

// SIMPLE 模式包头结构
typedef struct {
    uint8_t  type;
    uint8_t  flags;
    uint16_t seq;
} simple_pkt_hdr_t;

// SIMPLE 模式配对记录（UDP 无状态）
// 注意：SIMPLE 模式采用"配对缓存"机制：
//   A 注册 (local=alice, remote=bob)
//   B 注册 (local=bob, remote=alice)
//   服务器检测到双向匹配后，同时向 A 和 B 发送对方地址
// peer 指针状态：
//   NULL: 未配对
//   有效指针: 已配对，指向对端
//   (void*)-1: 对端已断开
typedef struct simple_pair_s {
    char local_peer_id[P2P_PEER_ID_MAX];    // 本端 ID
    char remote_peer_id[P2P_PEER_ID_MAX];   // 目标对端 ID
    struct sockaddr_in addr;                // 公网地址
    time_t last_seen;                       // 最后活跃时间
    bool valid;                             // 记录是否有效（true=有效, false=已过期或空闲）
    struct simple_pair_s *peer;             // 指向配对的对端
} simple_pair_t;

// RELAY 模式客户端（TCP 长连接，对应 p2p_signal_relay 模块）
typedef struct {
    int fd;
    char name[P2P_MAX_NAME];
    time_t last_active;                     // 最后活跃时间（用于检测死连接）
    bool valid;                             // 客户端是否有效（true=已连接, false=已断开）
} relay_client_t;

static simple_pair_t            g_simple_pairs[MAX_PEERS];
static relay_client_t           g_relay_clients[MAX_PEERS];

// 处理 RELAY 模式信令（TCP 长连接，对应 p2p_signal_relay 模块）
void handle_relay_signaling(int idx) {
    p2p_msg_hdr_t hdr;
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
           n, hdr.magic, hdr.type, hdr.length, P2P_SIGNAL_MAGIC);

    if (hdr.magic != P2P_SIGNAL_MAGIC) {
        printf("[TCP] Invalid magic from peer\n");
        close(fd);
        g_relay_clients[idx].valid = false;
        return;
    }

    // 用户请求登录
    if (hdr.type == MSG_LOGIN) {
        p2p_msg_login_t login;
        recv(fd, &login, sizeof(login), 0);
        strncpy(g_relay_clients[idx].name, login.name, P2P_MAX_NAME);
        g_relay_clients[idx].valid = true;
        printf("[TCP] Peer '%s' logged in (fd: %d)\n", g_relay_clients[idx].name, fd);
        fflush(stdout);

        p2p_msg_hdr_t ack = {P2P_SIGNAL_MAGIC, MSG_LOGIN_ACK, 0};
        send(fd, &ack, sizeof(ack), 0);
    }
    // 信令转发：MSG_CONNECT → MSG_SIGNAL，MSG_SIGNAL_ANS → MSG_SIGNAL_RELAY
    else if (hdr.type == MSG_CONNECT || hdr.type == MSG_SIGNAL_ANS) {

        // 接收目标对端名称
        char target_name[P2P_MAX_NAME];
        if (recv(fd, target_name, P2P_MAX_NAME, 0) != P2P_MAX_NAME) {
            printf("[TCP] Failed to receive target name from %s\n", g_relay_clients[idx].name);
            close(fd);
            g_relay_clients[idx].valid = false;
            return;
        }
        
        // 接收信令负载数据
        uint32_t payload_len = hdr.length - P2P_MAX_NAME;
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
               hdr.type == MSG_CONNECT ? "CONNECT" : "ANSWER",
               g_relay_clients[idx].name, target_name, payload_len);
        fflush(stdout);

        // 查找目标客户端并转发
        bool found = false;
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_relay_clients[i].valid && strcmp(g_relay_clients[i].name, target_name) == 0) {

                // 根据消息类型选择转发类型
                // MSG_CONNECT → MSG_SIGNAL（通知对方有人要连接）
                // MSG_SIGNAL_ANS → MSG_SIGNAL_RELAY（转发应答）
                uint8_t relay_type = (hdr.type == MSG_CONNECT) ? MSG_SIGNAL : MSG_SIGNAL_RELAY;
                
                p2p_msg_hdr_t relay_hdr = {P2P_SIGNAL_MAGIC, 
                    relay_type,
                    (uint32_t)(P2P_MAX_NAME + payload_len)
                };
                send(g_relay_clients[i].fd, &relay_hdr, sizeof(relay_hdr), 0);
                send(g_relay_clients[i].fd, g_relay_clients[idx].name, P2P_MAX_NAME, 0);  // 源客户端名称
                send(g_relay_clients[i].fd, payload, payload_len, 0);
                found = true;
                break;
            }
        }
        if (!found) {
            printf("[TCP] Target %s not found\n", target_name);
        }
        free(payload);
    }
    // 获取在线用户列表
    else if (hdr.type == MSG_LIST) {

        // 构造在线用户列表（逗号分隔）
        char list_buf[1024] = {0};
        int offset = 0;
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_relay_clients[i].valid && g_relay_clients[i].fd != fd) {
                int remaining = sizeof(list_buf) - offset;
                if (remaining < P2P_MAX_NAME + 2) {  // 确保有足够空间
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
        
        p2p_msg_hdr_t res_hdr = {P2P_SIGNAL_MAGIC, MSG_LIST_RES, (uint32_t)offset};
        send(fd, &res_hdr, sizeof(res_hdr), 0);
        if (offset > 0) {
            send(fd, list_buf, offset, 0);
        }
    }
    // 处理心跳
    else if (hdr.type == MSG_HEARTBEAT) {
        // 心跳的作用：
        // 1. 检测死连接（对方崩溃、网络断开等 TCP 无法检测的情况）
        // 2. 保持 NAT 映射（防止中间设备超时关闭）
        // 3. last_active 已在函数开头更新，这里无需额外处理
        
        // 可选：回复心跳响应（让客户端也能检测服务器状态）
        // p2p_msg_hdr_t ack = {P2P_SIGNAL_MAGIC, MSG_HEARTBEAT, 0};
        // send(fd, &ack, sizeof(ack), 0);
    }
    // 未知消息类型
    else {
        printf("[TCP] Unknown message type %d from %s\n", hdr.type, g_relay_clients[idx].name);
    }
}

// 处理 SIMPLE 模式信令（UDP 无状态）
void handle_simple_signaling(int udp_fd, uint8_t *buf, int len, struct sockaddr_in *from) {
    
    if (len < 4) return;  // 至少需要包头
    
    simple_pkt_hdr_t *hdr = (simple_pkt_hdr_t *)buf;
    uint8_t *payload = buf + 4;
    int payload_len = len - 4;
    
    char from_str[64];
    snprintf(from_str, sizeof(from_str), "%s:%d", 
             inet_ntoa(from->sin_addr), ntohs(from->sin_port));
    
    // SIMPLE_PKT_REGISTER: payload = [local_peer_id (32)] [remote_peer_id (32)]
    if (hdr->type == SIMPLE_PKT_REGISTER && payload_len >= P2P_PEER_ID_MAX * 2) {
        
        // 解析 local_pear_id 和 remote_peer_id
        char local_peer_id[P2P_PEER_ID_MAX + 1] = {0};
        char remote_peer_id[P2P_PEER_ID_MAX + 1] = {0};
        memcpy(local_peer_id, payload, P2P_PEER_ID_MAX);
        memcpy(remote_peer_id, payload + P2P_PEER_ID_MAX, P2P_PEER_ID_MAX);
        
        // 确保字符串以 \0 结尾（防止 peer_id 占满 32 字节）
        local_peer_id[P2P_PEER_ID_MAX] = '\0';
        remote_peer_id[P2P_PEER_ID_MAX] = '\0';
        
        printf("[UDP] REGISTER from %s: local_id='%s', remote_id='%s'\n", 
               from_str, local_peer_id, remote_peer_id);
        fflush(stdout);
        
        // 查找本端槽位（即配对：local_peer_id → remote_peer_id，且槽位有效）
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
                    g_simple_pairs[i].peer = NULL;  // 新配对，初始化为未配对
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
            
            // 记录本端的注册信息
            strncpy(g_simple_pairs[local_idx].local_peer_id, local_peer_id, P2P_PEER_ID_MAX);
            strncpy(g_simple_pairs[local_idx].remote_peer_id, remote_peer_id, P2P_PEER_ID_MAX);
            g_simple_pairs[local_idx].addr = *from;
            g_simple_pairs[local_idx].last_seen = time(NULL);
            g_simple_pairs[local_idx].valid = true;
            // peer 指针保持不变，除非对端已断开需要重新配对
            if (g_simple_pairs[local_idx].peer == (simple_pair_t*)(void*)-1) {
                g_simple_pairs[local_idx].peer = NULL;  // 对端已断开，重置为未配对
            }
        }
        
        // 查找反向配对（remote_peer_id → local_peer_id）
        // 只有找到反向配对才说明双方都已注册
        int remote_idx = -1;
        for (int i = 0; i < MAX_PEERS; i++) {
            if (g_simple_pairs[i].valid && 
                strcmp(g_simple_pairs[i].local_peer_id, remote_peer_id) == 0 &&
                strcmp(g_simple_pairs[i].remote_peer_id, local_peer_id) == 0) {
                remote_idx = i;
                break;
            }
        }
        
        if (remote_idx >= 0) {
            // 找到反向配对，说明双方都已注册
            simple_pair_t *local = &g_simple_pairs[local_idx];
            simple_pair_t *remote = &g_simple_pairs[remote_idx];
            
            // 判断是否首次匹配（任一方未配对）
            int first_match = (local->peer == NULL || remote->peer == NULL);
            
            // 建立双向指针关系
            if (first_match) {
                local->peer = remote;
                remote->peer = local;
            }
            
            // 构造 SIMPLE_PKT_PEER_INFO 响应
            uint8_t response[4 + 12];  // hdr + payload
            simple_pkt_hdr_t *resp_hdr = (simple_pkt_hdr_t *)response;
            resp_hdr->type = SIMPLE_PKT_PEER_INFO;
            resp_hdr->flags = 0;
            resp_hdr->seq = 0;
            
            // 向当前请求方发送对方地址
            struct sockaddr_in *target_addr = &remote->addr;
            memcpy(response + 4, &target_addr->sin_addr.s_addr, 4);      // 公网 IP
            memcpy(response + 8, &target_addr->sin_port, 2);              // 公网端口
            memcpy(response + 10, &target_addr->sin_addr.s_addr, 4);     // 私网 IP（暂用公网）
            memcpy(response + 14, &target_addr->sin_port, 2);             // 私网端口（暂用公网）
            
            // 情况2：返回对方 info（所有情况都需要）
            sendto(udp_fd, response, sizeof(response), 0, 
                   (struct sockaddr *)from, sizeof(*from));
            
            printf("[UDP] Sent PEER_INFO to %s (local='%s') for target '%s' (%s:%d)%s\n", 
                   from_str, local_peer_id, remote_peer_id,
                   inet_ntoa(target_addr->sin_addr), ntohs(target_addr->sin_port),
                   first_match ? " [FIRST MATCH]" : "");
            fflush(stdout);
            
            // 情况1（首次匹配）或情况3（地址变化）：通知对方本端地址
            if (first_match || (addr_changed && remote->peer == local && remote->peer != (simple_pair_t*)(void*)-1)) {
                struct sockaddr_in *my_addr = &local->addr;
                memcpy(response + 4, &my_addr->sin_addr.s_addr, 4);      // 公网 IP
                memcpy(response + 8, &my_addr->sin_port, 2);              // 公网端口
                memcpy(response + 10, &my_addr->sin_addr.s_addr, 4);     // 私网 IP（暂用公网）
                memcpy(response + 14, &my_addr->sin_port, 2);             // 私网端口（暂用公网）
                
                sendto(udp_fd, response, sizeof(response), 0,
                       (struct sockaddr *)target_addr, sizeof(*target_addr));
                
                printf("[UDP] Sent PEER_INFO to %s:%d (local='%s') for target '%s' (%s:%d)%s\n",
                       inet_ntoa(target_addr->sin_addr), ntohs(target_addr->sin_port),
                       remote_peer_id, local_peer_id,
                       inet_ntoa(my_addr->sin_addr), ntohs(my_addr->sin_port),
                       first_match ? " [BILATERAL]" : " [ADDR_CHANGED]");
                fflush(stdout);
            }
        } else {
            printf("[UDP] Target pair (%s → %s) not found (reverse pair needed)\n", 
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
                    strncpy(g_relay_clients[i].name, "unknown", P2P_MAX_NAME);
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
