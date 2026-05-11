#include "internal/observability/log.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

#define MP_CACHE_LOG_MESSAGE_CAPACITY 1024u
#define MP_CACHE_LOG_CONTEXT_CAPACITY 512u
#define MP_CACHE_LOG_TAIL_READ_CAPACITY (1024u * 1024u)

static void mp_cache_log_copy(char *destination, size_t destination_capacity, const char *source) {
    if (destination == NULL || destination_capacity == 0u) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    (void)snprintf(destination, destination_capacity, "%s", source);
}

int mp_cache_log_init(mp_cache_log_t *log, const mp_cache_config_t *config) {
    mp_logger_config_t logger_config;
    mp_log_status_t status;

    if (log == NULL || config == NULL) {
        return -1;
    }

    memset(log, 0, sizeof(*log));
    mp_logger_config_init_defaults(&logger_config);
    logger_config.buffer_capacity = 4096u;
    logger_config.message_capacity = 1024u;
    logger_config.context_capacity = 512u;
    logger_config.field_capacity = 8u;
    logger_config.format = MP_LOG_FORMAT_JSON;
    logger_config.pretty_output = 0;
    logger_config.stdout_min_level = MP_LOG_LEVEL_TRACE;
    logger_config.stdout_max_level = MP_LOG_LEVEL_INFO;
    logger_config.stderr_min_level = MP_LOG_LEVEL_WARNING;
    logger_config.stderr_max_level = MP_LOG_LEVEL_FATAL;
    logger_config.file_min_level = MP_LOG_LEVEL_TRACE;
    logger_config.file_max_level = MP_LOG_LEVEL_FATAL;

    mp_cache_log_copy(logger_config.service_name, sizeof(logger_config.service_name), config->service_name);
    mp_cache_log_copy(
        logger_config.environment_name,
        sizeof(logger_config.environment_name),
        config->environment_name);
    mp_cache_log_copy(logger_config.log_directory, sizeof(logger_config.log_directory), config->log_directory);
    mp_cache_log_copy(logger_config.file_name_prefix, sizeof(logger_config.file_name_prefix), "mp-cache");
    mp_cache_log_copy(
        logger_config.backup_file_name_prefix,
        sizeof(logger_config.backup_file_name_prefix),
        "mp-cache-internal");
    mp_cache_log_copy(
        logger_config.active_streams,
        sizeof(logger_config.active_streams),
        "stdout,stderr,file");

    status = mp_logger_create(&logger_config, &log->raw_logger);
    if (status != MP_LOG_STATUS_OK) {
        return -1;
    }

    status = mp_logger_start(log->raw_logger);
    if (status != MP_LOG_STATUS_OK) {
        mp_logger_destroy(log->raw_logger);
        log->raw_logger = NULL;
        return -1;
    }

    return 0;
}

void mp_cache_log_shutdown(mp_cache_log_t *log, uint32_t timeout_millis) {
    if (log == NULL || log->raw_logger == NULL) {
        return;
    }

    (void)mp_logger_shutdown(log->raw_logger, timeout_millis);
    mp_logger_destroy(log->raw_logger);
    log->raw_logger = NULL;
}

void mp_cache_log_writef(mp_cache_log_t *log, mp_log_level_t level, const char *context_text, const char *format, ...) {
    char message_buffer[MP_CACHE_LOG_MESSAGE_CAPACITY];
    char context_buffer[MP_CACHE_LOG_CONTEXT_CAPACITY];
    va_list arguments;

    if (log == NULL || log->raw_logger == NULL || format == NULL) {
        return;
    }

    va_start(arguments, format);
    (void)vsnprintf(message_buffer, sizeof(message_buffer), format, arguments);
    va_end(arguments);

    mp_cache_log_copy(context_buffer, sizeof(context_buffer), context_text == NULL ? "" : context_text);
    (void)mp_logger_log(log->raw_logger, level, message_buffer, context_buffer);
}

static int mp_cache_log_find_latest_file(
    const char *log_directory,
    char *out_path,
    size_t out_path_capacity,
    char *out_name,
    size_t out_name_capacity) {
    DIR *directory = NULL;
    struct dirent *entry = NULL;
    struct stat entry_status;
    struct stat latest_status;
    char path[MP_CACHE_PATH_CAP];
    int found = 0;

    if (log_directory == NULL || out_path == NULL || out_name == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(&latest_status, 0, sizeof(latest_status));
    directory = opendir(log_directory);
    if (directory == NULL) {
        return -1;
    }

    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        (void)snprintf(path, sizeof(path), "%s/%s", log_directory, entry->d_name);
        if (stat(path, &entry_status) != 0 || S_ISREG(entry_status.st_mode) == 0) {
            continue;
        }
        if (!found || entry_status.st_mtime > latest_status.st_mtime) {
            latest_status = entry_status;
            (void)snprintf(out_path, out_path_capacity, "%s", path);
            (void)snprintf(out_name, out_name_capacity, "%s", entry->d_name);
            found = 1;
        }
    }

    (void)closedir(directory);
    if (!found) {
        errno = ENOENT;
        return -1;
    }

    return 0;
}

int mp_cache_log_read_latest_tail(
    const char *log_directory,
    uint32_t max_lines,
    uint32_t requested_lines,
    char *out_file_name,
    size_t out_file_name_capacity,
    char **out_text) {
    char path[MP_CACHE_PATH_CAP];
    FILE *file = NULL;
    char *buffer = NULL;
    char *start = NULL;
    size_t file_size = 0u;
    size_t read_size = 0u;
    uint32_t target_lines = requested_lines == 0u ? 1u : requested_lines;
    uint32_t seen_lines = 0u;
    size_t index = 0u;

    if (log_directory == NULL || out_file_name == NULL || out_text == NULL || out_file_name_capacity == 0u) {
        errno = EINVAL;
        return -1;
    }

    if (target_lines > max_lines) {
        target_lines = max_lines;
    }
    if (mp_cache_log_find_latest_file(log_directory, path, sizeof(path), out_file_name, out_file_name_capacity) != 0) {
        return -1;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        (void)fclose(file);
        return -1;
    }
    file_size = (size_t)ftell(file);
    read_size = file_size > MP_CACHE_LOG_TAIL_READ_CAPACITY ? MP_CACHE_LOG_TAIL_READ_CAPACITY : file_size;
    if (fseek(file, (long)(file_size - read_size), SEEK_SET) != 0) {
        (void)fclose(file);
        return -1;
    }

    buffer = malloc(read_size + 1u);
    if (buffer == NULL) {
        (void)fclose(file);
        return -1;
    }
    if (read_size > 0u && fread(buffer, 1u, read_size, file) != read_size) {
        free(buffer);
        (void)fclose(file);
        return -1;
    }
    buffer[read_size] = '\0';
    (void)fclose(file);

    start = buffer;
    for (index = read_size; index > 0u; index--) {
        if (buffer[index - 1u] == '\n') {
            seen_lines++;
            if (seen_lines > target_lines) {
                start = buffer + index;
                break;
            }
        }
    }

    if (start != buffer) {
        size_t remaining = strlen(start);
        memmove(buffer, start, remaining + 1u);
    }

    *out_text = buffer;
    return 0;
}
