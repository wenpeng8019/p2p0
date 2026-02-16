/*
 * test_simple_server.c - SIMPLE 服务器完整测试
 * 
 * 测试策略：
 * 1. 基本双向配对机制（单元测试）
 * 2. Mock 服务器模拟真实交互
 * 3. 详细日志验证服务器行为
 * 
 * 测试覆盖：
 * - 双向配对缓存机制
 * - 首次匹配（双边通知）
 * - 地址变化推送
 * - 断线重连、超时清理
 * - peer指针三状态（NULL / valid / -1）
 */

#include "test_framework.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <arpa/inet.h>

// 测试日志开关
static bool g_verbose = true;

#define TEST_LOG(fmt, ...) \
    do { if (g_verbose) printf("[TEST] " fmt "\n", ##__VA_ARGS__); } while(0)

// 模拟服务器端的 simple_pair_t 结构
#define P2P_PEER_ID_MAX 32
#define MAX_PEERS 128
#define SIMPLE_PAIR_TIMEOUT 30

typedef struct simple_pair_s {
    char local_peer_id[P2P_PEER_ID_MAX];
    char remote_peer_id[P2P_PEER_ID_MAX];
    struct sockaddr_in addr;
    time_t last_seen;
    bool valid;
    struct simple_pair_s *peer;
} simple_pair_t;

static simple_pair_t g_simple_pairs[MAX_PEERS];

// 模拟服务器函数
void server_init(void) {
    memset(g_simple_pairs, 0, sizeof(g_simple_pairs));
    TEST_LOG("Mock server initialized (simple mode)");
}

int server_register(const char *local_id, const char *remote_id, 
                    const char *addr_str, uint16_t port) {
    TEST_LOG("Register request: %s -> %s (%s:%d)", local_id, remote_id, addr_str, port);
    
    // 1. 查找或创建本端记录
    int local_idx = -1;
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_simple_pairs[i].valid &&
            strcmp(g_simple_pairs[i].local_peer_id, local_id) == 0 &&
            strcmp(g_simple_pairs[i].remote_peer_id, remote_id) == 0) {
            local_idx = i;
            TEST_LOG("  Found existing record for %s->%s at index %d", local_id, remote_id, i);
            break;
        }
    }
    
    if (local_idx == -1) {
        for (int i = 0; i < MAX_PEERS; i++) {
            if (!g_simple_pairs[i].valid) {
                local_idx = i;
                g_simple_pairs[i].peer = NULL;
                TEST_LOG("  Created new record for %s->%s at index %d", local_id, remote_id, i);
                break;
            }
        }
    }
    
    if (local_idx < 0) {
        TEST_LOG("  ERROR: No free slots available");
        return -1;
    }
    
    // 2. 检测地址是否变化
    int addr_changed = 0;
    struct sockaddr_in new_addr;
    inet_pton(AF_INET, addr_str, &new_addr.sin_addr);
    new_addr.sin_port = htons(port);
    new_addr.sin_family = AF_INET;
    
    if (g_simple_pairs[local_idx].valid) {
        addr_changed = (memcmp(&g_simple_pairs[local_idx].addr, &new_addr, 
                              sizeof(new_addr)) != 0);
        if (addr_changed) {
            TEST_LOG("  Address changed detected for %s", local_id);
        }
    }
    
    // 3. 更新本端记录
    strncpy(g_simple_pairs[local_idx].local_peer_id, local_id, P2P_PEER_ID_MAX - 1);
    strncpy(g_simple_pairs[local_idx].remote_peer_id, remote_id, P2P_PEER_ID_MAX - 1);
    g_simple_pairs[local_idx].addr = new_addr;
    g_simple_pairs[local_idx].last_seen = time(NULL);
    g_simple_pairs[local_idx].valid = true;
    
    if (g_simple_pairs[local_idx].peer == (simple_pair_t*)(void*)-1) {
        TEST_LOG("  Resetting peer pointer from -1 to NULL (reconnecting)");
        g_simple_pairs[local_idx].peer = NULL;
    }
    
    // 4. 查找反向配对
    int remote_idx = -1;
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_simple_pairs[i].valid &&
            strcmp(g_simple_pairs[i].local_peer_id, remote_id) == 0 &&
            strcmp(g_simple_pairs[i].remote_peer_id, local_id) == 0) {
            remote_idx = i;
            TEST_LOG("  Found reverse pair: %s->%s at index %d", remote_id, local_id, i);
            break;
        }
    }
    
    if (remote_idx >= 0) {
        simple_pair_t *local = &g_simple_pairs[local_idx];
        simple_pair_t *remote = &g_simple_pairs[remote_idx];
        
        int first_match = (local->peer == NULL || remote->peer == NULL);
        
        if (first_match) {
            local->peer = remote;
            remote->peer = local;
            TEST_LOG("  ✓ FIRST MATCH: Established bidirectional pairing");
            TEST_LOG("    -> Send PEER_INFO to both %s and %s", local_id, remote_id);
            return 1;  // 首次匹配
        } else if (addr_changed && remote->peer == local && 
                   remote->peer != (simple_pair_t*)(void*)-1) {
            TEST_LOG("  ✓ ADDRESS CHANGE: Notify %s about new address", remote_id);
            return 2;  // 地址变化
        }
        TEST_LOG("  Already paired, no change");
        return 0;  // 已配对，无变化
    }
    
    TEST_LOG("  Reverse pair not found (%s not online yet)", remote_id);
    return -2;  // 未找到反向配对
}

