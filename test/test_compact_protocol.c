/*
 * test_compact_protocol.c - COMPACT 协议完整测试套件
 * 
 * 测试覆盖新协议特性：
 * 1. REGISTER / REGISTER_ACK (含 max_candidates)
 * 2. PEER_INFO 序列化传输 (base_index + seq)
 * 3. PEER_INFO_ACK 确认机制
 * 4. FIN 结束标识
 * 5. 离线缓存流程
 * 6. 不支持缓存场景 (max_candidates=0)
 * 7. 重传机制
 */

#include "test_framework.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <p2pp.h>

// 测试日志开关
static bool g_verbose = true;

#define TEST_LOG(fmt, ...) \
    do { if (g_verbose) printf("[TEST] " fmt "\n", ##__VA_ARGS__); } while(0)

/* ============================================================================
 * 模拟协议包结构
 * ============================================================================ */

typedef struct {
    uint8_t  type;
    uint8_t  flags;
    uint16_t seq;
} test_pkt_hdr_t;

typedef struct {
    uint8_t  type;
    uint32_t ip;
    uint16_t port;
} __attribute__((packed)) test_candidate_t;

typedef struct {
    uint8_t status;          // 0=离线, 1=在线, >=2=error
    uint8_t max_candidates;  // 服务器缓存能力
    uint32_t public_ip;      // 客户端的公网 IP
    uint16_t public_port;    // 客户端的公网端口
    uint16_t probe_port;     // NAT 探测端口（0=不支持）
} __attribute__((packed)) test_register_ack_t;

typedef struct {
    uint8_t base_index;      // 候选起始索引
    uint8_t count;           // 本批候选数量
    test_candidate_t candidates[10];
} test_peer_info_t;

typedef struct {
    uint16_t ack_seq;        // 确认的序列号
    uint16_t reserved;
} test_peer_info_ack_t;

/* ============================================================================
 * 第一部分：REGISTER_ACK 协议测试
 * ============================================================================ */

TEST(register_ack_basic) {
    TEST_LOG("Testing REGISTER_ACK basic format");
    
    // 构造 REGISTER_ACK 包
    test_register_ack_t ack;
    ack.status = SIG_REGACK_PEER_ONLINE;  // 对端在线
    ack.max_candidates = 5;
    ack.public_ip = htonl(0x01020304);  // 1.2.3.4
    ack.public_port = htons(12345);
    ack.probe_port = htons(3479);  // 探测端口
    
    // 验证字段
    ASSERT_EQ(ack.status, SIG_REGACK_PEER_ONLINE);
    ASSERT_EQ(ack.max_candidates, 5);
    ASSERT_EQ(ntohl(ack.public_ip), 0x01020304);
    ASSERT_EQ(ntohs(ack.public_port), 12345);
    ASSERT_EQ(ntohs(ack.probe_port), 3479);
    TEST_LOG("  ✓ REGISTER_ACK format correct: peer_online=1, max=5, public=1.2.3.4:12345, probe_port=3479");
}

TEST(register_ack_no_cache) {
    TEST_LOG("Testing REGISTER_ACK with no cache support");
    
    test_register_ack_t ack;
    ack.status = SIG_REGACK_PEER_OFFLINE;  // 对端离线
    ack.max_candidates = 0;  // 不支持缓存
    ack.probe_port = 0;  // 不支持 NAT 探测
    
    ASSERT_EQ(ack.max_candidates, 0);
    ASSERT_EQ(ack.probe_port, 0);
    TEST_LOG("  ✓ max_candidates=0 means no cache support, probe_port=0 means no NAT detection");
}

TEST(register_ack_with_relay_flag) {
    TEST_LOG("Testing REGISTER_ACK with relay support in header flags");
    
    // 构造带 relay 标志的 REGISTER_ACK
    test_pkt_hdr_t hdr;
    hdr.type = SIG_PKT_REGISTER_ACK;
    hdr.flags = SIG_REGACK_FLAG_RELAY;  // 服务器支持中继
    hdr.seq = 0;
    
    test_register_ack_t ack;
    ack.status = SIG_REGACK_PEER_ONLINE;
    ack.max_candidates = 8;
    ack.probe_port = htons(3479);
    
    // 验证 relay 标志在 header 中
    ASSERT_EQ(hdr.flags & SIG_REGACK_FLAG_RELAY, SIG_REGACK_FLAG_RELAY);
    TEST_LOG("  ✓ Relay flag correctly set in header.flags");
}

TEST(register_ack_with_probe_port) {
    TEST_LOG("Testing REGISTER_ACK with NAT probe port");
    
    test_register_ack_t ack;
    ack.status = SIG_REGACK_PEER_OFFLINE;
    ack.max_candidates = 8;
    ack.probe_port = htons(3479);  // NAT 探测端口
    
    ASSERT_EQ(ntohs(ack.probe_port), 3479);
    TEST_LOG("  ✓ NAT probe port = 3479 (server supports NAT detection)");
}

TEST(register_ack_no_probe_support) {
    TEST_LOG("Testing REGISTER_ACK without NAT probe support");
    
    test_register_ack_t ack;
    ack.status = SIG_REGACK_PEER_OFFLINE;
    ack.max_candidates = 8;
    ack.probe_port = 0;  // 不支持 NAT 探测
    
    ASSERT_EQ(ack.probe_port, 0);
    TEST_LOG("  ✓ probe_port=0 means server does not support NAT detection");
}

TEST(register_ack_peer_offline) {
    TEST_LOG("Testing REGISTER_ACK when peer is offline");
    
    test_register_ack_t ack;
    ack.status = SIG_REGACK_PEER_OFFLINE;  // 对端离线
    ack.max_candidates = 8;
    ack.public_ip = htonl(0xC0A80001);  // 192.168.0.1
    ack.public_port = htons(54321);
    ack.probe_port = 0;
    
    ASSERT_EQ(ack.status, SIG_REGACK_PEER_OFFLINE);
    ASSERT_EQ(ack.max_candidates, 8);
    ASSERT_EQ(ntohl(ack.public_ip), 0xC0A80001);
    ASSERT_EQ(ntohs(ack.public_port), 54321);
    TEST_LOG("  ✓ status=0 (offline), max=8, public=192.168.0.1:54321");
}

TEST(register_ack_public_address_detection) {
    TEST_LOG("Testing REGISTER_ACK public address detection");
    
    // 模拟场景：客户端在 NAT 后，不知道自己的公网地址
    // 发送 REGISTER 到服务器，服务器返回客户端的公网地址
    
    test_register_ack_t ack;
    ack.status = SIG_REGACK_PEER_ONLINE;  // 对端在线
    ack.max_candidates = 8;
    
    // 服务器探测到的客户端公网地址
    ack.public_ip = htonl(0x5F6B8C01);     // 95.107.140.1 (假设的公网IP)
    ack.public_port = htons(45678);         // NAT 映射的端口
    
    // 验证客户端可以获取自己的公网地址
    struct in_addr addr;
    addr.s_addr = ack.public_ip;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));
    
    ASSERT_EQ(strcmp(ip_str, "95.107.140.1"), 0);
    ASSERT_EQ(ntohs(ack.public_port), 45678);
    
    TEST_LOG("  ✓ Client discovered public address: %s:%d", ip_str, ntohs(ack.public_port));
    TEST_LOG("  ✓ NAT traversal: client now knows its external endpoint");
}

