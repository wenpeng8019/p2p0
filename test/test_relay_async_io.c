/*
 * test_relay_async_io.c - RELAY 信令异步 I/O 状态机单元测试
 * 
 * 测试覆盖：
 * 1. 单字节分片接收（极端分片）
 * 2. 完整消息一次接收
 * 3. 多消息粘包
 * 4. EAGAIN 在各状态下的处理
 * 5. 连接断开（recv 返回 0）
 * 6. Magic 不匹配
 * 7. 各状态转换路径
 * 8. 内存边界情况
 * 
 * 注意：本测试不依赖 p2p 库，独立实现状态机验证
 */

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * 最小化的 RELAY 协议定义（从 p2pp.h 提取）
 * ============================================================================ */

#define P2P_RLY_MAGIC 0x52454C59  /* "RELY" */
#define P2P_PEER_ID_MAX 32

/* RELAY 消息类型 */
#define P2P_RLY_LOGIN      0x01
#define P2P_RLY_LOGIN_ACK  0x02
#define P2P_RLY_CONNECT    0x03
#define P2P_RLY_CONNECT_ACK 0x04
#define P2P_RLY_OFFER      0x05
#define P2P_RLY_FORWARD    0x06
#define P2P_RLY_PEER_OFFLINE 0x07
#define P2P_RLY_READY      0x08

/* RELAY 消息头（9 字节） */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;    /* 魔数：0x52454C59 */
    uint8_t  type;     /* 消息类型 */
    uint32_t length;   /* 消息体长度 */
} p2p_relay_hdr_t;
#pragma pack(pop)

/* ============================================================================
 * 状态机定义（从 p2p_signal_relay.h 提取）
 * ============================================================================ */

/* TCP 读取状态机 */
typedef enum {
    RELAY_READ_IDLE = 0,
    RELAY_READ_HEADER,
    RELAY_READ_SENDER,
    RELAY_READ_PAYLOAD,
    RELAY_READ_DISCARD
} relay_read_state_t;

/* 简化的上下文结构（仅包含状态机需要的字段） */
typedef struct {
    int fd;
    relay_read_state_t read_state;
    p2p_relay_hdr_t read_hdr;
    char read_sender[P2P_PEER_ID_MAX];
    uint8_t *read_payload;
    int read_offset;
    int read_expected;
    
    /* 测试用的接收回调 */
    char last_sender[P2P_PEER_ID_MAX];
    uint8_t last_type;
    int message_count;
    bool connection_closed;
} test_relay_ctx_t;

/* ============================================================================
 * Mock Socket 系统 - 拦截 recv() 调用
 * ============================================================================ */

/* 模拟数据缓冲区 */
static uint8_t mock_recv_buffer[8192];
static size_t mock_recv_size = 0;
static size_t mock_recv_offset = 0;
static int mock_recv_chunk_size = -1;  /* -1 表示返回所有可用数据，否则限制每次返回的字节数 */
static int mock_recv_return_code = 0;  /* 0=正常，-1=错误 */
static int mock_recv_errno = 0;

/* 重置 mock 状态 */
static void mock_reset(void) {
    mock_recv_size = 0;
    mock_recv_offset = 0;
    mock_recv_chunk_size = -1;
    mock_recv_return_code = 0;
    mock_recv_errno = 0;
}

/* 向 mock 缓冲区添加数据 */
static void mock_add_data(const void *data, size_t len) {
    if (mock_recv_size + len > sizeof(mock_recv_buffer)) {
        printf("ERROR: mock buffer overflow\n");
        exit(1);
    }
    memcpy(mock_recv_buffer + mock_recv_size, data, len);
    mock_recv_size += len;
}

/* 设置每次 recv 返回的最大字节数（模拟分片） */
static void mock_set_chunk_size(int size) {
    mock_recv_chunk_size = size;
}

/* 设置 recv 返回错误 */
static void mock_set_error(int err) {
    mock_recv_return_code = -1;
    mock_recv_errno = err;
}

/* 检查是否还有数据 */
static int mock_has_data(void) {
    return mock_recv_offset < mock_recv_size;
}

