#include "internal/crypto/crypto.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MP_CACHE_SHA256_BLOCK_SIZE 64u

/* Tracks the incremental SHA-256 compression state for one digest operation. */
typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[MP_CACHE_SHA256_BLOCK_SIZE];
    size_t block_length;
} mp_cache_sha256_context_t;

/* Fixed SHA-256 round constants from the algorithm specification. */
static const uint32_t mp_cache_sha256_constants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

/* Rotate one 32-bit word right for the SHA-256 schedule and round functions. */
static uint32_t mp_cache_rotate_right(uint32_t value, uint32_t count) {
    return (value >> count) | (value << (32u - count));
}

static uint32_t mp_cache_sha256_big_sigma0(uint32_t value) {
    return mp_cache_rotate_right(value, 2u) ^ mp_cache_rotate_right(value, 13u) ^ mp_cache_rotate_right(value, 22u);
}

static uint32_t mp_cache_sha256_big_sigma1(uint32_t value) {
    return mp_cache_rotate_right(value, 6u) ^ mp_cache_rotate_right(value, 11u) ^ mp_cache_rotate_right(value, 25u);
}

static uint32_t mp_cache_sha256_small_sigma0(uint32_t value) {
    return mp_cache_rotate_right(value, 7u) ^ mp_cache_rotate_right(value, 18u) ^ (value >> 3u);
}

static uint32_t mp_cache_sha256_small_sigma1(uint32_t value) {
    return mp_cache_rotate_right(value, 17u) ^ mp_cache_rotate_right(value, 19u) ^ (value >> 10u);
}

/* Compress one 64-byte message block into the running SHA-256 state. */
static void mp_cache_sha256_transform(mp_cache_sha256_context_t *context, const uint8_t block[64]) {
    uint32_t schedule[64];
    uint32_t working[8];
    size_t index = 0u;

    for (index = 0u; index < 16u; index++) {
        schedule[index] = ((uint32_t)block[index * 4u] << 24u) | ((uint32_t)block[index * 4u + 1u] << 16u) |
                          ((uint32_t)block[index * 4u + 2u] << 8u) | (uint32_t)block[index * 4u + 3u];
    }

    for (index = 16u; index < 64u; index++) {
        schedule[index] = mp_cache_sha256_small_sigma1(schedule[index - 2u]) + schedule[index - 7u] +
                          mp_cache_sha256_small_sigma0(schedule[index - 15u]) + schedule[index - 16u];
    }

    for (index = 0u; index < 8u; index++) {
        working[index] = context->state[index];
    }

    for (index = 0u; index < 64u; index++) {
        uint32_t choice = (working[4] & working[5]) ^ ((~working[4]) & working[6]);
        uint32_t majority = (working[0] & working[1]) ^ (working[0] & working[2]) ^ (working[1] & working[2]);
        uint32_t temp1 = working[7] + mp_cache_sha256_big_sigma1(working[4]) + choice +
                         mp_cache_sha256_constants[index] + schedule[index];
        uint32_t temp2 = mp_cache_sha256_big_sigma0(working[0]) + majority;

        working[7] = working[6];
        working[6] = working[5];
        working[5] = working[4];
        working[4] = working[3] + temp1;
        working[3] = working[2];
        working[2] = working[1];
        working[1] = working[0];
        working[0] = temp1 + temp2;
    }

    for (index = 0u; index < 8u; index++) {
        context->state[index] += working[index];
    }
}

/* Seed the SHA-256 state words with the standard initial constants. */
static void mp_cache_sha256_init(mp_cache_sha256_context_t *context) {
    memset(context, 0, sizeof(*context));
    context->state[0] = 0x6a09e667u;
    context->state[1] = 0xbb67ae85u;
    context->state[2] = 0x3c6ef372u;
    context->state[3] = 0xa54ff53au;
    context->state[4] = 0x510e527fu;
    context->state[5] = 0x9b05688cu;
    context->state[6] = 0x1f83d9abu;
    context->state[7] = 0x5be0cd19u;
}