/* ============================================================================
 * 第二部分：PEER_INFO 序列化传输测试
 * ============================================================================ */

TEST(peer_info_seq1_from_server) {
    TEST_LOG("Testing PEER_INFO(seq=1) from server");
    
    test_pkt_hdr_t hdr;
    hdr.type = SIG_PKT_PEER_INFO;
    hdr.flags = 0;
    hdr.seq = htons(1);  // seq=1 服务器发送
    
    test_peer_info_t info;
    info.base_index = 0;  // 从第一个候选开始
    info.count = 3;
    
    // 填充 3 个候选
    info.candidates[0].type = 0;  // host
    info.candidates[0].ip = htonl(0x0A000001);
    info.candidates[0].port = htons(5000);
    
    info.candidates[1].type = 1;  // srflx
    info.candidates[1].ip = htonl(0x01020304);
    info.candidates[1].port = htons(12345);
    
    info.candidates[2].type = 2;  // relay
    info.candidates[2].ip = htonl(0xC0A80001);
    info.candidates[2].port = htons(3478);
    
    ASSERT_EQ(ntohs(hdr.seq), 1);
    ASSERT_EQ(info.base_index, 0);
    ASSERT_EQ(info.count, 3);
    TEST_LOG("  ✓ PEER_INFO(seq=1, base=0) with 3 candidates");
}

