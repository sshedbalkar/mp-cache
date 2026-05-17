#include "internal/config/config.h"

#include "internal/platform/fs.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Copy source into destination with guaranteed NUL termination. */
static void mp_cache_copy_string(char *destination, size_t destination_capacity, const char *source) {
    if (destination == NULL || destination_capacity == 0u) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    (void)snprintf(destination, destination_capacity, "%s", source);
}

/* Trim leading and trailing ASCII whitespace in place. */
static char *mp_cache_trim(char *value) {
    char *end = NULL;

    while (*value != '\0' && isspace((unsigned char)*value) != 0) {
        value++;
    }

    end = value + strlen(value);
    while (end > value && isspace((unsigned char)*(end - 1)) != 0) {
        end--;
    }
    *end = '\0';
    return value;
}

/* Parse one base-10 unsigned 32-bit integer with trailing-space tolerance. */
static bool mp_cache_parse_u32(const char *value, uint32_t *out_value) {
    char *end = NULL;
    unsigned long parsed = 0ul;

    if (value == NULL || out_value == NULL || *value == '\0') {
        return false;
    }

    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *mp_cache_trim(end) != '\0' || parsed > UINT32_MAX) {
        return false;
    }

    *out_value = (uint32_t)parsed;
    return true;
}

/* Parse one base-10 unsigned 64-bit integer with trailing-space tolerance. */
static bool mp_cache_parse_u64(const char *value, uint64_t *out_value) {
    char *end = NULL;
    unsigned long long parsed = 0ull;

    if (value == NULL || out_value == NULL || *value == '\0') {
        return false;
    }

    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno != 0 || end == value || *mp_cache_trim(end) != '\0') {
        return false;
    }

    *out_value = (uint64_t)parsed;
    return true;
}

