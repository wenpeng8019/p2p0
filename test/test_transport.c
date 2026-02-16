/*
 * test_transport.c - 传输层单元测试
 * 
 * 测试策略：
 * 1. 模拟 UDP 包队列（发送/接收）
 * 2. 创建虚拟 session，不依赖真实 socket
 * 3. 测试各传输层的 send_data / on_packet / tick 逻辑
 */

#include "test_framework.h"
#include "../src/p2p_internal.h"
#include "../src/p2p_stream.h"
#include "../src/p2p_udp.h"
#include <sys/socket.h>
#include <netinet/in.h>

/* ============================================================================
 * 模拟 UDP 包队列
 * ============================================================================ */

#define MAX_MOCK_PACKETS 100

typedef struct {
    uint8_t data[2048];
    int len;
    struct sockaddr_in from;
    uint64_t timestamp;
} mock_packet_t;

static mock_packet_t mock_send_queue[MAX_MOCK_PACKETS];
static int mock_send_count = 0;

static mock_packet_t mock_recv_queue[MAX_MOCK_PACKETS];
static int mock_recv_count = 0;

/* 重定向 sendto，捕获发送的包 */
static int mock_sock = 999;  // 虚拟 socket fd

/* 清空队列 */
void mock_reset(void) {
    mock_send_count = 0;
    mock_recv_count = 0;
}

/* 模拟发送：捕获包到发送队列 */
void mock_capture_send(int sock, const uint8_t *buf, int len, const struct sockaddr_in *to) {
    (void)sock;
    (void)to;
    if (mock_send_count >= MAX_MOCK_PACKETS) return;
    
    mock_packet_t *pkt = &mock_send_queue[mock_send_count++];
    memcpy(pkt->data, buf, len);
    pkt->len = len;
    pkt->timestamp = time_ms();
}

/* 模拟接收：从接收队列拿包 */
int mock_recv_packet(uint8_t *buf, int buf_size, struct sockaddr_in *from) {
    if (mock_recv_count == 0) return 0;
    
    mock_packet_t *pkt = &mock_recv_queue[0];
    int len = pkt->len < buf_size ? pkt->len : buf_size;
    memcpy(buf, pkt->data, len);
    if (from) *from = pkt->from;
    
    // 移除队首
    memmove(&mock_recv_queue[0], &mock_recv_queue[1], 
            (mock_recv_count - 1) * sizeof(mock_packet_t));
    mock_recv_count--;
    
    return len;
}

/* 将发送队列的包转移到接收队列（模拟网络传输） */
void mock_transfer_packets(void) {
    for (int i = 0; i < mock_send_count && mock_recv_count < MAX_MOCK_PACKETS; i++) {
        mock_recv_queue[mock_recv_count++] = mock_send_queue[i];
    }
    mock_send_count = 0;
}

/* ============================================================================
 * 测试辅助函数
 * ============================================================================ */

/* 创建虚拟 session */
p2p_session_t *create_mock_session(void) {
    p2p_session_t *s = calloc(1, sizeof(p2p_session_t));
    s->sock = mock_sock;
    s->state = P2P_STATE_CONNECTED;
    
    // 初始化 stream (nagle=0)
    stream_init(&s->stream, 0);
    
    // 初始化 reliable
    memset(&s->reliable, 0, sizeof(reliable_t));
    s->reliable.rto = RELIABLE_RTO_INIT;
    
    return s;
}

void destroy_mock_session(p2p_session_t *s) {
    free(s);
}

/* ============================================================================
 * Reliable 传输层测试
 * ============================================================================ */

TEST(reliable_send_recv) {
    mock_reset();
    p2p_session_t *s = create_mock_session();
    
    // 发送数据
    const char *test_data = "Hello, Reliable!";
    int len = strlen(test_data);
    
    // 写入 stream
    stream_write(&s->stream, test_data, len);
    
    // Flush 到 reliable 层
    int flushed = stream_flush_to_reliable(&s->stream, &s->reliable);
    
    // 验证数据被发送到 reliable 层
    ASSERT(flushed > 0);
    ASSERT_EQ(s->reliable.send_count, 1);
    ASSERT_EQ(s->reliable.send_seq, 1);
    ASSERT_EQ(s->reliable.send_buf[0].seq, 0);
    
    // 测试接收方向：模拟收到数据包
    uint8_t pkt[100];
    memcpy(pkt + 5, test_data, len);  // 跳过 DATA 头（5字节）
    reliable_on_data(&s->reliable, 0, pkt, len + 5);
    
    // 验证数据进入接收缓冲区
    ASSERT_EQ(s->reliable.recv_bitmap[0], 1);
    
    // 从 reliable 读取数据
    uint8_t recv_buf[100];
    int recv_len;
    int ret = reliable_recv_pkt(&s->reliable, recv_buf, &recv_len);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(recv_len, len + 5);
    
    destroy_mock_session(s);
}

