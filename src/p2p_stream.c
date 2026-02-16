/*
 * 流转换层（字节流 ↔ 数据包）
 *
 * ============================================================================
 * 概述
 * ============================================================================
 *
 * 本模块位于应用层（字节流 API）和可靠传输层（数据包 API）之间，负责：
 *   - 发送方向：将字节流切分为固定大小的数据包
 *   - 接收方向：将有序数据包重组为连续字节流
 *   - 可选的 Nagle 算法：合并小数据减少包数量
 *
 * ============================================================================
 * 协议栈位置
 * ============================================================================
 *
 *   ┌─────────────────────────────────────────────────────────────────┐
 *   │           Application Layer (p2p_send / p2p_recv)              │
 *   │                      字节流接口                                 │
 *   ├─────────────────────────────────────────────────────────────────┤
 *   │              Stream Layer (本模块)                              │
 *   │          分片 / 重组 / Nagle 批处理                             │
 *   ├─────────────────────────────────────────────────────────────────┤
 *   │              Reliable Layer (ARQ 重传)                         │
 *   │                    数据包接口                                   │
 *   └─────────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 * 数据包格式
 * ============================================================================
 *
 * 每个 DATA 包的有效载荷结构：
 *
 *   0                   1                   2                   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                    Stream Offset (32 bits)                   |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  | Frag Flags(8) |              Payload Data ...                |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * 字段说明：
 *   - Stream Offset (4B): 流偏移量（大端序），用于顺序验证
 *   - Frag Flags (1B):    分片标志
 *       - 0x01 = FIRST（首片）
 *       - 0x02 = LAST（末片）
 *       - 0x03 = WHOLE（完整消息，单片）
 *   - Payload Data:       应用数据
 *
 * ============================================================================
 * Nagle 算法
 * ============================================================================
 *
 * 当 nagle=1 时，小数据会在发送缓冲区累积，直到：
 *   - 累积数据量 >= P2P_STREAM_PAYLOAD（一个完整包）
 *   - 或应用显式调用 flush
 *
 * 优点：减少小包数量，提高网络效率
 * 缺点：增加延迟，不适合实时应用
 *
 * ============================================================================
 * 环形缓冲区
 * ============================================================================
 *
 * 使用环形缓冲区（ringbuf_t）管理发送和接收数据：
 *
 *     ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐
 *     │   │ D │ A │ T │ A │   │   │   │   │   │
 *     └───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘
 *           ↑               ↑
 *         head            tail
 *         (读)            (写)
 *
 *   - head: 下一个读取位置
 *   - tail: 下一个写入位置
 *   - 数据量 = (tail - head + RING_SIZE) % RING_SIZE
 *   - 空闲量 = RING_SIZE - 1 - 数据量
 */

#include "p2p_internal.h"
#include "p2p_stream.h"
#include <string.h>

/* ============================================================================
 * 环形缓冲区操作
 * ============================================================================ */

/*
 * 写入数据到环形缓冲区
 *
 * 将数据写入环形缓冲区尾部。如果空间不足，只写入能容纳的部分。
 *
 * @param r     环形缓冲区
 * @param data  待写入数据
 * @param len   数据长度
 * @return      实际写入的字节数
 */
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

/*
 * 从环形缓冲区读取数据
 *
 * 从环形缓冲区头部读取数据并移动读指针。
 *
 * @param r    环形缓冲区
 * @param buf  接收缓冲区
 * @param len  期望读取的长度
 * @return     实际读取的字节数
 */
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

/*
 * 窥视环形缓冲区数据（不移动指针）
 *
 * 类似 ring_read，但不改变读指针位置。用于预览数据。
 *
 * @param r    环形缓冲区
 * @param buf  接收缓冲区
 * @param len  期望读取的长度
 * @return     实际读取的字节数
 */
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

/*
 * 跳过环形缓冲区中的数据
 *
 * 移动读指针，丢弃指定长度的数据。
 *
 * @param r    环形缓冲区
 * @param len  要跳过的字节数
 */
void ring_skip(ringbuf_t *r, int len) {
    int avail = ring_used(r);
    if (len > avail) len = avail;
    r->head = (r->head + len) % RING_SIZE;
}

/* ============================================================================
 * 流层 API
 * ============================================================================ */

/*
 * 初始化流上下文
 *
 * @param st     流上下文
 * @param nagle  是否启用 Nagle 算法（1=启用，0=禁用）
 */
void stream_init(stream_t *st, int nagle) {
    memset(st, 0, sizeof(*st));
    st->nagle = nagle;
}

/*
 * 应用层写入字节流
 *
 * 将应用数据写入发送缓冲区。数据会在后续 flush 时发送。
 *
 * @param st   流上下文
 * @param buf  待发送数据
 * @param len  数据长度
 * @return     实际写入的字节数（可能小于 len，缓冲区满时）
 */