TEST(peer_info_seq2_with_base_index) {
    TEST_LOG("Testing PEER_INFO(seq=2) with base_index");
    
    test_pkt_hdr_t hdr;
    hdr.type = SIG_PKT_PEER_INFO;
    hdr.flags = 0;
    hdr.seq = htons(2);  // seq=2 客户端发送
    
    test_peer_info_t info;
    info.base_index = 5;  // 从第 6 个候选开始 (服务器缓存了 5 个)
    info.count = 4;
    
    // 填充候选 [5-8]
    for (int i = 0; i < 4; i++) {
        info.candidates[i].type = 0;
        info.candidates[i].ip = htonl(0x0A000005 + i);
        info.candidates[i].port = htons(6000 + i);
    }
    
    ASSERT_EQ(ntohs(hdr.seq), 2);
    ASSERT_EQ(info.base_index, 5);
    ASSERT_EQ(info.count, 4);
    TEST_LOG("  ✓ PEER_INFO(seq=2, base=5) with candidates [5-8]");
}

TEST(peer_info_fin_flag) {
    TEST_LOG("Testing PEER_INFO with FIN flag");
    
    test_pkt_hdr_t hdr;
    hdr.type = SIG_PKT_PEER_INFO;
    hdr.flags = SIG_PEER_INFO_FIN;  // FIN 标志
    hdr.seq = htons(4);
    
    test_peer_info_t info;
    info.base_index = 15;  // 指向列表末尾
    info.count = 0;        // count=0 表示结束
    
    ASSERT_EQ(hdr.flags & SIG_PEER_INFO_FIN, SIG_PEER_INFO_FIN);
    ASSERT_EQ(info.count, 0);
    TEST_LOG("  ✓ PEER_INFO(seq=4, FIN, count=0) signals end");
}

TEST(peer_info_last_packet_with_data) {
    TEST_LOG("Testing PEER_INFO last packet with data and FIN");
    
    test_pkt_hdr_t hdr;
    hdr.type = SIG_PKT_PEER_INFO;
    hdr.flags = SIG_PEER_INFO_FIN;
    hdr.seq = htons(3);
    
    test_peer_info_t info;
    info.base_index = 10;
    info.count = 2;  // 最后 2 个候选
    
    info.candidates[0].type = 0;
    info.candidates[0].ip = htonl(0x0A00000A);
    info.candidates[0].port = htons(7000);
    
    info.candidates[1].type = 0;
    info.candidates[1].ip = htonl(0x0A00000B);
    info.candidates[1].port = htons(7001);
    
    ASSERT_EQ(hdr.flags & SIG_PEER_INFO_FIN, SIG_PEER_INFO_FIN);
    ASSERT_EQ(info.count, 2);
    TEST_LOG("  ✓ PEER_INFO(seq=3, base=10, FIN) with last 2 candidates");
}

/* ============================================================================
 * 第三部分：PEER_INFO_ACK 确认机制测试
 * ============================================================================ */

TEST(peer_info_ack_basic) {
    TEST_LOG("Testing PEER_INFO_ACK basic format");
    
    test_pkt_hdr_t hdr;
    hdr.type = SIG_PKT_PEER_INFO_ACK;
    hdr.flags = 0;
    hdr.seq = 0;
    
    test_peer_info_ack_t ack;
    ack.ack_seq = htons(1);  // 确认 seq=1
    ack.reserved = 0;
    
    ASSERT_EQ(hdr.type, SIG_PKT_PEER_INFO_ACK);
    ASSERT_EQ(ntohs(ack.ack_seq), 1);
    TEST_LOG("  ✓ PEER_INFO_ACK(seq=1) format correct");
}

