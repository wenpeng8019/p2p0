
#ifndef P2P_CRYPTO_EXTRA_H
#define P2P_CRYPTO_EXTRA_H

#include <stdint.h>
#include <stddef.h>

/* DES 加密 (8 字节密钥，8 字节分组) */
int p2p_des_encrypt(const uint8_t *key, const uint8_t *input, size_t len, uint8_t *output);
int p2p_des_decrypt(const uint8_t *key, const uint8_t *input, size_t len, uint8_t *output);

/* Base64 编码/解码 */
int p2p_base64_encode(const uint8_t *src, size_t slen, char *dst, size_t dlen);
int p2p_base64_decode(const char *src, size_t slen, uint8_t *dst, size_t dlen);

#endif /* P2P_CRYPTO_EXTRA_H */
