#ifndef MP_CACHE_INTERNAL_CRYPTO_H
#define MP_CACHE_INTERNAL_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_CACHE_SHA256_SIZE 32u
#define MP_CACHE_NONCE_SIZE 16u

void mp_cache_sha256(const uint8_t *data, size_t length, uint8_t out_digest[MP_CACHE_SHA256_SIZE]);
void mp_cache_hmac_sha256(
    const uint8_t *key,
    size_t key_length,
    const uint8_t *data,
    size_t data_length,
    uint8_t out_digest[MP_CACHE_SHA256_SIZE]);
bool mp_cache_constant_time_equals(const uint8_t *left, const uint8_t *right, size_t length);
int mp_cache_random_bytes(uint8_t *buffer, size_t length);
int mp_cache_hex_encode(const uint8_t *bytes, size_t length, char *out_text, size_t out_capacity);
int mp_cache_hex_decode(const char *text, uint8_t *out_bytes, size_t out_capacity, size_t *out_length);
int mp_cache_base64_encode(const uint8_t *bytes, size_t length, char *out_text, size_t out_capacity, size_t *out_length);
int mp_cache_base64_decode(const char *text, uint8_t *out_bytes, size_t out_capacity, size_t *out_length);
void mp_cache_stream_xor(
    const uint8_t *key,
    size_t key_length,
    const uint8_t nonce[MP_CACHE_NONCE_SIZE],
    uint8_t *data,
    size_t length);

#endif
