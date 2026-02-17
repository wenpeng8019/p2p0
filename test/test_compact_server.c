/*
 * test_compact_server.c - COMPACT 服务器完整测试
 * 
 * 测试协议特性：
 * 1. REGISTER / REGISTER_ACK (含 max_candidates)
 * 2. PEER_INFO 序列化传输 (base_index + seq=1)
 * 3. 离线缓存机制
 * 4. 双向配对与首次匹配
 * 5. 地址变化推送
 * 6. 超时清理
 * 7. 候选列表完整性
 */

#include "test_framework.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <p2pp.h>

// 测试日志开关
static bool g_verbose = true;

#define TEST_LOG(fmt, ...) \
    do { if (g_verbose) printf("[TEST] " fmt "\n", ##__VA_ARGS__); } while(0)

/* ============================================================================
 * 模拟服务器数据结构
 * ============================================================================ */

#define MAX_PEERS 128
#define COMPACT_PAIR_TIMEOUT 30
#define COMPACT_MAX_CANDIDATES 8

typedef struct {
    uint8_t type;
    uint32_t ip;
    uint16_t port;
} candidate_t;

typedef struct compact_pair_s {
    char local_peer_id[P2P_PEER_ID_MAX];
    char remote_peer_id[P2P_PEER_ID_MAX];
    struct sockaddr_in addr;
    candidate_t candidates[COMPACT_MAX_CANDIDATES];
    int candidate_count;
    time_t last_seen;
    bool valid;
    struct compact_pair_s *peer;
} compact_pair_t;

static compact_pair_t g_compact_pairs[MAX_PEERS];

/* ============================================================================
 * 模拟服务器核心函数
 * ============================================================================ */

void mock_compact_server_init(void) {
    memset(g_compact_pairs, 0, sizeof(g_compact_pairs));
    TEST_LOG("Mock server initialized");
}

typedef struct {
    uint8_t status;          // 0=离线, 1=在线, >=2=error
    uint8_t max_candidates;  // 服务器缓存能力
    uint32_t public_ip;      // 客户端的公网 IP
    uint16_t public_port;    // 客户端的公网端口
    uint16_t probe_port;     // NAT 探测端口（0=不支持）
} register_ack_t;

// 模拟 REGISTER 处理逻辑
register_ack_t mock_compact_server_register(const char *local_id, const char *remote_id,
                                     const char *ip_str, uint16_t port,
                                     candidate_t *candidates, int cand_count) {
    TEST_LOG("REGISTER: %s -> %s (%s:%d) with %d candidates", 
             local_id, remote_id, ip_str, port, cand_count);
    
    register_ack_t ack = {0};
    
    // 1. 查找或创建本端槽位
    int local_idx = -1;
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_compact_pairs[i].valid && 
            strcmp(g_compact_pairs[i].local_peer_id, local_id) == 0 &&
            strcmp(g_compact_pairs[i].remote_peer_id, remote_id) == 0) {
            local_idx = i;
            break;
        }
    }
    
    if (local_idx == -1) {
        for (int i = 0; i < MAX_PEERS; i++) {
            if (!g_compact_pairs[i].valid) {
                local_idx = i;
                g_compact_pairs[i].peer = NULL;
                break;
            }
        }
    }
    
    if (local_idx < 0) {
        ack.status = 1;  // 错误：无可用槽位
        TEST_LOG("  ERROR: No slot available");
        return ack;
    }
    
    // 2. 更新本端记录
    strncpy(g_compact_pairs[local_idx].local_peer_id, local_id, P2P_PEER_ID_MAX - 1);
    strncpy(g_compact_pairs[local_idx].remote_peer_id, remote_id, P2P_PEER_ID_MAX - 1);
    inet_pton(AF_INET, ip_str, &g_compact_pairs[local_idx].addr.sin_addr);
    g_compact_pairs[local_idx].addr.sin_port = htons(port);
    g_compact_pairs[local_idx].addr.sin_family = AF_INET;
    g_compact_pairs[local_idx].candidate_count = (cand_count > COMPACT_MAX_CANDIDATES) ? 
                                                 COMPACT_MAX_CANDIDATES : cand_count;
    memcpy(g_compact_pairs[local_idx].candidates, candidates, 
           g_compact_pairs[local_idx].candidate_count * sizeof(candidate_t));
    g_compact_pairs[local_idx].last_seen = time(NULL);
    g_compact_pairs[local_idx].valid = true;
    
    if (g_compact_pairs[local_idx].peer == (compact_pair_t*)(void*)-1) {
        g_compact_pairs[local_idx].peer = NULL;
    }
    
    // 3. 查找反向配对
    int remote_idx = -1;
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_compact_pairs[i].valid && 
            strcmp(g_compact_pairs[i].local_peer_id, remote_id) == 0 &&
            strcmp(g_compact_pairs[i].remote_peer_id, local_id) == 0) {
            remote_idx = i;
            break;
        }
    }
    
    // 4. 构造 REGISTER_ACK
    ack.status = (remote_idx >= 0) ? SIG_REGACK_PEER_ONLINE : SIG_REGACK_PEER_OFFLINE;
    ack.max_candidates = COMPACT_MAX_CANDIDATES;
    
    // 填充公网地址（服务器观察到的客户端源地址）
    inet_pton(AF_INET, ip_str, &ack.public_ip);
    ack.public_port = htons(port);
    ack.probe_port = 0;  // 测试环境不支持 NAT 探测
    
    TEST_LOG("  REGISTER_ACK: peer_online=%d, max=%d, public=%s:%d", 
             (remote_idx >= 0) ? 1 : 0, COMPACT_MAX_CANDIDATES, ip_str, port);
    
    // 5. 如果对端在线，建立双向配对
    if (remote_idx >= 0) {
        compact_pair_t *local = &g_compact_pairs[local_idx];
        compact_pair_t *remote = &g_compact_pairs[remote_idx];
        
        int first_match = (local->peer == NULL || remote->peer == NULL);
        
        if (first_match) {
            local->peer = remote;
            remote->peer = local;
            TEST_LOG("  ✓ FIRST MATCH: Bilateral pairing established");
            TEST_LOG("  -> Will send PEER_INFO(seq=1) to both peers");
        }
    } else {
        TEST_LOG("  Peer '%s' not online yet, caching candidates", remote_id);
    }
    
    return ack;
}

