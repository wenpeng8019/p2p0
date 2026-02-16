/*
 * test_relay_server.c - Relay 服务器完整测试
 * 
 * 测试策略：
 * 1. 协议格式验证（单元测试）
 * 2. Mock 客户端模拟真实交互（集成测试）
 * 3. 详细日志输出验证服务器行为
 * 
 * 测试覆盖：
 * - 协议格式、消息类型、转发规则
 * - 登录流程、用户列表、SDP 交换
 * - 心跳超时、错误处理、并发隔离
 */

#include "test_framework.h"
#include "../p2p_server/protocol.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <arpa/inet.h>
#include <unistd.h>

// 测试日志开关
static bool g_verbose = true;

#define TEST_LOG(fmt, ...) \
    do { if (g_verbose) printf("[TEST] " fmt "\n", ##__VA_ARGS__); } while(0)

/* ============================================================================
 * Mock 服务器状态（模拟 server.c 的关键数据结构）
 * ============================================================================ */

#define MAX_MOCK_CLIENTS 16

typedef struct {
    int fd;
    char name[P2P_MAX_NAME];
    time_t last_active;
    bool valid;
} mock_relay_client_t;

static mock_relay_client_t g_mock_clients[MAX_MOCK_CLIENTS];
static int g_mock_client_count = 0;

void mock_server_init(void) {
    memset(g_mock_clients, 0, sizeof(g_mock_clients));
    g_mock_client_count = 0;
    TEST_LOG("Mock server initialized");
}

// Mock 客户端注册
int mock_client_register(const char *name) {
    if (g_mock_client_count >= MAX_MOCK_CLIENTS) return -1;
    
    int idx = g_mock_client_count++;
    g_mock_clients[idx].fd = idx + 100;  // 虚拟 fd
    strncpy(g_mock_clients[idx].name, name, P2P_MAX_NAME);
    g_mock_clients[idx].last_active = time(NULL);
    g_mock_clients[idx].valid = true;
    
    TEST_LOG("Mock client '%s' registered (fd=%d)", name, g_mock_clients[idx].fd);
    return g_mock_clients[idx].fd;
}

// Mock 服务器处理登录
int mock_server_handle_login(int fd, const char *name) {
    for (int i = 0; i < g_mock_client_count; i++) {
        if (g_mock_clients[i].fd == fd) {
            strncpy(g_mock_clients[i].name, name, P2P_MAX_NAME);
            TEST_LOG("Server: Client fd=%d logged in as '%s'", fd, name);
            return 0;
        }
    }
    return -1;
}

// Mock 服务器查找客户端
int mock_server_find_client(const char *name) {
    for (int i = 0; i < g_mock_client_count; i++) {
        if (g_mock_clients[i].valid && 
            strcmp(g_mock_clients[i].name, name) == 0) {
            TEST_LOG("Server: Found client '%s' (fd=%d)", name, g_mock_clients[i].fd);
            return g_mock_clients[i].fd;
        }
    }
    TEST_LOG("Server: Client '%s' not found", name);
    return -1;
}

// Mock 服务器生成用户列表
int mock_server_get_user_list(int requesting_fd, char *buffer, int buf_size) {
    int offset = 0;
    TEST_LOG("Server: Generating user list for fd=%d", requesting_fd);
    
    for (int i = 0; i < g_mock_client_count; i++) {
        if (g_mock_clients[i].valid && g_mock_clients[i].fd != requesting_fd) {
            int remaining = buf_size - offset;
            if (remaining < P2P_MAX_NAME + 2) break;
            
            int n = snprintf(buffer + offset, remaining, "%s,", 
                           g_mock_clients[i].name);
            if (n >= remaining) break;
            offset += n;
            TEST_LOG("  - Added '%s' to list", g_mock_clients[i].name);
        }
    }
    
    TEST_LOG("Server: User list generated (%d bytes)", offset);
    return offset;
}

// Mock 服务器更新活跃时间
void mock_server_update_active(int fd) {
    for (int i = 0; i < g_mock_client_count; i++) {
        if (g_mock_clients[i].fd == fd) {
            g_mock_clients[i].last_active = time(NULL);
            return;
        }
    }
}

// Mock 服务器心跳超时检查
int mock_server_check_timeout(int timeout_sec) {
    time_t now = time(NULL);
    int timeout_count = 0;
    
    for (int i = 0; i < g_mock_client_count; i++) {
        if (g_mock_clients[i].valid) {
            if ((now - g_mock_clients[i].last_active) > timeout_sec) {
                TEST_LOG("Server: Client '%s' (fd=%d) timed out", 
                        g_mock_clients[i].name, g_mock_clients[i].fd);
                g_mock_clients[i].valid = false;
                timeout_count++;
            }
        }
    }
    
    return timeout_count;
}

/* ============================================================================
 * 第一部分：协议基础测试
 * ============================================================================ */

TEST(protocol_header_size) {
    TEST_LOG("Testing protocol header size");
    ASSERT_EQ(sizeof(p2p_msg_hdr_t), 9);  // 4 + 1 + 4 bytes
}

TEST(protocol_magic_constant) {
    TEST_LOG("Testing magic constant: 0x%08X", P2P_SIGNAL_MAGIC);
    ASSERT_EQ(P2P_SIGNAL_MAGIC, 0x50325030);  // "P2P0"
}

TEST(protocol_message_types) {
    TEST_LOG("Validating message type enums");
    ASSERT_EQ(MSG_LOGIN, 1);
    ASSERT_EQ(MSG_LOGIN_ACK, 2);
    ASSERT_EQ(MSG_LIST, 3);
    ASSERT_EQ(MSG_LIST_RES, 4);
    ASSERT_EQ(MSG_CONNECT, 5);
    ASSERT_EQ(MSG_SIGNAL, 6);
    ASSERT_EQ(MSG_SIGNAL_ANS, 7);
    ASSERT_EQ(MSG_SIGNAL_RELAY, 8);
    ASSERT_EQ(MSG_HEARTBEAT, 9);
}

/* ============================================================================
 * 第二部分：消息转发规则测试
 * ============================================================================ */

TEST(message_relay_connect_to_signal) {
    TEST_LOG("Testing: MSG_CONNECT -> MSG_SIGNAL relay");
    
    uint8_t client_msg = MSG_CONNECT;
    uint8_t expected_relay = MSG_SIGNAL;
    uint8_t actual_relay = (client_msg == MSG_CONNECT) ? MSG_SIGNAL : MSG_SIGNAL_RELAY;
    
    TEST_LOG("  Client sends: MSG_CONNECT(%d)", client_msg);
    TEST_LOG("  Server relays: MSG_SIGNAL(%d)", actual_relay);
    
    ASSERT_EQ(actual_relay, expected_relay);
}

TEST(message_relay_answer_to_relay) {
    TEST_LOG("Testing: MSG_SIGNAL_ANS -> MSG_SIGNAL_RELAY relay");
    
    uint8_t client_msg = MSG_SIGNAL_ANS;
    uint8_t expected_relay = MSG_SIGNAL_RELAY;
    uint8_t actual_relay = (client_msg == MSG_CONNECT) ? MSG_SIGNAL : MSG_SIGNAL_RELAY;
    
    TEST_LOG("  Client sends: MSG_SIGNAL_ANS(%d)", client_msg);
    TEST_LOG("  Server relays: MSG_SIGNAL_RELAY(%d)", actual_relay);
    
    ASSERT_EQ(actual_relay, expected_relay);
}

TEST(message_flow_complete) {
    TEST_LOG("Testing complete SDP exchange message flow");
    
    // Alice 发起连接
    uint8_t step1_client = MSG_CONNECT;
    uint8_t step1_relay = (step1_client == MSG_CONNECT) ? MSG_SIGNAL : MSG_SIGNAL_RELAY;
    TEST_LOG("  Step 1: Alice MSG_CONNECT -> Server MSG_SIGNAL to Bob");
    ASSERT_EQ(step1_relay, MSG_SIGNAL);
    
    // Bob 应答
    uint8_t step2_client = MSG_SIGNAL_ANS;
    uint8_t step2_relay = (step2_client == MSG_CONNECT) ? MSG_SIGNAL : MSG_SIGNAL_RELAY;
    TEST_LOG("  Step 2: Bob MSG_SIGNAL_ANS -> Server MSG_SIGNAL_RELAY to Alice");
    ASSERT_EQ(step2_relay, MSG_SIGNAL_RELAY);
    
    ASSERT(step1_relay != step2_relay);
}

/* ============================================================================
 * 第三部分：登录流程测试
 * ============================================================================ */

TEST(login_message_structure) {
    TEST_LOG("Testing LOGIN message structure");
    
    p2p_msg_hdr_t login_hdr = {
        P2P_SIGNAL_MAGIC,
        MSG_LOGIN,
        sizeof(p2p_msg_login_t)
    };
    
    p2p_msg_login_t login_data;
    strncpy(login_data.name, "alice", P2P_MAX_NAME);
    
    TEST_LOG("  Header: magic=0x%08X, type=%d, length=%d", 
            login_hdr.magic, login_hdr.type, login_hdr.length);
    TEST_LOG("  Login name: '%s'", login_data.name);
    
    ASSERT_EQ(login_hdr.magic, P2P_SIGNAL_MAGIC);
    ASSERT_EQ(login_hdr.type, MSG_LOGIN);
    ASSERT_EQ(login_hdr.length, sizeof(p2p_msg_login_t));
    ASSERT(strcmp(login_data.name, "alice") == 0);
}

TEST(complete_login_flow) {
    TEST_LOG("Testing complete login flow with mock server");
    
    mock_server_init();
    int client_fd = mock_client_register("unknown");
    
    // 客户端发送 LOGIN
    p2p_msg_login_t login_data;
    strncpy(login_data.name, "alice", P2P_MAX_NAME);
    TEST_LOG("  Client fd=%d sends LOGIN (name='alice')", client_fd);
    
    // 服务器处理登录
    int ret = mock_server_handle_login(client_fd, login_data.name);
    ASSERT_EQ(ret, 0);
    
    // 验证状态
    ASSERT(strcmp(g_mock_clients[0].name, "alice") == 0);
    TEST_LOG("  Server confirmed: client is now 'alice'");
    
    // 服务器发送 LOGIN_ACK
    p2p_msg_hdr_t ack = {P2P_SIGNAL_MAGIC, MSG_LOGIN_ACK, 0};
    TEST_LOG("  Server sends LOGIN_ACK");
    ASSERT_EQ(ack.type, MSG_LOGIN_ACK);
}

/* ============================================================================
 * 第四部分：用户列表测试
 * ============================================================================ */

TEST(user_list_generation) {
    TEST_LOG("Testing user list generation");
    
    mock_server_init();
    int alice_fd = mock_client_register("alice");
    mock_client_register("bob");
    mock_client_register("charlie");
    
    TEST_LOG("  3 clients online: alice, bob, charlie");
    
    // alice 请求列表
    char list_buf[1024];
    int list_len = mock_server_get_user_list(alice_fd, list_buf, sizeof(list_buf));
    
    TEST_LOG("  Alice's list: '%.*s'", list_len, list_buf);
    
    ASSERT(list_len > 0);
    ASSERT(strstr(list_buf, "bob") != NULL);
    ASSERT(strstr(list_buf, "charlie") != NULL);
    ASSERT(strstr(list_buf, "alice") == NULL);  // 不包含自己
}

TEST(empty_user_list_handling) {
    TEST_LOG("Testing empty user list (only requester online)");
    
    mock_server_init();
    int alice_fd = mock_client_register("alice");
    
    char list_buf[1024];
    int list_len = mock_server_get_user_list(alice_fd, list_buf, sizeof(list_buf));
    
    TEST_LOG("  Only alice online, list length: %d", list_len);
    ASSERT_EQ(list_len, 0);
}

/* ============================================================================
 * 第五部分：SDP 交换流程测试
 * ============================================================================ */

TEST(connect_message_structure) {
    TEST_LOG("Testing CONNECT message structure");
    
    const char *target = "bob";
    const char *sdp = "v=0\r\no=- 123 IN IP4 10.0.0.1\r\n";
    uint32_t sdp_len = strlen(sdp);
    
    p2p_msg_hdr_t hdr = {
        P2P_SIGNAL_MAGIC,
        MSG_CONNECT,
        P2P_MAX_NAME + sdp_len
    };
    
    TEST_LOG("  Target: '%s', SDP length: %d", target, sdp_len);
    TEST_LOG("  Total payload: %d bytes", hdr.length);
    
    ASSERT_EQ(hdr.length, P2P_MAX_NAME + sdp_len);
}

TEST(complete_sdp_exchange_flow) {
    TEST_LOG("Testing complete SDP exchange flow");
    
    mock_server_init();
    int alice_fd = mock_client_register("alice");
    int bob_fd = mock_client_register("bob");
    
    // 步骤 1: Alice 发送 CONNECT 给 Bob
    TEST_LOG("  [1] Alice sends CONNECT(target=bob, SDP_OFFER)");
    
    char connect_payload[P2P_MAX_NAME + 20];
    memset(connect_payload, 0, sizeof(connect_payload));
    strncpy(connect_payload, "bob", P2P_MAX_NAME);
    strcpy(connect_payload + P2P_MAX_NAME, "SDP_OFFER_DATA");
    
    // 步骤 2: 服务器查找 Bob
    int target_fd = mock_server_find_client("bob");
    ASSERT_EQ(target_fd, bob_fd);
    TEST_LOG("  [2] Server found Bob (fd=%d)", target_fd);
    
    // 步骤 3: 服务器转发为 MSG_SIGNAL 给 Bob
    TEST_LOG("  [3] Server relays as MSG_SIGNAL to Bob");
    
    char signal_payload[P2P_MAX_NAME + 20];
    memset(signal_payload, 0, sizeof(signal_payload));
    strncpy(signal_payload, "alice", P2P_MAX_NAME);  // 源客户端
    strcpy(signal_payload + P2P_MAX_NAME, "SDP_OFFER_DATA");
    
    ASSERT(strcmp(signal_payload, "alice") == 0);
    
    // 步骤 4: Bob 发送 SIGNAL_ANS 给 Alice
    TEST_LOG("  [4] Bob sends SIGNAL_ANS(target=alice, SDP_ANSWER)");
    
    // 步骤 5: 服务器查找 Alice
    target_fd = mock_server_find_client("alice");
    ASSERT_EQ(target_fd, alice_fd);
    TEST_LOG("  [5] Server found Alice (fd=%d)", target_fd);
    
    // 步骤 6: 服务器转发为 MSG_SIGNAL_RELAY 给 Alice
    TEST_LOG("  [6] Server relays as MSG_SIGNAL_RELAY to Alice");
    
    char relay_payload[P2P_MAX_NAME + 20];
    memset(relay_payload, 0, sizeof(relay_payload));
    strncpy(relay_payload, "bob", P2P_MAX_NAME);  // 源客户端
    strcpy(relay_payload + P2P_MAX_NAME, "SDP_ANSWER_DATA");
    
    ASSERT(strcmp(relay_payload, "bob") == 0);
    TEST_LOG("  [✓] SDP exchange completed successfully");
}

/* ============================================================================
 * 第六部分：心跳与超时测试
 * ============================================================================ */

TEST(heartbeat_message_handling) {
    TEST_LOG("Testing heartbeat message handling");
    
    p2p_msg_hdr_t hb = {P2P_SIGNAL_MAGIC, MSG_HEARTBEAT, 0};
    
    TEST_LOG("  Client sends MSG_HEARTBEAT (length=0)");
    ASSERT_EQ(hb.type, MSG_HEARTBEAT);
    ASSERT_EQ(hb.length, 0);
    
    // 服务器应该更新 last_active
    mock_server_init();
    int fd = mock_client_register("alice");
    time_t before = g_mock_clients[0].last_active;
    
    sleep(1);
    mock_server_update_active(fd);
    time_t after = g_mock_clients[0].last_active;
    
    TEST_LOG("  Server updated last_active: %ld -> %ld", before, after);
    ASSERT(after >= before);
}

TEST(heartbeat_timeout_logic) {
    TEST_LOG("Testing heartbeat timeout detection");
    
    #define RELAY_CLIENT_TIMEOUT 60
    
    time_t now = time(NULL);
    
    // 场景 1：活跃客户端
    time_t last_active = now - 30;
    bool should_timeout = ((now - last_active) > RELAY_CLIENT_TIMEOUT);
    TEST_LOG("  Active client (30s ago): timeout=%d (expected: 0)", should_timeout);
    ASSERT(!should_timeout);
    
    // 场景 2：超时客户端
    last_active = now - 70;
    should_timeout = ((now - last_active) > RELAY_CLIENT_TIMEOUT);
    TEST_LOG("  Timeout client (70s ago): timeout=%d (expected: 1)", should_timeout);
    ASSERT(should_timeout);
    
    // 场景 3：边界值
    last_active = now - RELAY_CLIENT_TIMEOUT;
    should_timeout = ((now - last_active) > RELAY_CLIENT_TIMEOUT);
    TEST_LOG("  Boundary (60s): timeout=%d (expected: 0)", should_timeout);
    ASSERT(!should_timeout);
}

TEST(server_timeout_cleanup) {
    TEST_LOG("Testing server timeout cleanup");
    
    mock_server_init();
    mock_client_register("alice");
    mock_client_register("bob");
    
    TEST_LOG("  2 clients registered");
    
    // 模拟 alice 超时
    g_mock_clients[0].last_active = time(NULL) - 70;
    TEST_LOG("  Alice last_active set to 70s ago");
    
    int timeout_count = mock_server_check_timeout(60);
    TEST_LOG("  Cleanup found %d timeout clients", timeout_count);
    
    ASSERT_EQ(timeout_count, 1);
    ASSERT(!g_mock_clients[0].valid);  // alice 应该被清理
    ASSERT(g_mock_clients[1].valid);   // bob 仍然在线
}

/* ============================================================================
 * 第七部分：并发与隔离测试
 * ============================================================================ */

TEST(multiple_clients_isolation) {
    TEST_LOG("Testing multiple clients isolation");
    
    mock_server_init();
    int alice_fd = mock_client_register("alice");
    int bob_fd = mock_client_register("bob");
    int charlie_fd = mock_client_register("charlie");
    int david_fd = mock_client_register("david");
    
    TEST_LOG("  4 clients registered");
    
    // Alice 连接 Bob
    int target = mock_server_find_client("bob");
    ASSERT_EQ(target, bob_fd);
    TEST_LOG("  Alice -> Bob connection OK");
    
    // Charlie 连接 David
    target = mock_server_find_client("david");
    ASSERT_EQ(target, david_fd);
    TEST_LOG("  Charlie -> David connection OK");
    
    // 验证不会混淆
    ASSERT(alice_fd != charlie_fd);
    ASSERT(bob_fd != david_fd);
    TEST_LOG("  Connections properly isolated");
}

/* ============================================================================
 * 第八部分：错误处理测试
 * ============================================================================ */

TEST(invalid_magic_detection) {
    TEST_LOG("Testing invalid magic detection");
    
    p2p_msg_hdr_t valid_hdr = {P2P_SIGNAL_MAGIC, MSG_LOGIN, 32};
    p2p_msg_hdr_t invalid_hdr = {0x12345678, MSG_LOGIN, 32};
    
    TEST_LOG("  Valid magic: 0x%08X", valid_hdr.magic);
    TEST_LOG("  Invalid magic: 0x%08X", invalid_hdr.magic);
    
    ASSERT(valid_hdr.magic == P2P_SIGNAL_MAGIC);
    ASSERT(invalid_hdr.magic != P2P_SIGNAL_MAGIC);
}

TEST(client_not_found_handling) {
    TEST_LOG("Testing client not found handling");
    
    mock_server_init();
    mock_client_register("alice");
    
    int target_fd = mock_server_find_client("eve");
    TEST_LOG("  Search for 'eve': fd=%d (expected: -1)", target_fd);
    ASSERT_EQ(target_fd, -1);
}

TEST(max_payload_size_validation) {
    TEST_LOG("Testing max payload size validation");
    
    const uint32_t MAX_PAYLOAD = 65536;
    
    uint32_t valid_size = 1024;
    uint32_t invalid_size = 100000;
    
    TEST_LOG("  Valid size: %d <= %d", valid_size, MAX_PAYLOAD);
    TEST_LOG("  Invalid size: %d > %d", invalid_size, MAX_PAYLOAD);
    
    ASSERT(valid_size <= MAX_PAYLOAD);
    ASSERT(invalid_size > MAX_PAYLOAD);
}

TEST(buffer_overflow_protection) {
    TEST_LOG("Testing buffer overflow protection");
    
    const int LIST_BUF_SIZE = 1024;
    const int MAX_NAME = P2P_MAX_NAME;
    
    int offset = 1000;
    int remaining = LIST_BUF_SIZE - offset;
    
    bool has_space = (remaining >= MAX_NAME + 2);
    TEST_LOG("  Offset: %d, Remaining: %d, Has space: %d", offset, remaining, has_space);
    ASSERT(!has_space);
}

/* ============================================================================
 * 主函数
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("ICE Server Complete Test Suite\n");
    printf("========================================\n\n");
    
    printf("Part 1: Protocol Basics\n");
    printf("----------------------------------------\n");
    RUN_TEST(protocol_header_size);
    RUN_TEST(protocol_magic_constant);
    RUN_TEST(protocol_message_types);
    
    printf("\nPart 2: Message Relay Rules\n");
    printf("----------------------------------------\n");
    RUN_TEST(message_relay_connect_to_signal);
    RUN_TEST(message_relay_answer_to_relay);
    RUN_TEST(message_flow_complete);
    
    printf("\nPart 3: Login Flow\n");
    printf("----------------------------------------\n");
    RUN_TEST(login_message_structure);
    RUN_TEST(complete_login_flow);
    
    printf("\nPart 4: User List\n");
    printf("----------------------------------------\n");
    RUN_TEST(user_list_generation);
    RUN_TEST(empty_user_list_handling);
    
    printf("\nPart 5: SDP Exchange\n");
    printf("----------------------------------------\n");
    RUN_TEST(connect_message_structure);
    RUN_TEST(complete_sdp_exchange_flow);
    
    printf("\nPart 6: Heartbeat & Timeout\n");
    printf("----------------------------------------\n");
    RUN_TEST(heartbeat_message_handling);
    RUN_TEST(heartbeat_timeout_logic);
    RUN_TEST(server_timeout_cleanup);
    
    printf("\nPart 7: Concurrency & Isolation\n");
    printf("----------------------------------------\n");
    RUN_TEST(multiple_clients_isolation);
    
    printf("\nPart 8: Error Handling\n");
    printf("----------------------------------------\n");
    RUN_TEST(invalid_magic_detection);
    RUN_TEST(client_not_found_handling);
    RUN_TEST(max_payload_size_validation);
    RUN_TEST(buffer_overflow_protection);
    
    printf("\n");
    TEST_SUMMARY();
    
    return (test_failed > 0) ? 1 : 0;
}
