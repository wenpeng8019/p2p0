/*
 * test_candidate_types.c - 候选类型系统测试
 * 
 * 测试新重构的候选类型系统：
 * 1. p2p_candidate_entry_t (可序列化基础类型)
 * 2. p2p_remote_candidate_entry_t (运行时扩展类型)
 * 3. 类型安全的 first-member embedding
 * 4. pack/unpack 候选序列化
 */

#include "test_framework.h"
#include "../src/p2p_internal.h"
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <arpa/inet.h>
#endif

static bool g_verbose = true;
#define TEST_LOG(fmt, ...) \
    do { if (g_verbose) printf("[TEST] " fmt "\n", ##__VA_ARGS__); } while(0)

/* ============================================================================
 * 基础类型大小和布局测试
 * ============================================================================ */

TEST(candidate_entry_size) {
    TEST_LOG("Testing p2p_candidate_entry_t size and layout");
    
    // 验证基础类型不包含运行时字段
    p2p_candidate_entry_t base;
    memset(&base, 0, sizeof(base));
    
    // 基础类型应该只包含：type, addr, base_addr, priority
    // 不应该包含 last_punch_send_ms
    ASSERT_GE(sizeof(p2p_candidate_entry_t), 4 + 16 + 16 + 4);  // 至少40字节
    ASSERT_LT(sizeof(p2p_candidate_entry_t), 100);  // 不应该过大
    
    TEST_LOG("  ✓ p2p_candidate_entry_t size = %zu bytes", sizeof(p2p_candidate_entry_t));
}

TEST(remote_candidate_entry_size) {
    TEST_LOG("Testing p2p_remote_candidate_entry_t size and layout");
    
    // 验证扩展类型包含基础类型 + 运行时字段
    p2p_remote_candidate_entry_t remote;
    memset(&remote, 0, sizeof(remote));
    
    // 扩展类型 = 基础类型 + last_punch_send_ms (8字节)
    size_t expected_min_size = sizeof(p2p_candidate_entry_t) + sizeof(uint64_t);
    ASSERT_GE(sizeof(p2p_remote_candidate_entry_t), expected_min_size);
    
    TEST_LOG("  ✓ p2p_remote_candidate_entry_t size = %zu bytes", sizeof(p2p_remote_candidate_entry_t));
    TEST_LOG("  ✓ Contains base (%zu) + runtime (%zu)", 
             sizeof(p2p_candidate_entry_t), sizeof(uint64_t));
}

/* ============================================================================
 * First-Member Embedding 类型安全测试
 * ============================================================================ */

TEST(first_member_embedding) {
    TEST_LOG("Testing first-member embedding (type-safe casting)");
    
    p2p_remote_candidate_entry_t remote;
    memset(&remote, 0, sizeof(remote));
    
    // 设置基础字段
    remote.cand.type = P2P_ICE_CAND_HOST;
    remote.cand.priority = 12345;
    remote.cand.addr.sin_family = AF_INET;
    remote.cand.addr.sin_addr.s_addr = htonl(0xC0A80001);  // 192.168.0.1
    remote.cand.addr.sin_port = htons(8080);
    
    // 设置运行时字段
    remote.last_punch_send_ms = 9876543210ULL;
    
    // 验证通过 .cand 访问基础字段
    ASSERT_EQ(remote.cand.type, P2P_ICE_CAND_HOST);
    ASSERT_EQ(remote.cand.priority, 12345);
    ASSERT_EQ(ntohl(remote.cand.addr.sin_addr.s_addr), 0xC0A80001);
    ASSERT_EQ(ntohs(remote.cand.addr.sin_port), 8080);
    
    // 验证运行时字段独立存在
    ASSERT_EQ(remote.last_punch_send_ms, 9876543210ULL);
    
    // 验证指针转换安全性（first member 可以安全转换）
    p2p_candidate_entry_t *base_ptr = &remote.cand;
    ASSERT_EQ(base_ptr->type, P2P_ICE_CAND_HOST);
    ASSERT_EQ(base_ptr->priority, 12345);
    
    TEST_LOG("  ✓ First-member embedding works correctly");
    TEST_LOG("  ✓ Base fields accessible via .cand prefix");
    TEST_LOG("  ✓ Runtime field last_punch_send_ms = %llu", remote.last_punch_send_ms);
}

/* ============================================================================
 * 候选类型枚举测试
 * ============================================================================ */

TEST(ice_candidate_types) {
    TEST_LOG("Testing ICE candidate type enums");
    
    // 验证 ICE 候选类型常量
    ASSERT_EQ(P2P_ICE_CAND_HOST, 0);
    ASSERT_EQ(P2P_ICE_CAND_SRFLX, 1);
    ASSERT_EQ(P2P_ICE_CAND_RELAY, 2);
    ASSERT_EQ(P2P_ICE_CAND_PRFLX, 3);
    
    TEST_LOG("  ✓ ICE candidate types: HOST=0, SRFLX=1, RELAY=2, PRFLX=3");
}

TEST(compact_candidate_types) {
    TEST_LOG("Testing COMPACT candidate type enums");
    
    // 验证 COMPACT 候选类型常量（数值对齐但语义独立）
    ASSERT_EQ(P2P_COMPACT_CAND_HOST, 0);
    ASSERT_EQ(P2P_COMPACT_CAND_SRFLX, 1);
    ASSERT_EQ(P2P_COMPACT_CAND_RELAY, 2);
    ASSERT_EQ(P2P_COMPACT_CAND_PRFLX, 3);
    
    TEST_LOG("  ✓ COMPACT candidate types: HOST=0, SRFLX=1, RELAY=2, PRFLX=3");
    TEST_LOG("  ✓ Values align with ICE types (intentional)");
}

/* ============================================================================
 * 候选序列化/反序列化测试
 * ============================================================================ */

TEST(pack_unpack_candidate) {
    TEST_LOG("Testing pack_candidate and unpack_candidate");
    
    // 创建候选
    p2p_candidate_entry_t orig;
    memset(&orig, 0, sizeof(orig));
    orig.type = P2P_ICE_CAND_SRFLX;
    orig.priority = 0x7FFFFFFF;
    orig.addr.sin_family = AF_INET;
    orig.addr.sin_addr.s_addr = htonl(0x08080808);  // 8.8.8.8
    orig.addr.sin_port = htons(9999);
    orig.base_addr.sin_family = AF_INET;
    orig.base_addr.sin_addr.s_addr = htonl(0xC0A80002);  // 192.168.0.2
    orig.base_addr.sin_port = htons(5000);
    
    // 序列化
    uint8_t wire[32];
    pack_candidate(&orig, wire);
    
    // 反序列化
    p2p_candidate_entry_t unpacked;
    memset(&unpacked, 0, sizeof(unpacked));
    unpack_candidate(&unpacked, wire);
    
    // 验证
    ASSERT_EQ(unpacked.type, P2P_ICE_CAND_SRFLX);
    ASSERT_EQ(unpacked.priority, 0x7FFFFFFF);
    ASSERT_EQ(unpacked.addr.sin_family, AF_INET);
    ASSERT_EQ(ntohl(unpacked.addr.sin_addr.s_addr), 0x08080808);
    ASSERT_EQ(ntohs(unpacked.addr.sin_port), 9999);
    ASSERT_EQ(unpacked.base_addr.sin_family, AF_INET);
    ASSERT_EQ(ntohl(unpacked.base_addr.sin_addr.s_addr), 0xC0A80002);
    ASSERT_EQ(ntohs(unpacked.base_addr.sin_port), 5000);
    
    TEST_LOG("  ✓ Pack/unpack preserves all fields");
    TEST_LOG("  ✓ Wire format: 32 bytes (4+12+12+4)");
}

TEST(unpack_initializes_runtime_fields) {
    TEST_LOG("Testing unpack_candidate doesn't touch runtime fields");
    
    // 创建远端候选并设置运行时字段
    p2p_remote_candidate_entry_t remote;
    memset(&remote, 0, sizeof(remote));
    remote.last_punch_send_ms = 12345ULL;  // 预设运行时字段
    
    // 准备线协议数据
    uint8_t wire[32];
    memset(wire, 0, sizeof(wire));
    // type = HOST (0)
    wire[0] = 0; wire[1] = 0; wire[2] = 0; wire[3] = 0;
    
    // 反序列化到 remote.cand（仅操作基础字段）
    unpack_candidate(&remote.cand, wire);
    
    // 验证：unpack 不应修改运行时字段
    ASSERT_EQ(remote.last_punch_send_ms, 12345ULL);
    ASSERT_EQ(remote.cand.type, 0);
    
    TEST_LOG("  ✓ unpack_candidate preserves runtime fields");
    TEST_LOG("  ✓ last_punch_send_ms unchanged = %llu", remote.last_punch_send_ms);
}

/* ============================================================================
 * 数组访问模式测试
 * ============================================================================ */

TEST(remote_candidate_array_access) {
    TEST_LOG("Testing remote candidate array access patterns");
    
    // 模拟 session 中的数组
    p2p_remote_candidate_entry_t candidates[3];
    memset(candidates, 0, sizeof(candidates));
    
    // 填充数据
    for (int i = 0; i < 3; i++) {
        candidates[i].cand.type = P2P_ICE_CAND_HOST + i;
        candidates[i].cand.priority = 1000 + i;
        candidates[i].cand.addr.sin_port = htons(8000 + i);
        candidates[i].last_punch_send_ms = i * 1000ULL;
    }
    
    // 验证通过 .cand.* 路径访问
    ASSERT_EQ(candidates[0].cand.type, P2P_ICE_CAND_HOST);
    ASSERT_EQ(candidates[1].cand.type, P2P_ICE_CAND_SRFLX);
    ASSERT_EQ(candidates[2].cand.type, P2P_ICE_CAND_RELAY);
    
    ASSERT_EQ(candidates[0].last_punch_send_ms, 0ULL);
    ASSERT_EQ(candidates[1].last_punch_send_ms, 1000ULL);
    ASSERT_EQ(candidates[2].last_punch_send_ms, 2000ULL);
    
    TEST_LOG("  ✓ Array access pattern: candidates[i].cand.* works");
    TEST_LOG("  ✓ Runtime fields: candidates[i].last_punch_send_ms works");
}

/* ============================================================================
 * 主函数
 * ============================================================================ */

int main(int argc, char **argv) {
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_verbose = true;
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            g_verbose = false;
        }
    }

    printf("\n=== Candidate Type System Tests ===\n\n");

    // 基础类型测试
    RUN_TEST(candidate_entry_size);
    RUN_TEST(remote_candidate_entry_size);
    
    // 类型安全测试
    RUN_TEST(first_member_embedding);
    
    // 枚举测试
    RUN_TEST(ice_candidate_types);
    RUN_TEST(compact_candidate_types);
    
    // 序列化测试
    RUN_TEST(pack_unpack_candidate);
    RUN_TEST(unpack_initializes_runtime_fields);
    
    // 数组访问测试
    RUN_TEST(remote_candidate_array_access);

    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", test_passed);
    printf("Failed: %d\n", test_failed);
    printf("Total:  %d\n", test_passed + test_failed);

    return (test_failed == 0) ? 0 : 1;
}
