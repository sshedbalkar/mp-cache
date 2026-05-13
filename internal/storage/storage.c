#include "internal/storage/storage.h"

#include "internal/crypto/crypto.h"
#include "internal/platform/fs.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MP_CACHE_STATE_MAGIC "MPCSTAT1"
#define MP_CACHE_JOURNAL_MAGIC "MPCJNL1"
#define MP_CACHE_EXPORT_MAGIC "MPCEXPT1"
#define MP_CACHE_PACKET_HEADER_SIZE 64u
#define MP_CACHE_STATE_VERSION 1u
#define MP_CACHE_JOURNAL_VERSION 1u

/* Tags the operation type encoded inside one journal packet payload. */
typedef enum {
    MP_CACHE_JOURNAL_OP_SET = 1,
    MP_CACHE_JOURNAL_OP_DELETE = 2,
    MP_CACHE_JOURNAL_OP_CLIENT_UPSERT = 3,
    MP_CACHE_JOURNAL_OP_PURGE_ALL = 4
} mp_cache_journal_op_t;

/* Owns a growable byte buffer used while serializing packets and state blobs. */
typedef struct {
    uint8_t *bytes;
    size_t length;
    size_t capacity;
} mp_cache_buffer_t;

/* Carries counting context while scanning only currently active cache entries. */
typedef struct {
    size_t count;
    int64_t now_utc_seconds;
} mp_cache_active_count_t;

/* Carries serialization context while emitting only currently active cache entries. */
typedef struct {
    mp_cache_buffer_t *buffer;
    int64_t now_utc_seconds;
} mp_cache_serialize_entries_t;

/* Write one storage-scoped log line when observability is available. */
static void mp_cache_storage_log(mp_cache_storage_t *storage, mp_log_level_t level, const char *message) {
    if (storage != NULL && storage->log != NULL) {
        mp_cache_log_writef(storage->log, level, "storage", "%s", message);
    }
}

/* Format one bounded filesystem path and fail closed when truncation would occur. */
static int mp_cache_storage_format_path(char *out_path, size_t out_path_capacity, const char *format, ...) {
    va_list arguments;
    int written = 0;

    if (out_path == NULL || out_path_capacity == 0u || format == NULL) {
        errno = EINVAL;
        return -1;
    }

    va_start(arguments, format);
    written = vsnprintf(out_path, out_path_capacity, format, arguments);
    va_end(arguments);

    if (written < 0 || (size_t)written >= out_path_capacity) {
        out_path[0] = '\0';
        errno = ENAMETOOLONG;
        return -1;
    }

    return 0;
}

/* Release one growable byte buffer and clear its bookkeeping. */
static void mp_cache_buffer_destroy(mp_cache_buffer_t *buffer) {
    if (buffer == NULL) {
        return;
    }

    free(buffer->bytes);
    memset(buffer, 0, sizeof(*buffer));
}