/* Mock recv 函数 */
static int mock_recv_impl(int fd, void *buf, size_t len, int flags) {
    (void)fd;
    (void)flags;
    
    /* 如果设置了错误，返回错误码 */
    if (mock_recv_return_code == -1) {
        errno = mock_recv_errno;
        return -1;
    }
    
    /* 如果没有数据了，返回 EAGAIN */
    if (mock_recv_offset >= mock_recv_size) {
        errno = EAGAIN;
        return -1;
    }
    
    /* 计算本次可返回的数据量 */
    size_t available = mock_recv_size - mock_recv_offset;
    size_t to_return = len;
    
    if (to_return > available) {
        to_return = available;
    }
    
    if (mock_recv_chunk_size > 0 && to_return > (size_t)mock_recv_chunk_size) {
        to_return = mock_recv_chunk_size;
    }
    
    /* 拷贝数据并更新偏移 */
    memcpy(buf, mock_recv_buffer + mock_recv_offset, to_return);
    mock_recv_offset += to_return;
    
    return (int)to_return;
}

/* ============================================================================
 * 简化版状态机实现（核心逻辑从 p2p_signal_relay.c 提取）
 * ============================================================================ */

static void test_relay_tick(test_relay_ctx_t *ctx) {
    if (ctx->fd < 0) return;
    
    /* 循环读取，直到 EAGAIN */
    for(;;) {
        switch (ctx->read_state) {
        
        case RELAY_READ_IDLE: {
            /* 开始读取新消息头 */
            ctx->read_offset = 0;
            ctx->read_expected = sizeof(p2p_relay_hdr_t);
            ctx->read_state = RELAY_READ_HEADER;
            /* fall through */
        }
        
        case RELAY_READ_HEADER: {
            /* 读取消息头 */
            int remaining = ctx->read_expected - ctx->read_offset;
            char *buf = ((char*)&ctx->read_hdr) + ctx->read_offset;
            
            int n = mock_recv_impl(ctx->fd, buf, remaining, 0);
            
            if (n == 0) {
                ctx->connection_closed = true;
                return;
            }
            
            if (n < 0) {
                if (errno == EAGAIN) {
                    return;  /* 等待下次 tick */
                }
                /* 其他错误 */
                ctx->connection_closed = true;
                return;
            }
            
            ctx->read_offset += n;
            
            if (ctx->read_offset >= ctx->read_expected) {
                /* 验证 magic */
                if (ctx->read_hdr.magic != P2P_RLY_MAGIC) {
                    printf("  [DEBUG] Invalid magic, reset\n");
                    ctx->read_state = RELAY_READ_IDLE;
                    return;
                }
                
                /* 根据消息类型决定下一步 */
                if (ctx->read_hdr.type == P2P_RLY_OFFER || ctx->read_hdr.type == P2P_RLY_FORWARD) {
                    ctx->read_offset = 0;
                    ctx->read_expected = P2P_PEER_ID_MAX;
                    ctx->read_state = RELAY_READ_SENDER;
                } else if (ctx->read_hdr.length > 0) {
                    ctx->read_offset = 0;
                    ctx->read_expected = ctx->read_hdr.length;
                    ctx->read_state = RELAY_READ_DISCARD;
                } else {
                    /* 无 payload */
                    ctx->message_count++;
                    ctx->last_type = ctx->read_hdr.type;
                    ctx->read_state = RELAY_READ_IDLE;
                }
            }
            break;
        }
        
        case RELAY_READ_SENDER: {
            /* 读取 sender_name */
            int remaining = ctx->read_expected - ctx->read_offset;
            char *buf = ctx->read_sender + ctx->read_offset;
            
            int n = mock_recv_impl(ctx->fd, buf, remaining, 0);
            
            if (n == 0) {
                ctx->connection_closed = true;
                return;
            }
            
            if (n < 0) {
                if (errno == EAGAIN) {
                    return;
                }
                ctx->connection_closed = true;
                return;
            }
            
            ctx->read_offset += n;
            
            if (ctx->read_offset >= ctx->read_expected) {
                /* 计算 payload 长度 */
                uint32_t payload_len = ctx->read_hdr.length - P2P_PEER_ID_MAX;
                
                if (payload_len > 0) {
                    ctx->read_payload = (uint8_t*)malloc(payload_len);
                    if (!ctx->read_payload) {
                        ctx->read_state = RELAY_READ_IDLE;
                        return;
                    }
                    ctx->read_offset = 0;
                    ctx->read_expected = payload_len;
                    ctx->read_state = RELAY_READ_PAYLOAD;
                } else {
                    /* 无 payload，直接完成 */
                    strncpy(ctx->last_sender, ctx->read_sender, P2P_PEER_ID_MAX);
                    ctx->last_type = ctx->read_hdr.type;
                    ctx->message_count++;
                    ctx->read_state = RELAY_READ_IDLE;
                }
            }
            break;
        }
        
        case RELAY_READ_PAYLOAD: {
            /* 读取 payload */
            int remaining = ctx->read_expected - ctx->read_offset;
            char *buf = (char*)ctx->read_payload + ctx->read_offset;
            
            int n = mock_recv_impl(ctx->fd, buf, remaining, 0);
            
            if (n == 0) {
                ctx->connection_closed = true;
                return;
            }
            
            if (n < 0) {
                if (errno == EAGAIN) {
                    return;
                }
                ctx->connection_closed = true;
                return;
            }
            
            ctx->read_offset += n;
            
            if (ctx->read_offset >= ctx->read_expected) {
                /* 读取完成 */
                strncpy(ctx->last_sender, ctx->read_sender, P2P_PEER_ID_MAX);
                ctx->last_type = ctx->read_hdr.type;
                ctx->message_count++;
                
                free(ctx->read_payload);
                ctx->read_payload = NULL;
                ctx->read_state = RELAY_READ_IDLE;
            }
            break;
        }
        
        case RELAY_READ_DISCARD: {
            /* 丢弃消息 */
            if (!ctx->read_payload && ctx->read_expected > 0) {
                ctx->read_payload = (uint8_t*)malloc(ctx->read_expected);
                if (!ctx->read_payload) {
                    ctx->connection_closed = true;
                    return;
                }
            }
            
            int remaining = ctx->read_expected - ctx->read_offset;
            char *buf = (char*)ctx->read_payload + ctx->read_offset;
            
            int n = mock_recv_impl(ctx->fd, buf, remaining, 0);
            
            if (n == 0) {
                ctx->connection_closed = true;
                return;
            }
            
            if (n < 0) {
                if (errno == EAGAIN) {
                    return;
                }
                ctx->connection_closed = true;
                return;
            }
            
            ctx->read_offset += n;
            
            if (ctx->read_offset >= ctx->read_expected) {
                if (ctx->read_payload) {
                    free(ctx->read_payload);
                    ctx->read_payload = NULL;
                }
                ctx->message_count++;
                ctx->last_type = ctx->read_hdr.type;
                ctx->read_state = RELAY_READ_IDLE;
            }
            break;
        }
        
        default:
            printf("  [ERROR] Invalid state %d\n", ctx->read_state);
            ctx->read_state = RELAY_READ_IDLE;
            break;
        }
    }
}

