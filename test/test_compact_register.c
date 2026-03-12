/*
 * test_compact_register.c - COMPACT REGISTER 协议单元测试
 *
 * 测试 REGISTER 协议的各种情况：
 *   1. 正常注册（peer 离线）
 *   2. 正常注册（peer 在线）
 *   3. 重复注册（同 instance_id）
 *   4. instance_id 变更（客户端重启）
 *   5. 无效 payload（长度不足）
 *   6. 无效 instance_id（=0）
 *   7. 带候选地址的注册
 *
 * 用法：
 *   ./test_compact_register <server_path> [port]
 */

#define MOD_TAG "TEST"

#include <stdc.h>
#include <p2p.h>
#include <p2pp.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

// 默认配置
#define DEFAULT_SERVER_PORT     9333
#define DEFAULT_SERVER_HOST     "127.0.0.1"
#define RECV_TIMEOUT_MS         2000

// 测试 peer ID 定义
#define PEER_ALICE              "alice"
#define PEER_BOB                "bob"
#define PEER_UNKNOWN            "unknown_peer"

// 测试状态
static int g_server_port = DEFAULT_SERVER_PORT;
static const char *g_server_host = DEFAULT_SERVER_HOST;
static pid_t g_server_pid = 0;
static sock_t g_sock = P_INVALID_SOCKET;

// instrument 日志收集
#define MAX_LOG_ENTRIES 100
static struct {
    uint8_t chn;
    char tag[32];
    char txt[256];
} g_logs[MAX_LOG_ENTRIES];
static volatile int g_log_count = 0;

// 测试结果
static int g_tests_passed = 0;
static int g_tests_failed = 0;

///////////////////////////////////////////////////////////////////////////////
// 工具函数
///////////////////////////////////////////////////////////////////////////////

static void on_instrument_log(uint16_t rid, uint8_t chn, const char* tag, char *txt, int len) {
    (void)len;
    
    // 忽略本地进程的日志（rid=0），只收集 server 子进程的
    if (rid == 0) return;
    
    // 保存日志（注意：instrument 回调在独立线程，简单测试允许 race）
    int idx = g_log_count;
    if (idx < MAX_LOG_ENTRIES) {
        g_log_count = idx + 1;
        g_logs[idx].chn = chn;
        strncpy(g_logs[idx].tag, tag ? tag : "", sizeof(g_logs[idx].tag) - 1);
        g_logs[idx].tag[sizeof(g_logs[idx].tag) - 1] = '\0';
        strncpy(g_logs[idx].txt, txt, sizeof(g_logs[idx].txt) - 1);
        g_logs[idx].txt[sizeof(g_logs[idx].txt) - 1] = '\0';
    }
    
    // 实时显示
    const char* color;
    switch (chn) {
        case LOG_SLOT_DEBUG:   color = "\033[36m"; break;
        case LOG_SLOT_INFO:    color = "\033[32m"; break;
        case LOG_SLOT_WARN:    color = "\033[33m"; break;
        case LOG_SLOT_ERROR:   color = "\033[31m"; break;
        default:               color = "\033[37m"; break;
    }
    printf("%s    [SERVER] %s: %s\033[0m\n", color, tag, txt);
}

// 清空日志缓存
static void clear_logs(void) {
    g_log_count = 0;
}

// 在日志中搜索指定文本
static int find_log(const char *pattern) {
    for (int i = 0; i < g_log_count; i++) {
        if (strstr(g_logs[i].txt, pattern)) return i;
    }
    return -1;
}

// 构造 REGISTER 包
static int build_register(uint8_t *buf, int buf_size,
                          const char *local_peer_id,
                          const char *remote_peer_id,
                          uint32_t instance_id,
                          int candidate_count,
                          p2p_compact_candidate_t *candidates) {
    if (buf_size < 4 + 32 + 32 + 4 + 1) return -1;
    
    int n = 0;
    
    // 包头 [type=0x80][flags=0][seq=0]
    buf[n++] = SIG_PKT_REGISTER;
    buf[n++] = 0;
    buf[n++] = 0;
    buf[n++] = 0;
    
    // local_peer_id (32 bytes)
    memset(buf + n, 0, 32);
    if (local_peer_id) strncpy((char*)(buf + n), local_peer_id, 31);
    n += 32;
    
    // remote_peer_id (32 bytes)
    memset(buf + n, 0, 32);
    if (remote_peer_id) strncpy((char*)(buf + n), remote_peer_id, 31);
    n += 32;
    
    // instance_id (4 bytes, 网络字节序)
    buf[n++] = (instance_id >> 24) & 0xFF;
    buf[n++] = (instance_id >> 16) & 0xFF;
    buf[n++] = (instance_id >> 8) & 0xFF;
    buf[n++] = instance_id & 0xFF;
    
    // candidate_count
    if (candidate_count > 255) candidate_count = 255;
    buf[n++] = (uint8_t)candidate_count;
    
    // candidates
    for (int i = 0; i < candidate_count && candidates; i++) {
        buf[n++] = candidates[i].type;
        memcpy(buf + n, &candidates[i].ip, 4);
        n += 4;
        memcpy(buf + n, &candidates[i].port, 2);
        n += 2;
    }
    
    return n;
}

