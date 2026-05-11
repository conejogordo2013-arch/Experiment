#include "jcl_crypto.h"

static uint32_t rotl32(uint32_t value, unsigned shift) {
    return (value << shift) | (value >> (32U - shift));
}

static uint32_t load_key_word(const uint8_t key[JCL_KEY_LEN], size_t offset) {
    return ((uint32_t)key[offset] << 24) | ((uint32_t)key[offset + 1] << 16) |
           ((uint32_t)key[offset + 2] << 8) | (uint32_t)key[offset + 3];
}

void jcl_stream_xor(const uint8_t key[JCL_KEY_LEN], const uint8_t *nonce,
                    size_t nonce_len, const uint8_t *in, uint8_t *out,
                    size_t len) {
    uint32_t state = 0x4a434c41U ^ load_key_word(key, 0);

    for (size_t i = 0; i < nonce_len; ++i) {
        state ^= (uint32_t)nonce[i] << ((i % 4U) * 8U);
        state = rotl32(state + 0x9e3779b9U + load_key_word(key, (i % 4U) * 4U), 7);
    }

    for (size_t i = 0; i < len; ++i) {
        state ^= load_key_word(key, (i % 4U) * 4U) + (uint32_t)i;
        state = rotl32(state * 1664525U + 1013904223U, 11);
        out[i] = in[i] ^ (uint8_t)(state >> ((i % 4U) * 8U));
    }
}

void jcl_mac64(const uint8_t key[JCL_KEY_LEN], const uint8_t *data,
               size_t data_len, uint8_t out[JCL_MAC_LEN]) {
    uint32_t a = 0x6a636c31U ^ load_key_word(key, 0);
    uint32_t b = 0x616c7068U ^ load_key_word(key, 4);

    for (size_t i = 0; i < data_len; ++i) {
        a = rotl32(a ^ data[i] ^ key[i % JCL_KEY_LEN], 5) + b;
        b = rotl32(b + data[i] + key[(i * 7U) % JCL_KEY_LEN], 9) ^ a;
    }

    for (size_t round = 0; round < 8; ++round) {
        a = rotl32(a + load_key_word(key, (round % 4U) * 4U), 3);
        b = rotl32(b ^ a ^ (uint32_t)round, 13);
    }

    out[0] = (uint8_t)(a >> 24);
    out[1] = (uint8_t)(a >> 16);
    out[2] = (uint8_t)(a >> 8);
    out[3] = (uint8_t)a;
    out[4] = (uint8_t)(b >> 24);
    out[5] = (uint8_t)(b >> 16);
    out[6] = (uint8_t)(b >> 8);
    out[7] = (uint8_t)b;
}

void jcl_derive_response(const uint8_t key[JCL_KEY_LEN], const uint8_t *rand,
                         size_t rand_len, uint8_t sres[JCL_MAC_LEN],
                         uint8_t session_key[JCL_KEY_LEN]) {
    uint8_t label[64];
    size_t label_len = 0;
    const uint8_t prefix[] = {'J', 'C', 'L', '-', 'A', 'U', 'T', 'H'};

    for (size_t i = 0; i < sizeof(prefix); ++i) {
        label[label_len++] = prefix[i];
    }
    for (size_t i = 0; i < rand_len && label_len < sizeof(label); ++i) {
        label[label_len++] = rand[i];
    }

    jcl_mac64(key, label, label_len, sres);

    uint8_t zero[JCL_KEY_LEN] = {0};
    jcl_stream_xor(key, sres, JCL_MAC_LEN, zero, session_key, JCL_KEY_LEN);
}