void server_cleanup(void) {
    time_t now = time(NULL);
    int cleaned_count = 0;
    
    TEST_LOG("Running timeout cleanup (threshold: %d seconds)", SIMPLE_PAIR_TIMEOUT);
    
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_simple_pairs[i].valid && 
            (now - g_simple_pairs[i].last_seen) > SIMPLE_PAIR_TIMEOUT) {
            
            TEST_LOG("  Cleaning up %s->%s (inactive for %ld seconds)", 
                    g_simple_pairs[i].local_peer_id,
                    g_simple_pairs[i].remote_peer_id,
                    now - g_simple_pairs[i].last_seen);
            
            if (g_simple_pairs[i].peer != NULL && 
                g_simple_pairs[i].peer != (simple_pair_t*)(void*)-1) {
                TEST_LOG("    Marking peer's pointer as -1 (disconnected)");
                g_simple_pairs[i].peer->peer = (simple_pair_t*)(void*)-1;
            }
            
            g_simple_pairs[i].valid = false;
            g_simple_pairs[i].peer = NULL;
            cleaned_count++;
        }
    }
    
    TEST_LOG("Cleanup completed (%d records removed)", cleaned_count);
}

simple_pair_t* server_get_pair(const char *local_id, const char *remote_id) {
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_simple_pairs[i].valid &&
            strcmp(g_simple_pairs[i].local_peer_id, local_id) == 0 &&
            strcmp(g_simple_pairs[i].remote_peer_id, remote_id) == 0) {
            return &g_simple_pairs[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * 第一部分：基础配对测试
 * ============================================================================ */

TEST(basic_pairing) {
    TEST_LOG("Testing basic pairing mechanism");
    server_init();
    
    // Alice 注册连接 Bob
    int ret = server_register("alice", "bob", "10.0.0.1", 5000);
    TEST_LOG("  Result: %d (expected: -2, Bob not online)", ret);
    ASSERT_EQ(ret, -2);  // Bob 未注册，应该返回 -2
    
    // 验证 Alice 的记录已创建
    simple_pair_t *alice_pair = server_get_pair("alice", "bob");
    ASSERT_NOT_NULL(alice_pair);
    ASSERT(strcmp(alice_pair->local_peer_id, "alice") == 0);
    ASSERT(strcmp(alice_pair->remote_peer_id, "bob") == 0);
    ASSERT_NULL(alice_pair->peer);  // 未配对
    TEST_LOG("  ✓ Alice record created, waiting for Bob");
}

TEST(bidirectional_matching) {
    TEST_LOG("Testing bidirectional matching");
    server_init();
    
    // Alice 注册连接 Bob
    int ret1 = server_register("alice", "bob", "10.0.0.1", 5000);
    ASSERT_EQ(ret1, -2);  // Bob 未在线
    
    // Bob 注册连接 Alice - 应该触发双向匹配
    int ret2 = server_register("bob", "alice", "10.0.0.2", 6000);
    ASSERT_EQ(ret2, 1);  // 首次匹配
    
    // 验证双向指针已建立
    simple_pair_t *alice_pair = server_get_pair("alice", "bob");
    simple_pair_t *bob_pair = server_get_pair("bob", "alice");
    
    ASSERT_NOT_NULL(alice_pair);
    ASSERT_NOT_NULL(bob_pair);
    ASSERT(alice_pair->peer == bob_pair);
    ASSERT(bob_pair->peer == alice_pair);
    TEST_LOG("  ✓ Bidirectional pointers established");
}

TEST(first_match_bilateral_notification) {
    TEST_LOG("Testing first match bilateral notification");
    server_init();
    
    // 第一步：Alice 注册
    server_register("alice", "bob", "10.0.0.1", 5000);
    
    // 第二步：Bob 注册 - 触发首次匹配
    int ret = server_register("bob", "alice", "10.0.0.2", 6000);
    ASSERT_EQ(ret, 1);  // 返回 1 表示首次匹配，需要双边通知
    
    simple_pair_t *alice_pair = server_get_pair("alice", "bob");
    simple_pair_t *bob_pair = server_get_pair("bob", "alice");
    
    // 验证两者都应该收到 PEER_INFO
    // 在实际实现中，ret==1 表示需要向双方都发送
    ASSERT_NOT_NULL(alice_pair->peer);
    ASSERT_NOT_NULL(bob_pair->peer);
    TEST_LOG("  ✓ Server should send PEER_INFO to both clients");
}

/* ============================================================================
 * 第二部分：重连与地址变化测试
 * ============================================================================ */

TEST(already_paired_reconnect) {
    TEST_LOG("Testing reconnect with no address change");
    server_init();
    
    // 建立配对
    server_register("alice", "bob", "10.0.0.1", 5000);
    server_register("bob", "alice", "10.0.0.2", 6000);
    
    // Alice 重新注册（相同地址）
    int ret = server_register("alice", "bob", "10.0.0.1", 5000);
    TEST_LOG("  Result: %d (expected: 0, no change)", ret);
    ASSERT_EQ(ret, 0);  // 返回 0 表示已配对，无变化
    
    // 验证配对关系仍然保持
    simple_pair_t *alice_pair = server_get_pair("alice", "bob");
    simple_pair_t *bob_pair = server_get_pair("bob", "alice");
    ASSERT(alice_pair->peer == bob_pair);
    TEST_LOG("  ✓ Pairing maintained, no notification needed");
}

TEST(address_change_detection) {
    TEST_LOG("Testing address change detection & notification");
    server_init();
    
    // 建立配对
    server_register("alice", "bob", "10.0.0.1", 5000);
    server_register("bob", "alice", "10.0.0.2", 6000);
    
    // Alice 地址变化
    int ret = server_register("alice", "bob", "10.0.0.99", 5555);
    TEST_LOG("  Result: %d (expected: 2, address changed)", ret);
    ASSERT_EQ(ret, 2);  // 返回 2 表示地址变化，需要通知对方
    
    // 验证地址已更新
    simple_pair_t *alice_pair = server_get_pair("alice", "bob");
    ASSERT_NOT_NULL(alice_pair);
    
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &alice_pair->addr.sin_addr, addr_str, sizeof(addr_str));
    TEST_LOG("  New address: %s:%d", addr_str, ntohs(alice_pair->addr.sin_port));
    ASSERT(strcmp(addr_str, "10.0.0.99") == 0);
    ASSERT_EQ(ntohs(alice_pair->addr.sin_port), 5555);
    TEST_LOG("  ✓ Server should notify Bob about Alice's new address");
}

/* ============================================================================
 * 第三部分：超时与清理测试
 * ============================================================================ */

TEST(timeout_cleanup) {
    TEST_LOG("Testing timeout cleanup mechanism");
    server_init();
    
    // 建立配对
    server_register("alice", "bob", "10.0.0.1", 5000);
    server_register("bob", "alice", "10.0.0.2", 6000);
    
    simple_pair_t *alice_pair = server_get_pair("alice", "bob");
    simple_pair_t *bob_pair = server_get_pair("bob", "alice");
    
    // 模拟超时
    alice_pair->last_seen = time(NULL) - SIMPLE_PAIR_TIMEOUT - 1;
    TEST_LOG("  Simulated Alice timeout (%d+ seconds)", SIMPLE_PAIR_TIMEOUT);
    
    // 执行清理
    server_cleanup();
    
    // 验证 alice 记录已失效
    ASSERT(!alice_pair->valid);
    ASSERT_NULL(alice_pair->peer);
    TEST_LOG("  ✓ Alice record invalidated");
    
    // 验证 bob 的 peer 指针被标记为 -1（对方已断开）
    ASSERT(bob_pair->peer == (simple_pair_t*)(void*)-1);
    TEST_LOG("  ✓ Bob's peer pointer marked as -1 (disconnected)");
}

TEST(reconnect_after_timeout) {
    TEST_LOG("Testing reconnect after timeout");
    server_init();
    
    // 建立配对
    server_register("alice", "bob", "10.0.0.1", 5000);
    server_register("bob", "alice", "10.0.0.2", 6000);
    
    simple_pair_t *alice_pair = server_get_pair("alice", "bob");
    simple_pair_t *bob_pair = server_get_pair("bob", "alice");
    
    // 模拟 Alice 超时
    alice_pair->last_seen = time(NULL) - SIMPLE_PAIR_TIMEOUT - 1;
    server_cleanup();
    
    // Bob 的 peer 指针应该是 -1
    ASSERT(bob_pair->peer == (simple_pair_t*)(void*)-1);
    TEST_LOG("  Bob's peer pointer is -1 after Alice timeout");
    
    // Alice 重新注册
    // 注意：Alice 超时后被清理，重新注册时会：
    // 1. 创建新的 Alice 记录（peer=NULL）
    // 2. 找到 Bob 的记录（peer=-1）
    // 3. 把 Bob 的 peer=-1 重置为 NULL
    // 4. 检测到双方都是 NULL，触发首次匹配
    int ret1 = server_register("alice", "bob", "10.0.0.1", 5000);
    TEST_LOG("  Alice reconnects, result: %d (expected: 1, re-match)", ret1);
    ASSERT_EQ(ret1, 1);  // 重新匹配成功（首次匹配逻辑）
    
    // 检查 Alice 和 Bob 的配对已经重新建立
    alice_pair = server_get_pair("alice", "bob");
    bob_pair = server_get_pair("bob", "alice");
    ASSERT_NOT_NULL(alice_pair);
    ASSERT_NOT_NULL(bob_pair);
    ASSERT(alice_pair->peer == bob_pair);
    ASSERT(bob_pair->peer == alice_pair);
    TEST_LOG("  ✓ Pairing re-established successfully");
}

/* ============================================================================
 * 第四部分：并发与隔离测试
 * ============================================================================ */

TEST(multiple_pairs) {
    TEST_LOG("Testing multiple independent pairs");
    server_init();
    
    // 建立多个配对
    server_register("alice", "bob", "10.0.0.1", 5000);
    server_register("bob", "alice", "10.0.0.2", 6000);
    
    server_register("charlie", "david", "10.0.0.3", 7000);
    server_register("david", "charlie", "10.0.0.4", 8000);
    
    // 验证两个配对都正确建立
    simple_pair_t *alice_bob = server_get_pair("alice", "bob");
    simple_pair_t *bob_alice = server_get_pair("bob", "alice");
    simple_pair_t *charlie_david = server_get_pair("charlie", "david");
    simple_pair_t *david_charlie = server_get_pair("david", "charlie");
    
    ASSERT_NOT_NULL(alice_bob);
    ASSERT_NOT_NULL(bob_alice);
    ASSERT_NOT_NULL(charlie_david);
    ASSERT_NOT_NULL(david_charlie);
    
    // 验证配对关系正确
    ASSERT(alice_bob->peer == bob_alice);
    ASSERT(charlie_david->peer == david_charlie);
    
    // 验证配对之间不会混淆
    ASSERT(alice_bob->peer != charlie_david);
    ASSERT(alice_bob->peer != david_charlie);
    TEST_LOG("  ✓ Multiple pairs properly isolated");
}

TEST(asymmetric_registration_order) {
    TEST_LOG("Testing asymmetric registration order");
    server_init();
    
    // Bob 先注册
    int ret1 = server_register("bob", "alice", "10.0.0.2", 6000);
    ASSERT_EQ(ret1, -2);  // Alice 未注册
    TEST_LOG("  Bob registers first, waiting for Alice");
    
    // Alice 后注册
    int ret2 = server_register("alice", "bob", "10.0.0.1", 5000);
    ASSERT_EQ(ret2, 1);  // 首次匹配
    TEST_LOG("  Alice registers, triggers first match");
    
    // 验证配对成功
    simple_pair_t *alice_pair = server_get_pair("alice", "bob");
    simple_pair_t *bob_pair = server_get_pair("bob", "alice");
    ASSERT(alice_pair->peer == bob_pair);
    ASSERT(bob_pair->peer == alice_pair);
    TEST_LOG("  ✓ Order doesn't matter, pairing works both ways");
}

/* ============================================================================
 * 第五部分：peer 指针状态机测试
 * ============================================================================ */

TEST(peer_pointer_states) {
    TEST_LOG("Testing peer pointer state machine (NULL / valid / -1)");
    server_init();
    
    // 初始状态：peer = NULL（未配对）
    server_register("alice", "bob", "10.0.0.1", 5000);
    simple_pair_t *alice_pair = server_get_pair("alice", "bob");
    ASSERT_NULL(alice_pair->peer);
    TEST_LOG("  State 1: peer = NULL (waiting for remote)");
    
    // 配对后：peer = 有效指针
    server_register("bob", "alice", "10.0.0.2", 6000);
    alice_pair = server_get_pair("alice", "bob");
    simple_pair_t *bob_pair = server_get_pair("bob", "alice");
    ASSERT(alice_pair->peer == bob_pair);
    ASSERT(alice_pair->peer != NULL);
    ASSERT(alice_pair->peer != (simple_pair_t*)(void*)-1);
    TEST_LOG("  State 2: peer = valid pointer (paired)");
    
    // 对方断开后：peer = (void*)-1
    alice_pair->last_seen = time(NULL) - SIMPLE_PAIR_TIMEOUT - 1;
    server_cleanup();
    ASSERT(bob_pair->peer == (simple_pair_t*)(void*)-1);
    TEST_LOG("  State 3: peer = -1 (remote disconnected)");
    
    TEST_LOG("  ✓ State machine correct: NULL -> valid -> -1");
}

/* ============================================================================
 * 第六部分：REGISTER_ACK 协议测试
 * ============================================================================ */

// REGISTER_ACK 标志位 (与 include/p2p.h 保持一致)
#define REGACK_PEER_ONLINE  0x01
#define REGACK_CAN_CACHE    0x02
#define REGACK_CACHE_FULL   0x04

// 模拟服务器生成 REGISTER_ACK 响应
typedef struct {
    uint8_t status;     // 0 = success
    uint8_t flags;      // REGACK_* 标志位
} mock_register_ack_t;

mock_register_ack_t server_gen_register_ack(const char *local_id, const char *remote_id) {
    mock_register_ack_t ack = {0, 0};
    
    // 服务器总是支持候选缓存
    ack.flags |= REGACK_CAN_CACHE;
    
    // 查找对端是否在线
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_simple_pairs[i].valid &&
            strcmp(g_simple_pairs[i].local_peer_id, remote_id) == 0 &&
            strcmp(g_simple_pairs[i].remote_peer_id, local_id) == 0) {
            ack.flags |= REGACK_PEER_ONLINE;
            TEST_LOG("  Found peer %s online", remote_id);
            break;
        }
    }
    
    return ack;
}