// 发送并接收 REGISTER_ACK
typedef struct {
    int received;           // 是否收到响应
    uint8_t status;         // 0=offline, 1=online, 2=error
    uint64_t session_id;
    uint32_t instance_id;
    uint8_t max_candidates;
    uint32_t public_ip;
    uint16_t public_port;
    uint16_t probe_port;
    uint8_t flags;
} register_ack_t;

static int send_register_recv_ack(const uint8_t *pkt, int pkt_len, register_ack_t *ack, int timeout_ms) {
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_server_port);
    inet_pton(AF_INET, g_server_host, &server_addr.sin_addr);
    
    // 发送
    ssize_t sent = sendto(g_sock, (const char*)pkt, pkt_len, 0,
                          (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (sent != pkt_len) return -1;
    
    // 设置超时
    P_sock_rcvtimeo(g_sock, timeout_ms);
    
    // 接收
    uint8_t recv_buf[256];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t recv_len = recvfrom(g_sock, (char*)recv_buf, sizeof(recv_buf), 0,
                                 (struct sockaddr*)&from, &from_len);
    
    if (recv_len <= 0) {
        ack->received = 0;
        return 0;
    }
    
    // 解析
    ack->received = 1;
    if (recv_buf[0] != SIG_PKT_REGISTER_ACK) return -2;
    
    ack->flags = recv_buf[1];
    ack->status = recv_buf[4];
    
    // session_id (8 bytes)
    ack->session_id = 0;
    for (int i = 0; i < 8; i++) {
        ack->session_id = (ack->session_id << 8) | recv_buf[5 + i];
    }
    
    // instance_id (4 bytes)
    ack->instance_id = 0;
    for (int i = 0; i < 4; i++) {
        ack->instance_id = (ack->instance_id << 8) | recv_buf[13 + i];
    }
    
    ack->max_candidates = recv_buf[17];
    memcpy(&ack->public_ip, recv_buf + 18, 4);
    memcpy(&ack->public_port, recv_buf + 22, 2);
    ack->public_port = ntohs(ack->public_port);
    memcpy(&ack->probe_port, recv_buf + 24, 2);
    ack->probe_port = ntohs(ack->probe_port);
    
    return 1;
}

// 等待并接收下一个 UDP 包
static int recv_packet(uint8_t *buf, int buf_size, int timeout_ms) {
    P_sock_rcvtimeo(g_sock, timeout_ms);
    
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t n = recvfrom(g_sock, (char*)buf, buf_size, 0,
                          (struct sockaddr*)&from, &from_len);
    return (int)n;
}

// 测试结果报告
#define TEST_PASS(name) do { \
    printf("\033[32m  [PASS]\033[0m %s\n", name); \
    g_tests_passed++; \
} while(0)

#define TEST_FAIL(name, reason) do { \
    printf("\033[31m  [FAIL]\033[0m %s: %s\n", name, reason); \
    g_tests_failed++; \
} while(0)

///////////////////////////////////////////////////////////////////////////////
// 测试用例
///////////////////////////////////////////////////////////////////////////////

