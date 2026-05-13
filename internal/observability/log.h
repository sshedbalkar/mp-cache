#ifndef MP_CACHE_INTERNAL_OBSERVABILITY_LOG_H
#define MP_CACHE_INTERNAL_OBSERVABILITY_LOG_H

#include "internal/config/config.h"

#include "mp_logger.h"

/* Wraps the shared mp_logger instance used by service modules. */
typedef struct {
    mp_logger_t *raw_logger;
} mp_cache_log_t;

/* Create and start the service logger using config-derived defaults and paths. */
int mp_cache_log_init(mp_cache_log_t *log, const mp_cache_config_t *config);

/* Flush, shut down, and destroy the wrapped logger instance. */
void mp_cache_log_shutdown(mp_cache_log_t *log, uint32_t timeout_millis);

/* Format one message and enqueue it on the shared service logger. */
void mp_cache_log_writef(mp_cache_log_t *log, mp_log_level_t level, const char *context_text, const char *format, ...);

/* Read the most recent log file tail and allocate the returned text buffer for the caller. */
int mp_cache_log_read_latest_tail(
    const char *log_directory,
    uint32_t max_lines,
    uint32_t requested_lines,
    char *out_log_file_name,
    size_t out_log_file_name_capacity,
    char **out_log_text);

#endif
