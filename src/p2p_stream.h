
#ifndef P2P_STREAM_H
#define P2P_STREAM_H

#include <stdint.h>

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

struct stream_s {
    ringbuf_t send_ring;
    ringbuf_t recv_ring;
    uint32_t  send_offset;    /* 下一个要发送的字节偏移量 */
    uint32_t  recv_offset;    /* 下一个期望的字节偏移量 */
    int       nagle;          /* Nagle 批处理启用 */
    int       pending_bytes;  /* send_ring 中待发送的字节数 */
};

/* Forward declarations */
struct reliable_s;

void stream_init(struct stream_s *st, int nagle);
int  stream_write(struct stream_s *st, const void *buf, int len);
int  stream_read(struct stream_s *st, void *buf, int len);
int  stream_flush_to_reliable(struct stream_s *st, struct reliable_s *r);
int  stream_feed_from_reliable(struct stream_s *st, struct reliable_s *r);

///////////////////////////////////////////////////////////////////////////////

#endif /* P2P_STREAM_H */