// 测试 1: 正常注册，peer 离线
static void test_register_peer_offline(void) {
    const char *TEST_NAME = "register_peer_offline";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    uint8_t pkt[256];
    uint32_t inst_id = (uint32_t)P_tick_us();
    int len = build_register(pkt, sizeof(pkt), PEER_ALICE, PEER_UNKNOWN, inst_id, 0, NULL);
    
    register_ack_t ack = {0};
    int ret = send_register_recv_ack(pkt, len, &ack, RECV_TIMEOUT_MS);
    
    P_usleep(100 * 1000);  // 等待日志
    
    if (ret < 0) {
        TEST_FAIL(TEST_NAME, "send/recv error");
        return;
    }
    if (!ack.received) {
        TEST_FAIL(TEST_NAME, "no REGISTER_ACK received");
        return;
    }
    if (ack.status != SIG_REGACK_PEER_OFFLINE) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected status=0 (offline), got %d", ack.status);
        TEST_FAIL(TEST_NAME, msg);
        return;
    }
    if (ack.session_id == 0) {
        TEST_FAIL(TEST_NAME, "session_id should not be 0");
        return;
    }
    if (ack.instance_id != inst_id) {
        TEST_FAIL(TEST_NAME, "instance_id mismatch");
        return;
    }
    
    // 验证 server 日志
    if (find_log("accepted") < 0) {
        TEST_FAIL(TEST_NAME, "server log 'accepted' not found");
        return;
    }
    
    TEST_PASS(TEST_NAME);
    printf("    session_id=0x%016llx, max_cands=%d\n", 
           (unsigned long long)ack.session_id, ack.max_candidates);
}

// 测试 2: 正常注册，peer 在线（双方互相注册）
static void test_register_peer_online(void) {
    const char *TEST_NAME = "register_peer_online";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    uint8_t pkt[256];
    register_ack_t ack_alice = {0}, ack_bob = {0};
    uint32_t inst_alice = (uint32_t)P_tick_us();
    uint32_t inst_bob = inst_alice + 1000;
    
    // Alice 注册，等待 Bob
    int len = build_register(pkt, sizeof(pkt), PEER_ALICE, PEER_BOB, inst_alice, 0, NULL);
    send_register_recv_ack(pkt, len, &ack_alice, RECV_TIMEOUT_MS);
    
    if (!ack_alice.received || ack_alice.status != SIG_REGACK_PEER_OFFLINE) {
        TEST_FAIL(TEST_NAME, "Alice should get status=offline initially");
        return;
    }
    printf("    Alice registered, session_id=0x%016llx, waiting for Bob\n", 
           (unsigned long long)ack_alice.session_id);
    
    // Bob 注册，应该匹配 Alice
    len = build_register(pkt, sizeof(pkt), PEER_BOB, PEER_ALICE, inst_bob, 0, NULL);
    send_register_recv_ack(pkt, len, &ack_bob, RECV_TIMEOUT_MS);
    
    if (!ack_bob.received || ack_bob.status != SIG_REGACK_PEER_ONLINE) {
        TEST_FAIL(TEST_NAME, "Bob should get status=online (peer matched)");
        return;
    }
    printf("    Bob registered, session_id=0x%016llx, peer online!\n", 
           (unsigned long long)ack_bob.session_id);
    
    // 等待 PEER_INFO 包
    P_usleep(200 * 1000);
    
    // 检查是否收到 PEER_INFO
    uint8_t recv_buf[256];
    int got_peer_info = 0;
    for (int i = 0; i < 5; i++) {
        int n = recv_packet(recv_buf, sizeof(recv_buf), 200);
        if (n > 0 && recv_buf[0] == SIG_PKT_PEER_INFO) {
            got_peer_info = 1;
            printf("    Received PEER_INFO, len=%d\n", n);
            break;
        }
    }
    
    if (!got_peer_info) {
        // PEER_INFO 可能已经发给 Alice 了，但我们的 socket 可能收不到（取决于源端口）
        // 检查 server 日志
        if (find_log("Pairing complete") < 0) {
            TEST_FAIL(TEST_NAME, "Pairing complete log not found");
            return;
        }
    }
    
    if (find_log("Pairing complete") >= 0) {
        TEST_PASS(TEST_NAME);
    } else {
        TEST_FAIL(TEST_NAME, "Pairing not detected in server logs");
    }
}

// 测试 3: 重复注册（同 instance_id）
static void test_register_duplicate(void) {
    const char *TEST_NAME = "register_duplicate";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    uint8_t pkt[256];
    uint32_t inst_id = (uint32_t)P_tick_us() + 2000;
    register_ack_t ack1 = {0}, ack2 = {0};
    
    // 首次注册
    int len = build_register(pkt, sizeof(pkt), "dup_client", PEER_UNKNOWN, inst_id, 0, NULL);
    send_register_recv_ack(pkt, len, &ack1, RECV_TIMEOUT_MS);
    
    if (!ack1.received) {
        TEST_FAIL(TEST_NAME, "first REGISTER_ACK not received");
        return;
    }
    
    P_usleep(50 * 1000);
    
    // 重复注册（模拟 ACK 丢失重传）
    send_register_recv_ack(pkt, len, &ack2, RECV_TIMEOUT_MS);
    
    if (!ack2.received) {
        TEST_FAIL(TEST_NAME, "second REGISTER_ACK not received");
        return;
    }
    
    // session_id 应该相同
    if (ack1.session_id != ack2.session_id) {
        char msg[128];
        snprintf(msg, sizeof(msg), "session_id changed: 0x%llx -> 0x%llx",
                 (unsigned long long)ack1.session_id, (unsigned long long)ack2.session_id);
        TEST_FAIL(TEST_NAME, msg);
        return;
    }
    
    TEST_PASS(TEST_NAME);
    printf("    session_id remained: 0x%016llx\n", (unsigned long long)ack1.session_id);
}

