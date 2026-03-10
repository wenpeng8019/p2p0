/*
 * P2P 加密层接口 (DTLS Encryption Layer)
 *
 * 加密层与传输层正交：
 *   传输轴: reliable(默认) | PseudoTCP | SCTP      — 管可靠性
 *   加密轴: disabled | MbedTLS | OpenSSL            — 管加密
 *
 * 协议栈分层：
 *   方案 A（reliable / pseudotcp + DTLS）:
 *     app → stream → reliable/pseudotcp → DTLS encrypt → UDP
 *
 *   方案 B（SCTP + DTLS = WebRTC DataChannel 标准）:
 *     app → stream → SCTP → DTLS encrypt → UDP
 *
 * 加密层不关心上层用什么传输机制，只负责对出站数据报加密、对入站数据报解密。
 * 所有模块统一通过 p2p_send_packet() 发送，该函数自动处理加密和中继封装。
 */

#ifndef P2P_DTLS_H
#define P2P_DTLS_H

#include <stdint.h>

struct p2p_session;
struct sockaddr_in;

/* ============================================================================
 * 加密层操作接口（DTLS backend vtable）
 * ============================================================================ */
typedef struct p2p_dtls_ops {
    const char *name;

    /* 初始化加密层（分配 SSL 上下文等） */
    int   (*init)(struct p2p_session *s);

    /* 关闭加密层（释放 SSL 资源） */
    void  (*close)(struct p2p_session *s);

    /* 周期驱动（握手推进、DTLS 重传定时器） */
    void  (*tick)(struct p2p_session *s);

    /* 握手是否完成 */
    int   (*is_ready)(struct p2p_session *s);

    /*
     * 加密并发送一个 P2P 数据包
     *
     * 明文格式: [type|flags|seq|payload]  （内层 P2P 包头 + 负载）
     * 密文通过 p2p_send_dtls_record() 发出
     *
     * @param type/flags/seq  原始包头字段（DATA, ACK 等基础类型）
     * @param payload         原始负载
     * @param payload_len     负载长度
     * @return                发送的负载字节数, 0=暂不可发, -1=失败
     */
    int   (*encrypt_send)(struct p2p_session *s, const struct sockaddr_in *addr,
                          uint8_t type, uint8_t flags, uint16_t seq,
                          const void *payload, int payload_len);

    /*
     * 解密收到的 DTLS 记录
     *
     * @param in       收到的 DTLS 记录（密文）
     * @param in_len   密文长度
     * @param out      输出缓冲区（解密后的 [type|flags|seq|payload]）
     * @param out_cap  输出缓冲区容量
     * @return         >0=解密后长度（含内层包头）, 0=握手包（无应用数据）, -1=错误
     */
    int   (*decrypt_recv)(struct p2p_session *s, const uint8_t *in, int in_len,
                          uint8_t *out, int out_cap);
} p2p_dtls_ops_t;

/* ============================================================================
 * 加密层包类型（定义在 p2pp.h）
 *
 *   P2P_PKT_CRYPTO       直连: [P2P_HDR: type=0x22] [DTLS record]
 *   P2P_PKT_RELAY_CRYPTO 中继: [P2P_HDR: type=0xA2] [session_id(8B) | DTLS record]
 * ============================================================================ */

/* ============================================================================
 * 后端声明
 * ============================================================================ */
#ifdef WITH_DTLS
extern const p2p_dtls_ops_t p2p_dtls_mbedtls;
#endif
#ifdef WITH_OPENSSL
extern const p2p_dtls_ops_t p2p_dtls_openssl;
#endif

#endif /* P2P_DTLS_H */
