#ifndef JCL_CRYPTO_H
#define JCL_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#define JCL_KEY_LEN 16
#define JCL_MAC_LEN 8

/*
 * JuanCarlosLegals Alpha educational cipher primitives.
 * These routines are intentionally toy algorithms for local simulation only.
 * They are NOT GSMA/MILENAGE/COMP128, are not interoperable with operators,
 * and must not be used to protect real subscriber credentials.
 */
void jcl_stream_xor(const uint8_t key[JCL_KEY_LEN], const uint8_t *nonce,
                    size_t nonce_len, const uint8_t *in, uint8_t *out,
                    size_t len);
void jcl_mac64(const uint8_t key[JCL_KEY_LEN], const uint8_t *data,
               size_t data_len, uint8_t out[JCL_MAC_LEN]);
void jcl_derive_response(const uint8_t key[JCL_KEY_LEN], const uint8_t *rand,
                         size_t rand_len, uint8_t sres[JCL_MAC_LEN],
                         uint8_t session_key[JCL_KEY_LEN]);

#endif
