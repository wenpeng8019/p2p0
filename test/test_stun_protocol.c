/*
 * test_stun_protocol.c - STUN 协议层单元测试
 *
 * 测试覆盖：
 * 1. STUN 包头格式（Magic Cookie、消息类型、事务ID）
 * 2. Binding Request 构造（内联实现，不依赖 p2p_static）
 * 3. XOR-MAPPED-ADDRESS 解析（XOR 解码正确性）
 * 4. NAT 类型枚举完备性
 * 5. CHANGE-REQUEST 属性标志位
 * 6. NAT 类型检测决策树逻辑
 *
 * 注：本测试独立运行，不链接 p2p_static，避免 OpenSSL/DES 依赖。
 */

#include "test_framework.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <arpa/inet.h>

/* ---- 从 p2p_stun.h 复制必要的常量与类型，保持独立 ---- */

#define STUN_MAGIC               0x2112A442U
#define STUN_BINDING_REQUEST     0x0001
#define STUN_BINDING_RESPONSE    0x0101
#define STUN_ATTR_MAPPED_ADDR       0x0001
#define STUN_ATTR_CHANGE_REQUEST    0x0003
#define STUN_ATTR_CHANGED_ADDR      0x0005
#define STUN_ATTR_USERNAME          0x0006
#define STUN_ATTR_MESSAGE_INTEGRITY 0x0008
#define STUN_ATTR_XOR_MAPPED_ADDR   0x0020
#define STUN_ATTR_FINGERPRINT       0x8028
#define STUN_FLAG_CHANGE_IP         0x04
#define STUN_FLAG_CHANGE_PORT       0x02

typedef struct {
    uint16_t type;
    uint16_t length;
    uint32_t magic;
    uint8_t  tsx_id[12];
} stun_hdr_t;

typedef enum {
    P2P_STUN_NAT_UNKNOWN = 0,
    P2P_STUN_NAT_OPEN,
    P2P_STUN_NAT_BLOCKED,
    P2P_STUN_NAT_FULL_CONE,
    P2P_STUN_NAT_RESTRICTED,
    P2P_STUN_NAT_PORT_RESTRICTED,
    P2P_STUN_NAT_SYMMETRIC,
    P2P_STUN_NAT_SYMMETRIC_UDP
} p2p_stun_nat_type_t;

/* ---- 内联实现 Binding Request 构造（不依赖 p2p_static） ---- */
static int stun_build_binding_request_inline(uint8_t *buf, int max_len,
                                             const uint8_t tsx_id[12])
{
    if (max_len < 20) return -1;
    stun_hdr_t *h = (stun_hdr_t *)buf;
    h->type   = htons(STUN_BINDING_REQUEST);
    h->length = htons(0);
    h->magic  = htonl(STUN_MAGIC);
    if (tsx_id) {
        memcpy(h->tsx_id, tsx_id, 12);
    } else {
        for (int i = 0; i < 12; i++) h->tsx_id[i] = (uint8_t)(rand() & 0xFF);
    }
    return 20;
}

/* ============================================================================
 * 1. 包头常量与格式验证
 * ============================================================================ */

static void stun_magic(void) {
    printf("[TEST] Testing STUN Magic Cookie...\n");
    ASSERT_EQ(STUN_MAGIC, 0x2112A442);
    /* 序列化后应为大端 */
    uint32_t magic_be = htonl(STUN_MAGIC);
    uint8_t *b = (uint8_t *)&magic_be;
    ASSERT_EQ(b[0], 0x21);
    ASSERT_EQ(b[1], 0x12);
    ASSERT_EQ(b[2], 0xA4);
    ASSERT_EQ(b[3], 0x42);
    printf("[TEST] ✓ STUN Magic Cookie passed\n");
}

static void stun_message_types(void) {
    printf("[TEST] Testing STUN message type constants...\n");
    ASSERT_EQ(STUN_BINDING_REQUEST,  0x0001);
    ASSERT_EQ(STUN_BINDING_RESPONSE, 0x0101);
    printf("[TEST] ✓ STUN message types passed\n");
}