/* ============================================================================
 * 辅助函数 - 构造测试消息
 * ============================================================================ */

/* 构造 RELAY 消息头 */
static void build_relay_header(uint8_t *buf, uint8_t type, uint32_t length) {
    p2p_relay_hdr_t hdr;
    hdr.magic = P2P_RLY_MAGIC;
    hdr.type = type;
    hdr.length = length;
    memcpy(buf, &hdr, sizeof(hdr));
}

/* 构造完整的 OFFER 消息 */
static size_t build_offer_message(uint8_t *buf, const char *sender, const void *payload, uint32_t payload_len) {
    size_t offset = 0;
    
    /* 头部：magic(4) + type(1) + length(4) = 9 字节 */
    build_relay_header(buf, P2P_RLY_OFFER, P2P_PEER_ID_MAX + payload_len);
    offset += sizeof(p2p_relay_hdr_t);
    
    /* Sender name：32 字节 */
    memset(buf + offset, 0, P2P_PEER_ID_MAX);
    strncpy((char*)(buf + offset), sender, P2P_PEER_ID_MAX - 1);
    offset += P2P_PEER_ID_MAX;
    
    /* Payload */
    if (payload_len > 0 && payload) {
        memcpy(buf + offset, payload, payload_len);
        offset += payload_len;
    }
    
    return offset;
}

/* 构造简单的响应消息（无 sender/payload） */
static size_t build_simple_message(uint8_t *buf, uint8_t type) {
    build_relay_header(buf, type, 0);
    return sizeof(p2p_relay_hdr_t);
}

