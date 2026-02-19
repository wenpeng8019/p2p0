/*
 * DTLS 传输层实现（基于 MbedTLS）
 *
 * ============================================================================
 * DTLS (Datagram Transport Layer Security) 协议概述
 * ============================================================================
 *
 * DTLS 是 TLS 协议的 UDP 版本，定义于 RFC 6347 (DTLS 1.2)。
 * 在 WebRTC 中，DTLS 用于保护媒体和数据通道的传输安全。
 *
 * DTLS vs TLS 的区别：
 * ┌────────────────────┬─────────────────────┬─────────────────────┐
 * │ 特性               │ TLS                 │ DTLS                │
 * ├────────────────────┼─────────────────────┼─────────────────────┤
 * │ 传输层             │ TCP（可靠）         │ UDP（不可靠）       │
 * │ 消息边界           │ 无                  │ 保留                │
 * │ 重传机制           │ 依赖 TCP            │ 内置重传            │
 * │ 记录号             │ 隐式（顺序）        │ 显式（防重放）      │
 * │ 握手消息           │ 完整发送            │ 支持分片            │
 * │ Cookie 机制        │ 无                  │ 防 DoS 攻击         │
 * └────────────────────┴─────────────────────┴─────────────────────┘
 *
 * ============================================================================
 * DTLS 记录层格式 (Record Layer)
 * ============================================================================
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | ContentType   |    Version (major.minor)      |    Epoch      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                       Sequence Number (48 bits)               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         Length                |                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
 * |                        Fragment (加密后的数据)                |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * 头部字段（13 字节）：
 *   - ContentType (1字节):    内容类型
 *       20 = change_cipher_spec（切换密码套件）
 *       21 = alert（警告）
 *       22 = handshake（握手）
 *       23 = application_data（应用数据）
 *   - Version (2字节):        协议版本（DTLS 1.2 = 0xFEFD）
 *   - Epoch (2字节):          密钥周期（握手完成后增加）
 *   - Sequence Number (6字节): 序列号（防重放攻击）
 *   - Length (2字节):         负载长度
 *
 * ============================================================================
 * DTLS 握手流程
 * ============================================================================
 *
 *  Client                                          Server
 *    │                                               │
 *    │──────── ClientHello ────────────────────────>│
 *    │                                               │
 *    │<─────── HelloVerifyRequest (Cookie) ─────────│  ← 防 DoS
 *    │                                               │
 *    │──────── ClientHello (with Cookie) ──────────>│
 *    │                                               │
 *    │<─────── ServerHello ─────────────────────────│
 *    │<─────── Certificate (可选) ──────────────────│
 *    │<─────── ServerKeyExchange (可选) ────────────│
 *    │<─────── CertificateRequest (可选) ───────────│
 *    │<─────── ServerHelloDone ─────────────────────│
 *    │                                               │
 *    │──────── Certificate (可选) ─────────────────>│
 *    │──────── ClientKeyExchange ──────────────────>│
 *    │──────── CertificateVerify (可选) ───────────>│
 *    │──────── [ChangeCipherSpec] ─────────────────>│
 *    │──────── Finished ───────────────────────────>│
 *    │                                               │
 *    │<─────── [ChangeCipherSpec] ──────────────────│
 *    │<─────── Finished ────────────────────────────│
 *    │                                               │
 *    │═══════════ 加密应用数据传输 ═════════════════│
 *
 * ============================================================================
 * DTLS-SRTP (WebRTC 媒体加密)
 * ============================================================================
 *
 * 在 WebRTC 中，DTLS 用于密钥协商，然后派生出 SRTP 密钥：
 *
 *   DTLS 握手 → 导出密钥材料 → SRTP 加密媒体流
 *
 * 本实现用于 DataChannel 数据加密（DTLS 直接封装应用数据）。
 *
 * ============================================================================
 * PSK (Pre-Shared Key) 模式
 * ============================================================================
 *
 * 本实现支持 PSK 模式，避免证书管理的复杂性：
 *   - 双方预先共享一个密钥（auth_key）
 *   - 握手时使用 PSK 密码套件
 *   - 适合设备间直接通信场景
 *
 * ============================================================================
 * MbedTLS 库说明
 * ============================================================================
 *
 * MbedTLS 是轻量级 TLS/DTLS 库，特点：
 *   - 模块化设计，可裁剪
 *   - 内存占用小（适合嵌入式）
 *   - 支持 PSK、ECC 等多种密码套件
 *   - 完整实现 DTLS 1.0/1.2
 */

