#ifndef MP_CACHE_INTERNAL_CONFIG_H
#define MP_CACHE_INTERNAL_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MP_CACHE_TEXT_CAP 128u
#define MP_CACHE_NAME_CAP 64u
#define MP_CACHE_SECRET_REF_CAP 256u
#define MP_CACHE_PATH_CAP 4096u

typedef struct {
    char service_name[MP_CACHE_NAME_CAP];
    char environment_name[MP_CACHE_TEXT_CAP];
    char socket_path[MP_CACHE_PATH_CAP];
    char pid_file_path[MP_CACHE_PATH_CAP];
    char data_directory[MP_CACHE_PATH_CAP];
    char export_directory[MP_CACHE_PATH_CAP];
    char log_directory[MP_CACHE_PATH_CAP];
    char bootstrap_admin_token_secret_ref[MP_CACHE_SECRET_REF_CAP];
    char storage_key_secret_ref[MP_CACHE_SECRET_REF_CAP];
    uint64_t memory_limit_bytes;
    uint32_t default_ttl_seconds;
    uint32_t min_ttl_seconds;
    uint32_t max_ttl_seconds;
    uint32_t max_key_bytes;
    uint32_t max_value_bytes;
    uint32_t bucket_count;
    uint32_t max_export_files;
    uint32_t shutdown_timeout_millis;
    uint32_t sweep_interval_seconds;
} mp_cache_config_t;

typedef enum {
    MP_CACHE_CONFIG_STATUS_OK = 0,
    MP_CACHE_CONFIG_STATUS_NOT_FOUND = 1,
    MP_CACHE_CONFIG_STATUS_IO_ERROR = 2,
    MP_CACHE_CONFIG_STATUS_PARSE_ERROR = 3,
    MP_CACHE_CONFIG_STATUS_VALIDATION_ERROR = 4,
    MP_CACHE_CONFIG_STATUS_INVALID_ARGUMENT = 5
} mp_cache_config_status_t;

void mp_cache_config_init_defaults(mp_cache_config_t *config);

mp_cache_config_status_t mp_cache_config_load_file(
    const char *path,
    mp_cache_config_t *config);

mp_cache_config_status_t mp_cache_config_write_template(
    const char *path,
    const mp_cache_config_t *config);

void mp_cache_config_print(FILE *stream, const mp_cache_config_t *config, const char *source_path);

const char *mp_cache_config_status_name(mp_cache_config_status_t status);

#endif
