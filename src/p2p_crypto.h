
#ifndef P2P_CRYPTO_H
#define P2P_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

/* CRC32 — STUN Fingerprint 校验 */
uint32_t p2p_crc32(const uint8_t *data, int len);

/* HMAC-SHA1 — STUN Message-Integrity 签名 */
void p2p_hmac_sha1(const uint8_t* key, int key_len,
                   const uint8_t* data, int data_len,
                   uint8_t* digest);

/* Base64 — pubsub 信令 auth_key 编解码 */
int p2p_base64_encode(const uint8_t *src, size_t slen, char *dst, size_t dlen);
int p2p_base64_decode(const char *src, size_t slen, uint8_t *dst, size_t dlen);

/* DES — pubsub 信令数据加密（简化 XOR，非安全实现） */
int p2p_des_encrypt(const uint8_t *key, const uint8_t *input, size_t len, uint8_t *output);
int p2p_des_decrypt(const uint8_t *key, const uint8_t *input, size_t len, uint8_t *output);

#endif /* P2P_CRYPTO_H */
