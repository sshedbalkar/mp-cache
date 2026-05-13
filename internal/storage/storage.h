#ifndef MP_CACHE_INTERNAL_STORAGE_H
#define MP_CACHE_INTERNAL_STORAGE_H

#include "internal/cache/cache.h"
#include "internal/config/config.h"
#include "internal/observability/log.h"
#include "internal/security/security.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Owns the encrypted persistence paths, derived key material, and logger reference.
 * Call mp_cache_storage_init() before use and mp_cache_storage_destroy() on shutdown.
 */
typedef struct {
    char checkpoint_path[MP_CACHE_PATH_CAP];
    char journal_path[MP_CACHE_PATH_CAP];
    char export_directory[MP_CACHE_PATH_CAP];
    uint8_t storage_key[MP_CACHE_TOKEN_HASH_SIZE];
    uint64_t max_packet_bytes;
    uint32_t max_export_files;
    mp_cache_log_t *log;
} mp_cache_storage_t;

/* Describes one completed export artifact and the counts captured within it. */
typedef struct {
    char export_path[MP_CACHE_PATH_CAP];
    char digest_hex[(MP_CACHE_TOKEN_HASH_SIZE * 2u) + 1u];
    size_t entry_count;
    size_t client_count;
} mp_cache_export_result_t;

/* Initialize storage paths, derive the persistence key, and create required directories. */
int mp_cache_storage_init(mp_cache_storage_t *storage, const mp_cache_config_t *config, mp_cache_log_t *log);

/* Clear any in-memory storage state retained by the module. */
void mp_cache_storage_destroy(mp_cache_storage_t *storage);

/* Load checkpoint and journal state into store and security, discarding expired entries by time. */
int mp_cache_storage_load_state(
    mp_cache_storage_t *storage,
    mp_cache_store_t *store,
    mp_cache_security_t *security,
    int64_t now_utc_seconds);

/* Write a full encrypted checkpoint and truncate journal replay state after success. */
int mp_cache_storage_checkpoint(
    mp_cache_storage_t *storage,
    const mp_cache_store_t *store,
    const mp_cache_security_t *security,
    int64_t now_utc_seconds);

/* Append a SET journal operation for one cache entry. */
int mp_cache_storage_append_set(
    mp_cache_storage_t *storage,
    const char *key,
    size_t key_length,
    const uint8_t *value,
    size_t value_length,
    int64_t expires_at_utc_seconds);

/* Append a DELETE journal operation for key. */
int mp_cache_storage_append_delete(mp_cache_storage_t *storage, const char *key, size_t key_length);

/* Append a client upsert journal operation for one registered principal. */
int mp_cache_storage_append_client(
    mp_cache_storage_t *storage,
    const mp_cache_client_record_t *record);

/* Append a journal marker that clears all live cache entries before subsequent replay. */
int mp_cache_storage_append_purge_all(mp_cache_storage_t *storage);

/* Export the current store and client registry into an encrypted artifact under export_directory. */
int mp_cache_storage_export_state(
    mp_cache_storage_t *storage,
    const mp_cache_store_t *store,
    const mp_cache_security_t *security,
    int64_t now_utc_seconds,
    mp_cache_export_result_t *out_result);

/* Import an encrypted export artifact, replacing the current store and client registry contents. */
int mp_cache_storage_import_state(
    mp_cache_storage_t *storage,
    const char *import_path,
    mp_cache_store_t *store,
    mp_cache_security_t *security,
    int64_t now_utc_seconds);

#endif