int stream_write(stream_t *st, const void *buf, int len) {
    int n = ring_write(&st->send_ring, buf, len);
    st->pending_bytes += n;
    return n;
}

/*
 * 应用层读取字节流
 *
 * 从接收缓冲区读取已重组的数据。
 *
 * @param st   流上下文
 * @param buf  接收缓冲区
 * @param len  缓冲区大小
 * @return     实际读取的字节数（缓冲区空时返回 0）
 */
int stream_read(stream_t *st, void *buf, int len) {
    return ring_read(&st->recv_ring, buf, len);
}

/*
 * 刷新发送缓冲区到可靠层
 *
 * 将发送缓冲区中的字节流切分为数据包，发送到可靠传输层。
 *
 * 处理流程：
 *   1. 检查缓冲区数据量
 *   2. 如果启用 Nagle 且数据不足一个包，等待更多数据
 *   3. 循环切分数据：
 *      a. 取最多 P2P_STREAM_PAYLOAD 字节
 *      b. 编码 DATA 子头（偏移量 + 分片标志）
 *      c. 发送到可靠层
 *   4. 更新流偏移量
 *
 * @param st  流上下文
 * @param r   可靠传输层上下文
 * @return    实际发送的字节数
 */
int stream_flush_to_reliable(stream_t *st, reliable_t *r) {

    int total_queued = ring_used(&st->send_ring);
    if (total_queued == 0) return 0;

    /* Nagle 算法：数据不足一个完整包时，等待累积 */
    if (st->nagle && total_queued < P2P_STREAM_PAYLOAD)
        return 0;

    int first = 1;      /* 是否为首片 */
    int flushed = 0;    /* 已发送字节数 */

    /* 循环发送，直到缓冲区空或窗口满 */
    while (ring_used(&st->send_ring) > 0 && reliable_window_avail(r) > 0) {
        int remaining = ring_used(&st->send_ring);
        int chunk = remaining;
        if (chunk > P2P_STREAM_PAYLOAD)
            chunk = P2P_STREAM_PAYLOAD;

        int is_last = (remaining <= P2P_STREAM_PAYLOAD) ? 1 : 0;

        /* 编码 DATA 子头 */
        uint8_t pkt[P2P_MAX_PAYLOAD];
        
        /* 流偏移量（4 字节，大端序） */
        uint32_t off = st->send_offset;
        pkt[0] = (uint8_t)(off >> 24);
        pkt[1] = (uint8_t)(off >> 16);
        pkt[2] = (uint8_t)(off >> 8);
        pkt[3] = (uint8_t)(off);

        /* 分片标志 */
        uint8_t fflags = 0;
        if (first)   fflags |= P2P_FRAG_FIRST;  /* 首片 */
        if (is_last) fflags |= P2P_FRAG_LAST;   /* 末片 */
        pkt[4] = fflags;

        /* 复制流数据到包体 */
        ring_read(&st->send_ring, pkt + P2P_DATA_HDR_SIZE, chunk);

        /* 发送到可靠层 */
        if (reliable_send_pkt(r, pkt, P2P_DATA_HDR_SIZE + chunk) < 0)
            break;  /* 发送窗口已满，停止发送 */

        st->send_offset += chunk;
        st->pending_bytes -= chunk;
        first = 0;
        flushed += chunk;
    }

    return flushed;
}

/*
 * 从可靠层接收数据到流缓冲区
 *
 * 从可靠传输层获取有序数据包，剥离 DATA 子头，
 * 将有效载荷写入接收环形缓冲区。
 *
 * 处理流程：
 *   1. 循环从可靠层出队数据包
 *   2. 验证包长度（至少包含 DATA 子头）
 *   3. 剥离子头，写入接收缓冲区
 *   4. 更新接收偏移量
 *
 * @param st  流上下文
 * @param r   可靠传输层上下文
 * @return    接收的总字节数
 */
int stream_feed_from_reliable(stream_t *st, reliable_t *r) {
    
    int total = 0;
    uint8_t pkt[P2P_MAX_PAYLOAD];
    int pkt_len;

    /* 循环处理所有可用的有序数据包 */
    while (reliable_recv_pkt(r, pkt, &pkt_len) == 0) {
        /* 验证最小包长度 */
        if (pkt_len < P2P_DATA_HDR_SIZE) continue;  /* 格式错误，跳过 */

        /* 
         * 解析流偏移和分片标志（预留，用于将来的顺序验证）
         * uint32_t off = (pkt[0]<<24)|(pkt[1]<<16)|(pkt[2]<<8)|pkt[3];
         * uint8_t fflags = pkt[4];
         */

        /* 提取并写入有效载荷 */
        int data_len = pkt_len - P2P_DATA_HDR_SIZE;
        if (data_len > 0) {
            int n = ring_write(&st->recv_ring, pkt + P2P_DATA_HDR_SIZE, data_len);
            total += n;
            st->recv_offset += n;
        }
    }

    return total;
}