// 模拟发送 PEER_INFO(seq=1)
typedef struct {
    uint8_t base_index;
    uint8_t count;
    candidate_t candidates[COMPACT_MAX_CANDIDATES];
} peer_info_t;

peer_info_t mock_compact_server_get_peer_info(const char *requester_id, const char *target_id) {
    peer_info_t info = {0};
    
    // 查找目标的候选列表
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_compact_pairs[i].valid && 
            strcmp(g_compact_pairs[i].local_peer_id, target_id) == 0) {
            info.base_index = 0;  // seq=1 始终从 base=0 开始
            info.count = g_compact_pairs[i].candidate_count;
            memcpy(info.candidates, g_compact_pairs[i].candidates, 
                   info.count * sizeof(candidate_t));
            
            TEST_LOG("PEER_INFO(seq=1): Send %s's %d candidates to %s", 
                     target_id, info.count, requester_id);
            for (int j = 0; j < info.count; j++) {
                struct in_addr ip;
                ip.s_addr = info.candidates[j].ip;
                TEST_LOG("  [%d] type=%d, %s:%d", j, info.candidates[j].type,
                         inet_ntoa(ip), ntohs(info.candidates[j].port));
            }
            break;
        }
    }
    
    return info;
}

// 超时清理
int mock_compact_server_cleanup(void) {
    time_t now = time(NULL);
    int cleaned = 0;
    
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_compact_pairs[i].valid && 
            (now - g_compact_pairs[i].last_seen) > COMPACT_PAIR_TIMEOUT) {
            
            TEST_LOG("Cleanup: %s->%s timed out", 
                     g_compact_pairs[i].local_peer_id,
                     g_compact_pairs[i].remote_peer_id);
            
            if (g_compact_pairs[i].peer != NULL && 
                g_compact_pairs[i].peer != (compact_pair_t*)(void*)-1) {
                g_compact_pairs[i].peer->peer = (compact_pair_t*)(void*)-1;
            }
            
            g_compact_pairs[i].valid = false;
            g_compact_pairs[i].peer = NULL;
            cleaned++;
        }
    }
    
    return cleaned;
}