/* Apply one parsed section/key/value tuple to config and reject unknown keys. */
static bool mp_cache_apply_value(
    mp_cache_config_t *config,
    const char *section,
    const char *key,
    const char *value) {
    if (strcmp(section, MP_CACHE_CONFIG_SECTION_SERVICE) == 0) {
        if (strcmp(key, MP_CACHE_CONFIG_KEY_ENVIRONMENT_NAME) == 0) {
            mp_cache_copy_string(config->environment_name, sizeof(config->environment_name), value);
            return true;
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_SERVICE_NAME) == 0) {
            mp_cache_copy_string(config->service_name, sizeof(config->service_name), value);
            return true;
        }
        return false;
    }

    if (strcmp(section, MP_CACHE_CONFIG_SECTION_SERVER) == 0) {
        if (strcmp(key, MP_CACHE_CONFIG_KEY_SOCKET_PATH) == 0) {
            mp_cache_copy_string(config->socket_path, sizeof(config->socket_path), value);
            return true;
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_PID_FILE_PATH) == 0) {
            mp_cache_copy_string(config->pid_file_path, sizeof(config->pid_file_path), value);
            return true;
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_SHUTDOWN_TIMEOUT_MILLIS) == 0) {
            return mp_cache_parse_u32(value, &config->shutdown_timeout_millis);
        }
        return false;
    }

    if (strcmp(section, MP_CACHE_CONFIG_SECTION_CACHE) == 0) {
        if (strcmp(key, MP_CACHE_CONFIG_KEY_MEMORY_LIMIT_BYTES) == 0) {
            return mp_cache_parse_u64(value, &config->memory_limit_bytes);
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_DEFAULT_TTL_SECONDS) == 0) {
            return mp_cache_parse_u32(value, &config->default_ttl_seconds);
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_MIN_TTL_SECONDS) == 0) {
            return mp_cache_parse_u32(value, &config->min_ttl_seconds);
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_MAX_TTL_SECONDS) == 0) {
            return mp_cache_parse_u32(value, &config->max_ttl_seconds);
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_MAX_KEY_BYTES) == 0) {
            return mp_cache_parse_u32(value, &config->max_key_bytes);
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_MAX_VALUE_BYTES) == 0) {
            return mp_cache_parse_u32(value, &config->max_value_bytes);
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_BUCKET_COUNT) == 0) {
            return mp_cache_parse_u32(value, &config->bucket_count);
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_SWEEP_INTERVAL_SECONDS) == 0) {
            return mp_cache_parse_u32(value, &config->sweep_interval_seconds);
        }
        return false;
    }

    if (strcmp(section, MP_CACHE_CONFIG_SECTION_STORAGE) == 0) {
        if (strcmp(key, MP_CACHE_CONFIG_KEY_DATA_DIRECTORY) == 0) {
            mp_cache_copy_string(config->data_directory, sizeof(config->data_directory), value);
            return true;
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_EXPORT_DIRECTORY) == 0) {
            mp_cache_copy_string(config->export_directory, sizeof(config->export_directory), value);
            return true;
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_CHECKPOINT_PATH) == 0) {
            mp_cache_copy_string(config->checkpoint_path, sizeof(config->checkpoint_path), value);
            return true;
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_JOURNAL_PATH) == 0) {
            mp_cache_copy_string(config->journal_path, sizeof(config->journal_path), value);
            return true;
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_MAX_EXPORT_FILES) == 0) {
            return mp_cache_parse_u32(value, &config->max_export_files);
        }
        return false;
    }

    if (strcmp(section, MP_CACHE_CONFIG_SECTION_OBSERVABILITY) == 0) {
        if (strcmp(key, MP_CACHE_CONFIG_KEY_LOG_DIRECTORY) == 0) {
            mp_cache_copy_string(config->log_directory, sizeof(config->log_directory), value);
            return true;
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_MAX_LOG_LINES) == 0) {
            return mp_cache_parse_u32(value, &config->max_log_lines);
        }
        return false;
    }

    if (strcmp(section, MP_CACHE_CONFIG_SECTION_SECURITY) == 0) {
        if (strcmp(key, MP_CACHE_CONFIG_KEY_BOOTSTRAP_ADMIN_TOKEN_SECRET_REF) == 0) {
            mp_cache_copy_string(
                config->bootstrap_admin_token_secret_ref,
                sizeof(config->bootstrap_admin_token_secret_ref),
                value);
            return true;
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_STORAGE_KEY_SECRET_REF) == 0) {
            mp_cache_copy_string(
                config->storage_key_secret_ref,
                sizeof(config->storage_key_secret_ref),
                value);
            return true;
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_RATE_LIMIT_REQUESTS) == 0) {
            return mp_cache_parse_u32(value, &config->rate_limit_requests);
        }
        if (strcmp(key, MP_CACHE_CONFIG_KEY_RATE_LIMIT_WINDOW_SECONDS) == 0) {
            return mp_cache_parse_u32(value, &config->rate_limit_window_seconds);
        }
        return false;
    }

    return false;
}

/* Validate that all required config strings and numeric bounds are present and coherent. */
static mp_cache_config_status_t mp_cache_validate(const mp_cache_config_t *config) {
    if (config->service_name[0] == '\0' ||
        config->environment_name[0] == '\0' ||
        config->socket_path[0] == '\0' ||
        config->pid_file_path[0] == '\0' ||
        config->log_directory[0] == '\0' ||
        config->data_directory[0] == '\0' ||
        config->export_directory[0] == '\0' ||
        config->checkpoint_path[0] == '\0' ||
        config->journal_path[0] == '\0' ||
        config->bootstrap_admin_token_secret_ref[0] == '\0' ||
        config->storage_key_secret_ref[0] == '\0') {
        return MP_CACHE_CONFIG_STATUS_VALIDATION_ERROR;
    }

    if (config->memory_limit_bytes == 0u ||
        config->default_ttl_seconds == 0u ||
        config->min_ttl_seconds == 0u ||
        config->max_ttl_seconds == 0u ||
        config->max_key_bytes == 0u ||
        config->max_value_bytes == 0u ||
        config->bucket_count == 0u ||
        config->max_export_files == 0u ||
        config->shutdown_timeout_millis == 0u ||
        config->sweep_interval_seconds == 0u ||
        config->max_log_lines == 0u ||
        config->rate_limit_requests == 0u ||
        config->rate_limit_window_seconds == 0u) {
        return MP_CACHE_CONFIG_STATUS_VALIDATION_ERROR;
    }

    if (config->default_ttl_seconds < config->min_ttl_seconds ||
        config->default_ttl_seconds > config->max_ttl_seconds) {
        return MP_CACHE_CONFIG_STATUS_VALIDATION_ERROR;
    }

    if (config->min_ttl_seconds > config->max_ttl_seconds) {
        return MP_CACHE_CONFIG_STATUS_VALIDATION_ERROR;
    }

    return MP_CACHE_CONFIG_STATUS_OK;
}

