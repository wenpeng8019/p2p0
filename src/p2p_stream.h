
#ifndef P2P_STREAM_H
#define P2P_STREAM_H

#include <stdint.h>
#include "p2p_udp.h"           /* P2P_MAX_PAYLOAD */

/* ============================================================================
 * 流层分片常量
 * ============================================================================ */

/* DATA 子包头 (5 bytes, 作为负载数据的一部分) */
#define P2P_FRAG_FIRST          0x01
#define P2P_FRAG_LAST           0x02
#define P2P_FRAG_WHOLE          0x03                // FIRST | LAST

typedef struct {
    uint32_t stream_offset;                         // 网络字节序
    uint8_t  frag_flags;
} p2p_data_hdr_t;

#define P2P_DATA_HDR_SIZE       5
#define P2P_STREAM_PAYLOAD      (P2P_MAX_PAYLOAD - P2P_DATA_HDR_SIZE)  /* 1191 */

///////////////////////////////////////////////////////////////////////////////

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

int  ring_write(ringbuf_t *r, const void *data, int len);
int  ring_read(ringbuf_t *r, void *buf, int len);
int  ring_peek(const ringbuf_t *r, void *buf, int len);
void ring_skip(ringbuf_t *r, int len);

///////////////////////////////////////////////////////////////////////////////

typedef struct stream {
    ringbuf_t send_ring;
    ringbuf_t recv_ring;
    uint32_t  send_offset;    /* 下一个要发送的字节偏移量 */
    uint32_t  recv_offset;    /* 下一个期望的字节偏移量 */
    int       nagle;          /* Nagle 批处理启用 */
    int       pending_bytes;  /* send_ring 中待发送的字节数 */
} stream_t;

/* Forward declarations */
struct reliable;

void stream_init(struct stream *st, int nagle);
int  stream_write(struct stream *st, const void *buf, int len);
int  stream_read(struct stream *st, void *buf, int len);
int  stream_flush_to_reliable(struct stream *st, struct reliable *r);
int  stream_feed_from_reliable(struct stream *st, struct reliable *r);

///////////////////////////////////////////////////////////////////////////////

#endif /* P2P_STREAM_H */