/* Feed arbitrary-length input into the staged SHA-256 block buffer. */
static void mp_cache_sha256_update(mp_cache_sha256_context_t *context, const uint8_t *data, size_t length) {
    size_t index = 0u;

    if (data == NULL || length == 0u) {
        return;
    }

    context->bit_count += (uint64_t)length * 8u;
    while (index < length) {
        size_t to_copy = MP_CACHE_SHA256_BLOCK_SIZE - context->block_length;
        if (to_copy > length - index) {
            to_copy = length - index;
        }

        memcpy(context->block + context->block_length, data + index, to_copy);
        context->block_length += to_copy;
        index += to_copy;

        if (context->block_length == MP_CACHE_SHA256_BLOCK_SIZE) {
            mp_cache_sha256_transform(context, context->block);
            context->block_length = 0u;
        }
    }
}

/* Finalize padding, append the bit length, and emit the digest bytes. */
static void mp_cache_sha256_final(mp_cache_sha256_context_t *context, uint8_t out_digest[MP_CACHE_SHA256_SIZE]) {
    uint8_t length_block[8];
    size_t index = 0u;

    context->block[context->block_length++] = 0x80u;
    if (context->block_length > 56u) {
        while (context->block_length < MP_CACHE_SHA256_BLOCK_SIZE) {
            context->block[context->block_length++] = 0u;
        }
        mp_cache_sha256_transform(context, context->block);
        context->block_length = 0u;
    }

    while (context->block_length < 56u) {
        context->block[context->block_length++] = 0u;
    }

    for (index = 0u; index < sizeof(length_block); index++) {
        length_block[sizeof(length_block) - index - 1u] = (uint8_t)(context->bit_count >> (index * 8u));
    }
    memcpy(context->block + 56u, length_block, sizeof(length_block));
    mp_cache_sha256_transform(context, context->block);

    for (index = 0u; index < 8u; index++) {
        out_digest[index * 4u] = (uint8_t)(context->state[index] >> 24u);
        out_digest[index * 4u + 1u] = (uint8_t)(context->state[index] >> 16u);
        out_digest[index * 4u + 2u] = (uint8_t)(context->state[index] >> 8u);
        out_digest[index * 4u + 3u] = (uint8_t)context->state[index];
    }
}

void mp_cache_sha256(const uint8_t *input_bytes, size_t input_length, uint8_t out_digest[MP_CACHE_SHA256_SIZE]) {
    mp_cache_sha256_context_t context;

    if (out_digest == NULL) {
        return;
    }

    mp_cache_sha256_init(&context);
    mp_cache_sha256_update(&context, input_bytes, input_length);
    mp_cache_sha256_final(&context, out_digest);
}

void mp_cache_hmac_sha256(
    const uint8_t *secret_key,
    size_t secret_key_length,
    const uint8_t *message_bytes,
    size_t message_length,
    uint8_t out_digest[MP_CACHE_SHA256_SIZE]) {
    uint8_t normalized_key[MP_CACHE_SHA256_BLOCK_SIZE];
    uint8_t inner_block[MP_CACHE_SHA256_BLOCK_SIZE];
    uint8_t outer_block[MP_CACHE_SHA256_BLOCK_SIZE];
    uint8_t inner_digest[MP_CACHE_SHA256_SIZE];
    mp_cache_sha256_context_t context;
    size_t index = 0u;

    if (out_digest == NULL) {
        return;
    }

    memset(normalized_key, 0, sizeof(normalized_key));
    if (secret_key != NULL && secret_key_length > 0u) {
        if (secret_key_length > MP_CACHE_SHA256_BLOCK_SIZE) {
            mp_cache_sha256(secret_key, secret_key_length, normalized_key);
        } else {
            memcpy(normalized_key, secret_key, secret_key_length);
        }
    }

    for (index = 0u; index < MP_CACHE_SHA256_BLOCK_SIZE; index++) {
        inner_block[index] = normalized_key[index] ^ 0x36u;
        outer_block[index] = normalized_key[index] ^ 0x5cu;
    }

    mp_cache_sha256_init(&context);
    mp_cache_sha256_update(&context, inner_block, sizeof(inner_block));
    mp_cache_sha256_update(&context, message_bytes, message_length);
    mp_cache_sha256_final(&context, inner_digest);

    mp_cache_sha256_init(&context);
    mp_cache_sha256_update(&context, outer_block, sizeof(outer_block));
    mp_cache_sha256_update(&context, inner_digest, sizeof(inner_digest));
    mp_cache_sha256_final(&context, out_digest);
}