void mp_cache_config_init_defaults(mp_cache_config_t *config) {
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    mp_cache_copy_string(config->service_name, sizeof(config->service_name), MP_CACHE_DEFAULT_SERVICE_NAME);
    mp_cache_copy_string(config->environment_name, sizeof(config->environment_name), MP_CACHE_DEFAULT_ENVIRONMENT_NAME);
    mp_cache_copy_string(config->socket_path, sizeof(config->socket_path), MP_CACHE_DEFAULT_SOCKET_PATH);
    mp_cache_copy_string(config->pid_file_path, sizeof(config->pid_file_path), MP_CACHE_DEFAULT_PID_FILE_PATH);
    mp_cache_copy_string(config->data_directory, sizeof(config->data_directory), MP_CACHE_DEFAULT_DATA_DIRECTORY);
    mp_cache_copy_string(config->export_directory, sizeof(config->export_directory), MP_CACHE_DEFAULT_EXPORT_DIRECTORY);
    mp_cache_copy_string(config->checkpoint_path, sizeof(config->checkpoint_path), MP_CACHE_DEFAULT_CHECKPOINT_PATH);
    mp_cache_copy_string(config->journal_path, sizeof(config->journal_path), MP_CACHE_DEFAULT_JOURNAL_PATH);
    mp_cache_copy_string(config->log_directory, sizeof(config->log_directory), MP_CACHE_DEFAULT_LOG_DIRECTORY);
    mp_cache_copy_string(
        config->bootstrap_admin_token_secret_ref,
        sizeof(config->bootstrap_admin_token_secret_ref),
        MP_CACHE_DEFAULT_BOOTSTRAP_ADMIN_TOKEN_SECRET_REF);
    mp_cache_copy_string(
        config->storage_key_secret_ref,
        sizeof(config->storage_key_secret_ref),
        MP_CACHE_DEFAULT_STORAGE_KEY_SECRET_REF);
    config->memory_limit_bytes = MP_CACHE_DEFAULT_MEMORY_LIMIT_BYTES;
    config->default_ttl_seconds = MP_CACHE_DEFAULT_TTL_SECONDS;
    config->min_ttl_seconds = MP_CACHE_DEFAULT_MIN_TTL_SECONDS;
    config->max_ttl_seconds = MP_CACHE_DEFAULT_MAX_TTL_SECONDS;
    config->max_key_bytes = MP_CACHE_DEFAULT_MAX_KEY_BYTES;
    config->max_value_bytes = MP_CACHE_DEFAULT_MAX_VALUE_BYTES;
    config->bucket_count = MP_CACHE_DEFAULT_BUCKET_COUNT;
    config->max_export_files = MP_CACHE_DEFAULT_MAX_EXPORT_FILES;
    config->shutdown_timeout_millis = MP_CACHE_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS;
    config->sweep_interval_seconds = MP_CACHE_DEFAULT_SWEEP_INTERVAL_SECONDS;
    config->max_log_lines = MP_CACHE_DEFAULT_MAX_LOG_LINES;
    config->rate_limit_requests = MP_CACHE_DEFAULT_RATE_LIMIT_REQUESTS;
    config->rate_limit_window_seconds = MP_CACHE_DEFAULT_RATE_LIMIT_WINDOW_SECONDS;
}

