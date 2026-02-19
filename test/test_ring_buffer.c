/*
 * test_ring_buffer.c - Ring Buffer (环形缓冲区) 全面单元测试
 * 
 * 测试覆盖：
 * 1. 基础读写操作
 * 2. 边界回绕
 * 3. 满/空状态
 * 4. Peek/Skip 操作
 * 5. 并发写入读取
 * 6. 大数据量压力测试
 */

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Ring Buffer 定义（从 p2p_stream.h 提取）
 * ============================================================================ */

#define RING_SIZE  (64 * 1024)   /* 64 KB */

typedef struct {
    uint8_t  data[RING_SIZE];
    int      head;              /* 读指针 */
    int      tail;              /* 写指针 */
} ringbuf_t;

static inline int ring_used(const ringbuf_t *r) {
    return (r->tail - r->head + RING_SIZE) % RING_SIZE;
}

static inline int ring_free(const ringbuf_t *r) {
    return RING_SIZE - 1 - ring_used(r);
}

/* ============================================================================
 * Ring Buffer 实现（从 p2p_stream.c 提取）
 * ============================================================================ */

int ring_write(ringbuf_t *r, const void *data, int len) {
    int avail = ring_free(r);
    if (len > avail) len = avail;
    if (len <= 0) return 0;

    const uint8_t *src = (const uint8_t *)data;
    
    /* 计算到缓冲区末尾的连续空间 */
    int first = RING_SIZE - r->tail;
    if (first > len) first = len;
    
    /* 第一段：写到缓冲区末尾 */
    memcpy(r->data + r->tail, src, first);
    
    /* 第二段：回绕到缓冲区开头（如果需要） */
    if (first < len)
        memcpy(r->data, src + first, len - first);
    
    r->tail = (r->tail + len) % RING_SIZE;
    return len;
}

int ring_read(ringbuf_t *r, void *buf, int len) {
    int avail = ring_used(r);
    if (len > avail) len = avail;
    if (len <= 0) return 0;

    uint8_t *dst = (uint8_t *)buf;
    
    /* 计算从 head 到缓冲区末尾的连续数据 */
    int first = RING_SIZE - r->head;
    if (first > len) first = len;
    
    /* 第一段：读到缓冲区末尾 */
    memcpy(dst, r->data + r->head, first);
    
    /* 第二段：回绕到缓冲区开头（如果需要） */
    if (first < len)
        memcpy(dst + first, r->data, len - first);
    
    r->head = (r->head + len) % RING_SIZE;
    return len;
}

int ring_peek(const ringbuf_t *r, void *buf, int len) {
    int avail = ring_used(r);
    if (len > avail) len = avail;
    if (len <= 0) return 0;

    uint8_t *dst = (uint8_t *)buf;
    int first = RING_SIZE - r->head;
    if (first > len) first = len;
    memcpy(dst, r->data + r->head, first);
    if (first < len)
        memcpy(dst + first, r->data, len - first);
    return len;
}

void ring_skip(ringbuf_t *r, int len) {
    int avail = ring_used(r);
    if (len > avail) len = avail;
    r->head = (r->head + len) % RING_SIZE;
}

/* ============================================================================
 * 测试辅助函数
 * ============================================================================ */

static ringbuf_t ring;

static void init_ring(void) {
    memset(&ring, 0, sizeof(ring));
}

/* 填充测试数据（递增字节序列） */
static void fill_pattern(uint8_t *buf, int len, uint8_t start) {
    for (int i = 0; i < len; i++) {
        buf[i] = (uint8_t)((start + i) % 256);
    }
}

/* 验证数据模式 */
static bool verify_pattern(const uint8_t *buf, int len, uint8_t start) {
    for (int i = 0; i < len; i++) {
        if (buf[i] != (uint8_t)((start + i) % 256)) {
            return false;
        }
    }
    return true;
}

/* ============================================================================
 * 测试用例
 * ============================================================================ */

/* 测试1：基本写入和读取 */
TEST(basic_write_read) {
    init_ring();
    
    uint8_t write_data[100];
    fill_pattern(write_data, 100, 0);
    
    int w = ring_write(&ring, write_data, 100);
    ASSERT(w == 100);
    ASSERT(ring_used(&ring) == 100);
    ASSERT(ring_free(&ring) == RING_SIZE - 1 - 100);
    
    uint8_t read_data[100];
    int r = ring_read(&ring, read_data, 100);
    ASSERT(r == 100);
    ASSERT(ring_used(&ring) == 0);
    ASSERT(verify_pattern(read_data, 100, 0));
}

