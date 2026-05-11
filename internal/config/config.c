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

#define MP_CACHE_LINE_CAPACITY 2048u

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

static bool mp_cache_apply_value(
    mp_cache_config_t *config,
    const char *section,
    const char *key,
    const char *value) {
    if (strcmp(section, "service") == 0) {
        if (strcmp(key, "environment_name") == 0) {
            mp_cache_copy_string(config->environment_name, sizeof(config->environment_name), value);
            return true;
        }
        if (strcmp(key, "service_name") == 0) {
            mp_cache_copy_string(config->service_name, sizeof(config->service_name), value);
            return true;
        }
        return false;
    }

    if (strcmp(section, "server") == 0) {
        if (strcmp(key, "socket_path") == 0) {
            mp_cache_copy_string(config->socket_path, sizeof(config->socket_path), value);
            return true;
        }
        if (strcmp(key, "pid_file_path") == 0) {
            mp_cache_copy_string(config->pid_file_path, sizeof(config->pid_file_path), value);
            return true;
        }
        if (strcmp(key, "shutdown_timeout_millis") == 0) {
            return mp_cache_parse_u32(value, &config->shutdown_timeout_millis);
        }
        return false;
    }

    if (strcmp(section, "cache") == 0) {
        if (strcmp(key, "memory_limit_bytes") == 0) {
            return mp_cache_parse_u64(value, &config->memory_limit_bytes);
        }
        if (strcmp(key, "default_ttl_seconds") == 0) {
            return mp_cache_parse_u32(value, &config->default_ttl_seconds);
        }
        if (strcmp(key, "min_ttl_seconds") == 0) {
            return mp_cache_parse_u32(value, &config->min_ttl_seconds);
        }
        if (strcmp(key, "max_ttl_seconds") == 0) {
            return mp_cache_parse_u32(value, &config->max_ttl_seconds);
        }
        if (strcmp(key, "max_key_bytes") == 0) {
            return mp_cache_parse_u32(value, &config->max_key_bytes);
        }
        if (strcmp(key, "max_value_bytes") == 0) {
            return mp_cache_parse_u32(value, &config->max_value_bytes);
        }
        if (strcmp(key, "bucket_count") == 0) {
            return mp_cache_parse_u32(value, &config->bucket_count);
        }
        if (strcmp(key, "sweep_interval_seconds") == 0) {
            return mp_cache_parse_u32(value, &config->sweep_interval_seconds);
        }
        return false;
    }

    if (strcmp(section, "storage") == 0) {
        if (strcmp(key, "data_directory") == 0) {
            mp_cache_copy_string(config->data_directory, sizeof(config->data_directory), value);
            return true;
        }
        if (strcmp(key, "export_directory") == 0) {
            mp_cache_copy_string(config->export_directory, sizeof(config->export_directory), value);
            return true;
        }
        if (strcmp(key, "checkpoint_path") == 0) {
            mp_cache_copy_string(config->checkpoint_path, sizeof(config->checkpoint_path), value);
            return true;
        }
        if (strcmp(key, "journal_path") == 0) {
            mp_cache_copy_string(config->journal_path, sizeof(config->journal_path), value);
            return true;
        }
        if (strcmp(key, "max_export_files") == 0) {
            return mp_cache_parse_u32(value, &config->max_export_files);
        }
        return false;
    }

    if (strcmp(section, "observability") == 0) {
        if (strcmp(key, "log_directory") == 0) {
            mp_cache_copy_string(config->log_directory, sizeof(config->log_directory), value);
            return true;
        }
        if (strcmp(key, "max_log_lines") == 0) {
            return mp_cache_parse_u32(value, &config->max_log_lines);
        }
        return false;
    }

    if (strcmp(section, "security") == 0) {
        if (strcmp(key, "bootstrap_admin_token_secret_ref") == 0) {
            mp_cache_copy_string(
                config->bootstrap_admin_token_secret_ref,
                sizeof(config->bootstrap_admin_token_secret_ref),
                value);
            return true;
        }
        if (strcmp(key, "storage_key_secret_ref") == 0) {
            mp_cache_copy_string(
                config->storage_key_secret_ref,
                sizeof(config->storage_key_secret_ref),
                value);
            return true;
        }
        if (strcmp(key, "rate_limit_requests") == 0) {
            return mp_cache_parse_u32(value, &config->rate_limit_requests);
        }
        if (strcmp(key, "rate_limit_window_seconds") == 0) {
            return mp_cache_parse_u32(value, &config->rate_limit_window_seconds);
        }
        return false;
    }

    return false;
}

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
    mp_cache_copy_string(config->service_name, sizeof(config->service_name), "mp-cache");
    mp_cache_copy_string(config->environment_name, sizeof(config->environment_name), "local");
    mp_cache_copy_string(config->socket_path, sizeof(config->socket_path), "/tmp/mp-cache/run/mp-cache.sock");
    mp_cache_copy_string(config->pid_file_path, sizeof(config->pid_file_path), "/tmp/mp-cache/run/mp-cache.pid");
    mp_cache_copy_string(config->data_directory, sizeof(config->data_directory), ".tmp/data");
    mp_cache_copy_string(config->export_directory, sizeof(config->export_directory), ".tmp/exports");
    mp_cache_copy_string(config->checkpoint_path, sizeof(config->checkpoint_path), ".tmp/data/state.checkpoint");
    mp_cache_copy_string(config->journal_path, sizeof(config->journal_path), ".tmp/data/state.journal");
    mp_cache_copy_string(config->log_directory, sizeof(config->log_directory), "logging");
    mp_cache_copy_string(
        config->bootstrap_admin_token_secret_ref,
        sizeof(config->bootstrap_admin_token_secret_ref),
        "env:MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN");
    mp_cache_copy_string(
        config->storage_key_secret_ref,
        sizeof(config->storage_key_secret_ref),
        "env:MP_SECRET_LOCAL_STORAGE_KEY");
    config->memory_limit_bytes = 256u * 1024u * 1024u;
    config->default_ttl_seconds = 172800u;
    config->min_ttl_seconds = 1u;
    config->max_ttl_seconds = 2592000u;
    config->max_key_bytes = 256u;
    config->max_value_bytes = 1024u * 1024u;
    config->bucket_count = 4096u;
    config->max_export_files = 16u;
    config->shutdown_timeout_millis = 5000u;
    config->sweep_interval_seconds = 5u;
    config->max_log_lines = 200u;
    config->rate_limit_requests = 240u;
    config->rate_limit_window_seconds = 60u;
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
        "[service]\n"
        "# Valid values: local, development, qa, staging, production\n"
        "environment_name = %s\n"
        "service_name = %s\n\n"
        "[server]\n"
        "# Use an absolute socket path in long-lived deployments.\n"
        "socket_path = %s\n"
        "pid_file_path = %s\n"
        "shutdown_timeout_millis = %" PRIu32 "\n\n"
        "[cache]\n"
        "# Maximum total in-memory cache footprint in bytes.\n"
        "memory_limit_bytes = %" PRIu64 "\n"
        "# Default TTL used when a write does not provide an explicit TTL.\n"
        "default_ttl_seconds = %" PRIu32 "\n"
        "min_ttl_seconds = %" PRIu32 "\n"
        "max_ttl_seconds = %" PRIu32 "\n"
        "max_key_bytes = %" PRIu32 "\n"
        "max_value_bytes = %" PRIu32 "\n"
        "bucket_count = %" PRIu32 "\n"
        "sweep_interval_seconds = %" PRIu32 "\n\n"
        "[storage]\n"
        "data_directory = %s\n"
        "export_directory = %s\n"
        "checkpoint_path = %s\n"
        "journal_path = %s\n"
        "max_export_files = %" PRIu32 "\n\n"
        "[observability]\n"
        "log_directory = %s\n"
        "max_log_lines = %" PRIu32 "\n\n"
        "[security]\n"
        "# Local development may use env refs. Non-local deployments should prefer file refs.\n"
        "bootstrap_admin_token_secret_ref = %s\n"
        "storage_key_secret_ref = %s\n"
        "rate_limit_requests = %" PRIu32 "\n"
        "rate_limit_window_seconds = %" PRIu32 "\n",
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
    (void)fprintf(stream, "service.environment_name=%s\n", config->environment_name);
    (void)fprintf(stream, "service.service_name=%s\n", config->service_name);
    (void)fprintf(stream, "server.socket_path=%s\n", config->socket_path);
    (void)fprintf(stream, "server.pid_file_path=%s\n", config->pid_file_path);
    (void)fprintf(stream, "server.shutdown_timeout_millis=%" PRIu32 "\n", config->shutdown_timeout_millis);
    (void)fprintf(stream, "cache.memory_limit_bytes=%" PRIu64 "\n", config->memory_limit_bytes);
    (void)fprintf(stream, "cache.default_ttl_seconds=%" PRIu32 "\n", config->default_ttl_seconds);
    (void)fprintf(stream, "cache.min_ttl_seconds=%" PRIu32 "\n", config->min_ttl_seconds);
    (void)fprintf(stream, "cache.max_ttl_seconds=%" PRIu32 "\n", config->max_ttl_seconds);
    (void)fprintf(stream, "cache.max_key_bytes=%" PRIu32 "\n", config->max_key_bytes);
    (void)fprintf(stream, "cache.max_value_bytes=%" PRIu32 "\n", config->max_value_bytes);
    (void)fprintf(stream, "cache.bucket_count=%" PRIu32 "\n", config->bucket_count);
    (void)fprintf(stream, "cache.sweep_interval_seconds=%" PRIu32 "\n", config->sweep_interval_seconds);
    (void)fprintf(stream, "storage.data_directory=%s\n", config->data_directory);
    (void)fprintf(stream, "storage.export_directory=%s\n", config->export_directory);
    (void)fprintf(stream, "storage.checkpoint_path=%s\n", config->checkpoint_path);
    (void)fprintf(stream, "storage.journal_path=%s\n", config->journal_path);
    (void)fprintf(stream, "storage.max_export_files=%" PRIu32 "\n", config->max_export_files);
    (void)fprintf(stream, "observability.log_directory=%s\n", config->log_directory);
    (void)fprintf(stream, "observability.max_log_lines=%" PRIu32 "\n", config->max_log_lines);
    (void)fprintf(stream, "security.bootstrap_admin_token_secret_ref=%s\n", config->bootstrap_admin_token_secret_ref);
    (void)fprintf(stream, "security.storage_key_secret_ref=%s\n", config->storage_key_secret_ref);
    (void)fprintf(stream, "security.rate_limit_requests=%" PRIu32 "\n", config->rate_limit_requests);
    (void)fprintf(stream, "security.rate_limit_window_seconds=%" PRIu32 "\n", config->rate_limit_window_seconds);
}

const char *mp_cache_config_status_name(mp_cache_config_status_t status) {
    switch (status) {
        case MP_CACHE_CONFIG_STATUS_OK:
            return "OK";
        case MP_CACHE_CONFIG_STATUS_NOT_FOUND:
            return "NOT_FOUND";
        case MP_CACHE_CONFIG_STATUS_IO_ERROR:
            return "IO_ERROR";
        case MP_CACHE_CONFIG_STATUS_PARSE_ERROR:
            return "PARSE_ERROR";
        case MP_CACHE_CONFIG_STATUS_VALIDATION_ERROR:
            return "VALIDATION_ERROR";
        case MP_CACHE_CONFIG_STATUS_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        default:
            return "UNKNOWN";
    }
}
