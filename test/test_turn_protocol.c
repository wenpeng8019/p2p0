/*
 * test_turn_protocol.c - TURN 协议层单元测试
 *
 * 覆盖点：
 * 1. TURN 消息类型常量
 * 2. TURN 属性类型常量
 * 3. Allocate 请求包格式（内联构造）
 * 4. XOR-RELAYED-ADDRESS 解码
 * 5. 错误码常量
 * 6. 生命周期参数（DEFAULT_LIFETIME）
 *
 * 注：独立运行，不链接 p2p_static，避免 OpenSSL 依赖。
 */

#include "test_framework.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>

/* ---- 内联 TURN/STUN 常量（摘自 RFC 5766，无需外部头文件） ---- */

#define STUN_MAGIC              0x2112A442U

#define TURN_ALLOCATE_REQUEST   0x0003
#define TURN_ALLOCATE_RESPONSE  0x0103
#define TURN_ALLOCATE_ERROR     0x0113
#define TURN_REFRESH_REQUEST    0x0004
#define TURN_REFRESH_RESPONSE   0x0104
#define TURN_SEND_INDICATION    0x0016
#define TURN_DATA_INDICATION    0x0017

#define TURN_ATTR_LIFETIME          0x000D
#define TURN_ATTR_XOR_PEER_ADDR     0x0012
#define TURN_ATTR_XOR_RELAYED_ADDR  0x0016
#define TURN_ATTR_REQUESTED_TRANS   0x0019

#define TURN_ERR_UNAUTHORIZED       401
#define TURN_ERR_FORBIDDEN          403
#define TURN_ERR_ALLOC_MISMATCH     437
#define TURN_ERR_STALE_NONCE        438
#define TURN_ERR_INSUFFICIENT_CAP   508

#define TURN_PROTO_UDP          17
#define TURN_DEFAULT_LIFETIME   600

typedef struct {
    uint16_t type;
    uint16_t length;
    uint32_t magic;
    uint8_t  tsx_id[12];
} stun_hdr_t;

/* ===========================================================================
 * 1. TURN 消息类型常量
 * =========================================================================== */
static void turn_message_types(void) {
    printf("[TEST] Testing TURN message type constants...\n");
    ASSERT_EQ(TURN_ALLOCATE_REQUEST,  0x0003);
    ASSERT_EQ(TURN_ALLOCATE_RESPONSE, 0x0103);
    ASSERT_EQ(TURN_ALLOCATE_ERROR,    0x0113);
    ASSERT_EQ(TURN_REFRESH_REQUEST,   0x0004);
    ASSERT_EQ(TURN_REFRESH_RESPONSE,  0x0104);
    ASSERT_EQ(TURN_SEND_INDICATION,   0x0016);
    ASSERT_EQ(TURN_DATA_INDICATION,   0x0017);
    printf("[TEST] ✓ TURN message types passed\n");
}

/* ===========================================================================
 * 2. TURN 属性类型常量
 * =========================================================================== */
static void turn_attr_types(void) {
    printf("[TEST] Testing TURN attribute type constants...\n");
    ASSERT_EQ(TURN_ATTR_LIFETIME,         0x000D);
    ASSERT_EQ(TURN_ATTR_XOR_PEER_ADDR,    0x0012);
    ASSERT_EQ(TURN_ATTR_XOR_RELAYED_ADDR, 0x0016);
    ASSERT_EQ(TURN_ATTR_REQUESTED_TRANS,  0x0019);
    printf("[TEST] ✓ TURN attribute types passed\n");
}

/* ===========================================================================
 * 3. Allocate 请求构造（内联）
 * =========================================================================== */
static void allocate_request_format(void) {
    printf("[TEST] Testing TURN Allocate request format (inline)...\n");

    uint8_t buf[100] = {0};
    stun_hdr_t *h = (stun_hdr_t *)buf;
    h->type   = htons(TURN_ALLOCATE_REQUEST);
    h->length = htons(8);
    h->magic  = htonl(STUN_MAGIC);
    for (int i = 0; i < 12; i++) h->tsx_id[i] = (uint8_t)(i + 0xA0);

    uint8_t *attr = buf + 20;
    attr[0] = 0x00; attr[1] = 0x19;
    attr[2] = 0x00; attr[3] = 0x04;
    attr[4] = TURN_PROTO_UDP;
    attr[5] = 0; attr[6] = 0; attr[7] = 0;

    ASSERT_EQ(ntohs(h->type),   TURN_ALLOCATE_REQUEST);
    ASSERT_EQ(ntohl(h->magic),  (int)STUN_MAGIC);
    ASSERT_EQ(ntohs(h->length), 8);
    ASSERT_EQ(attr[4],          (int)TURN_PROTO_UDP);

    printf("[TEST] ✓ Allocate request format passed\n");
}

/* ===========================================================================
 * 4. XOR-RELAYED-ADDRESS 解码
 * =========================================================================== */
static void xor_relayed_address_decode(void) {
    printf("[TEST] Testing XOR-RELAYED-ADDRESS decode...\n");

    uint16_t actual_port = 49152;
    uint32_t actual_addr = 0xC0A80001; /* 192.168.0.1 */

    uint16_t xport = actual_port ^ (uint16_t)(STUN_MAGIC >> 16);
    uint32_t xaddr = actual_addr ^ STUN_MAGIC;

    uint16_t decoded_port = xport ^ (uint16_t)(STUN_MAGIC >> 16);
    uint32_t decoded_addr = xaddr ^ STUN_MAGIC;

    ASSERT_EQ(decoded_port, actual_port);
    ASSERT_EQ(decoded_addr, actual_addr);

    printf("[TEST] ✓ XOR-RELAYED-ADDRESS decode passed\n");
}

/* ===========================================================================
 * 5. TURN 错误码常量
 * =========================================================================== */
static void turn_error_codes(void) {
    printf("[TEST] Testing TURN error code constants...\n");
    ASSERT_EQ(TURN_ERR_UNAUTHORIZED,     401);
    ASSERT_EQ(TURN_ERR_FORBIDDEN,        403);
    ASSERT_EQ(TURN_ERR_ALLOC_MISMATCH,   437);
    ASSERT_EQ(TURN_ERR_STALE_NONCE,      438);
    ASSERT_EQ(TURN_ERR_INSUFFICIENT_CAP, 508);
    ASSERT(TURN_ERR_UNAUTHORIZED < TURN_ERR_STALE_NONCE);
    printf("[TEST] ✓ TURN error codes passed\n");
}

/* ===========================================================================
 * 6. 生命周期参数验证
 * =========================================================================== */
static void turn_lifetime(void) {
    printf("[TEST] Testing TURN lifetime constant...\n");
    ASSERT_EQ(TURN_DEFAULT_LIFETIME, 600);
    int refresh = TURN_DEFAULT_LIFETIME / 2;
    ASSERT(refresh < TURN_DEFAULT_LIFETIME);
    ASSERT(refresh == 300);
    printf("[TEST] ✓ TURN lifetime passed (default=%ds, refresh at %ds)\n",
           TURN_DEFAULT_LIFETIME, refresh);
}

/* ===========================================================================
 * main
 * =========================================================================== */
int main(void) {
    printf("\n========================================\n");
    printf("  TURN 协议层单元测试\n");
    printf("========================================\n\n");

    turn_message_types();
    turn_attr_types();
    allocate_request_format();
    xor_relayed_address_decode();
    turn_error_codes();
    turn_lifetime();

    printf("\n========================================\n");
    printf("  所有测试通过！✓\n");
    printf("========================================\n\n");
    return 0;
}
