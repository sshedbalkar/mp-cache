#ifndef MP_CACHE_INTERNAL_OBSERVABILITY_LOG_H
#define MP_CACHE_INTERNAL_OBSERVABILITY_LOG_H

#include "internal/config/config.h"

#include "mp_logger.h"

typedef struct {
    mp_logger_t *raw_logger;
} mp_cache_log_t;

int mp_cache_log_init(mp_cache_log_t *log, const mp_cache_config_t *config);
void mp_cache_log_shutdown(mp_cache_log_t *log, uint32_t timeout_millis);
void mp_cache_log_writef(mp_cache_log_t *log, mp_log_level_t level, const char *context_text, const char *format, ...);

#endif