TEST(register_ack_peer_offline) {
    TEST_LOG("Testing REGISTER_ACK when peer is offline");
    server_init();
    
    // Alice 注册，Bob 不在线
    server_register("alice", "bob", "10.0.0.1", 5000);
    
    mock_register_ack_t ack = server_gen_register_ack("alice", "bob");
    
    ASSERT_EQ(ack.status, 0);
    ASSERT((ack.flags & REGACK_PEER_ONLINE) == 0);  // peer 不在线
    ASSERT((ack.flags & REGACK_CAN_CACHE) != 0);    // 支持缓存
    TEST_LOG("  ✓ ACK flags: peer_online=0, can_cache=1");
}

TEST(register_ack_peer_online) {
    TEST_LOG("Testing REGISTER_ACK when peer is online");
    server_init();
    
    // Bob 先注册
    server_register("bob", "alice", "10.0.0.2", 6000);
    
    // Alice 注册，此时 Bob 在线
    server_register("alice", "bob", "10.0.0.1", 5000);
    
    mock_register_ack_t ack = server_gen_register_ack("alice", "bob");
    
    ASSERT_EQ(ack.status, 0);
    ASSERT((ack.flags & REGACK_PEER_ONLINE) != 0);  // peer 在线
    ASSERT((ack.flags & REGACK_CAN_CACHE) != 0);    // 支持缓存
    TEST_LOG("  ✓ ACK flags: peer_online=1, can_cache=1");
}

