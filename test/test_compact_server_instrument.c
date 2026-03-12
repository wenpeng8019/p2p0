/*
 * test_compact_server_instrument.c - COMPACT 服务器 instrument 监控测试
 *
 * 使用 stdc 分布式 instrument 机制监控 server 日志，并发送 REGISTER 协议包。
 *
 * 测试流程：
 *   1. 启动 instrument 监听（UDP 广播）
 *   2. 等待接收 server 启动日志
 *   3. 发送 REGISTER 包到 server
 *   4. 接收 REGISTER_ACK 并显示 server 处理日志
 *
 * 用法：
 *   1. 先在 DEBUG 模式编译并启动 p2p_server：
 *      ./p2p_server -p 9333
 *
 *   2. 运行本测试：
 *      ./test_compact_server_instrument [server_port]
 *
 * 注意：server 必须在 DEBUG 模式编译（未定义 NDEBUG）才会输出 instrument 日志。
 */

#define MOD_TAG "TEST"

#include <stdc.h>
#include <p2p.h>
#include <p2pp.h>
#include <stdio.h>
#include <string.h>

// 默认配置
#define DEFAULT_SERVER_PORT     9333
#define DEFAULT_SERVER_HOST     "127.0.0.1"
#define TEST_LOCAL_PEER_ID      "test_client_001"
#define TEST_REMOTE_PEER_ID     "test_client_002"

// 测试状态
static volatile int g_log_count = 0;
static volatile int g_register_ack_received = 0;

// instrument 日志回调
static void on_instrument_log(uint8_t chn, const char* tag, char *txt, int len) {
    (void)len;
    
    // 显示带颜色的日志
    const char* level_name;
    const char* color;
    switch (chn) {
        case LOG_SLOT_VERBOSE: level_name = "V"; color = "\033[37m"; break;
        case LOG_SLOT_DEBUG:   level_name = "D"; color = "\033[36m"; break;
        case LOG_SLOT_INFO:    level_name = "I"; color = "\033[32m"; break;
        case LOG_SLOT_WARN:    level_name = "W"; color = "\033[33m"; break;
        case LOG_SLOT_ERROR:   level_name = "E"; color = "\033[31m"; break;
        case LOG_SLOT_FATAL:   level_name = "F"; color = "\033[35m"; break;
        default:               level_name = "?"; color = "\033[0m";  break;
    }
    
    printf("%s[SERVER %s] %s: %s\033[0m\n", color, level_name, tag, txt);
    g_log_count++;
}

// 构造 REGISTER 包
static int build_register_packet(uint8_t *buf, int buf_size,
                                  const char *local_peer_id,
                                  const char *remote_peer_id,
                                  uint32_t instance_id) {
    if (buf_size < 4 + 32 + 32 + 4 + 1) return -1;
    
    int n = 0;
    
    // 包头 [type=0x80][flags=0][seq=0]
    buf[n++] = SIG_PKT_REGISTER;
    buf[n++] = 0;  // flags
    buf[n++] = 0;  // seq high
    buf[n++] = 0;  // seq low
    
    // local_peer_id (32 bytes, 右侧补 0)
    memset(buf + n, 0, 32);
    strncpy((char*)(buf + n), local_peer_id, 31);
    n += 32;
    
    // remote_peer_id (32 bytes, 右侧补 0)
    memset(buf + n, 0, 32);
    strncpy((char*)(buf + n), remote_peer_id, 31);
    n += 32;
    
    // instance_id (4 bytes, 网络字节序)
    buf[n++] = (instance_id >> 24) & 0xFF;
    buf[n++] = (instance_id >> 16) & 0xFF;
    buf[n++] = (instance_id >> 8) & 0xFF;
    buf[n++] = instance_id & 0xFF;
    
    // candidate_count (1 byte) - 暂时不发送候选
    buf[n++] = 0;
    
    return n;
}

// 解析 REGISTER_ACK 包
static void parse_register_ack(const uint8_t *buf, int len) {
    if (len < 4 + 22) {
        printf("  [WARN] REGISTER_ACK too short: %d bytes\n", len);
        return;
    }
    
    // 跳过包头(4字节)
    const uint8_t *payload = buf + 4;
    uint8_t flags = buf[1];  // 包头 flags
    
    // 解析 payload
    uint8_t status = payload[0];
    
    // session_id (8 bytes, 网络字节序)
    uint64_t session_id = 0;
    for (int i = 0; i < 8; i++) {
        session_id = (session_id << 8) | payload[1 + i];
    }
    
    // instance_id (4 bytes, 网络字节序)
    uint32_t instance_id = 0;
    for (int i = 0; i < 4; i++) {
        instance_id = (instance_id << 8) | payload[9 + i];
    }
    
    uint8_t max_candidates = payload[13];
    
    // public_ip (4 bytes)，public_port (2 bytes)
    struct in_addr public_ip;
    memcpy(&public_ip, payload + 14, 4);
    uint16_t public_port = (payload[18] << 8) | payload[19];
    
    // probe_port (2 bytes)
    uint16_t probe_port = (payload[20] << 8) | payload[21];
    
    printf("\n===== REGISTER_ACK Received =====\n");
    printf("  Status:         %d (%s)\n", status, 
           status == 0 ? "peer offline" : status == 1 ? "peer online" : "error");
    printf("  Session ID:     0x%016llx\n", (unsigned long long)session_id);
    printf("  Instance ID:    0x%08x\n", instance_id);
    printf("  Max Candidates: %d\n", max_candidates);
    printf("  Public Addr:    %s:%d\n", inet_ntoa(public_ip), public_port);
    printf("  Probe Port:     %d\n", probe_port);
    printf("  Flags:          0x%02x (relay=%d, msg=%d)\n", 
           flags, !!(flags & SIG_REGACK_FLAG_RELAY), !!(flags & SIG_REGACK_FLAG_MSG));
    printf("=================================\n\n");
    
    g_register_ack_received = 1;
}