#include "p2p_internal.h"
#include "p2p_udp.h"
#include "p2p_log.h"
#include "p2p_lang.h"
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include <mbedtls/debug.h>
#include <string.h>
#include <stdlib.h>

/*
 * DTLS 调试回调
 * MbedTLS 通过此函数输出调试信息
 */
static void p2p_dtls_debug(void *ctx, int level, const char *file, int line, const char *str) {
    (void)ctx; (void)level;
    P2P_LOG_DEBUG("dtls", "[MBEDTLS] %s:%04d: %s", file, line, str);
}

/*
 * ============================================================================
 * DTLS 定时器结构
 * ============================================================================
 *
 * DTLS 需要定时器来处理重传：
 *   - int_ms: 中间超时（可选，用于检查是否需要提前处理）
 *   - fin_ms: 最终超时（必须，触发重传）
 *   - snapshot: 定时器启动时的时间戳
 *
 * 返回值约定：
 *   -1: 定时器已取消
 *    0: 未超时
 *    1: 中间超时已到
 *    2: 最终超时已到（需要重传）
 */
typedef struct {
    uint64_t snapshot;   /* 定时器启动时的时间戳 (ms) */
    uint32_t int_ms;     /* 中间超时 (毫秒) */
    uint32_t fin_ms;     /* 最终超时 (毫秒) */
} p2p_dtls_timer_t;

/*
 * 设置 DTLS 定时器
 * 当 fin_ms=0 时表示取消定时器
 */
static void p2p_dtls_set_timer(void *ctx, uint32_t int_ms, uint32_t fin_ms) {
    p2p_dtls_timer_t *timer = (p2p_dtls_timer_t *)ctx;
    timer->int_ms = int_ms;
    timer->fin_ms = fin_ms;
    if (fin_ms != 0) timer->snapshot = time_ms();
}

/*
 * 查询 DTLS 定时器状态
 */
static int p2p_dtls_get_timer(void *ctx) {
    p2p_dtls_timer_t *timer = (p2p_dtls_timer_t *)ctx;
    if (timer->fin_ms == 0) return -1;  /* 定时器已取消 */
    uint64_t elapsed = time_ms() - timer->snapshot;
    if (elapsed >= timer->fin_ms) return 2;  /* 最终超时 → 需重传 */
    if (elapsed >= timer->int_ms) return 1;  /* 中间超时 */
    return 0;  /* 未超时 */
}

/*
 * ============================================================================
 * DTLS 上下文结构
 * ============================================================================
 *
 * 包含 MbedTLS 所需的所有状态：
 *   - ssl:           SSL/TLS 会话上下文
 *   - conf:          SSL 配置（密码套件、证书等）
 *   - ctr_drbg:      随机数生成器（Counter-mode DRBG）
 *   - entropy:       熵源（用于初始化 DRBG）
 *   - timer:         重传定时器
 *   - recv_buf/len:  接收缓冲区（BIO 回调使用）
 */
typedef struct {
    mbedtls_ssl_context     ssl;         /* SSL 上下文 */
    mbedtls_ssl_config      conf;        /* SSL 配置 */
    mbedtls_ctr_drbg_context ctr_drbg;   /* 随机数生成器 */
    mbedtls_entropy_context entropy;     /* 熵源 */
    p2p_dtls_timer_t        timer;       /* 重传定时器 */
    int                     handshake_done;  /* 握手是否完成 */
    uint8_t                 recv_buf[P2P_MTU];  /* 接收缓冲区 */
    int                     recv_len;    /* 缓冲区中的数据长度 */
} p2p_dtls_ctx_t;

/*
 * ============================================================================
 * p2p_dtls_send: 自定义 BIO 发送回调
 * ============================================================================
 *
 * MbedTLS 通过此回调发送 DTLS 数据包。
 * 我们将数据封装为 P2P_PKT_DATA 类型，通过 UDP 发送。
 *
 * 数据流向：
 *   MbedTLS 加密数据 → p2p_dtls_send() → UDP 发送
 *
 * @param ctx  会话上下文 (p2p_session_t*)
 * @param buf  要发送的 DTLS 记录
 * @param len  数据长度
 * @return     发送的字节数，或 MBEDTLS_ERR_SSL_xxx 错误码
 */