TEST(reliable_window_full) {
    mock_reset();
    p2p_session_t *s = create_mock_session();
    
    // 填满发送窗口
    uint8_t data[100];
    for (int i = 0; i < RELIABLE_WINDOW; i++) {
        int ret = reliable_send_pkt(&s->reliable, data, sizeof(data));
        ASSERT_EQ(ret, 0);
    }
    
    // 窗口已满，再发送应该失败
    int ret = reliable_send_pkt(&s->reliable, data, sizeof(data));
    ASSERT_EQ(ret, -1);
    
    // 验证窗口计数
    ASSERT_EQ(s->reliable.send_count, RELIABLE_WINDOW);
    ASSERT_EQ(reliable_window_avail(&s->reliable), 0);
    
    destroy_mock_session(s);
}

TEST(reliable_recv_order) {
    mock_reset();
    p2p_session_t *s = create_mock_session();
    
    // 模拟接收乱序的包
    uint8_t pkt1[20], pkt2[20], pkt3[20];
    memcpy(pkt1, "Packet 1", 9);
    memcpy(pkt2, "Packet 2", 9);
    memcpy(pkt3, "Packet 3", 9);
    
    // 先收到 seq=1 和 seq=2，seq=0 丢失
    reliable_on_data(&s->reliable, 1, pkt2, 9);
    reliable_on_data(&s->reliable, 2, pkt3, 9);
    
    // 尝试读取，应该读不到（等待 seq=0）
    uint8_t buf[100];
    int len;
    int ret = reliable_recv_pkt(&s->reliable, buf, &len);
    ASSERT_EQ(ret, -1);  // 没有按序的包
    
    // 收到 seq=0
    reliable_on_data(&s->reliable, 0, pkt1, 9);
    
    // 现在应该能按序读出 3 个包
    ret = reliable_recv_pkt(&s->reliable, buf, &len);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(memcmp(buf, "Packet 1", 8), 0);
    
    ret = reliable_recv_pkt(&s->reliable, buf, &len);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(memcmp(buf, "Packet 2", 8), 0);
    
    ret = reliable_recv_pkt(&s->reliable, buf, &len);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(memcmp(buf, "Packet 3", 8), 0);
    
    destroy_mock_session(s);
}

/* ============================================================================
 * Stream 层测试
 * ============================================================================ */

TEST(stream_write_read) {
    stream_t stream;
    stream_init(&stream, 0);  // nagle=0
    
    // 写入数据到发送缓冲区
    const char *data = "Hello, Stream!";
    int len = strlen(data);
    int written = stream_write(&stream, data, len);
    
    ASSERT_EQ(written, len);
    
    // stream_write 写入 send_ring
    // 我们需要手动模拟数据到 recv_ring 才能读取
    ring_write(&stream.recv_ring, data, len);
    
    // 读取数据
    char buf[100];
    int read = stream_read(&stream, buf, sizeof(buf));
    
    ASSERT_EQ(read, len);
    ASSERT_EQ(memcmp(buf, data, len), 0);
}

TEST(stream_ring_buffer_wrap) {
    stream_t stream;
    stream_init(&stream, 0);  // nagle=0
    
    // 写入接近满的数据到 recv_ring（模拟接收）
    char large_data[RING_SIZE * 2];
    memset(large_data, 'A', sizeof(large_data));
    
    int written = ring_write(&stream.recv_ring, large_data, RING_SIZE - 100);
    ASSERT(written > 0);
    
    // 读取一半
    char buf[RING_SIZE];
    int read = stream_read(&stream, buf, written / 2);
    ASSERT_EQ(read, written / 2);
    
    // 再写入，测试环形缓冲区回绕
    written = ring_write(&stream.recv_ring, large_data, 200);
    ASSERT(written > 0);
}

