/*
 * 加密工具函数（DES 加密、Base64 编解码）
 *
 * ============================================================================
 * 概述
 * ============================================================================
 *
 * 本模块提供 P2P 信令加密所需的基础加密功能：
 *   - Base64 编解码：用于将二进制数据转换为可打印字符串
 *   - DES 加密/解密：用于保护信令数据的机密性
 *
 * ============================================================================
 * 安全警告
 * ============================================================================
 *
 * DES 算法已被认为是不安全的（密钥长度仅 56 位）
 * 本实现使用简化的 XOR 密码（仅用于演示和兼容性）
 * 生产环境应使用 AES-256 或 ChaCha20-Poly1305
 *
 * 如需真正的安全性：
 *   - 编译时启用 WITH_DTLS=1 使用 MbedTLS
 *   - 或使用 OpenSSL 的 EVP 接口
 *
 * ============================================================================
 * Base64 编码原理
 * ============================================================================
 *
 * Base64 将 3 字节（24 位）的二进制数据转换为 4 个可打印字符（每个 6 位）。
 *
 *  原始字节:  |    字节1    |    字节2    |    字节3    |
 *             | 8 bits      | 8 bits      | 8 bits      |
 *             |                24 bits                  |
 *                             ↓
 *  Base64:    | 6 bits | 6 bits | 6 bits | 6 bits |
 *             | char1  | char2  | char3  | char4  |
 *
 * 字符映射表：
 *   0-25  → A-Z
 *   26-51 → a-z
 *   52-61 → 0-9
 *   62    → +
 *   63    → /
 *   填充  → =
 *
 * 填充规则：
 *   - 输入长度 % 3 == 1 → 输出末尾添加 "=="
 *   - 输入长度 % 3 == 2 → 输出末尾添加 "="
 *   - 输入长度 % 3 == 0 → 无填充
 *
 * 输出长度计算：
 *   output_len = ceil(input_len / 3) * 4
 */

#include "p2p_crypto_extra.h"
#include <string.h>

/* ============================= Base64 实现 ============================= */

/*
 * Base64 字符映射表（RFC 4648 标准）
 * 索引 0-63 对应 64 个可打印字符
 */
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*
 * ============================================================================
 * Base64 编码
 * ============================================================================
 *
 * 将二进制数据编码为 Base64 字符串。
 *
 * 编码过程：
 *   1. 每 3 字节为一组
 *   2. 将 24 位拆分为 4 个 6 位组
 *   3. 每个 6 位组映射到 base64_table 中的字符
 *   4. 不足 3 字节时使用 '=' 填充
 *
 * @param src   输入：二进制数据
 * @param slen  输入数据长度
 * @param dst   输出：Base64 字符串（含 NUL 结尾）
 * @param dlen  输出缓冲区大小
 * @return      编码后的字符串长度（不含 NUL），-1=失败
 */
int p2p_base64_encode(const uint8_t *src, size_t slen, char *dst, size_t dlen) {
    size_t i, j = 0;
    uint32_t triple;  /* 存储 3 字节组成的 24 位值 */
    
    /* 检查缓冲区是否足够：编码后长度 = (slen + 2) / 3 * 4 */
    if (dlen < ((slen + 2) / 3) * 4 + 1) return -1;
    
    /*
     * 处理完整的 3 字节组
     * 每 3 字节生成 4 个 Base64 字符
     */
    for (i = 0; i < slen - slen % 3; i += 3) {
        /* 将 3 字节合并为 24 位整数 */
        triple = (src[i] << 16) | (src[i + 1] << 8) | src[i + 2];
        
        /* 拆分为 4 个 6 位组并映射 */
        dst[j++] = base64_table[(triple >> 18) & 0x3F];  /* 高 6 位 */
        dst[j++] = base64_table[(triple >> 12) & 0x3F];
        dst[j++] = base64_table[(triple >> 6) & 0x3F];
        dst[j++] = base64_table[triple & 0x3F];          /* 低 6 位 */
    }
    
    /*
     * 处理剩余字节（填充处理）
     */
    if (slen % 3 == 1) {
        /* 剩余 1 字节 → 2 个有效字符 + 2 个 '=' */
        triple = src[i] << 16;
        dst[j++] = base64_table[(triple >> 18) & 0x3F];
        dst[j++] = base64_table[(triple >> 12) & 0x3F];
        dst[j++] = '=';
        dst[j++] = '=';
    } else if (slen % 3 == 2) {
        /* 剩余 2 字节 → 3 个有效字符 + 1 个 '=' */
        triple = (src[i] << 16) | (src[i + 1] << 8);
        dst[j++] = base64_table[(triple >> 18) & 0x3F];
        dst[j++] = base64_table[(triple >> 12) & 0x3F];
        dst[j++] = base64_table[(triple >> 6) & 0x3F];
        dst[j++] = '=';
    }
    
    dst[j] = '\0';  /* NUL 结尾 */
    return j;
}