int main(int argc, char *argv[]) {
    int server_port = DEFAULT_SERVER_PORT;
    const char *server_host = DEFAULT_SERVER_HOST;
    
    // 解析命令行参数
    if (argc > 1) {
        server_port = atoi(argv[1]);
        if (server_port <= 0 || server_port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return 1;
        }
    }
    if (argc > 2) {
        server_host = argv[2];
    }
    
    printf("=== COMPACT Server Instrument Test ===\n");
    printf("Server: %s:%d\n", server_host, server_port);
    printf("Instrument port: %d\n", INSTRUMENT_PORT);
    printf("\n");
    
    // 1. 启动 instrument 监听
    printf("[1] Starting instrument listener...\n");
    ret_t ret = instrument_listen(on_instrument_log);
    if (ret != E_NONE) {
        fprintf(stderr, "Failed to start instrument listener: %d\n", ret);
        fprintf(stderr, "Note: Another process might be using port %d\n", INSTRUMENT_PORT);
        return 1;
    }
    printf("    Listening on UDP port %d (broadcast)\n", INSTRUMENT_PORT);
    
    // 等待一段时间接收 server 的启动日志
    printf("\n[2] Waiting for server logs (2 seconds)...\n");
    P_usleep(2000 * 1000);
    printf("    Received %d log messages so far\n", g_log_count);
    
    // 2. 创建 UDP socket 发送 REGISTER
    printf("\n[3] Creating UDP socket...\n");
    sock_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == P_INVALID_SOCKET) {
        fprintf(stderr, "Failed to create socket: %d\n", P_sock_errno());
        return 1;
    }
    
    // 解析服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_host, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server address: %s\n", server_host);
        P_sock_close(sock);
        return 1;
    }
    
    // 3. 构造并发送 REGISTER 包
    printf("\n[4] Sending REGISTER packet...\n");
    printf("    Local Peer ID:  %s\n", TEST_LOCAL_PEER_ID);
    printf("    Remote Peer ID: %s\n", TEST_REMOTE_PEER_ID);
    
    uint8_t pkt[256];
    uint32_t instance_id = (uint32_t)P_tick_us();  // 使用时间戳作为 instance_id
    printf("    Instance ID:    0x%08x\n", instance_id);
    
    int pkt_len = build_register_packet(pkt, sizeof(pkt),
                                         TEST_LOCAL_PEER_ID,
                                         TEST_REMOTE_PEER_ID,
                                         instance_id);
    if (pkt_len < 0) {
        fprintf(stderr, "Failed to build REGISTER packet\n");
        P_sock_close(sock);
        return 1;
    }
    
    ssize_t sent = sendto(sock, (const char*)pkt, pkt_len, 0,
                          (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (sent != pkt_len) {
        fprintf(stderr, "Failed to send REGISTER: %d\n", P_sock_errno());
        P_sock_close(sock);
        return 1;
    }
    printf("    Sent %zd bytes to %s:%d\n", sent, server_host, server_port);
    
    // 4. 等待 REGISTER_ACK 和 server 日志
    printf("\n[5] Waiting for REGISTER_ACK...\n");
    
    // 设置接收超时
    P_sock_rcvtimeo(sock, 3000);  // 3 秒超时
    
    uint8_t recv_buf[1024];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    ssize_t recv_len = recvfrom(sock, (char*)recv_buf, sizeof(recv_buf), 0,
                                 (struct sockaddr*)&from_addr, &from_len);
    
    if (recv_len > 0) {
        printf("    Received %zd bytes from %s:%d\n", recv_len,
               inet_ntoa(from_addr.sin_addr), ntohs(from_addr.sin_port));
        
        if (recv_buf[0] == SIG_PKT_REGISTER_ACK) {
            parse_register_ack(recv_buf, (int)recv_len);
        } else {
            printf("    Unknown packet type: 0x%02x\n", recv_buf[0]);
        }
    } else {
        printf("    Timeout waiting for REGISTER_ACK\n");
    }
    
    // 5. 继续等待 server 日志
    printf("\n[6] Waiting for more server logs (2 seconds)...\n");
    P_usleep(2000 * 1000);
    
    // 6. 显示统计
    printf("\n===== Test Summary =====\n");
    printf("Log messages received: %d\n", g_log_count);
    printf("REGISTER_ACK received: %s\n", g_register_ack_received ? "YES" : "NO");
    printf("========================\n");
    
    // 清理
    P_sock_close(sock);
    
    return g_register_ack_received ? 0 : 1;
}