/* ============================================================================
 * 第七部分：ICE_CANDIDATES 增量上报测试
 * ============================================================================ */

#define MAX_CANDIDATES 8

typedef struct {
    uint32_t ip;
    uint16_t port;
    uint8_t  type;  // 0=host, 1=srflx, 2=relay
} mock_candidate_t;

// 扩展 simple_pair 结构以支持候选缓存
typedef struct {
    simple_pair_t base;
    mock_candidate_t candidates[MAX_CANDIDATES];
    int candidate_count;
} simple_pair_ext_t;

static simple_pair_ext_t g_ext_pairs[MAX_PEERS];

void ext_server_init(void) {
    memset(g_ext_pairs, 0, sizeof(g_ext_pairs));
    TEST_LOG("Extended mock server initialized (with candidate cache)");
}

int ext_server_add_candidate(const char *local_id, const char *remote_id,
                              uint32_t ip, uint16_t port, uint8_t type) {
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_ext_pairs[i].base.valid &&
            strcmp(g_ext_pairs[i].base.local_peer_id, local_id) == 0 &&
            strcmp(g_ext_pairs[i].base.remote_peer_id, remote_id) == 0) {
            
            if (g_ext_pairs[i].candidate_count >= MAX_CANDIDATES) {
                TEST_LOG("  Candidate cache full for %s->%s", local_id, remote_id);
                return -1;
            }
            
            int idx = g_ext_pairs[i].candidate_count++;
            g_ext_pairs[i].candidates[idx].ip = ip;
            g_ext_pairs[i].candidates[idx].port = port;
            g_ext_pairs[i].candidates[idx].type = type;
            
            TEST_LOG("  Added candidate %d for %s->%s: type=%d", 
                    idx, local_id, remote_id, type);
            return idx;
        }
    }
    
    // 创建新记录
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!g_ext_pairs[i].base.valid) {
            strncpy(g_ext_pairs[i].base.local_peer_id, local_id, P2P_PEER_ID_MAX - 1);
            strncpy(g_ext_pairs[i].base.remote_peer_id, remote_id, P2P_PEER_ID_MAX - 1);
            g_ext_pairs[i].base.valid = true;
            g_ext_pairs[i].base.last_seen = time(NULL);
            g_ext_pairs[i].candidate_count = 1;
            g_ext_pairs[i].candidates[0].ip = ip;
            g_ext_pairs[i].candidates[0].port = port;
            g_ext_pairs[i].candidates[0].type = type;
            
            TEST_LOG("  Created record with first candidate for %s->%s", local_id, remote_id);
            return 0;
        }
    }
    
    return -1;
}