// 测试 4: instance_id 变更（客户端重启）
static void test_register_instance_change(void) {
    const char *TEST_NAME = "register_instance_change";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    uint8_t pkt[256];
    uint32_t inst_old = (uint32_t)P_tick_us() + 3000;
    uint32_t inst_new = inst_old + 10000;
    register_ack_t ack1 = {0}, ack2 = {0};
    
    // 首次注册
    int len = build_register(pkt, sizeof(pkt), "restart_client", PEER_UNKNOWN, inst_old, 0, NULL);
    send_register_recv_ack(pkt, len, &ack1, RECV_TIMEOUT_MS);
    
    if (!ack1.received) {
        TEST_FAIL(TEST_NAME, "first REGISTER_ACK not received");
        return;
    }
    printf("    First registration: session_id=0x%016llx, inst=%u\n", 
           (unsigned long long)ack1.session_id, inst_old);
    
    P_usleep(100 * 1000);
    clear_logs();
    
    // 新 instance_id 注册（模拟客户端重启）
    len = build_register(pkt, sizeof(pkt), "restart_client", PEER_UNKNOWN, inst_new, 0, NULL);
    send_register_recv_ack(pkt, len, &ack2, RECV_TIMEOUT_MS);
    
    if (!ack2.received) {
        TEST_FAIL(TEST_NAME, "second REGISTER_ACK not received");
        return;
    }
    printf("    Second registration: session_id=0x%016llx, inst=%u\n", 
           (unsigned long long)ack2.session_id, inst_new);
    
    // session_id 应该不同
    if (ack1.session_id == ack2.session_id) {
        TEST_FAIL(TEST_NAME, "session_id should change after instance_id change");
        return;
    }
    
    // 检查 server 日志
    P_usleep(100 * 1000);
    if (find_log("new instance") < 0 && find_log("resetting session") < 0) {
        TEST_FAIL(TEST_NAME, "server did not log instance change");
        return;
    }
    
    TEST_PASS(TEST_NAME);
}