static int p2p_dtls_send(void *ctx, const unsigned char *buf, size_t len) {
    p2p_session_t *s = (p2p_session_t *)ctx;
    int ret = udp_send_packet(s->sock, &s->active_addr, P2P_PKT_DATA, 0, 0, buf, (int)len);
    if (ret < 0) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    return (int)len;  /* MbedTLS 期望返回发送的字节数 */
}

/*
 * ============================================================================
 * p2p_dtls_recv: 自定义 BIO 接收回调
 * ============================================================================
 *
 * MbedTLS 通过此回调读取接收到的 DTLS 数据包。
 * 数据由 dtls_on_packet() 预先写入 recv_buf。
 *
 * 数据流向：
 *   UDP 接收 → dtls_on_packet() → recv_buf → p2p_dtls_recv() → MbedTLS 解密
 *
 * @param ctx  会话上下文 (p2p_session_t*)
 * @param buf  输出缓冲区
 * @param len  缓冲区大小
 * @return     读取的字节数，或 MBEDTLS_ERR_SSL_WANT_READ
 */
static int p2p_dtls_recv(void *ctx, unsigned char *buf, size_t len) {
    p2p_session_t *s = (p2p_session_t *)ctx;
    p2p_dtls_ctx_t *dtls = (p2p_dtls_ctx_t *)s->transport_data;
    if (!dtls || dtls->recv_len <= 0) return MBEDTLS_ERR_SSL_WANT_READ;

    size_t to_copy = (len < (size_t)dtls->recv_len) ? len : (size_t)dtls->recv_len;
    memcpy(buf, dtls->recv_buf, to_copy);
    dtls->recv_len = 0;  /* 读取后清空缓冲区 */
    return (int)to_copy;
}

/*
 * ============================================================================
 * 初始化 DTLS 传输层
 * ============================================================================
 *
 * 初始化步骤：
 *   1. 分配 DTLS 上下文
 *   2. 初始化随机数生成器（熵源 + CTR-DRBG）
 *   3. 配置 SSL（客户端/服务端模式、DTLS 传输）
 *   4. 配置认证模式（本实现使用 PSK 或跳过验证）
 *   5. 设置 BIO 回调（发送/接收）
 *   6. 设置定时器回调（重传）
 *
 * @param s  P2P 会话
 * @return   0=成功，-1=失败
 */
static int dtls_init(p2p_session_t *s) {
    p2p_dtls_ctx_t *dtls = calloc(1, sizeof(p2p_dtls_ctx_t));
    s->transport_data = dtls;
    
    /* 初始化 MbedTLS 各组件 */
    mbedtls_ssl_init(&dtls->ssl);
    mbedtls_ssl_config_init(&dtls->conf);
    mbedtls_ctr_drbg_init(&dtls->ctr_drbg);
    mbedtls_entropy_init(&dtls->entropy);

    /*
     * 初始化随机数生成器
     * CTR-DRBG (Counter-mode Deterministic Random Bit Generator)
     * 种子 "p2p" 仅用于个性化，实际随机性来自熵源
     */
    mbedtls_ctr_drbg_seed(
        &dtls->ctr_drbg,
        mbedtls_entropy_func,
        &dtls->entropy,
        (const unsigned char *)"p2p",
        3
    );
    
    /*
     * 配置 SSL：
     *   - 角色：根据 dtls_server 配置决定是客户端还是服务端
     *   - 传输：DTLS（数据报）
     *   - 预设：默认（包含常用密码套件）
     */
    mbedtls_ssl_config_defaults(&dtls->conf,
                                s->cfg.dtls_server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
                                MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                MBEDTLS_SSL_PRESET_DEFAULT);
                                
    /*
     * 认证模式：
     *   MBEDTLS_SSL_VERIFY_NONE - 不验证对端证书（使用 PSK 或自签名场景）
     *   生产环境应使用证书验证
     */
    mbedtls_ssl_conf_authmode(&dtls->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&dtls->conf, mbedtls_ctr_drbg_random, &dtls->ctr_drbg);
    
    /*
     * PSK (Pre-Shared Key) 配置
     * 
     * 如果提供了 auth_key，则使用 PSK 密码套件：
     *   - PSK: 预共享密钥本身
     *   - Identity: 密钥标识符（用于选择正确的密钥）
     *
     * PSK 优势：无需 PKI 基础设施，适合设备间直连
     */
    if (s->cfg.auth_key) {
        mbedtls_ssl_conf_psk(&dtls->conf, 
                             (const unsigned char *)s->cfg.auth_key, 
                             strlen(s->cfg.auth_key),
                             (const unsigned char *)"p2p_id",  /* PSK Identity */
                             6);
    }

    /* 调试输出配置 */
    mbedtls_ssl_conf_dbg(&dtls->conf, p2p_dtls_debug, NULL);
    mbedtls_debug_set_threshold(1);  /* 1=错误级别，减少调试噪音 */

    /*
     * DTLS Cookie 配置（仅服务端）
     *
     * Cookie 用于防止 DoS 攻击：
     *   - 客户端发送 ClientHello
     *   - 服务端返回 HelloVerifyRequest（包含 Cookie）
     *   - 客户端必须重发 ClientHello（包含 Cookie）
     *   - 只有能收到响应的客户端才能继续握手
     *
     * NULL 表示禁用 Cookie（仅用于测试，不安全！）
     */
    if (s->cfg.dtls_server) {
        mbedtls_ssl_conf_dtls_cookies(&dtls->conf, NULL, NULL, NULL);
    }

    int ret;
    if ((ret = mbedtls_ssl_setup(&dtls->ssl, &dtls->conf)) != 0) {
        P2P_LOG_ERROR("dtls", MSG(MSG_DTLS_SETUP_FAIL), -ret);
        return -1;
    }
    
    /* 设置 BIO 回调（数据发送/接收） */
    mbedtls_ssl_set_bio(&dtls->ssl, s, p2p_dtls_send, p2p_dtls_recv, NULL);
    
    /* 设置定时器回调（DTLS 重传机制） */
    mbedtls_ssl_set_timer_cb(&dtls->ssl, &dtls->timer, p2p_dtls_set_timer, p2p_dtls_get_timer);

    return 0;
}