TEST(ice_candidates_incremental) {
    TEST_LOG("Testing ICE_CANDIDATES incremental upload");
    ext_server_init();
    
    // 模拟客户端逐步上报候选
    // 第一次：上报 host 候选
    int ret1 = ext_server_add_candidate("alice", "bob", 0x0A000001, 5000, 0);
    ASSERT_EQ(ret1, 0);
    ASSERT_EQ(g_ext_pairs[0].candidate_count, 1);
    TEST_LOG("  ✓ Added host candidate");
    
    // 第二次：上报 srflx 候选
    int ret2 = ext_server_add_candidate("alice", "bob", 0x01020304, 12345, 1);
    ASSERT_EQ(ret2, 1);
    ASSERT_EQ(g_ext_pairs[0].candidate_count, 2);
    TEST_LOG("  ✓ Added srflx candidate");
    
    // 第三次：上报 relay 候选
    int ret3 = ext_server_add_candidate("alice", "bob", 0xC0A80001, 3478, 2);
    ASSERT_EQ(ret3, 2);
    ASSERT_EQ(g_ext_pairs[0].candidate_count, 3);
    TEST_LOG("  ✓ Added relay candidate");
    
    TEST_LOG("  ✓ Incremental upload completed (3 candidates)");
}

TEST(ice_candidates_cache_full) {
    TEST_LOG("Testing ICE_CANDIDATES cache full scenario");
    ext_server_init();
    
    // 填满候选缓存
    for (int i = 0; i < MAX_CANDIDATES; i++) {
        int ret = ext_server_add_candidate("alice", "bob", 0x0A000000 + i, 5000 + i, 0);
        ASSERT(ret >= 0);
    }
    ASSERT_EQ(g_ext_pairs[0].candidate_count, MAX_CANDIDATES);
    TEST_LOG("  Filled %d candidates", MAX_CANDIDATES);
    
    // 尝试添加更多候选，应该失败
    int ret = ext_server_add_candidate("alice", "bob", 0x0A0000FF, 9999, 0);
    ASSERT_EQ(ret, -1);
    TEST_LOG("  ✓ Cache full, additional candidate rejected");
}

