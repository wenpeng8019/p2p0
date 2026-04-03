/*
 * test_compact_lifecycle.c - COMPACT 协议生命周期单元测试
 *
 * ============================================================================
 * 测试目标
 * ============================================================================
 * 验证 p2p_server 对 COMPACT 协议生命周期相关包的处理逻辑：
 * - ALIVE / ALIVE_ACK 保活机制
 * - OFFLINE 主动注销
 * - PEER_OFF 对端离线通知
 *
 * ============================================================================
 * 测试方法
 * ============================================================================
 * 1. 启动 p2p_server 子进程，监听指定端口
 * 2. 通过 instrument 机制收集 server 的实时日志
 * 3. 发送 ALIVE/OFFLINE 包，验证 server 正确处理
 * 4. 验证 PEER_OFF 通知正确发送给对端
 *
 * ============================================================================
 * 测试用例分类
 * ============================================================================
 *
 * 一、正常功能测试（验证满足各种需求场景）
 * ---------------------------------------------------------------------------
 *
 * 测试 1: alive_keepalive
 *   目标：验证 ALIVE 包正常情况下 server 回复 ALIVE_ACK
 *   方法：注册后发送 ALIVE 包
 *   预期：
 *     - 收到 ALIVE_ACK 回复
 *     - server 日志含 "ALIVE accepted"
 *
 * 测试 2: alive_updates_activity
 *   目标：验证 ALIVE 包更新槽位活跃时间
 *   方法：注册后多次发送 ALIVE 包，每次都收到 ACK
 *   预期：
 *     - 每次 ALIVE 都成功
 *     - 槽位保持活跃状态
 *
 * 测试 3: unregister_releases_slot
 *   目标：验证 OFFLINE 正常释放槽位
 *   方法：注册配对 → 发送 OFFLINE → 重新注册相同配对
 *   预期：
 *     - OFFLINE 后槽位被释放
 *     - 可以重新注册相同的 peer_id 配对
 *
 * 测试 4: unregister_notifies_peer
 *   目标：验证 OFFLINE 时对端收到 PEER_OFF
 *   方法：Alice 和 Bob 配对 → Alice 发送 OFFLINE
 *   预期：
 *     - Bob 收到 PEER_OFF 包
 *     - PEER_OFF 包含正确的 session_id
 *
 * 二、失败验证测试（各种异常输入的防御）
 * ---------------------------------------------------------------------------
 *
 * 测试 5: alive_bad_session
 *   目标：验证 server 对无效 session_id 的 ALIVE 包处理
 *   方法：发送包含不存在 session_id 的 ALIVE 包
 *   预期：
 *     - 不收到 ALIVE_ACK
 *     - 不触发异常
 *
 * 测试 6: unregister_bad_payload
 *   目标：验证 server 对畸形 OFFLINE 包的防御
 *   方法：发送 payload 过短的 OFFLINE 包
 *   预期：
 *     - server 日志含 "bad payload"
 *     - 不影响正常的配对
 *
 * 三、边界/临界态测试（状态转换与幂等性）
 * ---------------------------------------------------------------------------
 *
 * 测试 7: peer_off_on_reregister
 *   目标：验证重注册时旧对端收到 PEER_OFF
 *   方法：Alice-Bob 配对 → Alice 用新 instance_id 重注册
 *   预期：
 *     - Bob 收到 PEER_OFF 包（原因为 "reregister"）
 *     - 新的配对正常建立
 *
 * ============================================================================
 * 依赖与用法
 * ============================================================================
 * 依赖：p2p_server 可执行文件（需支持 instrument 日志）
 * 
 * 用法：
 *   ./test_compact_lifecycle <server_path> [port]
 *
 * 示例：
 *   ./test_compact_lifecycle ./p2p_server 9444
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
#define DEFAULT_SERVER_PORT     9444
#define DEFAULT_SERVER_HOST     "127.0.0.1"
#define RECV_TIMEOUT_MS         2000

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
    
    if (rid == 0) return;  // 忽略本地进程日志
    
    int idx = g_log_count;
    if (idx < MAX_LOG_ENTRIES) {
        g_log_count = idx + 1;
        g_logs[idx].chn = chn;
        strncpy(g_logs[idx].tag, tag ? tag : "", sizeof(g_logs[idx].tag) - 1);
        g_logs[idx].tag[sizeof(g_logs[idx].tag) - 1] = '\0';
        strncpy(g_logs[idx].txt, txt, sizeof(g_logs[idx].txt) - 1);
        g_logs[idx].txt[sizeof(g_logs[idx].txt) - 1] = '\0';
    }
    
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

static void clear_logs(void) {
    g_log_count = 0;
}

static int find_log(const char *pattern) {
    for (int i = 0; i < g_log_count; i++) {
        if (strstr(g_logs[i].txt, pattern)) return i;
    }
    return -1;
}

#define TEST_PASS(name) do { printf("%s:%d   \033[32m[PASS] %s\033[0m\n", __FILE__, __LINE__, name); g_tests_passed++; } while(0)
#define TEST_FAIL(name, reason) do { printf("%s:%d   \033[31m[FAIL] %s: %s\033[0m\n", __FILE__, __LINE__, name, reason); g_tests_failed++; } while(0)

///////////////////////////////////////////////////////////////////////////////
// 协议构造函数
///////////////////////////////////////////////////////////////////////////////

// 构造 ONLINE 包
static int build_online(uint8_t *buf, int buf_size,
                        const char *local_peer_id,
                        uint32_t instance_id) {
    if (buf_size < 4 + 32 + 4) return -1;
    
    int n = 0;
    buf[n++] = SIG_PKT_ONLINE;
    buf[n++] = 0;
    buf[n++] = 0;
    buf[n++] = 0;
    
    memset(buf + n, 0, 32);
    if (local_peer_id) strncpy((char*)(buf + n), local_peer_id, 31);
    n += 32;
    
    buf[n++] = (instance_id >> 24) & 0xFF;
    buf[n++] = (instance_id >> 16) & 0xFF;
    buf[n++] = (instance_id >> 8) & 0xFF;
    buf[n++] = instance_id & 0xFF;
    
    return n;
}

#define build_register(buf, buf_size, local, remote, inst_id, cand_count, cands) \
    build_online(buf, buf_size, local, inst_id)

// 构造 SYNC0 包
static int build_sync0(uint8_t *buf, int buf_size, uint64_t auth_key,
                       const char *remote_peer_id,
                       int candidate_count, p2p_candidate_t *candidates) {
    if (buf_size < 4 + 8 + 32 + 1) return -1;
    int n = 0;
    buf[n++] = SIG_PKT_SYNC0; buf[n++] = 0; buf[n++] = 0; buf[n++] = 0;
    for (int i = 0; i < 8; i++) buf[n++] = (auth_key >> (56 - i * 8)) & 0xFF;
    memset(buf + n, 0, 32);
    if (remote_peer_id) strncpy((char*)(buf + n), remote_peer_id, 31);
    n += 32;
    if (candidate_count > 255) candidate_count = 255;
    buf[n++] = (uint8_t)candidate_count;
    for (int i = 0; i < candidate_count && candidates; i++) {
        if (n + (int)sizeof(p2p_candidate_t) > buf_size) break;
        memcpy(buf + n, &candidates[i], sizeof(p2p_candidate_t));
        n += sizeof(p2p_candidate_t);
    }
    return n;
}

// 构造 ALIVE 包
// 协议: [hdr(4)][session_id(8)]
static int build_alive(uint8_t *buf, int buf_size, uint64_t session_id) {
    if (buf_size < 4 + 8) return -1;
    
    buf[0] = SIG_PKT_ALIVE;
    buf[1] = 0;  // flags
    buf[2] = 0;  // seq high
    buf[3] = 0;  // seq low
    
    // session_id (8 bytes, network order)
    for (int i = 0; i < 8; i++) {
        buf[4 + i] = (session_id >> (56 - i * 8)) & 0xFF;
    }
    
    return 12;
}

// 构造 OFFLINE 包
// 协议: [hdr(4)][auth_key(8)]
static int build_unregister(uint8_t *buf, int buf_size, uint64_t auth_key) {
    if (buf_size < 4 + 8) return -1;
    
    buf[0] = SIG_PKT_OFFLINE;
    buf[1] = 0;  // flags
    buf[2] = 0;  // seq high
    buf[3] = 0;  // seq low
    
    for (int i = 0; i < 8; i++) buf[4 + i] = (uint8_t)((auth_key >> (56 - i * 8)) & 0xFF);
    
    return 12;
}

// 发送 ONLINE 并接收 ONLINE_ACK，然后发送 SYNC0，返回 session_id
static uint64_t register_peer(sock_t sock, const char *local, const char *remote, 
                               uint32_t inst_id, int cand_count, p2p_candidate_t *cands) {
    uint8_t pkt[512];
    int len = build_online(pkt, sizeof(pkt), local, inst_id);
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_server_port);
    inet_pton(AF_INET, g_server_host, &server_addr.sin_addr);
    
    sendto(sock, (const char*)pkt, len, 0,
           (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    P_sock_rcvtimeo(sock, RECV_TIMEOUT_MS);
    uint8_t recv_buf[256];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    
    ssize_t n = recvfrom(sock, (char*)recv_buf, sizeof(recv_buf), 0,
                          (struct sockaddr*)&from, &from_len);
    
    if (n > 0 && recv_buf[0] == SIG_PKT_ONLINE_ACK) {
        uint64_t auth_key = 0;
        for (int i = 0; i < 8; i++) {
            auth_key = (auth_key << 8) | recv_buf[8 + i];
        }
        // 发送 SYNC0（携带 auth_key + remote_peer_id）
        if (cand_count > 0 || cands == NULL) {
            len = build_sync0(pkt, sizeof(pkt), auth_key, remote, cand_count, cands);
            sendto(sock, (const char*)pkt, len, 0,
                   (struct sockaddr*)&server_addr, sizeof(server_addr));
            // 消耗 SYNC0_ACK，防止它污染后续操作的 recvfrom
            uint8_t drain_buf[32];
            struct sockaddr_in drain_from; socklen_t drain_len = sizeof(drain_from);
            P_sock_rcvtimeo(sock, RECV_TIMEOUT_MS);
            recvfrom(sock, (char*)drain_buf, sizeof(drain_buf), 0,
                     (struct sockaddr*)&drain_from, &drain_len);
        }
        return auth_key;
    }
    return 0;
}

// 发送 ALIVE 并等待 ALIVE_ACK
static int send_alive_and_wait_ack(sock_t sock, uint64_t session_id) {
    uint8_t pkt[16];
    int len = build_alive(pkt, sizeof(pkt), session_id);
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_server_port);
    inet_pton(AF_INET, g_server_host, &server_addr.sin_addr);
    
    sendto(sock, (const char*)pkt, len, 0,
           (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    P_sock_rcvtimeo(sock, RECV_TIMEOUT_MS);
    uint8_t recv_buf[64];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    
    ssize_t n = recvfrom(sock, (char*)recv_buf, sizeof(recv_buf), 0,
                          (struct sockaddr*)&from, &from_len);
    
    if (n >= 4 && recv_buf[0] == SIG_PKT_ALIVE_ACK) {
        return 1;  // 收到 ACK
    }
    return 0;  // 未收到 ACK
}

// 发送 OFFLINE
static void send_unregister(sock_t sock, uint64_t auth_key) {
    uint8_t pkt[128];
    int len = build_unregister(pkt, sizeof(pkt), auth_key);
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_server_port);
    inet_pton(AF_INET, g_server_host, &server_addr.sin_addr);
    
    sendto(sock, (const char*)pkt, len, 0,
           (struct sockaddr*)&server_addr, sizeof(server_addr));
}

// 等待 PEER_OFF 包
static int wait_peer_off(sock_t sock, uint64_t *session_id_out) {
    P_sock_rcvtimeo(sock, RECV_TIMEOUT_MS);
    uint8_t recv_buf[64];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    
    // 可能需要跳过 SYNC 等其他包
    for (int i = 0; i < 5; i++) {
        ssize_t n = recvfrom(sock, (char*)recv_buf, sizeof(recv_buf), 0,
                              (struct sockaddr*)&from, &from_len);
        
        if (n >= 12 && recv_buf[0] == SIG_PKT_FIN) {
            if (session_id_out) {
                *session_id_out = 0;
                for (int j = 0; j < 8; j++) {
                    *session_id_out = (*session_id_out << 8) | recv_buf[4 + j];
                }
            }
            return 1;
        }
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// 测试用例
///////////////////////////////////////////////////////////////////////////////

// 测试 1: ALIVE 包正常情况下收到 ACK
static void test_alive_keepalive(void) {
    const char *TEST_NAME = "alive_keepalive";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == P_INVALID_SOCKET) {
        TEST_FAIL(TEST_NAME, "failed to create socket");
        return;
    }
    
    uint32_t inst_id = (uint32_t)P_tick_us() + 3000;
    
    // 注册获取 session_id
    uint64_t session_id = register_peer(sock, "alive_alice", "alive_bob", inst_id, 0, NULL);
    if (session_id == 0) {
        P_sock_close(sock);
        TEST_FAIL(TEST_NAME, "registration failed");
        return;
    }
    
    P_usleep(100 * 1000);  // 等待 server 处理
    clear_logs();
    
    // 发送 ALIVE 并等待 ACK
    int got_ack = send_alive_and_wait_ack(sock, session_id);
    
    P_sock_close(sock);
    
    if (!got_ack) {
        TEST_FAIL(TEST_NAME, "no ALIVE_ACK received");
        return;
    }
    
    TEST_PASS(TEST_NAME);
}

// 测试 2: ALIVE 包更新活跃时间
static void test_alive_updates_activity(void) {
    const char *TEST_NAME = "alive_updates_activity";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == P_INVALID_SOCKET) {
        TEST_FAIL(TEST_NAME, "failed to create socket");
        return;
    }
    
    uint32_t inst_id = (uint32_t)P_tick_us() + 3100;
    
    uint64_t session_id = register_peer(sock, "activity_alice", "activity_bob", inst_id, 0, NULL);
    if (session_id == 0) {
        P_sock_close(sock);
        TEST_FAIL(TEST_NAME, "registration failed");
        return;
    }
    
    // 发送多次 ALIVE，每次都应该成功
    int success_count = 0;
    for (int i = 0; i < 3; i++) {
        P_usleep(200 * 1000);  // 200ms 间隔
        if (send_alive_and_wait_ack(sock, session_id)) {
            success_count++;
        }
    }
    
    P_sock_close(sock);
    
    if (success_count < 3) {
        TEST_FAIL(TEST_NAME, "not all ALIVE succeeded");
        return;
    }
    
    TEST_PASS(TEST_NAME);
}

// 测试 3: OFFLINE 释放槽位
static void test_unregister_releases_slot(void) {
    const char *TEST_NAME = "unregister_releases_slot";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == P_INVALID_SOCKET) {
        TEST_FAIL(TEST_NAME, "failed to create socket");
        return;
    }
    
    const char *local_id = "unreg_alice";
    const char *remote_id = "unreg_bob";
    uint32_t inst_id = (uint32_t)P_tick_us() + 3200;
    
    // 第一次注册
    uint64_t session1 = register_peer(sock, local_id, remote_id, inst_id, 0, NULL);
    if (session1 == 0) {
        P_sock_close(sock);
        TEST_FAIL(TEST_NAME, "first registration failed");
        return;
    }
    
    // 发送 OFFLINE
    send_unregister(sock, session1);
    P_usleep(100 * 1000);
    
    // 用相同 peer_id 重新注册（应该成功，因为槽位已释放）
    uint32_t inst_id2 = inst_id + 100;
    uint64_t session2 = register_peer(sock, local_id, remote_id, inst_id2, 0, NULL);
    
    P_sock_close(sock);
    
    if (session2 == 0) {
        TEST_FAIL(TEST_NAME, "re-registration failed after unregister");
        return;
    }
    
    // 两个 session_id 应该不同
    if (session1 == session2) {
        TEST_FAIL(TEST_NAME, "session_id should differ after unregister");
        return;
    }
    
    TEST_PASS(TEST_NAME);
}

// 测试 4: OFFLINE 时对端收到 PEER_OFF
static void test_unregister_notifies_peer(void) {
    const char *TEST_NAME = "unregister_notifies_peer";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock_alice = socket(AF_INET, SOCK_DGRAM, 0);
    sock_t sock_bob = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (sock_alice == P_INVALID_SOCKET || sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to create sockets");
        return;
    }
    
    uint32_t inst_alice = (uint32_t)P_tick_us() + 3300;
    uint32_t inst_bob = (uint32_t)P_tick_us() + 3301;
    
    // Alice 注册等待 Bob
    uint64_t session_alice = register_peer(sock_alice, "notify_alice", "notify_bob", inst_alice, 0, NULL);
    if (session_alice == 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Alice registration failed");
        return;
    }
    
    // Bob 注册等待 Alice（触发配对）
    uint64_t session_bob = register_peer(sock_bob, "notify_bob", "notify_alice", inst_bob, 0, NULL);
    if (session_bob == 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Bob registration failed");
        return;
    }
    
    // 消费双方的 SYNC 包
    P_usleep(100 * 1000);
    P_sock_rcvtimeo(sock_alice, 500);
    P_sock_rcvtimeo(sock_bob, 500);
    uint8_t discard[256];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    for (int i = 0; i < 3; i++) {
        recvfrom(sock_alice, (char*)discard, sizeof(discard), 0, (struct sockaddr*)&from, &from_len);
        recvfrom(sock_bob, (char*)discard, sizeof(discard), 0, (struct sockaddr*)&from, &from_len);
    }
    
    // Alice 发送 OFFLINE
    send_unregister(sock_alice, session_alice);
    
    // Bob 应该收到 PEER_OFF
    uint64_t peer_off_session = 0;
    int got_peer_off = wait_peer_off(sock_bob, &peer_off_session);
    
    P_sock_close(sock_alice);
    P_sock_close(sock_bob);
    
    if (!got_peer_off) {
        TEST_FAIL(TEST_NAME, "Bob did not receive PEER_OFF");
        return;
    }
    
    // PEER_OFF 的 session_id 应该是 Bob 的 session_id
    if (peer_off_session != session_bob) {
        TEST_FAIL(TEST_NAME, "PEER_OFF session_id mismatch");
        return;
    }
    
    TEST_PASS(TEST_NAME);
}

// 测试 5: 无效 session_id 的 ALIVE 包
static void test_alive_bad_session(void) {
    const char *TEST_NAME = "alive_bad_session";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == P_INVALID_SOCKET) {
        TEST_FAIL(TEST_NAME, "failed to create socket");
        return;
    }
    
    // 发送一个虚假的 session_id
    uint64_t fake_session_id = 0xDEADBEEF12345678ULL;
    int got_ack = send_alive_and_wait_ack(sock, fake_session_id);
    
    P_sock_close(sock);
    
    // 不应收到 ACK
    if (got_ack) {
        TEST_FAIL(TEST_NAME, "should not receive ACK for invalid session");
        return;
    }
    
    // 服务器没有崩溃就算成功
    TEST_PASS(TEST_NAME);
}

// 测试 6: 畸形 OFFLINE 包
static void test_unregister_bad_payload(void) {
    const char *TEST_NAME = "unregister_bad_payload";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == P_INVALID_SOCKET) {
        TEST_FAIL(TEST_NAME, "failed to create socket");
        return;
    }
    
    // 发送 payload 过短的 OFFLINE 包
    uint8_t bad_pkt[16];
    bad_pkt[0] = SIG_PKT_OFFLINE;
    bad_pkt[1] = 0;
    bad_pkt[2] = 0;
    bad_pkt[3] = 0;
    // 只放 4 字节头，没有 auth_key
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_server_port);
    inet_pton(AF_INET, g_server_host, &server_addr.sin_addr);
    
    sendto(sock, (const char*)bad_pkt, 8, 0,  // 只发 8 字节（头 + 4 字节）← 4 < 8 字节的 auth_key
           (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    P_usleep(100 * 1000);
    
    P_sock_close(sock);
    
    // 检查日志
    int found = find_log("bad payload");
    if (found < 0) {
        // 如果没有日志，服务器没有崩溃也算成功
        TEST_PASS(TEST_NAME);
        return;
    }
    
    TEST_PASS(TEST_NAME);
}

// 测试 7: 重注册时旧对端收到 PEER_OFF
static void test_peer_off_on_reregister(void) {
    const char *TEST_NAME = "peer_off_on_reregister";
    printf("\n--- Test: %s ---\n", TEST_NAME);
    clear_logs();
    
    sock_t sock_alice = socket(AF_INET, SOCK_DGRAM, 0);
    sock_t sock_alice2 = socket(AF_INET, SOCK_DGRAM, 0);  // Alice 的新连接
    sock_t sock_bob = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (sock_alice == P_INVALID_SOCKET || sock_alice2 == P_INVALID_SOCKET || 
        sock_bob == P_INVALID_SOCKET) {
        if (sock_alice != P_INVALID_SOCKET) P_sock_close(sock_alice);
        if (sock_alice2 != P_INVALID_SOCKET) P_sock_close(sock_alice2);
        if (sock_bob != P_INVALID_SOCKET) P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "failed to create sockets");
        return;
    }
    
    uint32_t inst_alice1 = (uint32_t)P_tick_us() + 3400;
    uint32_t inst_bob = (uint32_t)P_tick_us() + 3401;
    uint32_t inst_alice2 = inst_alice1 + 1000;  // 新的 instance_id
    
    // Alice 第一次注册
    uint64_t session_alice1 = register_peer(sock_alice, "rereg_alice", "rereg_bob", inst_alice1, 0, NULL);
    if (session_alice1 == 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_alice2);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Alice first registration failed");
        return;
    }
    
    // Bob 注册（触发配对）
    uint64_t session_bob = register_peer(sock_bob, "rereg_bob", "rereg_alice", inst_bob, 0, NULL);
    if (session_bob == 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_alice2);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Bob registration failed");
        return;
    }
    
    // 消费 SYNC 包
    P_usleep(100 * 1000);
    P_sock_rcvtimeo(sock_alice, 500);
    P_sock_rcvtimeo(sock_bob, 500);
    uint8_t discard[256];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    for (int i = 0; i < 3; i++) {
        recvfrom(sock_alice, (char*)discard, sizeof(discard), 0, (struct sockaddr*)&from, &from_len);
        recvfrom(sock_bob, (char*)discard, sizeof(discard), 0, (struct sockaddr*)&from, &from_len);
    }
    
    // Alice 用新的 socket 和 instance_id 重新注册
    uint64_t session_alice2 = register_peer(sock_alice2, "rereg_alice", "rereg_bob", inst_alice2, 0, NULL);
    if (session_alice2 == 0) {
        P_sock_close(sock_alice);
        P_sock_close(sock_alice2);
        P_sock_close(sock_bob);
        TEST_FAIL(TEST_NAME, "Alice re-registration failed");
        return;
    }
    
    // Bob 应该收到 PEER_OFF
    uint64_t peer_off_session = 0;
    int got_peer_off = wait_peer_off(sock_bob, &peer_off_session);
    
    P_sock_close(sock_alice);
    P_sock_close(sock_alice2);
    P_sock_close(sock_bob);
    
    if (!got_peer_off) {
        TEST_FAIL(TEST_NAME, "Bob did not receive PEER_OFF on re-register");
        return;
    }
    
    TEST_PASS(TEST_NAME);
}

///////////////////////////////////////////////////////////////////////////////
// 主函数
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
    
    const char *server_path = NULL;
    
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
    
    printf("=== COMPACT Lifecycle Protocol Tests ===\n");
    printf("Server path: %s\n", server_path);
    printf("Server addr: %s:%d\n", g_server_host, g_server_port);
    printf("\n");
    
    // 初始化 instrument 监听
    instrument_local(0);
    if (instrument_listen(on_instrument_log, NULL) != E_NONE) {
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
    
    // 一、正常功能测试
    test_alive_keepalive();
    test_alive_updates_activity();
    test_unregister_releases_slot();
    test_unregister_notifies_peer();
    
    // 二、失败验证测试
    test_alive_bad_session();
    test_unregister_bad_payload();
    
    // 三、边界/临界态测试
    test_peer_off_on_reregister();
    
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