/*
 * ============================================================================
 * 通过 DTLS 发送加密数据
 * ============================================================================
 *
 * 将应用层数据通过 DTLS 加密后发送。
 * 必须在握手完成后调用。
 *
 * @param s    P2P 会话
 * @param buf  要发送的明文数据
 * @param len  数据长度
 * @return     发送的字节数，0=需要重试，-1=失败
 */
static int dtls_send(p2p_session_t *s, const void *buf, int len) {
    p2p_dtls_ctx_t *dtls = (p2p_dtls_ctx_t *)s->transport_data;
    if (!dtls) return -1;
    
    int ret = mbedtls_ssl_write(&dtls->ssl, (const unsigned char *)buf, len);
    if (ret <= 0) {
        /* WANT_READ/WANT_WRITE 表示需要更多 I/O 操作 */
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            return 0;
        return -1;
    }
    return ret;
}

/*
 * ============================================================================
 * 周期性处理（驱动握手和重传）
 * ============================================================================
 *
 * 主要任务：
 *   1. 握手阶段：驱动握手状态机
 *   2. 握手超时：触发重传
 *
 * mbedtls_ssl_handshake() 是非阻塞的：
 *   - 返回 0: 握手成功
 *   - 返回 WANT_READ/WANT_WRITE: 等待更多数据
 *   - 其他: 错误
 */
static void dtls_tick(p2p_session_t *s) {
    p2p_dtls_ctx_t *dtls = (p2p_dtls_ctx_t *)s->transport_data;
    if (!dtls) return;

    if (!dtls->handshake_done) {
        int ret = mbedtls_ssl_handshake(&dtls->ssl);
        if (ret == 0) {
            dtls->handshake_done = 1;
            P2P_LOG_INFO("dtls", "%s", MSG(MSG_DTLS_HANDSHAKE_DONE));
        } else if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char ebuf[128];
            mbedtls_strerror(ret, ebuf, sizeof(ebuf));
            P2P_LOG_ERROR("dtls", MSG(MSG_DTLS_HANDSHAKE_FAIL), ebuf, -ret);
            s->state = P2P_STATE_ERROR;
        }
    }
}

/*
 * ============================================================================
 * 处理接收的 DTLS 数据包
 * ============================================================================
 *
 * 处理流程：
 *   1. 将数据存入 recv_buf（供 p2p_dtls_recv 回调读取）
 *   2. 如果握手已完成，调用 mbedtls_ssl_read 解密应用数据
 *   3. 如果握手未完成，调用 dtls_tick 推进握手
 *
 * @param s       P2P 会话
 * @param type    数据包类型（必须是 P2P_PKT_DATA）
 * @param payload DTLS 记录数据
 * @param len     数据长度
 * @param from    发送方地址
 */