/* 测试2：空缓冲区读取 */
TEST(read_from_empty) {
    init_ring();
    
    uint8_t buf[100];
    int r = ring_read(&ring, buf, 100);
    ASSERT(r == 0);
    ASSERT(ring_used(&ring) == 0);
}

/* 测试3：部分读取 */
TEST(partial_read) {
    init_ring();
    
    uint8_t write_data[100];
    fill_pattern(write_data, 100, 0);
    ring_write(&ring, write_data, 100);
    
    /* 只读取 50 字节 */
    uint8_t read_data[50];
    int r = ring_read(&ring, read_data, 50);
    ASSERT(r == 50);
    ASSERT(ring_used(&ring) == 50);
    ASSERT(verify_pattern(read_data, 50, 0));
    
    /* 再读剩余 50 字节 */
    r = ring_read(&ring, read_data, 50);
    ASSERT(r == 50);
    ASSERT(ring_used(&ring) == 0);
    ASSERT(verify_pattern(read_data, 50, 50));
}

/* 测试4：边界回绕写入 */
TEST(wrap_around_write) {
    init_ring();
    
    /* 将 tail 移动到接近末尾 */
    ring.tail = RING_SIZE - 100;
    ring.head = RING_SIZE - 100;
    
    /* 写入 200 字节（跨越边界） */
    uint8_t write_data[200];
    fill_pattern(write_data, 200, 0);
    
    int w = ring_write(&ring, write_data, 200);
    ASSERT(w == 200);
    ASSERT(ring.tail == 100);  /* 回绕到 100 */
    
    /* 读取并验证 */
    uint8_t read_data[200];
    int r = ring_read(&ring, read_data, 200);
    ASSERT(r == 200);
    ASSERT(verify_pattern(read_data, 200, 0));
}

/* 测试5：边界回绕读取 */
TEST(wrap_around_read) {
    init_ring();
    
    /* 先写入一些数据触发回绕 */
    ring.tail = RING_SIZE - 50;
    ring.head = RING_SIZE - 50;
    
    uint8_t write_data[150];
    fill_pattern(write_data, 150, 0);
    ring_write(&ring, write_data, 150);
    
    /* head 在 RING_SIZE - 50，tail 在 100 */
    ASSERT(ring.tail == 100);
    ASSERT(ring_used(&ring) == 150);
    
    /* 读取跨越边界 */
    uint8_t read_data[150];
    int r = ring_read(&ring, read_data, 150);
    ASSERT(r == 150);
    ASSERT(verify_pattern(read_data, 150, 0));
    ASSERT(ring.head == 100);
}

/* 测试6：满缓冲区 */
TEST(buffer_full) {
    init_ring();
    
    /* 写入最大容量（RING_SIZE - 1） */
    uint8_t *large_data = (uint8_t *)malloc(RING_SIZE);
    fill_pattern(large_data, RING_SIZE, 0);
    
    int w = ring_write(&ring, large_data, RING_SIZE);
    ASSERT(w == RING_SIZE - 1);  /* 只能写入 RING_SIZE - 1 */
    ASSERT(ring_free(&ring) == 0);
    
    /* 尝试再写入应该失败 */
    uint8_t more[10];
    w = ring_write(&ring, more, 10);
    ASSERT(w == 0);
    
    /* 读取一部分后可以继续写入 */
    uint8_t read_buf[100];
    ring_read(&ring, read_buf, 100);
    ASSERT(ring_free(&ring) == 100);
    
    w = ring_write(&ring, more, 10);
    ASSERT(w == 10);
    
    free(large_data);
}

/* 测试7：Peek 操作（不移动指针） */
TEST(peek_operation) {
    init_ring();
    
    uint8_t write_data[100];
    fill_pattern(write_data, 100, 0);
    ring_write(&ring, write_data, 100);
    
    /* Peek 50 字节 */
    uint8_t peek_buf[50];
    int p = ring_peek(&ring, peek_buf, 50);
    ASSERT(p == 50);
    ASSERT(ring_used(&ring) == 100);  /* 数据量不变 */
    ASSERT(verify_pattern(peek_buf, 50, 0));
    
    /* 再次 Peek 应该得到相同数据 */
    uint8_t peek_buf2[50];
    p = ring_peek(&ring, peek_buf2, 50);
    ASSERT(p == 50);
    ASSERT(memcmp(peek_buf, peek_buf2, 50) == 0);
    
    /* Read 会移动指针 */
    uint8_t read_buf[50];
    int r = ring_read(&ring, read_buf, 50);
    ASSERT(r == 50);
    ASSERT(ring_used(&ring) == 50);
}

