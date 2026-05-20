#ifndef MP_CACHE_INTERNAL_CONFIG_H
#define MP_CACHE_INTERNAL_CONFIG_H

#include "internal/config/constants.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Carries the fully resolved non-secret bootstrap configuration for one service instance.
 * Every path field is null-terminated and sized for direct use by runtime modules.
 */
typedef struct {
    /* Stable service identifier emitted in user-facing output and logs. */
    char service_name[MP_CACHE_NAME_CAP];
    /* Environment label such as local, qa, staging, or production. */
    char environment_name[MP_CACHE_TEXT_CAP];
    /* Build artifact version in MAJOR.MINOR.HOTFIX format. */
    char build_version[MP_CACHE_TEXT_CAP];
    /* Unix domain socket used for local control-plane HTTP traffic. */
    char socket_path[MP_CACHE_PATH_CAP];
    /* PID file path written by the runtime after startup. */
    char pid_file_path[MP_CACHE_PATH_CAP];
    /* Root directory for local persistent state. */
    char data_directory[MP_CACHE_PATH_CAP];
    /* Directory that stores encrypted export artifacts. */
    char export_directory[MP_CACHE_PATH_CAP];
    /* Encrypted checkpoint file path. */
    char checkpoint_path[MP_CACHE_PATH_CAP];
    /* Encrypted append-only journal file path. */
    char journal_path[MP_CACHE_PATH_CAP];
    /* Directory that stores mp_logger output files. */
    char log_directory[MP_CACHE_PATH_CAP];
    /* Subproject bootstrap file used before parent overrides are applied. */
    char logger_bootstrap_path[MP_CACHE_PATH_CAP];
    char logger_format[MP_CACHE_TEXT_CAP];
    char logger_file_name_prefix[MP_CACHE_NAME_CAP];
    char logger_backup_file_name_prefix[MP_CACHE_NAME_CAP];
    char logger_active_streams[MP_CACHE_TEXT_CAP];
    char logger_stdout_min_level[MP_CACHE_TEXT_CAP];
    char logger_stdout_max_level[MP_CACHE_TEXT_CAP];
    char logger_stderr_min_level[MP_CACHE_TEXT_CAP];
    char logger_stderr_max_level[MP_CACHE_TEXT_CAP];
    char logger_file_min_level[MP_CACHE_TEXT_CAP];
    char logger_file_max_level[MP_CACHE_TEXT_CAP];
    char logger_udp_min_level[MP_CACHE_TEXT_CAP];
    char logger_udp_max_level[MP_CACHE_TEXT_CAP];
    char logger_udp_host[MP_CACHE_TEXT_CAP];
    /* Runtime secret reference for the bootstrap admin bearer token. */
    char bootstrap_admin_token_secret_ref[MP_CACHE_SECRET_REF_CAP];
    /* Runtime secret reference used to derive the persistence key. */
    char storage_key_secret_ref[MP_CACHE_SECRET_REF_CAP];
    /* Maximum total cache memory available to live entries. */
    uint64_t memory_limit_bytes;
    /* Default TTL applied when a request does not supply one explicitly. */
    uint32_t default_ttl_seconds;
    /* Minimum accepted TTL for write requests. */
    uint32_t min_ttl_seconds;
    /* Maximum accepted TTL for write requests. */
    uint32_t max_ttl_seconds;
    /* Maximum accepted cache-key length. */
    uint32_t max_key_bytes;
    /* Maximum accepted cache-value length. */
    uint32_t max_value_bytes;
    /* Bucket count used by the in-memory hash table. */
    uint32_t bucket_count;
    /* Maximum number of retained export files on disk. */
    uint32_t max_export_files;
    /* Grace period for runtime shutdown and logger drain. */
    uint32_t shutdown_timeout_millis;
    /* Periodic sweep interval for expired-entry cleanup. */
    uint32_t sweep_interval_seconds;
    /* Maximum number of lines returned by log-tail endpoints. */
    uint32_t max_log_lines;
    uint32_t logger_buffer_capacity;
    uint32_t logger_message_capacity;
    uint32_t logger_context_capacity;
    uint32_t logger_field_capacity;
    uint32_t logger_field_key_capacity;
    uint32_t logger_field_value_capacity;
    uint32_t logger_pretty_output;
    uint32_t logger_udp_port;
    /* Per-principal request budget within one rate-limit window. */
    uint32_t rate_limit_requests;
    /* Length of the rate-limit window in seconds. */
    uint32_t rate_limit_window_seconds;
} mp_cache_config_t;

/* Describes the stable outcomes returned by config parsing and validation. */
typedef enum {
    MP_CACHE_CONFIG_STATUS_OK = 0,
    MP_CACHE_CONFIG_STATUS_NOT_FOUND = 1,
    MP_CACHE_CONFIG_STATUS_IO_ERROR = 2,
    MP_CACHE_CONFIG_STATUS_PARSE_ERROR = 3,
    MP_CACHE_CONFIG_STATUS_VALIDATION_ERROR = 4,
    MP_CACHE_CONFIG_STATUS_INVALID_ARGUMENT = 5
} mp_cache_config_status_t;

/* Populate config with the compiled bootstrap defaults used when no file is present. */
void mp_cache_config_init_defaults(mp_cache_config_t *config);

/* Load, parse, and validate one YAML config file into config. */
mp_cache_config_status_t mp_cache_config_load_file(
    const char *config_path,
    mp_cache_config_t *config);

/* Write a commented config template to template_path using config as the source values. */
mp_cache_config_status_t mp_cache_config_write_template(
    const char *template_path,
    const mp_cache_config_t *config);

/* Print the effective config in a human-readable format for diagnostics and tooling. */
void mp_cache_config_print(FILE *stream, const mp_cache_config_t *config, const char *source_path);

/* Return a stable name for status suitable for diagnostics. */
const char *mp_cache_config_status_name(mp_cache_config_status_t status);

#endif