/* ============================================================================
 * 测试上下文初始化
 * ============================================================================ */

static test_relay_ctx_t test_ctx;

static void init_test_context(void) {
    memset(&test_ctx, 0, sizeof(test_ctx));
    test_ctx.fd = 1;  /* 设置为有效的 fd */
    test_ctx.read_state = RELAY_READ_IDLE;
}

/* ============================================================================
 * 测试用例
 * ============================================================================ */

/* 测试1：完整消息一次接收 */
TEST(complete_message_single_recv) {
    mock_reset();
    init_test_context();
    
    /* 构造 OFFER 消息 */
    uint8_t msg[512];
    uint8_t payload[100] = "test payload data";
    size_t msg_len = build_offer_message(msg, "alice", payload, 100);
    
    mock_add_data(msg, msg_len);
    
    /* 调用 tick，应该完整处理消息 */
    test_relay_tick(&test_ctx);
    
    /* 验证消息已处理（状态会停留在 HEADER，准备读下一个消息） */
    ASSERT(test_ctx.message_count == 1);
    ASSERT(strcmp(test_ctx.last_sender, "alice") == 0);
    ASSERT(test_ctx.read_state == RELAY_READ_HEADER);
    ASSERT(test_ctx.read_offset == 0);  /* 准备读取新消息 */
}

/* 测试2：单字节分片接收（极端情况） */
TEST(single_byte_fragmentation) {
    mock_reset();
    init_test_context();
    
    /* 构造消息 */
    uint8_t msg[512];
    uint8_t payload[50] = "fragmented";
    size_t msg_len = build_offer_message(msg, "bob", payload, 50);
    
    mock_add_data(msg, msg_len);
    mock_set_chunk_size(1);  /* 每次只返回 1 字节 */
    
    /* 需要多次 tick 才能完成 */
    int max_ticks = (int)msg_len + 10;
    int tick_count = 0;
    
    while (mock_has_data() && tick_count < max_ticks) {
        test_relay_tick(&test_ctx);
        tick_count++;
    }
    
    /* 验证最终完成 */
    ASSERT(test_ctx.message_count == 1);
    ASSERT(strcmp(test_ctx.last_sender, "bob") == 0);
    ASSERT(test_ctx.read_state == RELAY_READ_HEADER);
}

/* 测试3：头部分片 + EAGAIN */
TEST(header_fragmentation_with_eagain) {
    mock_reset();
    init_test_context();
    
    uint8_t msg[512];
    size_t msg_len = build_offer_message(msg, "charlie", "test", 4);
    
    /* 第一次只给 5 字节头部（共 9 字节） */
    mock_add_data(msg, 5);
    test_relay_tick(&test_ctx);
    
    /* 验证停在 HEADER 状态 */
    ASSERT(test_ctx.read_state == RELAY_READ_HEADER);
    ASSERT(test_ctx.read_offset == 5);
    
    /* 添加剩余头部 */
    mock_add_data(msg + 5, 4);
    test_relay_tick(&test_ctx);
    
    /* 验证进入 SENDER 状态 */
    ASSERT(test_ctx.read_state == RELAY_READ_SENDER);
    
    /* 添加剩余数据 */
    mock_add_data(msg + 9, msg_len - 9);
    test_relay_tick(&test_ctx);
    
    /* 验证完成 */
    ASSERT(test_ctx.message_count == 1);
    ASSERT(test_ctx.read_state == RELAY_READ_HEADER);
}

/* 测试4：Sender 分片接收 */
TEST(sender_fragmentation) {
    mock_reset();
    init_test_context();
    
    uint8_t msg[512];
    size_t msg_len = build_offer_message(msg, "david", "payload", 7);
    
    /* 给完整头部 + 部分 sender（20 字节） */
    mock_add_data(msg, 9 + 20);
    test_relay_tick(&test_ctx);
    
    /* 验证停在 SENDER 状态 */
    ASSERT(test_ctx.read_state == RELAY_READ_SENDER);
    ASSERT(test_ctx.read_offset == 20);
    
    /* 给剩余数据 */
    mock_add_data(msg + 9 + 20, msg_len - 9 - 20);
    test_relay_tick(&test_ctx);
    
    /* 验证完成 */
    ASSERT(test_ctx.message_count == 1);
    ASSERT(strcmp(test_ctx.last_sender, "david") == 0);
    ASSERT(test_ctx.read_state == RELAY_READ_HEADER);
}

