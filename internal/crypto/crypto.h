#ifndef MP_CACHE_INTERNAL_CRYPTO_H
#define MP_CACHE_INTERNAL_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_CACHE_SHA256_SIZE 32u
#define MP_CACHE_NONCE_SIZE 16u

void mp_cache_sha256(const uint8_t *input_bytes, size_t input_length, uint8_t out_digest[MP_CACHE_SHA256_SIZE]);
void mp_cache_hmac_sha256(
    const uint8_t *secret_key,
    size_t secret_key_length,
    const uint8_t *message_bytes,
    size_t message_length,
    uint8_t out_digest[MP_CACHE_SHA256_SIZE]);
bool mp_cache_constant_time_equals(const uint8_t *left_bytes, const uint8_t *right_bytes, size_t byte_length);
int mp_cache_random_bytes(uint8_t *out_random_bytes, size_t byte_length);
int mp_cache_hex_encode(const uint8_t *input_bytes, size_t input_length, char *out_hex_text, size_t out_capacity);
int mp_cache_hex_decode(const char *hex_text, uint8_t *out_bytes, size_t out_capacity, size_t *out_byte_length);
int mp_cache_base64_encode(
    const uint8_t *input_bytes,
    size_t input_length,
    char *out_base64_text,
    size_t out_capacity,
    size_t *out_base64_length);
int mp_cache_base64_decode(
    const char *base64_text,
    uint8_t *out_bytes,
    size_t out_capacity,
    size_t *out_byte_length);
void mp_cache_stream_xor(
    const uint8_t *stream_key,
    size_t stream_key_length,
    const uint8_t nonce[MP_CACHE_NONCE_SIZE],
    uint8_t *inout_bytes,
    size_t byte_length);

#endif