/* Grow buffer so an additional payload segment can be appended safely. */
static int mp_cache_buffer_reserve(mp_cache_buffer_t *buffer, size_t additional_length) {
    uint8_t *next_bytes = NULL;
    size_t next_capacity = 0u;

    if (buffer == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (additional_length > SIZE_MAX - buffer->length) {
        errno = EOVERFLOW;
        return -1;
    }
    if (buffer->length + additional_length <= buffer->capacity) {
        return 0;
    }

    next_capacity = buffer->capacity == 0u ? 256u : buffer->capacity;
    while (next_capacity < buffer->length + additional_length) {
        if (next_capacity > SIZE_MAX / 2u) {
            next_capacity = buffer->length + additional_length;
            break;
        }
        next_capacity *= 2u;
    }

    next_bytes = realloc(buffer->bytes, next_capacity);
    if (next_bytes == NULL) {
        return -1;
    }

    buffer->bytes = next_bytes;
    buffer->capacity = next_capacity;
    return 0;
}

/* Append arbitrary bytes to the growable serialization buffer. */
static int mp_cache_buffer_append(mp_cache_buffer_t *buffer, const void *bytes, size_t length) {
    if (buffer == NULL || (bytes == NULL && length > 0u)) {
        errno = EINVAL;
        return -1;
    }
    if (mp_cache_buffer_reserve(buffer, length) != 0) {
        return -1;
    }

    if (length > 0u) {
        memcpy(buffer->bytes + buffer->length, bytes, length);
    }
    buffer->length += length;
    return 0;
}

/* Append one big-endian 32-bit integer to buffer. */
static int mp_cache_buffer_append_u32(mp_cache_buffer_t *buffer, uint32_t value) {
    uint8_t bytes[4];

    bytes[0] = (uint8_t)(value >> 24u);
    bytes[1] = (uint8_t)(value >> 16u);
    bytes[2] = (uint8_t)(value >> 8u);
    bytes[3] = (uint8_t)value;
    return mp_cache_buffer_append(buffer, bytes, sizeof(bytes));
}

/* Append one big-endian 64-bit integer to buffer. */
static int mp_cache_buffer_append_u64(mp_cache_buffer_t *buffer, uint64_t value) {
    uint8_t bytes[8];
    size_t index = 0u;

    for (index = 0u; index < sizeof(bytes); index++) {
        bytes[sizeof(bytes) - index - 1u] = (uint8_t)(value >> (index * 8u));
    }
    return mp_cache_buffer_append(buffer, bytes, sizeof(bytes));
}

/* Consume one big-endian 32-bit integer from a bounded byte slice. */
static int mp_cache_consume_u32(const uint8_t *bytes, size_t length, size_t *offset, uint32_t *out_value) {
    if (bytes == NULL || offset == NULL || out_value == NULL || *offset > length || length - *offset < 4u) {
        errno = EINVAL;
        return -1;
    }

    *out_value = ((uint32_t)bytes[*offset] << 24u) | ((uint32_t)bytes[*offset + 1u] << 16u) |
                 ((uint32_t)bytes[*offset + 2u] << 8u) | (uint32_t)bytes[*offset + 3u];
    *offset += 4u;
    return 0;
}

/* Consume one big-endian 64-bit integer from a bounded byte slice. */
static int mp_cache_consume_u64(const uint8_t *bytes, size_t length, size_t *offset, uint64_t *out_value) {
    size_t index = 0u;
    uint64_t value = 0u;

    if (bytes == NULL || offset == NULL || out_value == NULL || *offset > length || length - *offset < 8u) {
        errno = EINVAL;
        return -1;
    }

    for (index = 0u; index < 8u; index++) {
        value = (value << 8u) | bytes[*offset + index];
    }
    *out_value = value;
    *offset += 8u;
    return 0;
}

/* Consume a borrowed byte span from a bounded packet payload. */
static int mp_cache_consume_bytes(
    const uint8_t *bytes,
    size_t length,
    size_t *offset,
    size_t value_length,
    const uint8_t **out_value_bytes) {
    if (bytes == NULL || offset == NULL || out_value_bytes == NULL || *offset > length || length - *offset < value_length) {
        errno = EINVAL;
        return -1;
    }

    *out_value_bytes = bytes + *offset;
    *offset += value_length;
    return 0;
}

/* Write the full buffer to file unless a hard I/O error interrupts progress. */
static int mp_cache_storage_write_all(FILE *file, const void *buffer, size_t length) {
    size_t written = 0u;

    while (written < length) {
        size_t step = fwrite((const uint8_t *)buffer + written, 1u, length - written, file);
        if (step == 0u) {
            return -1;
        }
        written += step;
    }

    return 0;
}

/* Read exactly length bytes from file or fail when EOF arrives too early. */
static int mp_cache_storage_read_exact(FILE *file, void *buffer, size_t length) {
    size_t offset = 0u;

    while (offset < length) {
        size_t step = fread((uint8_t *)buffer + offset, 1u, length - offset, file);
        if (step == 0u) {
            if (feof(file) != 0) {
                errno = EINVAL;
            }
            return -1;
        }
        offset += step;
    }

    return 0;
}

/* Encrypt and MAC one payload into the shared on-disk packet envelope format. */
static int mp_cache_storage_build_packet(
    mp_cache_storage_t *storage,
    const char *magic,
    const uint8_t *payload,
    size_t payload_length,
    mp_cache_buffer_t *out_packet,
    uint8_t out_mac[MP_CACHE_TOKEN_HASH_SIZE]) {
    uint8_t nonce[MP_CACHE_NONCE_SIZE];
    uint8_t header[MP_CACHE_PACKET_HEADER_SIZE];
    uint8_t *ciphertext = NULL;
    mp_cache_buffer_t mac_input = {0};

    if (storage == NULL || magic == NULL || payload == NULL || out_packet == NULL || payload_length > storage->max_packet_bytes) {
        errno = EINVAL;
        return -1;
    }

    if (mp_cache_random_bytes(nonce, sizeof(nonce)) != 0) {
        return -1;
    }

    ciphertext = malloc(payload_length == 0u ? 1u : payload_length);
    if (ciphertext == NULL) {
        return -1;
    }
    if (payload_length > 0u) {
        memcpy(ciphertext, payload, payload_length);
        mp_cache_stream_xor(storage->storage_key, sizeof(storage->storage_key), nonce, ciphertext, payload_length);
    }

    memset(header, 0, sizeof(header));
    memcpy(header, magic, 8u);
    header[8] = 0u;
    header[9] = 0u;
    header[10] = 0u;
    header[11] = 1u;
    header[12] = (uint8_t)(payload_length >> 24u);
    header[13] = (uint8_t)(payload_length >> 16u);
    header[14] = (uint8_t)(payload_length >> 8u);
    header[15] = (uint8_t)payload_length;
    memcpy(header + 16u, nonce, sizeof(nonce));

    if (mp_cache_buffer_append(&mac_input, header, 16u + sizeof(nonce)) != 0 ||
        mp_cache_buffer_append(&mac_input, ciphertext, payload_length) != 0) {
        free(ciphertext);
        mp_cache_buffer_destroy(&mac_input);
        return -1;
    }

    mp_cache_hmac_sha256(
        storage->storage_key,
        sizeof(storage->storage_key),
        mac_input.bytes,
        mac_input.length,
        header + 32u);
    if (out_mac != NULL) {
        memcpy(out_mac, header + 32u, MP_CACHE_TOKEN_HASH_SIZE);
    }

    if (mp_cache_buffer_append(out_packet, header, sizeof(header)) != 0 ||
        mp_cache_buffer_append(out_packet, ciphertext, payload_length) != 0) {
        free(ciphertext);
        mp_cache_buffer_destroy(&mac_input);
        return -1;
    }

    free(ciphertext);
    mp_cache_buffer_destroy(&mac_input);
    return 0;
}

/* Serialize one complete packet envelope to a standalone file path. */
static int mp_cache_storage_write_packet_file(
    mp_cache_storage_t *storage,
    const char *file_path,
    const char *magic,
    const uint8_t *payload,
    size_t payload_length,
    uint8_t out_mac[MP_CACHE_TOKEN_HASH_SIZE]) {
    char temp_path[MP_CACHE_PATH_CAP];
    FILE *file = NULL;
    mp_cache_buffer_t packet = {0};
    int result = -1;

    if (storage == NULL || file_path == NULL || magic == NULL || payload == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (mp_cache_fs_ensure_parent_directory(file_path) != 0) {
        return -1;
    }

    if (mp_cache_storage_format_path(temp_path, sizeof(temp_path), "%s.tmp.%ld", file_path, (long)getpid()) != 0) {
        return -1;
    }
    if (mp_cache_storage_build_packet(storage, magic, payload, payload_length, &packet, out_mac) != 0) {
        mp_cache_buffer_destroy(&packet);
        return -1;
    }

    file = fopen(temp_path, "wb");
    if (file == NULL) {
        mp_cache_buffer_destroy(&packet);
        return -1;
    }

    if (mp_cache_storage_write_all(file, packet.bytes, packet.length) == 0 && fflush(file) == 0 && fclose(file) == 0 &&
        rename(temp_path, file_path) == 0) {
        result = 0;
    } else {
        if (file != NULL) {
            (void)fclose(file);
        }
        (void)unlink(temp_path);
    }

    mp_cache_buffer_destroy(&packet);
    return result;
}

/* Append one packet envelope to the journal file without rewriting prior entries. */
static int mp_cache_storage_append_packet(
    mp_cache_storage_t *storage,
    const char *file_path,
    const char *magic,
    const uint8_t *payload,
    size_t payload_length) {
    FILE *file = NULL;
    mp_cache_buffer_t packet = {0};
    int result = -1;

    if (storage == NULL || file_path == NULL || magic == NULL || payload == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (mp_cache_fs_ensure_parent_directory(file_path) != 0) {
        return -1;
    }
    if (mp_cache_storage_build_packet(storage, magic, payload, payload_length, &packet, NULL) != 0) {
        mp_cache_buffer_destroy(&packet);
        return -1;
    }

    file = fopen(file_path, "ab");
    if (file == NULL) {
        mp_cache_buffer_destroy(&packet);
        return -1;
    }

    if (mp_cache_storage_write_all(file, packet.bytes, packet.length) == 0 && fflush(file) == 0 && fclose(file) == 0) {
        result = 0;
    } else {
        if (file != NULL) {
            (void)fclose(file);
        }
    }

    mp_cache_buffer_destroy(&packet);
    return result;
}

/* Read, authenticate, and decrypt one packet envelope from an open stream. */
static int mp_cache_storage_read_packet_stream(
    mp_cache_storage_t *storage,
    FILE *file,
    const char *expected_magic,
    uint8_t **out_payload,
    size_t *out_payload_length,
    uint8_t out_mac[MP_CACHE_TOKEN_HASH_SIZE]) {
    uint8_t header[MP_CACHE_PACKET_HEADER_SIZE];
    uint8_t *ciphertext = NULL;
    uint8_t computed_mac[MP_CACHE_TOKEN_HASH_SIZE];
    mp_cache_buffer_t mac_input = {0};
    size_t payload_length = 0u;
    size_t bytes_read = 0u;
    int ch = 0;

    if (storage == NULL || file == NULL || expected_magic == NULL || out_payload == NULL || out_payload_length == NULL) {
        errno = EINVAL;
        return -1;
    }

    ch = fgetc(file);
    if (ch == EOF) {
        return 1;
    }
    if (ungetc(ch, file) == EOF) {
        return -1;
    }

    if (mp_cache_storage_read_exact(file, header, sizeof(header)) != 0) {
        return -1;
    }
    if (memcmp(header, expected_magic, 8u) != 0 || header[11] != 1u) {
        errno = EINVAL;
        return -1;
    }

    payload_length = ((size_t)header[12] << 24u) | ((size_t)header[13] << 16u) | ((size_t)header[14] << 8u) |
                     (size_t)header[15];
    if (payload_length > storage->max_packet_bytes) {
        errno = EOVERFLOW;
        return -1;
    }

    ciphertext = malloc(payload_length == 0u ? 1u : payload_length);
    if (ciphertext == NULL) {
        return -1;
    }
    bytes_read = payload_length == 0u ? 0u : fread(ciphertext, 1u, payload_length, file);
    if (bytes_read != payload_length) {
        free(ciphertext);
        errno = EINVAL;
        return -1;
    }

    if (mp_cache_buffer_append(&mac_input, header, 32u) != 0 || mp_cache_buffer_append(&mac_input, ciphertext, payload_length) != 0) {
        free(ciphertext);
        mp_cache_buffer_destroy(&mac_input);
        return -1;
    }
    mp_cache_hmac_sha256(
        storage->storage_key,
        sizeof(storage->storage_key),
        mac_input.bytes,
        mac_input.length,
        computed_mac);
    if (!mp_cache_constant_time_equals(computed_mac, header + 32u, sizeof(computed_mac))) {
        free(ciphertext);
        mp_cache_buffer_destroy(&mac_input);
        errno = EACCES;
        return -1;
    }

    mp_cache_stream_xor(
        storage->storage_key,
        sizeof(storage->storage_key),
        header + 16u,
        ciphertext,
        payload_length);
    *out_payload = ciphertext;
    *out_payload_length = payload_length;
    if (out_mac != NULL) {
        memcpy(out_mac, header + 32u, MP_CACHE_TOKEN_HASH_SIZE);
    }

    mp_cache_buffer_destroy(&mac_input);
    return 0;
}

/* Read, authenticate, and decrypt one packet envelope from a standalone file. */
static int mp_cache_storage_read_packet_file(
    mp_cache_storage_t *storage,
    const char *file_path,
    const char *expected_magic,
    uint8_t **out_payload,
    size_t *out_payload_length,
    uint8_t out_mac[MP_CACHE_TOKEN_HASH_SIZE]) {
    FILE *file = NULL;
    int result = 0;

    file = fopen(file_path, "rb");
    if (file == NULL) {
        return errno == ENOENT ? 1 : -1;
    }

    result = mp_cache_storage_read_packet_stream(storage, file, expected_magic, out_payload, out_payload_length, out_mac);
    if (result == 0) {
        int trailing = fgetc(file);
        if (trailing != EOF) {
            result = -1;
            errno = EINVAL;
        }
    }

    (void)fclose(file);
    return result;
}

/* Count only entries whose expiration still lies in the future. */
static int mp_cache_count_active_entry(
    const char *key,
    size_t key_length,
    const uint8_t *value,
    size_t value_length,
    int64_t expires_at_utc_seconds,
    void *context) {
    mp_cache_active_count_t *count = context;
    (void)key;
    (void)key_length;
    (void)value;
    (void)value_length;

    if (count != NULL && expires_at_utc_seconds > count->now_utc_seconds) {
        count->count++;
    }
    return 0;
}

/* Serialize one non-expired cache entry into the checkpoint or export payload. */
static int mp_cache_serialize_active_entry(
    const char *key,
    size_t key_length,
    const uint8_t *value,
    size_t value_length,
    int64_t expires_at_utc_seconds,
    void *context) {
    mp_cache_serialize_entries_t *serialize_context = context;

    if (serialize_context == NULL || serialize_context->buffer == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (expires_at_utc_seconds <= serialize_context->now_utc_seconds) {
        return 0;
    }

    if (mp_cache_buffer_append_u32(serialize_context->buffer, (uint32_t)key_length) != 0 ||
        mp_cache_buffer_append_u32(serialize_context->buffer, (uint32_t)value_length) != 0 ||
        mp_cache_buffer_append_u64(serialize_context->buffer, (uint64_t)expires_at_utc_seconds) != 0 ||
        mp_cache_buffer_append(serialize_context->buffer, key, key_length) != 0 ||
        mp_cache_buffer_append(serialize_context->buffer, value, value_length) != 0) {
        return -1;
    }

    return 0;
}

/* Serialize one registered client record into the checkpoint or export payload. */
static int mp_cache_serialize_client_record(const mp_cache_client_record_t *record, void *context) {
    mp_cache_buffer_t *buffer = context;
    size_t id_length = 0u;

    if (record == NULL || buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    id_length = strlen(record->client_id);
    if (mp_cache_buffer_append_u32(buffer, (uint32_t)id_length) != 0 ||
        mp_cache_buffer_append_u32(buffer, (uint32_t)record->role) != 0 ||
        mp_cache_buffer_append(buffer, record->token_hash, sizeof(record->token_hash)) != 0 ||
        mp_cache_buffer_append(buffer, record->client_id, id_length) != 0) {
        return -1;
    }

    return 0;
}

/* Serialize the live store and client registry into the portable state payload. */
static int mp_cache_storage_serialize_state(
    const mp_cache_store_t *store,
    const mp_cache_security_t *security,
    int64_t now_utc_seconds,
    mp_cache_buffer_t *out_buffer) {
    mp_cache_active_count_t entry_count = {0};
    mp_cache_serialize_entries_t serialize_entries = {0};
    size_t client_count = 0u;

    if (store == NULL || security == NULL || out_buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    entry_count.now_utc_seconds = now_utc_seconds;
    if (mp_cache_store_for_each(store, mp_cache_count_active_entry, &entry_count) != 0) {
        return -1;
    }
    client_count = mp_cache_security_client_count(security);

    if (mp_cache_buffer_append_u32(out_buffer, MP_CACHE_STATE_VERSION) != 0 ||
        mp_cache_buffer_append_u64(out_buffer, (uint64_t)now_utc_seconds) != 0 ||
        mp_cache_buffer_append_u32(out_buffer, (uint32_t)entry_count.count) != 0) {
        return -1;
    }

    serialize_entries.buffer = out_buffer;
    serialize_entries.now_utc_seconds = now_utc_seconds;
    if (mp_cache_store_for_each(store, mp_cache_serialize_active_entry, &serialize_entries) != 0 ||
        mp_cache_buffer_append_u32(out_buffer, (uint32_t)client_count) != 0 ||
        mp_cache_security_for_each_client(security, mp_cache_serialize_client_record, out_buffer) != 0) {
        return -1;
    }

    return 0;
}

/* Replace store and security contents from one validated state payload. */
static int mp_cache_storage_deserialize_state(
    const uint8_t *payload,
    size_t payload_length,
    mp_cache_store_t *store,
    mp_cache_security_t *security,
    int64_t now_utc_seconds) {
    size_t offset = 0u;
    uint32_t version = 0u;
    uint64_t checkpointed_at = 0u;
    uint32_t entry_count = 0u;
    uint32_t client_count = 0u;
    uint32_t index = 0u;

    if (payload == NULL || store == NULL || security == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (mp_cache_consume_u32(payload, payload_length, &offset, &version) != 0 ||
        mp_cache_consume_u64(payload, payload_length, &offset, &checkpointed_at) != 0 ||
        version != MP_CACHE_STATE_VERSION) {
        errno = EINVAL;
        return -1;
    }

    if (mp_cache_consume_u32(payload, payload_length, &offset, &entry_count) != 0) {
        return -1;
    }
    for (index = 0u; index < entry_count; index++) {
        uint32_t key_length = 0u;
        uint32_t value_length = 0u;
        uint64_t expires_at = 0u;
        const uint8_t *key = NULL;
        const uint8_t *value = NULL;

        if (mp_cache_consume_u32(payload, payload_length, &offset, &key_length) != 0 ||
            mp_cache_consume_u32(payload, payload_length, &offset, &value_length) != 0 ||
            mp_cache_consume_u64(payload, payload_length, &offset, &expires_at) != 0 ||
            mp_cache_consume_bytes(payload, payload_length, &offset, key_length, &key) != 0 ||
            mp_cache_consume_bytes(payload, payload_length, &offset, value_length, &value) != 0) {
            return -1;
        }

        if ((int64_t)expires_at > now_utc_seconds &&
            mp_cache_store_restore_entry(store, key, key_length, value, value_length, (int64_t)expires_at) !=
                MP_CACHE_STORE_STATUS_OK) {
            errno = EINVAL;
            return -1;
        }
    }

    if (mp_cache_consume_u32(payload, payload_length, &offset, &client_count) != 0) {
        return -1;
    }
    for (index = 0u; index < client_count; index++) {
        uint32_t id_length = 0u;
        uint32_t role_value = 0u;
        const uint8_t *token_hash = NULL;
        const uint8_t *client_id_bytes = NULL;
        char client_id[MP_CACHE_CLIENT_ID_CAP];

        if (mp_cache_consume_u32(payload, payload_length, &offset, &id_length) != 0 ||
            mp_cache_consume_u32(payload, payload_length, &offset, &role_value) != 0 ||
            mp_cache_consume_bytes(payload, payload_length, &offset, MP_CACHE_TOKEN_HASH_SIZE, &token_hash) != 0 ||
            id_length == 0u || id_length >= sizeof(client_id) ||
            mp_cache_consume_bytes(payload, payload_length, &offset, id_length, &client_id_bytes) != 0) {
            errno = EINVAL;
            return -1;
        }

        memcpy(client_id, client_id_bytes, id_length);
        client_id[id_length] = '\0';
        if (mp_cache_security_import_client_hash(
                security,
                client_id,
                (mp_cache_role_t)role_value,
                token_hash) != MP_CACHE_SECURITY_STATUS_OK) {
            errno = EINVAL;
            return -1;
        }
    }

    return offset == payload_length ? 0 : -1;
}

/* Build the payload for one journal operation before envelope encryption and MAC. */
static int mp_cache_storage_build_journal_payload(
    mp_cache_journal_op_t operation,
    int64_t now_utc_seconds,
    const void *subject,
    size_t subject_length,
    const void *value,
    size_t value_length,
    uint64_t extra_value,
    mp_cache_buffer_t *out_buffer) {
    if (out_buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (mp_cache_buffer_append_u32(out_buffer, MP_CACHE_JOURNAL_VERSION) != 0 ||
        mp_cache_buffer_append_u32(out_buffer, (uint32_t)operation) != 0 ||
        mp_cache_buffer_append_u64(out_buffer, (uint64_t)now_utc_seconds) != 0) {
        return -1;
    }

    switch (operation) {
        case MP_CACHE_JOURNAL_OP_SET:
            if (mp_cache_buffer_append_u32(out_buffer, (uint32_t)subject_length) != 0 ||
                mp_cache_buffer_append_u32(out_buffer, (uint32_t)value_length) != 0 ||
                mp_cache_buffer_append_u64(out_buffer, extra_value) != 0 ||
                mp_cache_buffer_append(out_buffer, subject, subject_length) != 0 ||
                mp_cache_buffer_append(out_buffer, value, value_length) != 0) {
                return -1;
            }
            return 0;
        case MP_CACHE_JOURNAL_OP_DELETE:
            if (mp_cache_buffer_append_u32(out_buffer, (uint32_t)subject_length) != 0 ||
                mp_cache_buffer_append(out_buffer, subject, subject_length) != 0) {
                return -1;
            }
            return 0;
        case MP_CACHE_JOURNAL_OP_CLIENT_UPSERT:
            if (mp_cache_buffer_append_u32(out_buffer, (uint32_t)subject_length) != 0 ||
                mp_cache_buffer_append_u32(out_buffer, (uint32_t)extra_value) != 0 ||
                mp_cache_buffer_append(out_buffer, value, MP_CACHE_TOKEN_HASH_SIZE) != 0 ||
                mp_cache_buffer_append(out_buffer, subject, subject_length) != 0) {
                return -1;
            }
            return 0;
        case MP_CACHE_JOURNAL_OP_PURGE_ALL:
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

/* Replay one decrypted journal payload against the live store and security state. */
static int mp_cache_storage_apply_journal_payload(
    const uint8_t *payload,
    size_t payload_length,
    mp_cache_store_t *store,
    mp_cache_security_t *security,
    int64_t now_utc_seconds) {
    size_t offset = 0u;
    uint32_t version = 0u;
    uint32_t operation = 0u;
    uint64_t recorded_at = 0u;

    if (payload == NULL || store == NULL || security == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (mp_cache_consume_u32(payload, payload_length, &offset, &version) != 0 ||
        mp_cache_consume_u32(payload, payload_length, &offset, &operation) != 0 ||
        mp_cache_consume_u64(payload, payload_length, &offset, &recorded_at) != 0 ||
        version != MP_CACHE_JOURNAL_VERSION) {
        errno = EINVAL;
        return -1;
    }

    switch ((mp_cache_journal_op_t)operation) {
        case MP_CACHE_JOURNAL_OP_SET: {
            uint32_t key_length = 0u;
            uint32_t value_length = 0u;
            uint64_t expires_at = 0u;
            const uint8_t *key = NULL;
            const uint8_t *value = NULL;

            if (mp_cache_consume_u32(payload, payload_length, &offset, &key_length) != 0 ||
                mp_cache_consume_u32(payload, payload_length, &offset, &value_length) != 0 ||
                mp_cache_consume_u64(payload, payload_length, &offset, &expires_at) != 0 ||
                mp_cache_consume_bytes(payload, payload_length, &offset, key_length, &key) != 0 ||
                mp_cache_consume_bytes(payload, payload_length, &offset, value_length, &value) != 0) {
                return -1;
            }
            if ((int64_t)expires_at > now_utc_seconds &&
                mp_cache_store_restore_entry(store, key, key_length, value, value_length, (int64_t)expires_at) !=
                    MP_CACHE_STORE_STATUS_OK) {
                errno = EINVAL;
                return -1;
            }
            return offset == payload_length ? 0 : -1;
        }
        case MP_CACHE_JOURNAL_OP_DELETE: {
            uint32_t key_length = 0u;
            const uint8_t *key = NULL;

            if (mp_cache_consume_u32(payload, payload_length, &offset, &key_length) != 0 ||
                mp_cache_consume_bytes(payload, payload_length, &offset, key_length, &key) != 0) {
                return -1;
            }
            (void)mp_cache_store_delete(store, key, key_length);
            return offset == payload_length ? 0 : -1;
        }
        case MP_CACHE_JOURNAL_OP_CLIENT_UPSERT: {
            uint32_t id_length = 0u;
            uint32_t role_value = 0u;
            const uint8_t *token_hash = NULL;
            const uint8_t *client_id_bytes = NULL;
            char client_id[MP_CACHE_CLIENT_ID_CAP];

            if (mp_cache_consume_u32(payload, payload_length, &offset, &id_length) != 0 ||
                mp_cache_consume_u32(payload, payload_length, &offset, &role_value) != 0 ||
                mp_cache_consume_bytes(payload, payload_length, &offset, MP_CACHE_TOKEN_HASH_SIZE, &token_hash) != 0 ||
                id_length == 0u || id_length >= sizeof(client_id) ||
                mp_cache_consume_bytes(payload, payload_length, &offset, id_length, &client_id_bytes) != 0) {
                return -1;
            }
            memcpy(client_id, client_id_bytes, id_length);
            client_id[id_length] = '\0';
            if (mp_cache_security_import_client_hash(
                    security,
                    client_id,
                    (mp_cache_role_t)role_value,
                    token_hash) != MP_CACHE_SECURITY_STATUS_OK) {
                errno = EINVAL;
                return -1;
            }
            return offset == payload_length ? 0 : -1;
        }
        case MP_CACHE_JOURNAL_OP_PURGE_ALL:
            mp_cache_store_clear(store);
            return offset == payload_length ? 0 : -1;
        default:
            errno = EINVAL;
            return -1;
    }
}

/* Count existing export artifacts so retention can be enforced before writing a new one. */
static int mp_cache_storage_export_count(mp_cache_storage_t *storage, size_t *out_count) {
    DIR *directory = NULL;
    struct dirent *entry = NULL;
    size_t count = 0u;

    if (storage == NULL || out_count == NULL) {
        errno = EINVAL;
        return -1;
    }

    directory = opendir(storage->export_directory);
    if (directory == NULL) {
        return errno == ENOENT ? (*out_count = 0u, 0) : -1;
    }

    while ((entry = readdir(directory)) != NULL) {
        if (strncmp(entry->d_name, "mp-cache-export-", 16u) == 0) {
            count++;
        }
    }

    (void)closedir(directory);
    *out_count = count;
    return 0;
}

/* Resolve an import path relative to export_directory when the caller passes a bare file name. */
static int mp_cache_storage_resolve_import_path(
    mp_cache_storage_t *storage,
    const char *requested_import_path,
    char *resolved_import_path,
    size_t out_capacity) {
    struct stat file_status;

    if (storage == NULL || requested_import_path == NULL || resolved_import_path == NULL || out_capacity == 0u) {
        errno = EINVAL;
        return -1;
    }

    if (strstr(requested_import_path, "../") != NULL || strstr(requested_import_path, "/..") != NULL) {
        errno = EACCES;
        return -1;
    }
    if (requested_import_path[0] == '/') {
        if (strncmp(requested_import_path, storage->export_directory, strlen(storage->export_directory)) != 0) {
            errno = EACCES;
            return -1;
        }
        if (mp_cache_storage_format_path(resolved_import_path, out_capacity, "%s", requested_import_path) != 0) {
            return -1;
        }
    } else if (strncmp(requested_import_path, storage->export_directory, strlen(storage->export_directory)) == 0) {
        if (mp_cache_storage_format_path(resolved_import_path, out_capacity, "%s", requested_import_path) != 0) {
            return -1;
        }
    } else {
        if (mp_cache_storage_format_path(
                resolved_import_path,
                out_capacity,
                "%s/%s",
                storage->export_directory,
                requested_import_path) != 0) {
            return -1;
        }
    }
    if (stat(resolved_import_path, &file_status) != 0 || S_ISREG(file_status.st_mode) == 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int mp_cache_storage_init(mp_cache_storage_t *storage, const mp_cache_config_t *config, mp_cache_log_t *log) {
    char storage_secret[MP_CACHE_SECRET_REF_CAP];

    if (storage == NULL || config == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(storage, 0, sizeof(*storage));
    if (mp_cache_storage_format_path(storage->checkpoint_path, sizeof(storage->checkpoint_path), "%s", config->checkpoint_path) != 0 ||
        mp_cache_storage_format_path(storage->journal_path, sizeof(storage->journal_path), "%s", config->journal_path) != 0 ||
        mp_cache_storage_format_path(
            storage->export_directory,
            sizeof(storage->export_directory),
            "%s",
            config->export_directory) != 0) {
        return -1;
    }
    storage->max_export_files = config->max_export_files;
    storage->max_packet_bytes = config->memory_limit_bytes + (16u * 1024u * 1024u);
    storage->log = log;

    if (mp_cache_secret_resolve(config->storage_key_secret_ref, storage_secret, sizeof(storage_secret)) != 0) {
        return -1;
    }
    mp_cache_secret_derive_key(storage_secret, storage->storage_key);
    return 0;
}

void mp_cache_storage_destroy(mp_cache_storage_t *storage) {
    if (storage == NULL) {
        return;
    }

    memset(storage, 0, sizeof(*storage));
}

int mp_cache_storage_load_state(
    mp_cache_storage_t *storage,
    mp_cache_store_t *store,
    mp_cache_security_t *security,
    int64_t now_utc_seconds) {
    uint8_t *payload = NULL;
    size_t payload_length = 0u;
    FILE *journal = NULL;
    int result = 0;

    if (storage == NULL || store == NULL || security == NULL) {
        errno = EINVAL;
        return -1;
    }

    mp_cache_store_clear(store);
    mp_cache_security_clear_clients(security);

    result = mp_cache_storage_read_packet_file(storage, storage->checkpoint_path, MP_CACHE_STATE_MAGIC, &payload, &payload_length, NULL);
    if (result < 0) {
        mp_cache_storage_log(storage, MP_LOG_LEVEL_ERROR, "failed to read checkpoint state");
        free(payload);
        return -1;
    }
    if (result == 0) {
        if (mp_cache_storage_deserialize_state(payload, payload_length, store, security, now_utc_seconds) != 0) {
            mp_cache_storage_log(storage, MP_LOG_LEVEL_ERROR, "failed to deserialize checkpoint state");
            free(payload);
            return -1;
        }
        free(payload);
        payload = NULL;
    }

    journal = fopen(storage->journal_path, "rb");
    if (journal == NULL) {
        return errno == ENOENT ? 0 : -1;
    }

    for (;;) {
        payload_length = 0u;
        result = mp_cache_storage_read_packet_stream(storage, journal, MP_CACHE_JOURNAL_MAGIC, &payload, &payload_length, NULL);
        if (result == 1) {
            break;
        }
        if (result != 0 || mp_cache_storage_apply_journal_payload(payload, payload_length, store, security, now_utc_seconds) != 0) {
            free(payload);
            (void)fclose(journal);
            mp_cache_storage_log(storage, MP_LOG_LEVEL_ERROR, "failed to replay journal state");
            return -1;
        }
        free(payload);
        payload = NULL;
    }

    (void)fclose(journal);
    return 0;
}

int mp_cache_storage_checkpoint(
    mp_cache_storage_t *storage,
    const mp_cache_store_t *store,
    const mp_cache_security_t *security,
    int64_t now_utc_seconds) {
    mp_cache_buffer_t payload = {0};

    if (storage == NULL || store == NULL || security == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (mp_cache_storage_serialize_state(store, security, now_utc_seconds, &payload) != 0 ||
        mp_cache_storage_write_packet_file(
            storage,
            storage->checkpoint_path,
            MP_CACHE_STATE_MAGIC,
            payload.bytes,
            payload.length,
            NULL) != 0) {
        mp_cache_buffer_destroy(&payload);
        mp_cache_storage_log(storage, MP_LOG_LEVEL_ERROR, "failed to write checkpoint state");
        return -1;
    }

    mp_cache_buffer_destroy(&payload);
    if (mp_cache_fs_remove_path_if_exists(storage->journal_path) != 0) {
        return -1;
    }

    return 0;
}

int mp_cache_storage_append_set(
    mp_cache_storage_t *storage,
    const char *key,
    size_t key_length,
    const uint8_t *value,
    size_t value_length,
    int64_t expires_at_utc_seconds) {
    mp_cache_buffer_t payload = {0};
    int result = 0;

    if (storage == NULL || key == NULL || value == NULL) {
        errno = EINVAL;
        return -1;
    }

    result = mp_cache_storage_build_journal_payload(
        MP_CACHE_JOURNAL_OP_SET,
        time(NULL),
        key,
        key_length,
        value,
        value_length,
        (uint64_t)expires_at_utc_seconds,
        &payload);
    if (result == 0) {
        result = mp_cache_storage_append_packet(storage, storage->journal_path, MP_CACHE_JOURNAL_MAGIC, payload.bytes, payload.length);
    }
    mp_cache_buffer_destroy(&payload);
    return result;
}

int mp_cache_storage_append_delete(mp_cache_storage_t *storage, const char *key, size_t key_length) {
    mp_cache_buffer_t payload = {0};
    int result = 0;

    if (storage == NULL || key == NULL) {
        errno = EINVAL;
        return -1;
    }

    result = mp_cache_storage_build_journal_payload(
        MP_CACHE_JOURNAL_OP_DELETE,
        time(NULL),
        key,
        key_length,
        NULL,
        0u,
        0u,
        &payload);
    if (result == 0) {
        result = mp_cache_storage_append_packet(storage, storage->journal_path, MP_CACHE_JOURNAL_MAGIC, payload.bytes, payload.length);
    }
    mp_cache_buffer_destroy(&payload);
    return result;
}

int mp_cache_storage_append_client(mp_cache_storage_t *storage, const mp_cache_client_record_t *record) {
    mp_cache_buffer_t payload = {0};
    int result = 0;

    if (storage == NULL || record == NULL) {
        errno = EINVAL;
        return -1;
    }

    result = mp_cache_storage_build_journal_payload(
        MP_CACHE_JOURNAL_OP_CLIENT_UPSERT,
        time(NULL),
        record->client_id,
        strlen(record->client_id),
        record->token_hash,
        MP_CACHE_TOKEN_HASH_SIZE,
        (uint64_t)record->role,
        &payload);
    if (result == 0) {
        result = mp_cache_storage_append_packet(storage, storage->journal_path, MP_CACHE_JOURNAL_MAGIC, payload.bytes, payload.length);
    }
    mp_cache_buffer_destroy(&payload);
    return result;
}

int mp_cache_storage_append_purge_all(mp_cache_storage_t *storage) {
    mp_cache_buffer_t payload = {0};
    int result = 0;

    if (storage == NULL) {
        errno = EINVAL;
        return -1;
    }

    result = mp_cache_storage_build_journal_payload(
        MP_CACHE_JOURNAL_OP_PURGE_ALL,
        time(NULL),
        NULL,
        0u,
        NULL,
        0u,
        0u,
        &payload);
    if (result == 0) {
        result = mp_cache_storage_append_packet(storage, storage->journal_path, MP_CACHE_JOURNAL_MAGIC, payload.bytes, payload.length);
    }
    mp_cache_buffer_destroy(&payload);
    return result;
}

int mp_cache_storage_export_state(
    mp_cache_storage_t *storage,
    const mp_cache_store_t *store,
    const mp_cache_security_t *security,
    int64_t now_utc_seconds,
    mp_cache_export_result_t *out_result) {
    mp_cache_buffer_t payload = {0};
    uint8_t export_mac[MP_CACHE_TOKEN_HASH_SIZE];
    size_t export_count = 0u;

    if (storage == NULL || store == NULL || security == NULL || out_result == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (mp_cache_storage_export_count(storage, &export_count) != 0) {
        return -1;
    }
    if (export_count >= storage->max_export_files) {
        errno = ENOSPC;
        return -1;
    }

    if (mp_cache_storage_checkpoint(storage, store, security, now_utc_seconds) != 0) {
        return -1;
    }
    if (mp_cache_storage_serialize_state(store, security, now_utc_seconds, &payload) != 0) {
        mp_cache_buffer_destroy(&payload);
        return -1;
    }

    memset(out_result, 0, sizeof(*out_result));
    out_result->entry_count = 0u;
    out_result->client_count = mp_cache_security_client_count(security);
    if (mp_cache_storage_format_path(
            out_result->export_path,
            sizeof(out_result->export_path),
            "%s/mp-cache-export-%ld-%ld.bin",
            storage->export_directory,
            (long)now_utc_seconds,
            (long)getpid()) != 0) {
        mp_cache_buffer_destroy(&payload);
        return -1;
    }

    if (mp_cache_store_for_each(store, mp_cache_count_active_entry, &(mp_cache_active_count_t){0, now_utc_seconds}) == 0) {
        mp_cache_active_count_t counter = {0, now_utc_seconds};
        (void)mp_cache_store_for_each(store, mp_cache_count_active_entry, &counter);
        out_result->entry_count = counter.count;
    }

    if (mp_cache_storage_write_packet_file(
            storage,
            out_result->export_path,
            MP_CACHE_EXPORT_MAGIC,
            payload.bytes,
            payload.length,
            export_mac) != 0 ||
        mp_cache_hex_encode(export_mac, sizeof(export_mac), out_result->digest_hex, sizeof(out_result->digest_hex)) != 0) {
        mp_cache_buffer_destroy(&payload);
        return -1;
    }

    mp_cache_buffer_destroy(&payload);
    return 0;
}

int mp_cache_storage_import_state(
    mp_cache_storage_t *storage,
    const char *import_path,
    mp_cache_store_t *store,
    mp_cache_security_t *security,
    int64_t now_utc_seconds) {
    char resolved_path[MP_CACHE_PATH_CAP];
    uint8_t *payload = NULL;
    size_t payload_length = 0u;
    mp_cache_store_t imported_store;
    mp_cache_security_t imported_security;
    int result = -1;

    if (storage == NULL || import_path == NULL || store == NULL || security == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (mp_cache_storage_resolve_import_path(storage, import_path, resolved_path, sizeof(resolved_path)) != 0) {
        return -1;
    }
    if (mp_cache_storage_read_packet_file(storage, resolved_path, MP_CACHE_EXPORT_MAGIC, &payload, &payload_length, NULL) != 0) {
        free(payload);
        return -1;
    }

    memset(&imported_store, 0, sizeof(imported_store));
    imported_security = *security;
    imported_security.records = NULL;
    imported_security.count = 0u;
    imported_security.capacity = 0u;

    if (mp_cache_store_init(&imported_store, &(mp_cache_config_t){
                                             .bucket_count = (uint32_t)store->bucket_count,
                                             .memory_limit_bytes = store->memory_limit_bytes,
                                             .max_key_bytes = store->max_key_bytes,
                                             .max_value_bytes = store->max_value_bytes}) != 0) {
        free(payload);
        return -1;
    }

    if (mp_cache_storage_deserialize_state(payload, payload_length, &imported_store, &imported_security, now_utc_seconds) != 0) {
        mp_cache_store_destroy(&imported_store);
        mp_cache_security_destroy(&imported_security);
        free(payload);
        return -1;
    }

    mp_cache_store_clear(store);
    mp_cache_security_clear_clients(security);

    if (mp_cache_storage_deserialize_state(payload, payload_length, store, security, now_utc_seconds) == 0 &&
        mp_cache_storage_checkpoint(storage, store, security, now_utc_seconds) == 0) {
        result = 0;
    }

    mp_cache_store_destroy(&imported_store);
    mp_cache_security_destroy(&imported_security);
    free(payload);
    return result;
}