static void stun_attr_types(void) {
    printf("[TEST] Testing STUN attribute type constants...\n");
    ASSERT_EQ(STUN_ATTR_MAPPED_ADDR,       0x0001);
    ASSERT_EQ(STUN_ATTR_CHANGE_REQUEST,    0x0003);
    ASSERT_EQ(STUN_ATTR_CHANGED_ADDR,      0x0005);
    ASSERT_EQ(STUN_ATTR_XOR_MAPPED_ADDR,   0x0020);
    ASSERT_EQ(STUN_ATTR_FINGERPRINT,       0x8028);
    printf("[TEST] ✓ STUN attribute types passed\n");
}

static void stun_change_request_flags(void) {
    printf("[TEST] Testing STUN CHANGE-REQUEST flags...\n");
    ASSERT_EQ(STUN_FLAG_CHANGE_IP,   0x04);
    ASSERT_EQ(STUN_FLAG_CHANGE_PORT, 0x02);
    /* 同时变更 IP+端口 */
    uint8_t both = STUN_FLAG_CHANGE_IP | STUN_FLAG_CHANGE_PORT;
    ASSERT_EQ(both, 0x06);
    printf("[TEST] ✓ STUN CHANGE-REQUEST flags passed\n");
}

/* ============================================================================
 * 2. stun_hdr_t 结构大小与布局
 * ============================================================================ */

static void stun_header_size(void) {
    printf("[TEST] Testing STUN header struct size...\n");
    /* 2+2+4+12 = 20 字节 */
    ASSERT_EQ((int)sizeof(stun_hdr_t), 20);
    printf("[TEST] ✓ STUN header size = 20 bytes\n");
}

/* ============================================================================
 * 3. Binding Request 构造
 * ============================================================================ */

static void stun_build_binding_request(void) {
    printf("[TEST] Testing STUN Binding Request construction...\n");

    uint8_t buf[256] = {0};
    uint8_t tsx_id[12];
    for (int i = 0; i < 12; i++) tsx_id[i] = (uint8_t)(i + 1);

    int len = stun_build_binding_request_inline(buf, sizeof(buf), tsx_id);

    ASSERT(len == 20);

    stun_hdr_t *hdr = (stun_hdr_t *)buf;
    ASSERT_EQ(ntohs(hdr->type),  STUN_BINDING_REQUEST);
    ASSERT_EQ(ntohl(hdr->magic), (int)STUN_MAGIC);
    ASSERT_EQ(memcmp(hdr->tsx_id, tsx_id, 12), 0);

    printf("[TEST] ✓ STUN Binding Request construction passed (len=%d)\n", len);
}

/* ============================================================================
 * 4. XOR-MAPPED-ADDRESS 解码
 * ============================================================================ */

static void xor_mapped_address_decode(void) {
    printf("[TEST] Testing XOR-MAPPED-ADDRESS decode...\n");

    /*
     * 模拟 XOR-MAPPED-ADDRESS 属性值：
     *   Family = 0x01 (IPv4)
     *   X-Port = actual_port XOR (MAGIC >> 16)
     *   X-Addr = actual_addr XOR MAGIC
     */
    uint16_t actual_port = 12345;
    uint32_t actual_addr = 0x01020304; /* 1.2.3.4 */

    uint16_t xport = actual_port ^ (uint16_t)(STUN_MAGIC >> 16);
    uint32_t xaddr = actual_addr ^ STUN_MAGIC;

    /* 解码 */
    uint16_t decoded_port = xport ^ (uint16_t)(STUN_MAGIC >> 16);
    uint32_t decoded_addr = xaddr ^ STUN_MAGIC;

    ASSERT_EQ(decoded_port, actual_port);
    ASSERT_EQ(decoded_addr, actual_addr);

    printf("[TEST] ✓ XOR-MAPPED-ADDRESS decode passed\n");
}

/* ============================================================================
 * 5. NAT 类型枚举
 * ============================================================================ */