mp_cache_config_status_t mp_cache_config_load_file(const char *config_path, mp_cache_config_t *config) {
    FILE *file = NULL;
    char line_buffer[MP_CACHE_LINE_CAPACITY];
    char current_section[MP_CACHE_TEXT_CAP] = "";
    unsigned long line_number = 0ul;

    if (config_path == NULL || config == NULL) {
        return MP_CACHE_CONFIG_STATUS_INVALID_ARGUMENT;
    }

    file = fopen(config_path, "r");
    if (file == NULL) {
        return (errno == ENOENT) ? MP_CACHE_CONFIG_STATUS_NOT_FOUND : MP_CACHE_CONFIG_STATUS_IO_ERROR;
    }

    while (fgets(line_buffer, sizeof(line_buffer), file) != NULL) {
        char *line = mp_cache_trim(line_buffer);
        char *separator = NULL;
        char *key = NULL;
        char *value = NULL;
        size_t line_length = 0u;

        line_number++;
        if (*line == '\0' || *line == '#' || *line == ';') {
            continue;
        }

        line_length = strlen(line);
        if (line[0] == '[') {
            if (line_length < 3u || line[line_length - 1u] != ']') {
                (void)fclose(file);
                return MP_CACHE_CONFIG_STATUS_PARSE_ERROR;
            }
            line[line_length - 1u] = '\0';
            mp_cache_copy_string(current_section, sizeof(current_section), line + 1);
            continue;
        }

        separator = strchr(line, '=');
        if (separator == NULL) {
            (void)fclose(file);
            return MP_CACHE_CONFIG_STATUS_PARSE_ERROR;
        }

        *separator = '\0';
        key = mp_cache_trim(line);
        value = mp_cache_trim(separator + 1);

        if (current_section[0] == '\0' || *key == '\0' || !mp_cache_apply_value(config, current_section, key, value)) {
            (void)line_number;
            (void)fclose(file);
            return MP_CACHE_CONFIG_STATUS_PARSE_ERROR;
        }
    }

    if (ferror(file) != 0) {
        (void)fclose(file);
        return MP_CACHE_CONFIG_STATUS_IO_ERROR;
    }

    (void)fclose(file);
    return mp_cache_validate(config);
}