bool mp_cache_constant_time_equals(const uint8_t *left_bytes, const uint8_t *right_bytes, size_t byte_length) {
    size_t index = 0u;
    uint8_t diff = 0u;

    if (left_bytes == NULL || right_bytes == NULL) {
        return false;
    }

    for (index = 0u; index < byte_length; index++) {
        diff |= (uint8_t)(left_bytes[index] ^ right_bytes[index]);
    }

    return diff == 0u;
}

int mp_cache_random_bytes(uint8_t *out_random_bytes, size_t byte_length) {
    int fd = -1;
    size_t offset = 0u;

    if (out_random_bytes == NULL && byte_length > 0u) {
        errno = EINVAL;
        return -1;
    }

    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    while (offset < byte_length) {
        ssize_t bytes_read = read(fd, out_random_bytes + offset, byte_length - offset);
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            (void)close(fd);
            return -1;
        }
        if (bytes_read == 0) {
            (void)close(fd);
            errno = EIO;
            return -1;
        }
        offset += (size_t)bytes_read;
    }

    (void)close(fd);
    return 0;
}

/* Convert one hexadecimal character into its 4-bit numeric value. */
static int mp_cache_hex_nibble(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

int mp_cache_hex_encode(const uint8_t *input_bytes, size_t input_length, char *out_hex_text, size_t out_capacity) {
    static const char hex_chars[] = "0123456789abcdef";
    size_t index = 0u;

    if (out_hex_text == NULL || out_capacity < input_length * 2u + 1u) {
        errno = ENOSPC;
        return -1;
    }

    for (index = 0u; index < input_length; index++) {
        out_hex_text[index * 2u] = hex_chars[input_bytes[index] >> 4u];
        out_hex_text[index * 2u + 1u] = hex_chars[input_bytes[index] & 0x0fu];
    }
    out_hex_text[input_length * 2u] = '\0';
    return 0;
}

int mp_cache_hex_decode(const char *hex_text, uint8_t *out_bytes, size_t out_capacity, size_t *out_byte_length) {
    size_t hex_text_length = 0u;
    size_t index = 0u;

    if (hex_text == NULL || out_bytes == NULL) {
        errno = EINVAL;
        return -1;
    }

    hex_text_length = strlen(hex_text);
    if ((hex_text_length % 2u) != 0u || out_capacity < hex_text_length / 2u) {
        errno = EINVAL;
        return -1;
    }

    for (index = 0u; index < hex_text_length; index += 2u) {
        int high = mp_cache_hex_nibble(hex_text[index]);
        int low = mp_cache_hex_nibble(hex_text[index + 1u]);
        if (high < 0 || low < 0) {
            errno = EINVAL;
            return -1;
        }
        out_bytes[index / 2u] = (uint8_t)((high << 4u) | low);
    }

    if (out_byte_length != NULL) {
        *out_byte_length = hex_text_length / 2u;
    }
    return 0;
}

int mp_cache_base64_encode(
    const uint8_t *input_bytes,
    size_t input_length,
    char *out_base64_text,
    size_t out_capacity,
    size_t *out_base64_length) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t required = ((input_length + 2u) / 3u) * 4u;
    size_t in_index = 0u;
    size_t out_index = 0u;

    if (out_base64_text == NULL || out_capacity < required + 1u) {
        errno = ENOSPC;
        return -1;
    }

    while (in_index < input_length) {
        uint32_t chunk = 0u;
        size_t chunk_length = 0u;
        size_t pad_index = 0u;

        for (chunk_length = 0u; chunk_length < 3u && in_index < input_length; chunk_length++, in_index++) {
            chunk = (chunk << 8u) | input_bytes[in_index];
        }
        for (pad_index = chunk_length; pad_index < 3u; pad_index++) {
            chunk <<= 8u;
        }

        out_base64_text[out_index++] = alphabet[(chunk >> 18u) & 0x3fu];
        out_base64_text[out_index++] = alphabet[(chunk >> 12u) & 0x3fu];
        out_base64_text[out_index++] = chunk_length > 1u ? alphabet[(chunk >> 6u) & 0x3fu] : '=';
        out_base64_text[out_index++] = chunk_length > 2u ? alphabet[chunk & 0x3fu] : '=';
    }

    out_base64_text[out_index] = '\0';
    if (out_base64_length != NULL) {
        *out_base64_length = out_index;
    }
    return 0;
}