static void dtls_on_packet(struct p2p_session *s, uint8_t type, const uint8_t *payload, int len, const struct sockaddr_in *from) {
    p2p_dtls_ctx_t *dtls = (p2p_dtls_ctx_t *)s->transport_data;
    if (!dtls || type != P2P_PKT_DATA) return;
    (void)from;

    /* 检查数据长度 */
    if (len > (int)sizeof(dtls->recv_buf)) return;
    
    /* 存入接收缓冲区 */
    memcpy(dtls->recv_buf, payload, len);
    dtls->recv_len = len;

    if (dtls->handshake_done) {
        /* 握手已完成，解密应用数据 */
        uint8_t app_buf[P2P_MTU];
        int ret = mbedtls_ssl_read(&dtls->ssl, app_buf, sizeof(app_buf));
        if (ret > 0) {
            /* 将解密后的数据写入接收环形缓冲区 */
            ring_write(&s->stream.recv_ring, app_buf, ret);
        }
    } else {
        /* 握手进行中，推进握手状态机 */
        dtls_tick(s);
    }
}

/*
 * ============================================================================
 * 关闭 DTLS 会话
 * ============================================================================
 *
 * 清理步骤：
 *   1. 发送 close_notify alert（可选，此处省略）
 *   2. 释放 SSL 上下文
 *   3. 释放配置和随机数生成器
 *   4. 释放熵源
 *   5. 释放内存
 */
static void dtls_close(p2p_session_t *s) {
    p2p_dtls_ctx_t *dtls = (p2p_dtls_ctx_t *)s->transport_data;
    if (!dtls) return;
    
    /* 可选：发送 close_notify
     * mbedtls_ssl_close_notify(&dtls->ssl);
     */
    
    mbedtls_ssl_free(&dtls->ssl);
    mbedtls_ssl_config_free(&dtls->conf);
    mbedtls_ctr_drbg_free(&dtls->ctr_drbg);
    mbedtls_entropy_free(&dtls->entropy);
    free(dtls);
    s->transport_data = NULL;
}

/*
 * ============================================================================
 * 检查 DTLS 是否可用于发送数据
 * ============================================================================
 *
 * 只有握手完成后才能发送应用数据。
 *
 * @param s  P2P 会话
 * @return   1=就绪，0=未就绪
 */
static int dtls_is_ready(p2p_session_t *s) {
    p2p_dtls_ctx_t *dtls = (p2p_dtls_ctx_t *)s->transport_data;
    return dtls && dtls->handshake_done;
}

/*
 * ============================================================================
 * 传输层操作表
 * ============================================================================
 *
 * 注册为 P2P 传输层实现。
 *
 * 与其他传输层对比：
 * ┌─────────────────────┬───────────┬───────────┬─────────────┐
 * │ 传输层              │ 可靠性    │ 加密      │ 适用场景    │
 * ├─────────────────────┼───────────┼───────────┼─────────────┤
 * │ p2p_trans_compact    │ 无        │ 无        │ 低延迟      │
 * │ p2p_trans_reliable  │ ARQ       │ 无        │ 简单可靠    │
 * │ p2p_trans_pseudotcp │ TCP风格   │ 无        │ 拥塞控制    │
 * │ p2p_trans_sctp      │ SCTP      │ 无        │ 多流        │
 * │ p2p_trans_dtls      │ 无        │ TLS       │ 安全传输    │
 * └─────────────────────┴───────────┴───────────┴─────────────┘
 *
 * 注意：DTLS 本身不提供可靠性，如需可靠传输应与 SCTP 组合使用。
 */
const p2p_transport_ops_t p2p_trans_dtls = {
    .name      = "DTLS-MbedTLS",   /* 传输层名称 */
    .init      = dtls_init,        /* 初始化 */
    .tick      = dtls_tick,        /* 周期处理 */
    .send_data = dtls_send,        /* 发送数据 */
    .on_packet = dtls_on_packet,   /* 接收处理 */
    .is_ready  = dtls_is_ready,    /* 就绪检查 */
    .close     = dtls_close        /* 关闭清理 */
};