mp_cache_config_status_t mp_cache_config_write_template(
    const char *template_path,
    const mp_cache_config_t *config) {
    FILE *file = NULL;

    if (template_path == NULL || config == NULL) {
        return MP_CACHE_CONFIG_STATUS_INVALID_ARGUMENT;
    }

    if (mp_cache_fs_ensure_parent_directory(template_path) != 0) {
        return MP_CACHE_CONFIG_STATUS_IO_ERROR;
    }

    file = fopen(template_path, "w");
    if (file == NULL) {
        return MP_CACHE_CONFIG_STATUS_IO_ERROR;
    }

    (void)fprintf(
        file,
        "# mp-cache bootstrap configuration\n"
        "# Generated automatically from compiled defaults.\n"
        "# All TTL values are in seconds and all timestamps are UTC.\n\n"
        "[" MP_CACHE_CONFIG_SECTION_SERVICE "]\n"
        "# Valid values: local, development, qa, staging, production\n"
        MP_CACHE_CONFIG_KEY_ENVIRONMENT_NAME " = %s\n"
        MP_CACHE_CONFIG_KEY_SERVICE_NAME " = %s\n\n"
        "[" MP_CACHE_CONFIG_SECTION_SERVER "]\n"
        "# Use an absolute socket path in long-lived deployments.\n"
        MP_CACHE_CONFIG_KEY_SOCKET_PATH " = %s\n"
        MP_CACHE_CONFIG_KEY_PID_FILE_PATH " = %s\n"
        MP_CACHE_CONFIG_KEY_SHUTDOWN_TIMEOUT_MILLIS " = %" PRIu32 "\n\n"
        "[" MP_CACHE_CONFIG_SECTION_CACHE "]\n"
        "# Maximum total in-memory cache footprint in bytes.\n"
        MP_CACHE_CONFIG_KEY_MEMORY_LIMIT_BYTES " = %" PRIu64 "\n"
        "# Default TTL used when a write does not provide an explicit TTL.\n"
        MP_CACHE_CONFIG_KEY_DEFAULT_TTL_SECONDS " = %" PRIu32 "\n"
        MP_CACHE_CONFIG_KEY_MIN_TTL_SECONDS " = %" PRIu32 "\n"
        MP_CACHE_CONFIG_KEY_MAX_TTL_SECONDS " = %" PRIu32 "\n"
        MP_CACHE_CONFIG_KEY_MAX_KEY_BYTES " = %" PRIu32 "\n"
        MP_CACHE_CONFIG_KEY_MAX_VALUE_BYTES " = %" PRIu32 "\n"
        MP_CACHE_CONFIG_KEY_BUCKET_COUNT " = %" PRIu32 "\n"
        MP_CACHE_CONFIG_KEY_SWEEP_INTERVAL_SECONDS " = %" PRIu32 "\n\n"
        "[" MP_CACHE_CONFIG_SECTION_STORAGE "]\n"
        MP_CACHE_CONFIG_KEY_DATA_DIRECTORY " = %s\n"
        MP_CACHE_CONFIG_KEY_EXPORT_DIRECTORY " = %s\n"
        MP_CACHE_CONFIG_KEY_CHECKPOINT_PATH " = %s\n"
        MP_CACHE_CONFIG_KEY_JOURNAL_PATH " = %s\n"
        MP_CACHE_CONFIG_KEY_MAX_EXPORT_FILES " = %" PRIu32 "\n\n"
        "[" MP_CACHE_CONFIG_SECTION_OBSERVABILITY "]\n"
        MP_CACHE_CONFIG_KEY_LOG_DIRECTORY " = %s\n"
        MP_CACHE_CONFIG_KEY_MAX_LOG_LINES " = %" PRIu32 "\n\n"
        "[" MP_CACHE_CONFIG_SECTION_SECURITY "]\n"
        "# Local development may use env refs. Non-local deployments should prefer file refs.\n"
        MP_CACHE_CONFIG_KEY_BOOTSTRAP_ADMIN_TOKEN_SECRET_REF " = %s\n"
        MP_CACHE_CONFIG_KEY_STORAGE_KEY_SECRET_REF " = %s\n"
        MP_CACHE_CONFIG_KEY_RATE_LIMIT_REQUESTS " = %" PRIu32 "\n"
        MP_CACHE_CONFIG_KEY_RATE_LIMIT_WINDOW_SECONDS " = %" PRIu32 "\n",
        config->environment_name,
        config->service_name,
        config->socket_path,
        config->pid_file_path,
        config->shutdown_timeout_millis,
        config->memory_limit_bytes,
        config->default_ttl_seconds,
        config->min_ttl_seconds,
        config->max_ttl_seconds,
        config->max_key_bytes,
        config->max_value_bytes,
        config->bucket_count,
        config->sweep_interval_seconds,
        config->data_directory,
        config->export_directory,
        config->checkpoint_path,
        config->journal_path,
        config->max_export_files,
        config->log_directory,
        config->max_log_lines,
        config->bootstrap_admin_token_secret_ref,
        config->storage_key_secret_ref,
        config->rate_limit_requests,
        config->rate_limit_window_seconds);

    if (fclose(file) != 0) {
        return MP_CACHE_CONFIG_STATUS_IO_ERROR;
    }

    return MP_CACHE_CONFIG_STATUS_OK;
}