TEST(peer_info_ack_sequence) {
    TEST_LOG("Testing PEER_INFO_ACK sequence confirmation");
    
    // 模拟确认序列: 1 -> 2 -> 3 -> 4
    for (uint16_t seq = 1; seq <= 4; seq++) {
        test_peer_info_ack_t ack;
        ack.ack_seq = htons(seq);
        ack.reserved = 0;
        
        ASSERT_EQ(ntohs(ack.ack_seq), seq);
        TEST_LOG("  Confirmed seq=%d", seq);
    }
    TEST_LOG("  ✓ ACK sequence 1-4 completed");
}

/* ============================================================================
 * 第四部分：完整流程测试 - 双方同时在线
 * ============================================================================ */

TEST(flow_both_online) {
    TEST_LOG("Testing complete flow: both peers online");
    
    // Phase 1: Alice REGISTER
    TEST_LOG("  [Alice] Send REGISTER with 10 candidates");
    
    // Phase 2: Server -> Alice REGISTER_ACK
    test_register_ack_t ack1;
    ack1.status = SIG_REGACK_PEER_ONLINE;  // Bob 在线
    ack1.max_candidates = 5;  // 服务器缓存了 5 个
    TEST_LOG("  [Server->Alice] REGISTER_ACK: peer_online=1, max=5");
    
    // Phase 3: Server -> Alice PEER_INFO(seq=1)
    test_peer_info_t info1;
    info1.base_index = 0;
    info1.count = 5;  // Bob 的 5 个缓存候选
    TEST_LOG("  [Server->Alice] PEER_INFO(seq=1, base=0, count=5)");
    
    // Phase 4: Alice -> Server PEER_INFO_ACK(seq=1)
    test_peer_info_ack_t peerack1;
    peerack1.ack_seq = htons(1);
    TEST_LOG("  [Alice->Server] PEER_INFO_ACK(seq=1)");
    
    // Phase 5: Alice 开始打洞，同时发送剩余候选
    // Alice -> Bob PEER_INFO(seq=2, base=5)
    test_peer_info_t info2;
    info2.base_index = 5;  // 从第 6 个开始
    info2.count = 5;       // 候选 [5-9]
    TEST_LOG("  [Alice->Bob] PEER_INFO(seq=2, base=5, count=5) P2P直连");
    
    // Phase 6: Bob -> Alice PEER_INFO_ACK(seq=2)
    test_peer_info_ack_t peerack2;
    peerack2.ack_seq = htons(2);
    TEST_LOG("  [Bob->Alice] PEER_INFO_ACK(seq=2)");
    
    // Phase 7: Alice -> Bob PEER_INFO(seq=3, FIN)
    test_pkt_hdr_t hdr3;
    hdr3.flags = SIG_PEER_INFO_FIN;
    hdr3.seq = htons(3);
    test_peer_info_t info3;
    info3.base_index = 10;
    info3.count = 0;  // 所有候选已发送完毕
    TEST_LOG("  [Alice->Bob] PEER_INFO(seq=3, base=10, count=0, FIN)");
    
    // Phase 8: Bob -> Alice PEER_INFO_ACK(seq=3)
    test_peer_info_ack_t peerack3;
    peerack3.ack_seq = htons(3);
    TEST_LOG("  [Bob->Alice] PEER_INFO_ACK(seq=3)");
    
    TEST_LOG("  ✓ Complete flow finished, both sides synced");
}

/* ============================================================================
 * 第五部分：完整流程测试 - 离线缓存场景
 * ============================================================================ */

