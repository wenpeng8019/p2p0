#ifndef P2P_TRANSPORT_H
#define P2P_TRANSPORT_H

#include <stdint.h>

// 前置声明
struct p2p_session;
struct sockaddr_in;

// 传输层操作接口 (vtable)
typedef struct p2p_transport_ops {
    const char *name;

    /* 初始化传输模块 */
    int (*init)(struct p2p_session *s);

    /* 关闭传输模块 */
    void (*close)(struct p2p_session *s);

    /* 发送应用层数据 */
    int (*send_data)(struct p2p_session *s, const void *buf, int len);

    /* 周期性驱动逻辑 */
    void (*tick)(struct p2p_session *s);

    /* 处理来自底层的 UDP 包 */
    void (*on_packet)(struct p2p_session *s, uint8_t type, const uint8_t *payload, int len, const struct sockaddr_in *from);
    int  (*is_ready)(struct p2p_session *s);
} p2p_transport_ops_t;

// 已知的传输层具体实现
// 注：reliable 作为基础传输层，直接调用 reliable_* 函数，无需 VTable
extern const p2p_transport_ops_t p2p_trans_pseudotcp; /* 拥塞控制 */
extern const p2p_transport_ops_t p2p_trans_dtls;      /* DTLS (MbedTLS) */
extern const p2p_transport_ops_t p2p_trans_openssl;   /* DTLS (OpenSSL) */
extern const p2p_transport_ops_t p2p_trans_sctp;      /* SCTP (usrsctp) */

#endif /* P2P_TRANSPORT_H */