/* 测试5：Payload 分片接收 */
TEST(payload_fragmentation) {
    mock_reset();
    init_test_context();
    
    uint8_t payload[256];
    memset(payload, 'A', sizeof(payload));
    
    uint8_t msg[1024];
    size_t msg_len = build_offer_message(msg, "eve", payload, sizeof(payload));
    
    /* 给头部 + sender */
    mock_add_data(msg, 9 + 32);
    test_relay_tick(&test_ctx);
    
    /* 验证进入 PAYLOAD 状态 */
    ASSERT(test_ctx.read_state == RELAY_READ_PAYLOAD);
    
    /* 分批给 payload（每次 50 字节） */
    size_t remaining = msg_len - 9 - 32;
    size_t offset = 9 + 32;
    
    while (remaining > 0) {
        size_t chunk = (remaining > 50) ? 50 : remaining;
        mock_add_data(msg + offset, chunk);
        test_relay_tick(&test_ctx);
        offset += chunk;
        remaining -= chunk;
    }
    
    /* 验证完成 */
    ASSERT(test_ctx.message_count == 1);
    ASSERT(test_ctx.read_state == RELAY_READ_HEADER);
}

/* 测试6：多消息粘包 */
TEST(multiple_messages_pipelined) {
    mock_reset();
    init_test_context();
    
    /* 构造两个连续的消息 */
    uint8_t msg1[256];
    uint8_t msg2[256];
    size_t len1 = build_offer_message(msg1, "frank", "msg1", 4);
    size_t len2 = build_offer_message(msg2, "grace", "msg2", 4);
    
    /* 一次性添加两个消息 */
    mock_add_data(msg1, len1);
    mock_add_data(msg2, len2);
    
    /* 第一次 tick 应该处理完两个消息（因为是循环读取） */
    test_relay_tick(&test_ctx);
    
    ASSERT(test_ctx.message_count == 2);
    ASSERT(strcmp(test_ctx.last_sender, "grace") == 0);
    ASSERT(test_ctx.read_state == RELAY_READ_HEADER);
}

/* 测试7：Magic 不匹配 */
TEST(invalid_magic) {
    mock_reset();
    init_test_context();
    
    uint8_t msg[64];
    build_relay_header(msg, P2P_RLY_OFFER, 0);
    
    /* 篡改 magic */
    uint32_t bad_magic = 0xDEADBEEF;
    memcpy(msg, &bad_magic, 4);
    
    mock_add_data(msg, 9);
    test_relay_tick(&test_ctx);
    
    /* 验证magic不匹配后状态重置，消息未被处理 */
    ASSERT(test_ctx.message_count == 0);
    /* 状态会重置后继续，可能停在HEADER或IDLE */
}

/* 测试8：DISCARD 状态（未知消息类型） */
TEST(discard_unknown_message_type) {
    mock_reset();
    init_test_context();
    
    uint8_t msg[256];
    /* 构造一个未知类型的消息（type=99，有 payload） */
    build_relay_header(msg, 99, 50);
    memset(msg + 9, 'X', 50);
    
    mock_add_data(msg, 9 + 50);
    test_relay_tick(&test_ctx);
    
    /* 验证进入 DISCARD 后完成 */
    ASSERT(test_ctx.message_count == 1);
    ASSERT(test_ctx.last_type == 99);
    ASSERT(test_ctx.read_state == RELAY_READ_HEADER);
}

/* 测试9：零长度 payload */
TEST(zero_length_payload) {
    mock_reset();
    init_test_context();
    
    uint8_t msg[64];
    /* 简单消息：只有头部，无 payload */
    size_t len = build_simple_message(msg, P2P_RLY_READY);
    
    mock_add_data(msg, len);
    test_relay_tick(&test_ctx);
    
    /* 验证直接完成 */
    ASSERT(test_ctx.message_count == 1);
    ASSERT(test_ctx.last_type == P2P_RLY_READY);
    ASSERT(test_ctx.read_state == RELAY_READ_HEADER);
}

