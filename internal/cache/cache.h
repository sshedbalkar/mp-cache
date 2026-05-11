#ifndef MP_CACHE_INTERNAL_CACHE_H
#define MP_CACHE_INTERNAL_CACHE_H

#include "internal/config/config.h"

#include <stddef.h>
#include <stdint.h>

typedef struct mp_cache_entry mp_cache_entry_t;

typedef struct {
    mp_cache_entry_t **buckets;
    size_t bucket_count;
    size_t entry_count;
    size_t bytes_used;
    uint64_t memory_limit_bytes;
    uint32_t max_key_bytes;
    uint32_t max_value_bytes;
} mp_cache_store_t;

typedef struct {
    size_t entry_count;
    size_t bytes_used;
    uint64_t memory_limit_bytes;
} mp_cache_store_stats_t;

typedef enum {
    MP_CACHE_STORE_STATUS_OK = 0,
    MP_CACHE_STORE_STATUS_NOT_FOUND = 1,
    MP_CACHE_STORE_STATUS_EXPIRED = 2,
    MP_CACHE_STORE_STATUS_INVALID_ARGUMENT = 3,
    MP_CACHE_STORE_STATUS_LIMIT_EXCEEDED = 4,
    MP_CACHE_STORE_STATUS_NO_MEMORY = 5
} mp_cache_store_status_t;

int mp_cache_store_init(mp_cache_store_t *store, const mp_cache_config_t *config);
void mp_cache_store_destroy(mp_cache_store_t *store);

mp_cache_store_status_t mp_cache_store_set(
    mp_cache_store_t *store,
    const void *key,
    size_t key_length,
    const void *value,
    size_t value_length,
    uint32_t ttl_seconds,
    int64_t now_utc_seconds);

mp_cache_store_status_t mp_cache_store_get_copy(
    mp_cache_store_t *store,
    const void *key,
    size_t key_length,
    int64_t now_utc_seconds,
    uint8_t **out_value,
    size_t *out_value_length,
    int64_t *out_expires_at_utc_seconds);

mp_cache_store_status_t mp_cache_store_delete(
    mp_cache_store_t *store,
    const void *key,
    size_t key_length);

size_t mp_cache_store_purge_expired(mp_cache_store_t *store, int64_t now_utc_seconds);
void mp_cache_store_get_stats(const mp_cache_store_t *store, mp_cache_store_stats_t *out_stats);

const char *mp_cache_store_status_name(mp_cache_store_status_t status);

#endif