/* ============================================================================
 * 主函数
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("SIMPLE Server Complete Test Suite\n");
    printf("========================================\n\n");
    
    printf("Part 1: Basic Pairing\n");
    printf("----------------------------------------\n");
    RUN_TEST(basic_pairing);
    RUN_TEST(bidirectional_matching);
    RUN_TEST(first_match_bilateral_notification);
    
    printf("\nPart 2: Reconnect & Address Change\n");
    printf("----------------------------------------\n");
    RUN_TEST(already_paired_reconnect);
    RUN_TEST(address_change_detection);
    
    printf("\nPart 3: Timeout & Cleanup\n");
    printf("----------------------------------------\n");
    RUN_TEST(timeout_cleanup);
    RUN_TEST(reconnect_after_timeout);
    
    printf("\nPart 4: Concurrency & Isolation\n");
    printf("----------------------------------------\n");
    RUN_TEST(multiple_pairs);
    RUN_TEST(asymmetric_registration_order);
    
    printf("\nPart 5: Peer Pointer State Machine\n");
    printf("----------------------------------------\n");
    RUN_TEST(peer_pointer_states);
    
    printf("\nPart 6: REGISTER_ACK Protocol\n");
    printf("----------------------------------------\n");
    RUN_TEST(register_ack_peer_offline);
    RUN_TEST(register_ack_peer_online);
    
    printf("\nPart 7: ICE_CANDIDATES Protocol\n");
    printf("----------------------------------------\n");
    RUN_TEST(ice_candidates_incremental);
    RUN_TEST(ice_candidates_cache_full);
    
    printf("\n");
    TEST_SUMMARY();
    
    return (test_failed > 0) ? 1 : 0;
}