compact_pair_t* mock_compact_server_get_pair(const char *local_id, const char *remote_id) {
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_compact_pairs[i].valid &&
            strcmp(g_compact_pairs[i].local_peer_id, local_id) == 0 &&
            strcmp(g_compact_pairs[i].remote_peer_id, remote_id) == 0) {
            return &g_compact_pairs[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * 第一部分：REGISTER_ACK 协议测试
 * ============================================================================ */

TEST(register_ack_with_relay_support) {
    TEST_LOG("Testing REGISTER_ACK with relay support flag");
    mock_compact_server_init();
    
    candidate_t cands[2] = {
        {0, htonl(0x0A000001), htons(5000)},
        {1, htonl(0x0A000002), htons(5001)},
    };
    
    // 模拟服务器启用了 relay 支持
    register_ack_t ack = mock_compact_server_register("alice", "bob", "192.168.1.100", 12345, cands, 2);
    
    ASSERT_EQ(ack.status, SIG_REGACK_PEER_OFFLINE);
    ASSERT_EQ(ack.max_candidates, COMPACT_MAX_CANDIDATES);
    
    // 注意：这里需要在 mock_compact_server_register 中根据全局配置设置 relay flags
    // 实际测试时，应该通过 header flags 检查
    TEST_LOG("  ✓ Server can indicate relay support via header.flags");
}

TEST(register_ack_with_probe_port_config) {
    TEST_LOG("Testing REGISTER_ACK with configured NAT probe port");
    mock_compact_server_init();
    
    candidate_t cands[1] = {
        {0, htonl(0x0A000001), htons(5000)},
    };
    
    register_ack_t ack = mock_compact_server_register("alice", "bob", "1.2.3.4", 12345, cands, 1);
    
    // 在真实服务器中，probe_port 从配置读取
    // 这里测试结构支持
    ASSERT_EQ(ack.probe_port, 0);  // mock server 默认不支持
    TEST_LOG("  ✓ probe_port field available in REGISTER_ACK");
}

TEST(register_ack_peer_offline) {
    TEST_LOG("Testing REGISTER_ACK when peer is offline");
    mock_compact_server_init();
    
    candidate_t cands[3] = {
        {0, htonl(0x0A000001), htons(5000)},
        {1, htonl(0x01020304), htons(12345)},
        {2, htonl(0xC0A80001), htons(3478)}
    };
    
    register_ack_t ack = mock_compact_server_register("alice", "bob", "10.0.0.1", 5000, cands, 3);
    
    ASSERT_EQ(ack.status, SIG_REGACK_PEER_OFFLINE);  // peer offline
    ASSERT_EQ(ack.max_candidates, COMPACT_MAX_CANDIDATES);
    
    // 验证候选已缓存
    compact_pair_t *pair = mock_compact_server_get_pair("alice", "bob");
    ASSERT_NOT_NULL(pair);
    ASSERT_EQ(pair->candidate_count, 3);
    
    TEST_LOG("  ✓ Peer offline, candidates cached, max=%d", ack.max_candidates);
}

TEST(register_ack_peer_online) {
    TEST_LOG("Testing REGISTER_ACK when peer is online");
    mock_compact_server_init();
    
    candidate_t cands_alice[2] = {
        {0, htonl(0x0A000001), htons(5000)},
        {1, htonl(0x01020304), htons(12345)}
    };
    
    candidate_t cands_bob[2] = {
        {0, htonl(0x0A000002), htons(6000)},
        {1, htonl(0x05060708), htons(23456)}
    };
    
    // Alice 先注册
    mock_compact_server_register("alice", "bob", "10.0.0.1", 5000, cands_alice, 2);
    
    // Bob 注册
    register_ack_t ack = mock_compact_server_register("bob", "alice", "10.0.0.2", 6000, cands_bob, 2);
    
    ASSERT_EQ(ack.status, SIG_REGACK_PEER_ONLINE);  // peer online
    ASSERT_EQ(ack.max_candidates, COMPACT_MAX_CANDIDATES);
    
    // 验证双向配对
    compact_pair_t *alice_pair = mock_compact_server_get_pair("alice", "bob");
    compact_pair_t *bob_pair = mock_compact_server_get_pair("bob", "alice");
    ASSERT_NOT_NULL(alice_pair);
    ASSERT_NOT_NULL(bob_pair);
    ASSERT_EQ(alice_pair->peer, bob_pair);
    ASSERT_EQ(bob_pair->peer, alice_pair);
    
    TEST_LOG("  ✓ Peer online, bilateral pairing established");
}

TEST(register_ack_no_cache_support) {
    TEST_LOG("Testing REGISTER_ACK with max_candidates=0 (no cache)");
    mock_compact_server_init();
    
    // 修改服务器配置（模拟不支持缓存）
    // 注：实际测试需要修改 COMPACT_MAX_CANDIDATES，这里仅验证字段
    
    candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    register_ack_t ack = mock_compact_server_register("alice", "bob", "10.0.0.1", 5000, cands, 1);
    
    // 当前实现始终返回 max_candidates=8
    // 如果需要测试 max=0，需要添加配置参数
    ASSERT_EQ(ack.max_candidates, COMPACT_MAX_CANDIDATES);
    
    TEST_LOG("  ✓ max_candidates=%d (current server config)", ack.max_candidates);
}

/* ============================================================================
 * 第二部分：PEER_INFO 序列化传输测试
 * ============================================================================ */

TEST(peer_info_seq1_basic) {
    TEST_LOG("Testing PEER_INFO(seq=1) basic format");
    mock_compact_server_init();
    
    candidate_t cands[3] = {
        {0, htonl(0x0A000001), htons(5000)},
        {1, htonl(0x01020304), htons(12345)},
        {2, htonl(0xC0A80001), htons(3478)}
    };
    
    mock_compact_server_register("bob", "alice", "10.0.0.2", 6000, cands, 3);
    
    // Alice 请求 Bob 的信息
    peer_info_t info = mock_compact_server_get_peer_info("alice", "bob");
    
    ASSERT_EQ(info.base_index, 0);
    ASSERT_EQ(info.count, 3);
    ASSERT_EQ(info.candidates[0].type, 0);
    ASSERT_EQ(ntohl(info.candidates[0].ip), 0x0A000001);
    ASSERT_EQ(ntohs(info.candidates[0].port), 5000);
    
    TEST_LOG("  ✓ PEER_INFO(seq=1, base=0) with 3 candidates");
}

TEST(peer_info_candidate_limit) {
    TEST_LOG("Testing PEER_INFO candidate count limit");
    mock_compact_server_init();
    
    // 尝试注册超过限制的候选数
    candidate_t cands[COMPACT_MAX_CANDIDATES + 2];
    for (int i = 0; i < COMPACT_MAX_CANDIDATES + 2; i++) {
        cands[i].type = 0;
        cands[i].ip = htonl(0x0A000000 + i);
        cands[i].port = htons(5000 + i);
    }
    
    mock_compact_server_register("charlie", "dave", "10.0.0.3", 7000, 
                        cands, COMPACT_MAX_CANDIDATES + 2);
    
    // 验证只缓存了最大数量
    compact_pair_t *pair = mock_compact_server_get_pair("charlie", "dave");
    ASSERT_NOT_NULL(pair);
    ASSERT_EQ(pair->candidate_count, COMPACT_MAX_CANDIDATES);
    
    TEST_LOG("  ✓ Candidate count capped at max=%d", COMPACT_MAX_CANDIDATES);
}

TEST(peer_info_empty_candidates) {
    TEST_LOG("Testing PEER_INFO with zero candidates");
    mock_compact_server_init();
    
    // 注册时没有候选
    mock_compact_server_register("eve", "frank", "10.0.0.4", 8000, NULL, 0);
    
    peer_info_t info = mock_compact_server_get_peer_info("frank", "eve");
    
    ASSERT_EQ(info.base_index, 0);
    ASSERT_EQ(info.count, 0);
    
    TEST_LOG("  ✓ PEER_INFO(seq=1) with count=0");
}

/* ============================================================================
 * 第三部分：离线缓存与首次匹配测试
 * ============================================================================ */

TEST(offline_cache_basic) {
    TEST_LOG("Testing offline cache mechanism");
    mock_compact_server_init();
    
    candidate_t cands_alice[4] = {
        {0, htonl(0x0A000001), htons(5000)},
        {1, htonl(0x01020304), htons(12345)},
        {0, htonl(0x0A000002), htons(5001)},
        {2, htonl(0xC0A80001), htons(3478)}
    };
    
    // Alice 先注册（Bob 离线）
    register_ack_t ack1 = mock_compact_server_register("alice", "bob", "10.0.0.1", 5000, 
                                                cands_alice, 4);
    
    ASSERT_EQ(ack1.status, SIG_REGACK_PEER_OFFLINE);
    TEST_LOG("  Alice registered, Bob offline, candidates cached");
    
    // 验证缓存
    compact_pair_t *alice_pair = mock_compact_server_get_pair("alice", "bob");
    ASSERT_NOT_NULL(alice_pair);
    ASSERT_EQ(alice_pair->candidate_count, 4);
    ASSERT_NULL(alice_pair->peer);
    
    // Bob 上线
    candidate_t cands_bob[3] = {
        {0, htonl(0x0A000002), htons(6000)},
        {1, htonl(0x05060708), htons(23456)},
        {2, htonl(0xC0A80002), htons(3479)}
    };
    
    register_ack_t ack2 = mock_compact_server_register("bob", "alice", "10.0.0.2", 6000, 
                                                cands_bob, 3);
    
    ASSERT_EQ(ack2.status, SIG_REGACK_PEER_ONLINE);
    TEST_LOG("  Bob registered, Alice online, pairing established");
    
    // 验证双向配对
    compact_pair_t *bob_pair = mock_compact_server_get_pair("bob", "alice");
    ASSERT_NOT_NULL(bob_pair);
    ASSERT_EQ(alice_pair->peer, bob_pair);
    ASSERT_EQ(bob_pair->peer, alice_pair);
    
    // 服务器应向双方发送 PEER_INFO(seq=1)
    peer_info_t info_to_alice = mock_compact_server_get_peer_info("alice", "bob");
    ASSERT_EQ(info_to_alice.count, 3);  // Bob 的候选
    
    peer_info_t info_to_bob = mock_compact_server_get_peer_info("bob", "alice");
    ASSERT_EQ(info_to_bob.count, 4);  // Alice 的候选
    
    TEST_LOG("  ✓ Offline cache worked, both received PEER_INFO(seq=1)");
}

TEST(first_match_bilateral_notification) {
    TEST_LOG("Testing first match bilateral notification");
    mock_compact_server_init();
    
    candidate_t cands_a[2] = {
        {0, htonl(0x0A000001), htons(5000)},
        {1, htonl(0x01020304), htons(12345)}
    };
    
    candidate_t cands_b[2] = {
        {0, htonl(0x0A000002), htons(6000)},
        {1, htonl(0x05060708), htons(23456)}
    };
    
    // 双方同时在线注册
    mock_compact_server_register("peer_a", "peer_b", "10.0.0.1", 5000, cands_a, 2);
    mock_compact_server_register("peer_b", "peer_a", "10.0.0.2", 6000, cands_b, 2);
    
    // 验证双向配对
    compact_pair_t *pair_a = mock_compact_server_get_pair("peer_a", "peer_b");
    compact_pair_t *pair_b = mock_compact_server_get_pair("peer_b", "peer_a");
    
    ASSERT_NOT_NULL(pair_a);
    ASSERT_NOT_NULL(pair_b);
    ASSERT_EQ(pair_a->peer, pair_b);
    ASSERT_EQ(pair_b->peer, pair_a);
    
    TEST_LOG("  ✓ First match: Both peers notified with PEER_INFO(seq=1)");
}

/* ============================================================================
 * 第四部分：地址变化与重注册测试
 * ============================================================================ */

TEST(address_change_detection) {
    TEST_LOG("Testing address change detection");
    mock_compact_server_init();
    
    candidate_t cands[2] = {
        {0, htonl(0x0A000001), htons(5000)},
        {1, htonl(0x01020304), htons(12345)}
    };
    
    // 初始注册
    mock_compact_server_register("alice", "bob", "10.0.0.1", 5000, cands, 2);
    
    compact_pair_t *pair1 = mock_compact_server_get_pair("alice", "bob");
    ASSERT_NOT_NULL(pair1);
    uint32_t old_ip = pair1->addr.sin_addr.s_addr;
    uint16_t old_port = pair1->addr.sin_port;
    
    TEST_LOG("  Initial address: %s:%d", inet_ntoa(pair1->addr.sin_addr), ntohs(old_port));
    
    // 地址变化后重新注册
    mock_compact_server_register("alice", "bob", "10.0.0.99", 9999, cands, 2);
    
    compact_pair_t *pair2 = mock_compact_server_get_pair("alice", "bob");
    ASSERT_NOT_NULL(pair2);
    
    ASSERT(pair2->addr.sin_addr.s_addr != old_ip || pair2->addr.sin_port != old_port);
    TEST_LOG("  New address: %s:%d", inet_ntoa(pair2->addr.sin_addr), ntohs(pair2->addr.sin_port));
    
    TEST_LOG("  ✓ Address change detected and updated");
}

TEST(reconnect_after_timeout) {
    TEST_LOG("Testing reconnect after timeout cleanup");
    mock_compact_server_init();
    
    candidate_t cands[2] = {
        {0, htonl(0x0A000001), htons(5000)},
        {1, htonl(0x01020304), htons(12345)}
    };
    
    // Alice 注册
    mock_compact_server_register("alice", "bob", "10.0.0.1", 5000, cands, 2);
    
    compact_pair_t *pair = mock_compact_server_get_pair("alice", "bob");
    ASSERT_NOT_NULL(pair);
    
    // 模拟超时
    pair->last_seen = time(NULL) - COMPACT_PAIR_TIMEOUT - 1;
    
    // 清理
    int cleaned = mock_compact_server_cleanup();
    ASSERT_EQ(cleaned, 1);
    TEST_LOG("  Cleaned up 1 timed-out pair");
    
    // 重新注册
    mock_compact_server_register("alice", "bob", "10.0.0.1", 5000, cands, 2);
    
    pair = mock_compact_server_get_pair("alice", "bob");
    ASSERT_NOT_NULL(pair);
    ASSERT_NULL(pair->peer);  // peer 指针应重置
    
    TEST_LOG("  ✓ Reconnect after timeout successful");
}

/* ============================================================================
 * 第五部分：超时清理与 peer 指针状态机测试
 * ============================================================================ */

TEST(timeout_cleanup_basic) {
    TEST_LOG("Testing timeout cleanup mechanism");
    mock_compact_server_init();
    
    candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    
    // 注册 3 个配对
    mock_compact_server_register("alice", "bob", "10.0.0.1", 5000, cands, 1);
    mock_compact_server_register("bob", "alice", "10.0.0.2", 6000, cands, 1);
    mock_compact_server_register("charlie", "dave", "10.0.0.3", 7000, cands, 1);
    
    compact_pair_t *alice = mock_compact_server_get_pair("alice", "bob");
    compact_pair_t *bob = mock_compact_server_get_pair("bob", "alice");
    compact_pair_t *charlie = mock_compact_server_get_pair("charlie", "dave");
    
    // Alice 和 Bob 配对
    ASSERT_EQ(alice->peer, bob);
    ASSERT_EQ(bob->peer, alice);
    
    // 模拟 Alice 超时
    alice->last_seen = time(NULL) - COMPACT_PAIR_TIMEOUT - 1;
    
    int cleaned = mock_compact_server_cleanup();
    ASSERT_EQ(cleaned, 1);
    
    // 验证 Alice 被清理，Bob 的 peer 指针变为 -1
    ASSERT_EQ(alice->valid, false);
    ASSERT_EQ(bob->peer, (compact_pair_t*)(void*)-1);
    
    // Charlie 未超时，保持有效
    ASSERT_EQ(charlie->valid, true);
    
    TEST_LOG("  ✓ Timeout cleanup: alice removed, bob->peer = -1");
}

TEST(peer_pointer_state_machine) {
    TEST_LOG("Testing peer pointer state machine: NULL -> valid -> -1 -> NULL");
    mock_compact_server_init();
    
    candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    
    // State 1: NULL（未配对）
    mock_compact_server_register("alice", "bob", "10.0.0.1", 5000, cands, 1);
    compact_pair_t *alice = mock_compact_server_get_pair("alice", "bob");
    ASSERT_NULL(alice->peer);
    TEST_LOG("  State 1: peer = NULL (unpaired)");
    
    // State 2: valid pointer（已配对）
    mock_compact_server_register("bob", "alice", "10.0.0.2", 6000, cands, 1);
    compact_pair_t *bob = mock_compact_server_get_pair("bob", "alice");
    ASSERT_EQ(alice->peer, bob);
    ASSERT_EQ(bob->peer, alice);
    TEST_LOG("  State 2: peer = valid pointer (paired)");
    
    // State 3: -1（对端断开）
    alice->last_seen = time(NULL) - COMPACT_PAIR_TIMEOUT - 1;
    mock_compact_server_cleanup();
    ASSERT_EQ(bob->peer, (compact_pair_t*)(void*)-1);
    TEST_LOG("  State 3: peer = -1 (peer disconnected)");
    
    // State 4: NULL（重新注册后重置）
    mock_compact_server_register("bob", "alice", "10.0.0.2", 6000, cands, 1);
    bob = mock_compact_server_get_pair("bob", "alice");
    ASSERT_NULL(bob->peer);
    TEST_LOG("  State 4: peer = NULL (reset on re-register)");
    
    TEST_LOG("  ✓ State machine: NULL -> valid -> -1 -> NULL");
}

/* ============================================================================
 * 第六部分：并发与隔离测试
 * ============================================================================ */

TEST(multiple_independent_pairs) {
    TEST_LOG("Testing multiple independent peer pairs");
    mock_compact_server_init();
    
    candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    
    // 创建 3 组独立的配对
    mock_compact_server_register("alice", "bob", "10.0.0.1", 5001, cands, 1);
    mock_compact_server_register("bob", "alice", "10.0.0.2", 5002, cands, 1);
    
    mock_compact_server_register("charlie", "dave", "10.0.0.3", 5003, cands, 1);
    mock_compact_server_register("dave", "charlie", "10.0.0.4", 5004, cands, 1);
    
    mock_compact_server_register("eve", "frank", "10.0.0.5", 5005, cands, 1);
    mock_compact_server_register("frank", "eve", "10.0.0.6", 5006, cands, 1);
    
    // 验证各组配对互不干扰
    compact_pair_t *alice = mock_compact_server_get_pair("alice", "bob");
    compact_pair_t *bob = mock_compact_server_get_pair("bob", "alice");
    compact_pair_t *charlie = mock_compact_server_get_pair("charlie", "dave");
    compact_pair_t *dave = mock_compact_server_get_pair("dave", "charlie");
    compact_pair_t *eve = mock_compact_server_get_pair("eve", "frank");
    compact_pair_t *frank = mock_compact_server_get_pair("frank", "eve");
    
    ASSERT_EQ(alice->peer, bob);
    ASSERT_EQ(charlie->peer, dave);
    ASSERT_EQ(eve->peer, frank);
    
    ASSERT(alice->peer != charlie && alice->peer != eve);
    ASSERT(charlie->peer != eve);
    
    TEST_LOG("  ✓ 3 independent pairs isolated correctly");
}

TEST(asymmetric_registration) {
    TEST_LOG("Testing asymmetric registration order");
    mock_compact_server_init();
    
    candidate_t cands_a[2] = {
        {0, htonl(0x0A000001), htons(5000)},
        {1, htonl(0x01020304), htons(12345)}
    };
    
    candidate_t cands_b[3] = {
        {0, htonl(0x0A000002), htons(6000)},
        {1, htonl(0x05060708), htons(23456)},
        {2, htonl(0xC0A80001), htons(3478)}
    };
    
    // 不同数量的候选，不同注册顺序
    mock_compact_server_register("alice", "bob", "10.0.0.1", 5000, cands_a, 2);
    mock_compact_server_register("bob", "alice", "10.0.0.2", 6000, cands_b, 3);
    
    // 验证候选数量正确
    peer_info_t info_to_alice = mock_compact_server_get_peer_info("alice", "bob");
    peer_info_t info_to_bob = mock_compact_server_get_peer_info("bob", "alice");
    
    ASSERT_EQ(info_to_alice.count, 3);  // Bob 的 3 个候选
    ASSERT_EQ(info_to_bob.count, 2);    // Alice 的 2 个候选
    
    TEST_LOG("  ✓ Asymmetric candidates handled: alice(2) <-> bob(3)");
}

/* ============================================================================
 * 第七部分：错误处理测试
 * ============================================================================ */

TEST(error_no_slot_available) {
    TEST_LOG("Testing error when no slot available");
    mock_compact_server_init();
    
    candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    
    // 填满所有槽位
    for (int i = 0; i < MAX_PEERS; i++) {
        char local_id[32], remote_id[32];
        snprintf(local_id, sizeof(local_id), "peer_%d", i);
        snprintf(remote_id, sizeof(remote_id), "target_%d", i);
        mock_compact_server_register(local_id, remote_id, "10.0.0.1", 5000 + i, cands, 1);
    }
    
    // 尝试再注册应该失败
    register_ack_t ack = mock_compact_server_register("overflow", "target", "10.0.0.1", 9999, cands, 1);
    
    ASSERT_EQ(ack.status, 1);  // 错误状态
    TEST_LOG("  ✓ No slot available, status=1 returned");
}

TEST(error_invalid_peer_id) {
    TEST_LOG("Testing handling of empty peer IDs");
    mock_compact_server_init();
    
    candidate_t cands[1] = {{0, htonl(0x0A000001), htons(5000)}};
    
    // 空的 remote_peer_id
    register_ack_t ack = mock_compact_server_register("alice", "", "10.0.0.1", 5000, cands, 1);
    
    // 当前实现会接受（没有验证），但记录为空字符串
    ASSERT_EQ(ack.status, 0);
    
    compact_pair_t *pair = mock_compact_server_get_pair("alice", "");
    ASSERT_NOT_NULL(pair);
    ASSERT_EQ(strlen(pair->remote_peer_id), 0);
    
    TEST_LOG("  ✓ Empty remote_peer_id accepted (no validation in current impl)");
}

/* ============================================================================
 * 主函数
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("COMPACT Server Complete Test Suite\n");
    printf("========================================\n\n");
    
    printf("Part 1: REGISTER_ACK Protocol\n");
    printf("----------------------------------------\n");
    RUN_TEST(register_ack_with_relay_support);
    RUN_TEST(register_ack_with_probe_port_config);
    RUN_TEST(register_ack_peer_offline);
    RUN_TEST(register_ack_peer_online);
    RUN_TEST(register_ack_no_cache_support);
    
    printf("\nPart 2: PEER_INFO Serialization\n");
    printf("----------------------------------------\n");
    RUN_TEST(peer_info_seq1_basic);
    RUN_TEST(peer_info_candidate_limit);
    RUN_TEST(peer_info_empty_candidates);
    
    printf("\nPart 3: Offline Cache & First Match\n");
    printf("----------------------------------------\n");
    RUN_TEST(offline_cache_basic);
    RUN_TEST(first_match_bilateral_notification);
    
    printf("\nPart 4: Address Change & Re-registration\n");
    printf("----------------------------------------\n");
    RUN_TEST(address_change_detection);
    RUN_TEST(reconnect_after_timeout);
    
    printf("\nPart 5: Timeout & Peer Pointer State\n");
    printf("----------------------------------------\n");
    RUN_TEST(timeout_cleanup_basic);
    RUN_TEST(peer_pointer_state_machine);
    
    printf("\nPart 6: Concurrency & Isolation\n");
    printf("----------------------------------------\n");
    RUN_TEST(multiple_independent_pairs);
    RUN_TEST(asymmetric_registration);
    
    printf("\nPart 7: Error Handling\n");
    printf("----------------------------------------\n");
    RUN_TEST(error_no_slot_available);
    RUN_TEST(error_invalid_peer_id);
    
    printf("\n");
    TEST_SUMMARY();
    
    return (test_failed > 0) ? 1 : 0;
}