TEST(stream_flush_fragmentation) {
    mock_reset();
    p2p_session_t *s = create_mock_session();
    
    // 写入大于一个包的数据
    char large_data[P2P_STREAM_PAYLOAD * 2 + 100];
    memset(large_data, 'X', sizeof(large_data));
    
    stream_write(&s->stream, large_data, sizeof(large_data));
    
    // Flush 到 reliable
    int flushed = stream_flush_to_reliable(&s->stream, &s->reliable);
    
    ASSERT(flushed > 0);
    
    // 应该分片成多个包
    ASSERT(s->reliable.send_count >= 3);  // 至少 3 个包
    
    destroy_mock_session(s);
}

/* 边界测试：空数据 */
TEST(stream_empty_data) {
    stream_t stream;
    stream_init(&stream, 0);
    
    // 写入 0 字节
    int written = stream_write(&stream, "", 0);
    ASSERT_EQ(written, 0);
    
    // 读取应该返回 0
    char buf[10];
    int read = stream_read(&stream, buf, sizeof(buf));
    ASSERT_EQ(read, 0);
}

/* 边界测试：单字节 */
TEST(stream_single_byte) {
    stream_t stream;
    stream_init(&stream, 0);
    
    // 写入 1 字节
    char data = 'A';
    ring_write(&stream.recv_ring, &data, 1);
    
    // 读取
    char buf;
    int read = stream_read(&stream, &buf, 1);
    ASSERT_EQ(read, 1);
    ASSERT_EQ(buf, 'A');
}

/* 边界测试：正好一个包大小 */
TEST(stream_exact_packet_size) {
    mock_reset();
    p2p_session_t *s = create_mock_session();
    
    // 写入正好 P2P_STREAM_PAYLOAD 字节
    char data[P2P_STREAM_PAYLOAD];
    memset(data, 'B', sizeof(data));
    
    stream_write(&s->stream, data, sizeof(data));
    int flushed = stream_flush_to_reliable(&s->stream, &s->reliable);
    
    // 应该生成 1 个包
    ASSERT(flushed == P2P_STREAM_PAYLOAD);
    ASSERT_EQ(s->reliable.send_count, 1);
    
    destroy_mock_session(s);
}

/* 边界测试：环形缓冲区完全填满 */
TEST(ring_buffer_full) {
    stream_t stream;
    stream_init(&stream, 0);
    
    // 填满整个环形缓冲区（留一个空位）
    char data[RING_SIZE];
    memset(data, 'C', sizeof(data));
    
    int written = ring_write(&stream.send_ring, data, RING_SIZE - 1);
    ASSERT_EQ(written, RING_SIZE - 1);
    
    // 再写应该失败
    written = ring_write(&stream.send_ring, "X", 1);
    ASSERT_EQ(written, 0);
    
    // 读取一些数据
    char buf[100];
    int read = ring_read(&stream.send_ring, buf, 100);
    ASSERT_EQ(read, 100);
    
    // 现在应该能写入
    written = ring_write(&stream.send_ring, "YZ", 2);
    ASSERT_EQ(written, 2);
}

/* 边界测试：超大数据（多个窗口） */
TEST(reliable_large_data) {
    mock_reset();
    p2p_session_t *s = create_mock_session();
    
    // 写入超过窗口大小的数据
    // RELIABLE_WINDOW = 32, P2P_STREAM_PAYLOAD ≈ 1191
    int total_size = RELIABLE_WINDOW * P2P_STREAM_PAYLOAD + 500;
    char *large_data = malloc(total_size);
    memset(large_data, 'D', total_size);
    
    stream_write(&s->stream, large_data, total_size);
    
    // Flush，应该只发送窗口大小的数据
    int flushed = stream_flush_to_reliable(&s->stream, &s->reliable);
    
    // 窗口满了，无法全部发送
    ASSERT(flushed > 0);
    ASSERT_EQ(s->reliable.send_count, RELIABLE_WINDOW);
    ASSERT(flushed < total_size);
    
    free(large_data);
    destroy_mock_session(s);
}