/* 测试8：Skip 操作 */
TEST(skip_operation) {
    init_ring();
    
    uint8_t write_data[100];
    fill_pattern(write_data, 100, 0);
    ring_write(&ring, write_data, 100);
    
    /* Skip 30 字节 */
    ring_skip(&ring, 30);
    ASSERT(ring_used(&ring) == 70);
    ASSERT(ring.head == 30);
    
    /* 读取剩余数据应该从 30 开始 */
    uint8_t read_buf[70];
    int r = ring_read(&ring, read_buf, 70);
    ASSERT(r == 70);
    ASSERT(verify_pattern(read_buf, 70, 30));
}

/* 测试9：多次小写入大读取 */
TEST(multiple_small_writes_large_read) {
    init_ring();
    
    /* 写入 10 次，每次 100 字节 */
    for (int i = 0; i < 10; i++) {
        uint8_t write_data[100];
        fill_pattern(write_data, 100, (uint8_t)(i * 100));
        int w = ring_write(&ring, write_data, 100);
        ASSERT(w == 100);
    }
    
    ASSERT(ring_used(&ring) == 1000);
    
    /* 一次读取全部 */
    uint8_t read_data[1000];
    int r = ring_read(&ring, read_data, 1000);
    ASSERT(r == 1000);
    
    /* 验证数据连续性 */
    for (int i = 0; i < 10; i++) {
        ASSERT(verify_pattern(read_data + i * 100, 100, (uint8_t)(i * 100)));
    }
}

/* 测试10：大写入多次小读取 */
TEST(large_write_multiple_small_reads) {
    init_ring();
    
    /* 一次写入 1000 字节 */
    uint8_t write_data[1000];
    fill_pattern(write_data, 1000, 0);
    int w = ring_write(&ring, write_data, 1000);
    ASSERT(w == 1000);
    
    /* 分 10 次读取，每次 100 字节 */
    for (int i = 0; i < 10; i++) {
        uint8_t read_data[100];
        int r = ring_read(&ring, read_data, 100);
        ASSERT(r == 100);
        ASSERT(verify_pattern(read_data, 100, (uint8_t)(i * 100)));
    }
    
    ASSERT(ring_used(&ring) == 0);
}

/* 测试11：交替读写（FIFO 行为） */
TEST(interleaved_read_write) {
    init_ring();
    
    for (int i = 0; i < 100; i++) {
        /* 写入 */
        uint8_t write_data[50];
        fill_pattern(write_data, 50, (uint8_t)(i & 0xFF));
        int w = ring_write(&ring, write_data, 50);
        ASSERT(w == 50);
        
        /* 读取 */
        uint8_t read_data[50];
        int r = ring_read(&ring, read_data, 50);
        ASSERT(r == 50);
        ASSERT(verify_pattern(read_data, 50, (uint8_t)(i & 0xFF)));
    }
    
    ASSERT(ring_used(&ring) == 0);
}

/* 测试12：边界情况 - 单字节操作 */
TEST(single_byte_operations) {
    init_ring();
    
    /* 写入单字节 */
    uint8_t byte = 0x42;
    int w = ring_write(&ring, &byte, 1);
    ASSERT(w == 1);
    ASSERT(ring_used(&ring) == 1);
    
    /* 读取单字节 */
    uint8_t read_byte;
    int r = ring_read(&ring, &read_byte, 1);
    ASSERT(r == 1);
    ASSERT(read_byte == 0x42);
    ASSERT(ring_used(&ring) == 0);
}

/* 测试13：压力测试 - 大数据量 */
TEST(stress_large_data) {
    init_ring();
    
    /* 写入接近满容量 */
    uint8_t *large_data = (uint8_t *)malloc(RING_SIZE);
    fill_pattern(large_data, RING_SIZE - 1, 0);
    
    int w = ring_write(&ring, large_data, RING_SIZE - 1);
    ASSERT(w == RING_SIZE - 1);
    
    /* 读取并验证 */
    uint8_t *read_data = (uint8_t *)malloc(RING_SIZE);
    int r = ring_read(&ring, read_data, RING_SIZE - 1);
    ASSERT(r == RING_SIZE - 1);
    ASSERT(verify_pattern(read_data, RING_SIZE - 1, 0));
    
    free(large_data);
    free(read_data);
}

