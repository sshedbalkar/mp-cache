#ifndef MP_CACHE_INTERNAL_STORAGE_H
#define MP_CACHE_INTERNAL_STORAGE_H

#include "internal/cache/cache.h"
#include "internal/config/config.h"
#include "internal/observability/log.h"
#include "internal/security/security.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char checkpoint_path[MP_CACHE_PATH_CAP];
    char journal_path[MP_CACHE_PATH_CAP];
    char export_directory[MP_CACHE_PATH_CAP];
    uint8_t key[MP_CACHE_TOKEN_HASH_SIZE];
    uint64_t max_packet_bytes;
    uint32_t max_export_files;
    mp_cache_log_t *log;
} mp_cache_storage_t;

typedef struct {
    char path[MP_CACHE_PATH_CAP];
    char digest_hex[(MP_CACHE_TOKEN_HASH_SIZE * 2u) + 1u];
    size_t entry_count;
    size_t client_count;
} mp_cache_export_result_t;

int mp_cache_storage_init(mp_cache_storage_t *storage, const mp_cache_config_t *config, mp_cache_log_t *log);
void mp_cache_storage_destroy(mp_cache_storage_t *storage);

int mp_cache_storage_load_state(
    mp_cache_storage_t *storage,
    mp_cache_store_t *store,
    mp_cache_security_t *security,
    int64_t now_utc_seconds);
int mp_cache_storage_checkpoint(
    mp_cache_storage_t *storage,
    const mp_cache_store_t *store,
    const mp_cache_security_t *security,
    int64_t now_utc_seconds);

int mp_cache_storage_append_set(
    mp_cache_storage_t *storage,
    const char *key,
    size_t key_length,
    const uint8_t *value,
    size_t value_length,
    int64_t expires_at_utc_seconds);
int mp_cache_storage_append_delete(mp_cache_storage_t *storage, const char *key, size_t key_length);
int mp_cache_storage_append_client(
    mp_cache_storage_t *storage,
    const mp_cache_client_record_t *record);
int mp_cache_storage_append_purge_all(mp_cache_storage_t *storage);

int mp_cache_storage_export_state(
    mp_cache_storage_t *storage,
    const mp_cache_store_t *store,
    const mp_cache_security_t *security,
    int64_t now_utc_seconds,
    mp_cache_export_result_t *out_result);
int mp_cache_storage_import_state(
    mp_cache_storage_t *storage,
    const char *path,
    mp_cache_store_t *store,
    mp_cache_security_t *security,
    int64_t now_utc_seconds);

#endif
