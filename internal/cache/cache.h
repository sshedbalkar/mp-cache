#ifndef MP_CACHE_INTERNAL_CACHE_H
#define MP_CACHE_INTERNAL_CACHE_H

#include "internal/config/config.h"

#include <stddef.h>
#include <stdint.h>

/* Opaque chained-hash entry owned internally by mp_cache_store_t. */
typedef struct mp_cache_entry mp_cache_entry_t;

/*
 * Owns the bounded in-memory cache buckets and accounting derived from config.
 * Call mp_cache_store_init() before use and mp_cache_store_destroy() on shutdown.
 */
typedef struct {
    /* Hash buckets that anchor the chained entries. */
    mp_cache_entry_t **buckets;
    /* Fixed number of buckets allocated at initialization time. */
    size_t bucket_count;
    /* Current number of live entries across every bucket. */
    size_t entry_count;
    /* Approximate heap usage consumed by entries, keys, and values. */
    size_t bytes_used;
    /* Hard memory ceiling enforced before new or replacement writes. */
    uint64_t memory_limit_bytes;
    /* Maximum accepted cache-key length in bytes. */
    uint32_t max_key_bytes;
    /* Maximum accepted cache-value length in bytes. */
    uint32_t max_value_bytes;
} mp_cache_store_t;

/* Reports a snapshot of the current store occupancy and configured ceiling. */
typedef struct {
    size_t entry_count;
    size_t bytes_used;
    uint64_t memory_limit_bytes;
} mp_cache_store_stats_t;

/*
 * Visits one active cache entry during mp_cache_store_for_each().
 * Returning non-zero stops the walk and propagates that value to the caller.
 */
typedef int (*mp_cache_store_visit_fn)(
    const char *entry_key,
    size_t entry_key_length,
    const uint8_t *entry_value,
    size_t entry_value_length,
    int64_t expires_at_utc_seconds,
    void *context);

/* Names the stable outcomes returned by cache store operations. */
typedef enum {
    MP_CACHE_STORE_STATUS_OK = 0,
    MP_CACHE_STORE_STATUS_NOT_FOUND = 1,
    MP_CACHE_STORE_STATUS_EXPIRED = 2,
    MP_CACHE_STORE_STATUS_INVALID_ARGUMENT = 3,
    MP_CACHE_STORE_STATUS_LIMIT_EXCEEDED = 4,
    MP_CACHE_STORE_STATUS_NO_MEMORY = 5
} mp_cache_store_status_t;

/* Allocate bucket storage and copy the configured store limits into store. */
int mp_cache_store_init(mp_cache_store_t *store, const mp_cache_config_t *config);

/* Release every entry and reset store to an all-zero state. */
void mp_cache_store_destroy(mp_cache_store_t *store);

/*
 * Insert or replace a key with a TTL-relative expiration timestamp.
 * Returns LIMIT_EXCEEDED when the key, value, or projected memory use violates config.
 */
mp_cache_store_status_t mp_cache_store_set(
    mp_cache_store_t *store,
    const void *cache_key,
    size_t cache_key_length,
    const void *cache_value,
    size_t cache_value_length,
    uint32_t ttl_seconds,
    int64_t now_utc_seconds);

/*
 * Copy the stored value for cache_key into a newly allocated buffer owned by the caller.
 * Expired entries are removed eagerly and reported as MP_CACHE_STORE_STATUS_EXPIRED.
 */
mp_cache_store_status_t mp_cache_store_get_copy(
    mp_cache_store_t *store,
    const void *cache_key,
    size_t cache_key_length,
    int64_t now_utc_seconds,
    uint8_t **out_value_copy,
    size_t *out_value_copy_length,
    int64_t *out_expires_at_utc_seconds);

/* Delete cache_key if present. */
mp_cache_store_status_t mp_cache_store_delete(
    mp_cache_store_t *store,
    const void *cache_key,
    size_t cache_key_length);

/*
 * Restore a checkpoint or import entry whose expiration is already expressed in UTC seconds.
 * Unlike mp_cache_store_set(), this call does not derive the expiration from a TTL.
 */
mp_cache_store_status_t mp_cache_store_restore_entry(
    mp_cache_store_t *store,
    const void *cache_key,
    size_t cache_key_length,
    const void *cache_value,
    size_t cache_value_length,
    int64_t expires_at_utc_seconds);

/* Remove every entry from the store without changing configured limits. */
void mp_cache_store_clear(mp_cache_store_t *store);

/* Remove all entries whose expiration is at or before now_utc_seconds and return the purge count. */
size_t mp_cache_store_purge_expired(mp_cache_store_t *store, int64_t now_utc_seconds);

/* Walk every currently stored entry until visit returns non-zero or the walk completes. */
int mp_cache_store_for_each(const mp_cache_store_t *store, mp_cache_store_visit_fn visit, void *context);

/* Copy the current entry count, bytes used, and memory limit into out_stats. */
void mp_cache_store_get_stats(const mp_cache_store_t *store, mp_cache_store_stats_t *out_stats);

/* Return a stable uppercase-style name for status. */
const char *mp_cache_store_status_name(mp_cache_store_status_t status);

#endif