/* 边界测试：最大包大小 */
TEST(reliable_max_payload) {
    mock_reset();
    p2p_session_t *s = create_mock_session();
    
    // 发送最大允许的包
    uint8_t data[P2P_MAX_PAYLOAD];
    memset(data, 'E', sizeof(data));
    
    int ret = reliable_send_pkt(&s->reliable, data, sizeof(data));
    ASSERT_EQ(ret, 0);
    
    // 超过最大大小应该失败
    uint8_t oversized[P2P_MAX_PAYLOAD + 1];
    ret = reliable_send_pkt(&s->reliable, oversized, sizeof(oversized));
    ASSERT_EQ(ret, -1);
    
    destroy_mock_session(s);
}

/* 边界测试：环形缓冲区跨越边界读写 */
TEST(ring_buffer_boundary_cross) {
    stream_t stream;
    stream_init(&stream, 0);
    
    // 写入大部分缓冲区
    char data[RING_SIZE - 50];
    memset(data, 'F', sizeof(data));
    ring_write(&stream.recv_ring, data, sizeof(data));
    
    // 读取大部分（使 head 接近末尾）
    char buf[RING_SIZE - 100];
    ring_read(&stream.recv_ring, buf, sizeof(buf));
    
    // 读取剩余的 50 字节，清空缓冲区
    char remaining[50];
    int rem_read = ring_read(&stream.recv_ring, remaining, sizeof(remaining));
    ASSERT_EQ(rem_read, 50);
    
    // 现在缓冲区为空，head 和 tail 都在 RING_SIZE - 50 附近
    // 写入会跨越边界
    char wrap_data[200];
    memset(wrap_data, 'G', sizeof(wrap_data));
    int written = ring_write(&stream.recv_ring, wrap_data, sizeof(wrap_data));
    ASSERT_EQ(written, 200);
    
    // 读取跨越边界的数据
    char read_buf[200];
    int read = ring_read(&stream.recv_ring, read_buf, sizeof(read_buf));
    ASSERT_EQ(read, 200);
    ASSERT_EQ(memcmp(read_buf, wrap_data, read), 0);
}

/* ============================================================================
 * PseudoTCP 测试
 * ============================================================================ */

TEST(pseudotcp_congestion_window) {
    mock_reset();
    p2p_session_t *s = create_mock_session();
    
    // 初始化 PseudoTCP
    s->cfg.use_pseudotcp = 1;
    s->trans = &p2p_trans_pseudotcp;
    s->trans->init(s);
    
    // 验证初始拥塞窗口
    ASSERT(s->tcp.cwnd > 0);
    ASSERT_EQ(s->tcp.ssthresh, 65535);
    ASSERT_EQ(s->tcp.dup_acks, 0);
    
    // 模拟收到 ACK，拥塞窗口应该增长
    uint32_t initial_cwnd = s->tcp.cwnd;
    p2p_pseudotcp_on_ack(s, 0);
    
    ASSERT(s->tcp.cwnd > initial_cwnd);
    
    destroy_mock_session(s);
}

/* ============================================================================
 * 主函数
 * ============================================================================ */

int main(void) {
    printf("\n========================================\n");
    printf("P2P Transport Layer Unit Tests\n");
    printf("========================================\n\n");
    
    printf("Stream Layer Tests:\n");
    RUN_TEST(stream_write_read);
    RUN_TEST(stream_ring_buffer_wrap);
    RUN_TEST(stream_flush_fragmentation);
    
    printf("\nBoundary Tests:\n");
    RUN_TEST(stream_empty_data);
    RUN_TEST(stream_single_byte);
    RUN_TEST(stream_exact_packet_size);
    RUN_TEST(ring_buffer_full);
    RUN_TEST(ring_buffer_boundary_cross);
    RUN_TEST(reliable_large_data);
    RUN_TEST(reliable_max_payload);
    
    printf("\nReliable Layer Tests:\n");
    RUN_TEST(reliable_send_recv);
    RUN_TEST(reliable_window_full);
    RUN_TEST(reliable_recv_order);
    
    printf("\nPseudoTCP Layer Tests:\n");
    RUN_TEST(pseudotcp_congestion_window);
    
    TEST_SUMMARY();
    
    return (test_failed > 0) ? 1 : 0;
}
