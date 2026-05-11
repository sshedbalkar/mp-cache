#ifndef MP_CACHE_INTERNAL_HTTPSERVER_HTTP_SERVER_H
#define MP_CACHE_INTERNAL_HTTPSERVER_HTTP_SERVER_H

#include "internal/cache/cache.h"
#include "internal/config/config.h"
#include "internal/observability/log.h"

#include <signal.h>
#include <time.h>

typedef struct {
    int server_fd;
    time_t started_at_utc;
    const mp_cache_config_t *config;
    mp_cache_log_t *log;
    mp_cache_store_t *store;
} mp_cache_http_server_t;

int mp_cache_http_server_start(
    mp_cache_http_server_t *server,
    const mp_cache_config_t *config,
    mp_cache_log_t *log,
    mp_cache_store_t *store,
    time_t started_at_utc);

int mp_cache_http_server_serve(mp_cache_http_server_t *server, volatile sig_atomic_t *stop_requested);
void mp_cache_http_server_stop(mp_cache_http_server_t *server);

int mp_cache_http_client_health(const char *socket_path, FILE *stream);

#endif