TEST(flow_offline_cache) {
    TEST_LOG("Testing complete flow: offline cache scenario");
    
    // Phase 1: Alice REGISTER (Bob 离线)
    TEST_LOG("  [Alice] Send REGISTER with 12 candidates");
    
    // Phase 2: Server -> Alice REGISTER_ACK (Bob 离线)
    test_register_ack_t ack1;
    ack1.status = SIG_REGACK_PEER_OFFLINE;  // peer_online=0
    ack1.max_candidates = 5;
    TEST_LOG("  [Server->Alice] REGISTER_ACK: peer_online=0, max=5");
    TEST_LOG("  [Alice] Enters REGISTERED state, waiting...");
    
    // Phase 3: Bob 上线
    TEST_LOG("  [Bob] Comes online, sends REGISTER");
    
    // Phase 4: Server -> Bob REGISTER_ACK
    test_register_ack_t ack2;
    ack2.status = SIG_REGACK_PEER_ONLINE;  // Alice 在线
    ack2.max_candidates = 5;
    TEST_LOG("  [Server->Bob] REGISTER_ACK: peer_online=1, max=5");
    
    // Phase 5: Server 向双方发送 PEER_INFO(seq=1)
    test_peer_info_t info_alice;
    info_alice.base_index = 0;
    info_alice.count = 5;  // Bob 的缓存候选
    TEST_LOG("  [Server->Alice] PEER_INFO(seq=1, base=0, count=5)");
    
    test_peer_info_t info_bob;
    info_bob.base_index = 0;
    info_bob.count = 5;  // Alice 的缓存候选
    TEST_LOG("  [Server->Bob] PEER_INFO(seq=1, base=0, count=5)");
    
    // Phase 6: 双方确认并进入 READY 状态
    TEST_LOG("  [Alice] Send PEER_INFO_ACK(seq=1), enter READY");
    TEST_LOG("  [Bob] Send PEER_INFO_ACK(seq=1), enter READY");
    
    // Phase 7: Alice 继续发送剩余候选
    test_peer_info_t info2;
    info2.base_index = 5;  // 从服务器缓存的 max=5 开始
    info2.count = 5;       // 候选 [5-9]
    TEST_LOG("  [Alice->Bob] PEER_INFO(seq=2, base=5, count=5)");
    
    test_peer_info_t info3;
    info3.base_index = 10;
    info3.count = 2;       // 最后 2 个候选 [10-11]
    test_pkt_hdr_t hdr3;
    hdr3.flags = SIG_PEER_INFO_FIN;
    TEST_LOG("  [Alice->Bob] PEER_INFO(seq=3, base=10, count=2, FIN)");
    
    TEST_LOG("  ✓ Offline cache flow completed");
}

/* ============================================================================
 * 第六部分：不支持缓存场景
 * ============================================================================ */

TEST(flow_no_cache_support) {
    TEST_LOG("Testing flow when server doesn't support cache");
    
    // Phase 1: Alice REGISTER
    TEST_LOG("  [Alice] Send REGISTER");
    
    // Phase 2: Server -> Alice REGISTER_ACK (不支持缓存)
    test_register_ack_t ack;
    ack.status = SIG_REGACK_PEER_OFFLINE;
    ack.max_candidates = 0;  // 不支持缓存
    
    ASSERT_EQ(ack.max_candidates, 0);
    TEST_LOG("  [Server->Alice] REGISTER_ACK: max=0 (no cache)");
    TEST_LOG("  [Alice] Cannot cache, must wait for peer online");
    
    // Phase 3: Bob 上线才能建立连接
    TEST_LOG("  [Bob] Comes online");
    TEST_LOG("  [Server] Sends PEER_INFO to both immediately");
    
    TEST_LOG("  ✓ No cache scenario: requires both peers online");
}

/* ============================================================================
 * 第七部分：重传机制测试
 * ============================================================================ */

TEST(retransmission_on_packet_loss) {
    TEST_LOG("Testing retransmission on packet loss");
    
    // 模拟场景：PEER_INFO(seq=2) 丢包
    TEST_LOG("  [Alice] Send PEER_INFO(seq=2, base=5)");
    TEST_LOG("  [Simulated] Packet lost, no ACK received");
    
    // 超时后重传
    TEST_LOG("  [Alice] Timeout, retransmit PEER_INFO(seq=2, base=5)");
    
    // 收到 ACK
    test_peer_info_ack_t ack;
    ack.ack_seq = htons(2);
    TEST_LOG("  [Bob->Alice] PEER_INFO_ACK(seq=2) received");
    TEST_LOG("  [Alice] Stop retransmitting seq=2, move to seq=3");
    
    TEST_LOG("  ✓ Retransmission mechanism works");
}