/* Convert one base64 character into its 6-bit numeric value. */
static int mp_cache_base64_value(char character) {
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 26;
    }
    if (character >= '0' && character <= '9') {
        return character - '0' + 52;
    }
    if (character == '+') {
        return 62;
    }
    if (character == '/') {
        return 63;
    }
    return -1;
}

int mp_cache_base64_decode(const char *base64_text, uint8_t *out_bytes, size_t out_capacity, size_t *out_byte_length) {
    size_t base64_text_length = 0u;
    size_t in_index = 0u;
    size_t out_index = 0u;

    if (base64_text == NULL || out_bytes == NULL) {
        errno = EINVAL;
        return -1;
    }

    base64_text_length = strlen(base64_text);
    if ((base64_text_length % 4u) != 0u) {
        errno = EINVAL;
        return -1;
    }

    while (in_index < base64_text_length) {
        int values[4];
        size_t index = 0u;
        uint32_t chunk = 0u;
        size_t decoded_count = 3u;

        for (index = 0u; index < 4u; index++) {
            char character = base64_text[in_index + index];
            if (character == '=') {
                values[index] = 0;
                decoded_count--;
            } else {
                values[index] = mp_cache_base64_value(character);
                if (values[index] < 0) {
                    errno = EINVAL;
                    return -1;
                }
            }
        }

        chunk = ((uint32_t)values[0] << 18u) | ((uint32_t)values[1] << 12u) | ((uint32_t)values[2] << 6u) |
                (uint32_t)values[3];
        if (out_index + decoded_count > out_capacity) {
            errno = ENOSPC;
            return -1;
        }

        out_bytes[out_index++] = (uint8_t)(chunk >> 16u);
        if (base64_text[in_index + 2u] != '=') {
            out_bytes[out_index++] = (uint8_t)(chunk >> 8u);
        }
        if (base64_text[in_index + 3u] != '=') {
            out_bytes[out_index++] = (uint8_t)chunk;
        }

        in_index += 4u;
    }

    if (out_byte_length != NULL) {
        *out_byte_length = out_index;
    }
    return 0;
}

void mp_cache_stream_xor(
    const uint8_t *stream_key,
    size_t stream_key_length,
    const uint8_t nonce[MP_CACHE_NONCE_SIZE],
    uint8_t *inout_bytes,
    size_t byte_length) {
    uint64_t counter = 0u;
    size_t offset = 0u;

    if (stream_key == NULL || nonce == NULL || inout_bytes == NULL) {
        return;
    }

    while (offset < byte_length) {
        uint8_t counter_block[MP_CACHE_NONCE_SIZE + sizeof(counter)];
        uint8_t keystream[MP_CACHE_SHA256_SIZE];
        size_t block_index = 0u;
        size_t remaining = byte_length - offset;
        size_t chunk_length = remaining < sizeof(keystream) ? remaining : sizeof(keystream);

        memcpy(counter_block, nonce, MP_CACHE_NONCE_SIZE);
        for (block_index = 0u; block_index < sizeof(counter); block_index++) {
            counter_block[MP_CACHE_NONCE_SIZE + sizeof(counter) - block_index - 1u] = (uint8_t)(counter >> (block_index * 8u));
        }

        mp_cache_hmac_sha256(stream_key, stream_key_length, counter_block, sizeof(counter_block), keystream);
        for (block_index = 0u; block_index < chunk_length; block_index++) {
            inout_bytes[offset + block_index] ^= keystream[block_index];
        }

        counter++;
        offset += chunk_length;
    }
}
