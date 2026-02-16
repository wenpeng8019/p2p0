
#ifndef P2P_CRYPTO_H
#define P2P_CRYPTO_H

#include <stdint.h>

void p2p_hmac_sha1(const uint8_t* key, int key_len,
                   const uint8_t* data, int data_len,
                   uint8_t* digest);
uint32_t p2p_crc32(const uint8_t *data, int len);

#endif /* P2P_CRYPTO_H */