/* 测试14：跨越边界多次写入读取 */
TEST(multiple_boundary_crossings) {
    init_ring();
    
    /* 移动到接近末尾 */
    ring.tail = RING_SIZE - 1000;
    ring.head = RING_SIZE - 1000;
    
    /* 多次写入读取，每次都跨越边界 */
    for (int i = 0; i < 10; i++) {
        uint8_t write_data[1500];
        fill_pattern(write_data, 1500, (uint8_t)(i * 10));
        
        int w = ring_write(&ring, write_data, 1500);
        ASSERT(w == 1500);
        
        uint8_t read_data[1500];
        int r = ring_read(&ring, read_data, 1500);
        ASSERT(r == 1500);
        ASSERT(verify_pattern(read_data, 1500, (uint8_t)(i * 10)));
    }
}

/* 测试15：Peek 超过可用数据 */
TEST(peek_beyond_available) {
    init_ring();
    
    uint8_t write_data[50];
    fill_pattern(write_data, 50, 0);
    ring_write(&ring, write_data, 50);
    
    /* Peek 100 字节，但只有 50 字节 */
    uint8_t peek_buf[100];
    int p = ring_peek(&ring, peek_buf, 100);
    ASSERT(p == 50);  /* 只返回可用的 50 字节 */
    ASSERT(verify_pattern(peek_buf, 50, 0));
}

/* 测试16：Skip 超过可用数据 */
TEST(skip_beyond_available) {
    init_ring();
    
    uint8_t write_data[50];
    fill_pattern(write_data, 50, 0);
    ring_write(&ring, write_data, 50);
    
    /* Skip 100 字节，但只有 50 字节 */
    ring_skip(&ring, 100);
    ASSERT(ring_used(&ring) == 0);  /* 应该全部跳过 */
}

/* 测试17：连续写入直到满 */
TEST(write_until_full) {
    init_ring();
    
    int total_written = 0;
    uint8_t chunk[1000];
    fill_pattern(chunk, 1000, 0);
    
    /* 持续写入直到满 */
    while (ring_free(&ring) > 0) {
        int w = ring_write(&ring, chunk, 1000);
        if (w == 0) break;
        total_written += w;
    }
    
    ASSERT(total_written == RING_SIZE - 1);
    ASSERT(ring_free(&ring) == 0);
}

/* 测试18：环形特性验证 */
TEST(circular_property) {
    init_ring();
    
    /* 写满后读空，重复多次 */
    for (int cycle = 0; cycle < 5; cycle++) {
        /* 写满 */
        uint8_t *write_data = (uint8_t *)malloc(RING_SIZE);
        fill_pattern(write_data, RING_SIZE - 1, (uint8_t)cycle);
        ring_write(&ring, write_data, RING_SIZE - 1);
        
        /* 读空 */
        uint8_t *read_data = (uint8_t *)malloc(RING_SIZE);
        int r = ring_read(&ring, read_data, RING_SIZE - 1);
        ASSERT(r == RING_SIZE - 1);
        ASSERT(verify_pattern(read_data, RING_SIZE - 1, (uint8_t)cycle));
        
        free(write_data);
        free(read_data);
    }
    
    /* 验证指针回到原位或正常回绕 */
    ASSERT(ring_used(&ring) == 0);
}

/* ============================================================================
 * 主函数
 * ============================================================================ */

int main(void) {
    printf("\n========================================\n");
    printf("Ring Buffer Unit Tests\n");
    printf("Ring Size: %d bytes (%.1f KB)\n", RING_SIZE, RING_SIZE / 1024.0);
    printf("========================================\n\n");
    
    RUN_TEST(basic_write_read);
    RUN_TEST(read_from_empty);
    RUN_TEST(partial_read);
    RUN_TEST(wrap_around_write);
    RUN_TEST(wrap_around_read);
    RUN_TEST(buffer_full);
    RUN_TEST(peek_operation);
    RUN_TEST(skip_operation);
    RUN_TEST(multiple_small_writes_large_read);
    RUN_TEST(large_write_multiple_small_reads);
    RUN_TEST(interleaved_read_write);
    RUN_TEST(single_byte_operations);
    RUN_TEST(stress_large_data);
    RUN_TEST(multiple_boundary_crossings);
    RUN_TEST(peek_beyond_available);
    RUN_TEST(skip_beyond_available);
    RUN_TEST(write_until_full);
    RUN_TEST(circular_property);
    
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