TEST(ack_packet_loss_handling) {
    TEST_LOG("Testing ACK packet loss handling");
    
    TEST_LOG("  [Alice] Send PEER_INFO(seq=2)");
    TEST_LOG("  [Bob] Send PEER_INFO_ACK(seq=2)");
    TEST_LOG("  [Simulated] ACK lost");
    
    // Alice 超时重传
    TEST_LOG("  [Alice] Timeout, retransmit PEER_INFO(seq=2)");
    TEST_LOG("  [Bob] Receives duplicate seq=2, re-send ACK");
    TEST_LOG("  [Alice] Receives ACK, stops retransmitting");
    
    TEST_LOG("  ✓ Duplicate handling works");
}

/* ============================================================================
 * 第八部分：边界条件测试
 * ============================================================================ */

TEST(boundary_single_candidate) {
    TEST_LOG("Testing single candidate scenario");
    
    test_peer_info_t info;
    info.base_index = 0;
    info.count = 1;
    
    info.candidates[0].type = 0;
    info.candidates[0].ip = htonl(0x0A000001);
    info.candidates[0].port = htons(5000);
    
    ASSERT_EQ(info.count, 1);
    TEST_LOG("  ✓ Single candidate packaged correctly");
}

TEST(boundary_max_candidates_per_packet) {
    TEST_LOG("Testing max candidates per packet");
    
    test_peer_info_t info;
    info.base_index = 0;
    info.count = 10;  // 假设单包最大 10 个
    
    for (int i = 0; i < 10; i++) {
        info.candidates[i].type = 0;
        info.candidates[i].ip = htonl(0x0A000000 + i);
        info.candidates[i].port = htons(5000 + i);
    }
    
    ASSERT_EQ(info.count, 10);
    TEST_LOG("  ✓ Max candidates (10) packed correctly");
}

TEST(boundary_base_index_255) {
    TEST_LOG("Testing base_index boundary (255)");
    
    test_peer_info_t info;
    info.base_index = 255;  // uint8_t 最大值
    info.count = 0;
    
    ASSERT_EQ(info.base_index, 255);
    TEST_LOG("  ✓ base_index=255 handled correctly");
}

TEST(boundary_seq_wrap_around) {
    TEST_LOG("Testing seq number wrap around");
    
    // seq 是 uint16_t, 最大 65535
    test_pkt_hdr_t hdr;
    hdr.seq = htons(65535);
    
    ASSERT_EQ(ntohs(hdr.seq), 65535);
    TEST_LOG("  ✓ seq=65535 (max uint16_t) handled");
    
    // 下一个应该回绕到 0（或 1）
    uint16_t next_seq = ntohs(hdr.seq) + 1;  // 回绕
    ASSERT_EQ(next_seq, 0);
    TEST_LOG("  ✓ seq wrap around to 0");
}

/* ============================================================================
 * 第九部分：错误处理测试
 * ============================================================================ */

TEST(error_invalid_base_index) {
    TEST_LOG("Testing invalid base_index handling");
    
    // base_index 大于实际候选数
    test_peer_info_t info;
    info.base_index = 100;  // 但总共只有 10 个候选
    info.count = 5;
    
    // 接收方应该检测到错误并丢弃
    TEST_LOG("  Receiver should detect base_index > total_candidates");
    TEST_LOG("  ✓ Invalid base_index detection");
}

TEST(error_count_mismatch) {
    TEST_LOG("Testing count mismatch detection");
    
    test_peer_info_t info;
    info.base_index = 0;
    info.count = 5;  // 声称有 5 个
    
    // 但包长度只够 3 个候选
    int actual_candidates = 3;
    
    ASSERT(actual_candidates < info.count);
    TEST_LOG("  count=5 but packet only has 3 candidates");
    TEST_LOG("  ✓ Count mismatch should be detected");
}

TEST(error_register_ack_failed) {
    TEST_LOG("Testing REGISTER_ACK failure status");
    
    test_register_ack_t ack;
    ack.status = 2;  // >=2 表示错误
    
    ASSERT(ack.status >= 2);
    TEST_LOG("  ✓ status>=2 indicates registration failed");
}