/*
 * 单字符解码
 * 
 * 将 Base64 字符转换为 0-63 的整数值
 *
 * @param c  Base64 字符
 * @return   0-63 的值，-1=无效字符
 */
static int base64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';       /* A-Z → 0-25 */
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;  /* a-z → 26-51 */
    if (c >= '0' && c <= '9') return c - '0' + 52;  /* 0-9 → 52-61 */
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;  /* 无效字符 */
}

/*
 * ============================================================================
 * Base64 解码
 * ============================================================================
 *
 * 将 Base64 字符串解码为二进制数据。
 *
 * 解码过程：
 *   1. 每 4 个字符为一组
 *   2. 将 4 个 6 位值组合为 3 字节
 *   3. 处理 '=' 填充（减少输出字节数）
 *
 * @param src   输入：Base64 字符串
 * @param slen  输入字符串长度
 * @param dst   输出：二进制数据
 * @param dlen  输出缓冲区大小
 * @return      解码后的数据长度，-1=失败
 */
int p2p_base64_decode(const char *src, size_t slen, uint8_t *dst, size_t dlen) {
    size_t i = 0, j = 0;
    int values[4];  /* 存储 4 个字符对应的 6 位值 */
    
    while (i < slen) {
        /* 读取 4 个字符 */
        int k = 0;
        while (k < 4 && i < slen) {
            if (src[i] == '=') {
                values[k++] = 0;  /* 填充字符视为 0 */
            } else {
                values[k++] = base64_decode_char(src[i]);
            }
            i++;
        }
        
        if (k < 4) break;  /* 不完整的块，结束解码 */
        
        /* 检查输出缓冲区空间 */
        if (j + 3 > dlen) return -1;
        
        /*
         * 将 4 个 6 位值重组为 3 字节
         *
         *   values[0]  values[1]  values[2]  values[3]
         *   | 6 bits | | 6 bits | | 6 bits | | 6 bits |
         *        ↓          ↓          ↓          ↓
         *   |  byte1   |  byte2   |  byte3   |
         */
        dst[j++] = (values[0] << 2) | (values[1] >> 4);
        if (src[i - 2] != '=') dst[j++] = (values[1] << 4) | (values[2] >> 2);
        if (src[i - 1] != '=') dst[j++] = (values[2] << 6) | values[3];
    }
    
    return j;
}

/* ============================= DES 加密实现 ============================= */

/*
 * ============================================================================
 * DES (Data Encryption Standard) 说明
 * ============================================================================
 *
 * DES 是一种对称分组加密算法：
 *   - 密钥长度：64 位（实际有效 56 位，8 位用于奇偶校验）
 *   - 分组大小：64 位（8 字节）
 *   - 结构：16 轮 Feistel 网络
 *
 * 工作模式：
 *   - ECB (Electronic Codebook): 每块独立加密（不安全，有模式泄露）
 *   - CBC (Cipher Block Chaining): 块间链接（推荐）
 *   - CTR (Counter): 流模式（适合并行）
 *
 * 当前实现：简化的 XOR 密码（仅用于演示）
 *
 * 真正的 DES 实现包括：
 *   - 初始置换 (IP)
 *   - 16 轮 Feistel 函数（扩展、S-盒替换、P-盒置换）
 *   - 最终置换 (IP^-1)
 *   - 密钥调度算法
 *
 * 本实现使用简单的 XOR 操作代替复杂的 DES 算法，
 * 原因是完整 DES 实现需要约 500+ 行代码和多个置换表。
 * 如需安全性，请使用 MbedTLS 或 OpenSSL。
 */

/*
 * ============================================================================
 * DES 加密（简化版）
 * ============================================================================
 *
 * 警告：这是简化实现，使用 XOR 密码而非真正的 DES！
 * 仅用于演示和兼容性测试。
 *
 * 简化算法：
 *   output[i] = input[i] XOR key[i % 8]
 *
 * 这种方式的问题：
 *   - 容易被频率分析攻击
 *   - 相同明文产生相同密文
 *   - 密钥可通过已知明文恢复
 *
 * @param key     8 字节密钥
 * @param input   输入明文
 * @param len     数据长度（应为 8 的倍数）
 * @param output  输出密文
 * @return        加密后的数据长度
 */
int p2p_des_encrypt(const uint8_t *key, const uint8_t *input, size_t len, uint8_t *output) {
    /* 简单 XOR 密码（非安全实现） */
    for (size_t i = 0; i < len; i++) {
        output[i] = input[i] ^ key[i % 8];
    }
    return (int)len;
}

/*
 * ============================================================================
 * DES 解密（简化版）
 * ============================================================================
 *
 * XOR 密码是对称的，加密和解密使用相同的操作。
 *
 * @param key     8 字节密钥
 * @param input   输入密文
 * @param len     数据长度
 * @param output  输出明文
 * @return        解密后的数据长度
 */
int p2p_des_decrypt(const uint8_t *key, const uint8_t *input, size_t len, uint8_t *output) {
    /* XOR 是对称操作，直接调用加密函数 */
    return p2p_des_encrypt(key, input, len, output);
}