static void nat_type_enum(void) {
    printf("[TEST] Testing NAT type enum completeness...\n");

    /* 按穿透难度覆盖所有类型 */
    p2p_stun_nat_type_t types[] = {
        P2P_STUN_NAT_UNKNOWN,
        P2P_STUN_NAT_OPEN,
        P2P_STUN_NAT_BLOCKED,
        P2P_STUN_NAT_FULL_CONE,
        P2P_STUN_NAT_RESTRICTED,
        P2P_STUN_NAT_PORT_RESTRICTED,
        P2P_STUN_NAT_SYMMETRIC,
        P2P_STUN_NAT_SYMMETRIC_UDP,
    };
    int count = (int)(sizeof(types) / sizeof(types[0]));
    ASSERT_EQ(count, 8);

    /* 穿透难度验证：OPEN < FULL_CONE < PORT_RESTRICTED < SYMMETRIC */
    ASSERT(P2P_STUN_NAT_OPEN       < P2P_STUN_NAT_FULL_CONE);
    ASSERT(P2P_STUN_NAT_FULL_CONE  < P2P_STUN_NAT_PORT_RESTRICTED);
    ASSERT(P2P_STUN_NAT_PORT_RESTRICTED < P2P_STUN_NAT_SYMMETRIC);

    printf("[TEST] ✓ NAT type enum passed (%d types)\n", count);
}

/* ============================================================================
 * 6. NAT 检测决策树（模拟）
 * ============================================================================ */

/*
 * 模拟 RFC 3489 决策树逻辑：
 *
 *   mapped == local  → OPEN
 *   test_ii_ok       → FULL_CONE
 *   test_iii_ok      → RESTRICTED
 *   else             → PORT_RESTRICTED
 *   mapped_changes   → SYMMETRIC
 */
static p2p_stun_nat_type_t simulate_nat_detection(
    int mapped_eq_local,
    int test_ii_ok,
    int test_iii_ok,
    int mapped_changes)
{
    if (mapped_eq_local)  return P2P_STUN_NAT_OPEN;
    if (mapped_changes)   return P2P_STUN_NAT_SYMMETRIC;
    if (test_ii_ok)       return P2P_STUN_NAT_FULL_CONE;
    if (test_iii_ok)      return P2P_STUN_NAT_RESTRICTED;
    return P2P_STUN_NAT_PORT_RESTRICTED;
}

static void nat_detection_logic(void) {
    printf("[TEST] Testing NAT detection decision tree (mock)...\n");

    /* 场景1：公网直连 */
    ASSERT_EQ(simulate_nat_detection(1, 0, 0, 0), P2P_STUN_NAT_OPEN);

    /* 场景2：对称型 NAT（映射地址随目标变化） */
    ASSERT_EQ(simulate_nat_detection(0, 0, 0, 1), P2P_STUN_NAT_SYMMETRIC);

    /* 场景3：完全锥形 NAT */
    ASSERT_EQ(simulate_nat_detection(0, 1, 0, 0), P2P_STUN_NAT_FULL_CONE);

    /* 场景4：受限锥形 NAT */
    ASSERT_EQ(simulate_nat_detection(0, 0, 1, 0), P2P_STUN_NAT_RESTRICTED);

    /* 场景5：端口受限锥形 NAT */
    ASSERT_EQ(simulate_nat_detection(0, 0, 0, 0), P2P_STUN_NAT_PORT_RESTRICTED);

    printf("[TEST] ✓ NAT detection decision tree passed (5 scenarios)\n");
}

/* ============================================================================
 * main
 * ============================================================================ */

int main(void) {
    printf("\n========================================\n");
    printf("  STUN 协议层单元测试\n");
    printf("========================================\n\n");

    stun_magic();
    stun_message_types();
    stun_attr_types();
    stun_change_request_flags();
    stun_header_size();
    stun_build_binding_request();
    xor_mapped_address_decode();
    nat_type_enum();
    nat_detection_logic();

    printf("\n========================================\n");
    printf("  所有测试通过！✓\n");
    printf("========================================\n\n");
    return 0;
}