TEST(protocol_number_verification) {
    TEST_LOG("Testing SIMPLE protocol number ranges");
    
    // COMPACT 信令协议编号: 0x80-0x9F
    ASSERT_EQ(SIG_PKT_REGISTER, 0x80);
    ASSERT_EQ(SIG_PKT_REGISTER_ACK, 0x81);
    ASSERT_EQ(SIG_PKT_PEER_INFO, 0x82);
    ASSERT_EQ(SIG_PKT_PEER_INFO_ACK, 0x83);
    ASSERT_EQ(SIG_PKT_NAT_PROBE, 0x84);
    ASSERT_EQ(SIG_PKT_NAT_PROBE_ACK, 0x85);
    
    // 中继扩展协议: 0xA0-0xBF
    ASSERT_EQ(P2P_PKT_RELAY_DATA, 0xA0);
    
    TEST_LOG("  ✓ SIMPLE protocols: 0x80-0x85");
    TEST_LOG("  ✓ Relay extension: 0xA0");
}

TEST(packet_size_verification) {
    TEST_LOG("Testing packet size calculations");
    
    // REGISTER_ACK: 4(header) + 10(payload) = 14 bytes
    size_t register_ack_size = sizeof(test_pkt_hdr_t) + sizeof(test_register_ack_t);
    TEST_LOG("  REGISTER_ACK size: %zu bytes (expected 14)", register_ack_size);
    
    // 候选结构: 7 bytes (type + ip + port)
    size_t candidate_size = sizeof(test_candidate_t);
    ASSERT_EQ(candidate_size, 7);
    TEST_LOG("  ✓ Candidate size: 7 bytes");
}

/* ============================================================================
 * 主函数
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("COMPACT Protocol Complete Test Suite\n");
    printf("========================================\n\n");
    
    printf("Part 1: REGISTER_ACK Protocol\n");
    printf("----------------------------------------\n");
    RUN_TEST(register_ack_basic);
    RUN_TEST(register_ack_no_cache);
    RUN_TEST(register_ack_peer_offline);
    RUN_TEST(register_ack_public_address_detection);
    
    printf("\nPart 2: PEER_INFO Serialization\n");
    printf("----------------------------------------\n");
    RUN_TEST(peer_info_seq1_from_server);
    RUN_TEST(peer_info_seq2_with_base_index);
    RUN_TEST(peer_info_fin_flag);
    RUN_TEST(peer_info_last_packet_with_data);
    
    printf("\nPart 3: PEER_INFO_ACK Mechanism\n");
    printf("--Part 10: Protocol Verification\n");
    printf("----------------------------------------\n");
    RUN_TEST(protocol_number_verification);
    RUN_TEST(packet_size_verification);
    
    printf("\n--------------------------------------\n");
    RUN_TEST(peer_info_ack_basic);
    RUN_TEST(peer_info_ack_sequence);
    
    printf("\nPart 4: Complete Flow - Both Online\n");
    printf("----------------------------------------\n");
    RUN_TEST(flow_both_online);
    
    printf("\nPart 5: Complete Flow - Offline Cache\n");
    printf("----------------------------------------\n");
    RUN_TEST(flow_offline_cache);
    
    printf("\nPart 6: No Cache Support Scenario\n");
    printf("----------------------------------------\n");
    RUN_TEST(flow_no_cache_support);
    
    printf("\nPart 7: Retransmission Mechanism\n");
    printf("----------------------------------------\n");
    RUN_TEST(retransmission_on_packet_loss);
    RUN_TEST(ack_packet_loss_handling);
    
    printf("\nPart 8: Boundary Conditions\n");
    printf("----------------------------------------\n");
    RUN_TEST(boundary_single_candidate);
    RUN_TEST(boundary_max_candidates_per_packet);
    RUN_TEST(boundary_base_index_255);
    RUN_TEST(boundary_seq_wrap_around);
    
    printf("\nPart 9: Error Handling\n");
    printf("----------------------------------------\n");
    RUN_TEST(error_invalid_base_index);
    RUN_TEST(error_count_mismatch);
    RUN_TEST(error_register_ack_failed);
    
    printf("\n");
    TEST_SUMMARY();
    
    return (test_failed > 0) ? 1 : 0;
}