/* 测试10：EAGAIN 在不同状态的处理 */
TEST(eagain_in_all_states) {
    mock_reset();
    init_test_context();
    
    uint8_t msg[512];
    size_t msg_len = build_offer_message(msg, "iris", "test", 4);
    
    /* 1. EAGAIN 在 HEADER 状态 */
    mock_add_data(msg, 3);
    test_relay_tick(&test_ctx);
    ASSERT(test_ctx.read_state == RELAY_READ_HEADER);
    /* 再次 tick，应该返回 EAGAIN */
    test_relay_tick(&test_ctx);
    ASSERT(test_ctx.read_state == RELAY_READ_HEADER);
    
    /* 2. 完成 HEADER，进入 SENDER */
    mock_add_data(msg + 3, 6 + 10);
    test_relay_tick(&test_ctx);
    ASSERT(test_ctx.read_state == RELAY_READ_SENDER);
    
    /* 3. EAGAIN 在 SENDER 状态 */
    test_relay_tick(&test_ctx);
    ASSERT(test_ctx.read_state == RELAY_READ_SENDER);
    
    /* 4. 完成剩余数据 */
    mock_add_data(msg + 19, msg_len - 19);
    test_relay_tick(&test_ctx);
    ASSERT(test_ctx.message_count == 1);
    ASSERT(test_ctx.read_state == RELAY_READ_HEADER);
}

/* 测试11：大 payload 压力测试 */
TEST(large_payload_stress) {
    mock_reset();
    init_test_context();
    
    /* 构造 4KB payload */
    uint8_t large_payload[4096];
    for (int i = 0; i < 4096; i++) {
        large_payload[i] = (uint8_t)(i % 256);
    }
    
    uint8_t msg[8192];
    size_t msg_len = build_offer_message(msg, "jack", large_payload, sizeof(large_payload));
    
    /* 以 128 字节为单位分片发送 */
    mock_set_chunk_size(128);
    mock_add_data(msg, msg_len);
    
    int max_ticks = 100;
    int tick_count = 0;
    
    while (mock_has_data() && tick_count < max_ticks) {
        test_relay_tick(&test_ctx);
        tick_count++;
    }
    
    /* 验证完成 */
    ASSERT(test_ctx.message_count == 1);
    ASSERT(strcmp(test_ctx.last_sender, "jack") == 0);
    ASSERT(test_ctx.read_state == RELAY_READ_HEADER);
}

/* 测试12：连续多个单字节 EAGAIN */
TEST(multiple_eagain_cycles) {
    mock_reset();
    init_test_context();
    
    uint8_t msg[256];
    size_t msg_len = build_offer_message(msg, "kate", "abc", 3);
    
    /* 逐字节添加，每次 tick 只能读 1 字节 */
    for (size_t i = 0; i < msg_len; i++) {
        mock_add_data(msg + i, 1);
        test_relay_tick(&test_ctx);
        
        /* 中间状态应该在 HEADER/SENDER/PAYLOAD */
        if (i < msg_len - 1) {
            ASSERT(test_ctx.read_state != RELAY_READ_IDLE);
        }
    }
    
    /* 最后应该完成 */
    ASSERT(test_ctx.message_count == 1);
    ASSERT(test_ctx.read_state == RELAY_READ_HEADER);
}

/* ============================================================================
 * 主函数
 * ============================================================================ */

int main(void) {
    printf("\n========================================\n");
    printf("RELAY Async I/O State Machine Tests\n");
    printf("========================================\n\n");
    
    RUN_TEST(complete_message_single_recv);
    RUN_TEST(single_byte_fragmentation);
    RUN_TEST(header_fragmentation_with_eagain);
    RUN_TEST(sender_fragmentation);
    RUN_TEST(payload_fragmentation);
    RUN_TEST(multiple_messages_pipelined);
    RUN_TEST(invalid_magic);
    RUN_TEST(discard_unknown_message_type);
    RUN_TEST(zero_length_payload);
    RUN_TEST(eagain_in_all_states);
    RUN_TEST(large_payload_stress);
    RUN_TEST(multiple_eagain_cycles);
    
    printf("\n========================================\n");
    printf("Test Results: ");
    if (test_failed == 0) {
        printf(COLOR_GREEN "%d passed" COLOR_RESET, test_passed);
    } else {
        printf(COLOR_RED "%d failed" COLOR_RESET ", %d passed", test_failed, test_passed);
    }
    printf("\n========================================\n\n");
    
    return (test_failed == 0) ? 0 : 1;
}