void mp_cache_config_print(FILE *stream, const mp_cache_config_t *config, const char *source_path) {
    if (stream == NULL || config == NULL) {
        return;
    }

    (void)fprintf(stream, "config_source=%s\n", source_path == NULL ? "(defaults)" : source_path);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_SERVICE "." MP_CACHE_CONFIG_KEY_ENVIRONMENT_NAME "=%s\n", config->environment_name);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_SERVICE "." MP_CACHE_CONFIG_KEY_SERVICE_NAME "=%s\n", config->service_name);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_SERVER "." MP_CACHE_CONFIG_KEY_SOCKET_PATH "=%s\n", config->socket_path);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_SERVER "." MP_CACHE_CONFIG_KEY_PID_FILE_PATH "=%s\n", config->pid_file_path);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_SERVER "." MP_CACHE_CONFIG_KEY_SHUTDOWN_TIMEOUT_MILLIS "=%" PRIu32 "\n", config->shutdown_timeout_millis);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_CACHE "." MP_CACHE_CONFIG_KEY_MEMORY_LIMIT_BYTES "=%" PRIu64 "\n", config->memory_limit_bytes);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_CACHE "." MP_CACHE_CONFIG_KEY_DEFAULT_TTL_SECONDS "=%" PRIu32 "\n", config->default_ttl_seconds);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_CACHE "." MP_CACHE_CONFIG_KEY_MIN_TTL_SECONDS "=%" PRIu32 "\n", config->min_ttl_seconds);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_CACHE "." MP_CACHE_CONFIG_KEY_MAX_TTL_SECONDS "=%" PRIu32 "\n", config->max_ttl_seconds);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_CACHE "." MP_CACHE_CONFIG_KEY_MAX_KEY_BYTES "=%" PRIu32 "\n", config->max_key_bytes);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_CACHE "." MP_CACHE_CONFIG_KEY_MAX_VALUE_BYTES "=%" PRIu32 "\n", config->max_value_bytes);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_CACHE "." MP_CACHE_CONFIG_KEY_BUCKET_COUNT "=%" PRIu32 "\n", config->bucket_count);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_CACHE "." MP_CACHE_CONFIG_KEY_SWEEP_INTERVAL_SECONDS "=%" PRIu32 "\n", config->sweep_interval_seconds);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_STORAGE "." MP_CACHE_CONFIG_KEY_DATA_DIRECTORY "=%s\n", config->data_directory);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_STORAGE "." MP_CACHE_CONFIG_KEY_EXPORT_DIRECTORY "=%s\n", config->export_directory);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_STORAGE "." MP_CACHE_CONFIG_KEY_CHECKPOINT_PATH "=%s\n", config->checkpoint_path);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_STORAGE "." MP_CACHE_CONFIG_KEY_JOURNAL_PATH "=%s\n", config->journal_path);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_STORAGE "." MP_CACHE_CONFIG_KEY_MAX_EXPORT_FILES "=%" PRIu32 "\n", config->max_export_files);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_OBSERVABILITY "." MP_CACHE_CONFIG_KEY_LOG_DIRECTORY "=%s\n", config->log_directory);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_OBSERVABILITY "." MP_CACHE_CONFIG_KEY_MAX_LOG_LINES "=%" PRIu32 "\n", config->max_log_lines);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_SECURITY "." MP_CACHE_CONFIG_KEY_BOOTSTRAP_ADMIN_TOKEN_SECRET_REF "=%s\n", config->bootstrap_admin_token_secret_ref);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_SECURITY "." MP_CACHE_CONFIG_KEY_STORAGE_KEY_SECRET_REF "=%s\n", config->storage_key_secret_ref);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_SECURITY "." MP_CACHE_CONFIG_KEY_RATE_LIMIT_REQUESTS "=%" PRIu32 "\n", config->rate_limit_requests);
    (void)fprintf(stream, MP_CACHE_CONFIG_SECTION_SECURITY "." MP_CACHE_CONFIG_KEY_RATE_LIMIT_WINDOW_SECONDS "=%" PRIu32 "\n", config->rate_limit_window_seconds);
}

const char *mp_cache_config_status_name(mp_cache_config_status_t status) {
    switch (status) {
        case MP_CACHE_CONFIG_STATUS_OK:
            return MP_CACHE_STATUS_NAME_OK;
        case MP_CACHE_CONFIG_STATUS_NOT_FOUND:
            return MP_CACHE_STATUS_NAME_NOT_FOUND;
        case MP_CACHE_CONFIG_STATUS_IO_ERROR:
            return MP_CACHE_STATUS_NAME_IO_ERROR;
        case MP_CACHE_CONFIG_STATUS_PARSE_ERROR:
            return MP_CACHE_STATUS_NAME_PARSE_ERROR;
        case MP_CACHE_CONFIG_STATUS_VALIDATION_ERROR:
            return MP_CACHE_STATUS_NAME_VALIDATION_ERROR;
        case MP_CACHE_CONFIG_STATUS_INVALID_ARGUMENT:
            return MP_CACHE_STATUS_NAME_INVALID_ARGUMENT;
        default:
            return MP_CACHE_STATUS_NAME_UNKNOWN;
    }
}