// 测试 5: 无效 payload（长度不足）
static void test_register_bad_payload(void) {
    const char *TEST_NAME = "register_bad_payload";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    // 构造一个太短的 REGISTER 包（只有包头 + 部分 payload）
    uint8_t pkt[20];
    pkt[0] = SIG_PKT_REGISTER;
    pkt[1] = 0;
    pkt[2] = 0;
    pkt[3] = 0;
    memset(pkt + 4, 0, sizeof(pkt) - 4);
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_server_port);
    inet_pton(AF_INET, g_server_host, &server_addr.sin_addr);
    
    sendto(g_sock, (const char*)pkt, sizeof(pkt), 0,
           (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    // 不应该收到响应
    P_sock_rcvtimeo(g_sock, 500);
    uint8_t recv_buf[256];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t n = recvfrom(g_sock, (char*)recv_buf, sizeof(recv_buf), 0,
                          (struct sockaddr*)&from, &from_len);
    
    P_usleep(100 * 1000);
    
    if (n > 0) {
        TEST_FAIL(TEST_NAME, "should not receive response for bad payload");
        return;
    }
    
    // 检查 server 是否记录了错误
    if (find_log("bad payload") < 0) {
        TEST_FAIL(TEST_NAME, "server should log 'bad payload' error");
        return;
    }
    
    TEST_PASS(TEST_NAME);
}

// 测试 6: 无效 instance_id（=0）
static void test_register_invalid_instance_id(void) {
    const char *TEST_NAME = "register_invalid_instance_id";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    uint8_t pkt[256];
    int len = build_register(pkt, sizeof(pkt), "invalid_client", PEER_UNKNOWN, 0, 0, NULL);
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_server_port);
    inet_pton(AF_INET, g_server_host, &server_addr.sin_addr);
    
    sendto(g_sock, (const char*)pkt, len, 0,
           (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    // 不应该收到响应
    P_sock_rcvtimeo(g_sock, 500);
    uint8_t recv_buf[256];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t n = recvfrom(g_sock, (char*)recv_buf, sizeof(recv_buf), 0,
                          (struct sockaddr*)&from, &from_len);
    
    P_usleep(100 * 1000);
    
    if (n > 0) {
        TEST_FAIL(TEST_NAME, "should not receive response for instance_id=0");
        return;
    }
    
    // 检查 server 是否记录了错误
    if (find_log("invalid instance_id") < 0) {
        TEST_FAIL(TEST_NAME, "server should log 'invalid instance_id' error");
        return;
    }
    
    TEST_PASS(TEST_NAME);
}

// 测试 7: 带候选地址的注册
static void test_register_with_candidates(void) {
    const char *TEST_NAME = "register_with_candidates";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    // 构造候选列表（type: 0=host, 1=srflx）
    p2p_compact_candidate_t candidates[2];
    candidates[0].type = 0;  // host
    inet_pton(AF_INET, "192.168.1.100", &candidates[0].ip);
    candidates[0].port = htons(12345);
    candidates[1].type = 1;  // srflx
    inet_pton(AF_INET, "1.2.3.4", &candidates[1].ip);
    candidates[1].port = htons(54321);
    
    uint8_t pkt[256];
    uint32_t inst_id = (uint32_t)P_tick_us() + 5000;
    int len = build_register(pkt, sizeof(pkt), "cand_client", PEER_UNKNOWN, inst_id, 2, candidates);
    
    register_ack_t ack = {0};
    send_register_recv_ack(pkt, len, &ack, RECV_TIMEOUT_MS);
    
    P_usleep(100 * 1000);
    
    if (!ack.received) {
        TEST_FAIL(TEST_NAME, "REGISTER_ACK not received");
        return;
    }
    
    // 检查 server 日志是否记录了候选数量
    if (find_log("cands=2") < 0) {
        TEST_FAIL(TEST_NAME, "server should log 'cands=2'");
        return;
    }
    
    TEST_PASS(TEST_NAME);
    printf("    Registered with 2 candidates\n");
}

///////////////////////////////////////////////////////////////////////////////
// 主函数
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
    
    const char *server_path = NULL;
    
    // 解析命令行参数
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_path> [port]\n", argv[0]);
        return 1;
    }
    server_path = argv[1];
    if (argc > 2) {
        g_server_port = atoi(argv[2]);
        if (g_server_port <= 0 || g_server_port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[2]);
            return 1;
        }
    }
    
    printf("=== COMPACT REGISTER Protocol Tests ===\n");
    printf("Server path: %s\n", server_path);
    printf("Server addr: %s:%d\n", g_server_host, g_server_port);
    printf("\n");
    
    // 初始化 instrument 监听
    instrument_local();
    if (instrument_listen(on_instrument_log) != E_NONE) {
        fprintf(stderr, "Failed to start instrument listener\n");
        return 1;
    }
    
    // 启动 server 子进程
    printf("[*] Starting server...\n");
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", g_server_port);
    
    g_server_pid = fork();
    if (g_server_pid < 0) {
        fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
        return 1;
    } else if (g_server_pid == 0) {
        execl(server_path, server_path, "-p", port_str, NULL);
        fprintf(stderr, "Failed to exec: %s\n", strerror(errno));
        _exit(127);
    }
    printf("    Server PID: %d\n", g_server_pid);
    
    // 等待 server 启动
    P_usleep(500 * 1000);
    
    // 创建测试 socket
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock == P_INVALID_SOCKET) {
        fprintf(stderr, "Failed to create socket\n");
        goto cleanup;
    }
    
    // 运行测试用例
    printf("\n[*] Running tests...\n");
    
    test_register_peer_offline();
    test_register_peer_online();
    test_register_duplicate();
    test_register_instance_change();
    test_register_bad_payload();
    test_register_invalid_instance_id();
    test_register_with_candidates();
    
    // 清理
    if (g_sock != P_INVALID_SOCKET) {
        P_sock_close(g_sock);
        g_sock = P_INVALID_SOCKET;
    }

cleanup:
    // 终止 server
    if (g_server_pid > 0) {
        printf("\n[*] Terminating server...\n");
        kill(g_server_pid, SIGTERM);
        int status;
        waitpid(g_server_pid, &status, 0);
    }
    
    // 显示结果
    printf("\n===== Test Results =====\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    printf("========================\n");
    
    return g_tests_failed > 0 ? 1 : 0;
}
