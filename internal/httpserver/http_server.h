#ifndef MP_CACHE_INTERNAL_HTTPSERVER_HTTP_SERVER_H
#define MP_CACHE_INTERNAL_HTTPSERVER_HTTP_SERVER_H

#include "internal/cache/cache.h"
#include "internal/config/config.h"
#include "internal/observability/log.h"
#include "internal/security/security.h"
#include "internal/storage/storage.h"

#include <stdbool.h>
#include <signal.h>
#include <time.h>

typedef struct {
    char subject[MP_CACHE_CLIENT_ID_CAP];
    int64_t window_started_at;
    uint32_t request_count;
} mp_cache_rate_limit_entry_t;

typedef struct {
    mp_cache_rate_limit_entry_t *entries;
    size_t count;
    size_t capacity;
} mp_cache_rate_limiter_t;

typedef struct {
    uint64_t total_requests;
    uint64_t unauthorized_requests;
    uint64_t forbidden_requests;
    uint64_t rate_limited_requests;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t cache_sets;
    uint64_t cache_deletes;
    uint64_t client_registrations;
    uint64_t token_rotations;
    uint64_t exports;
    uint64_t imports;
    uint64_t log_reads;
} mp_cache_http_metrics_t;

typedef struct {
    int server_fd;
    time_t started_at_utc;
    time_t last_sweep_at_utc;
    const mp_cache_config_t *config;
    mp_cache_log_t *log;
    mp_cache_store_t *store;
    mp_cache_security_t *security;
    mp_cache_storage_t *storage;
    mp_cache_rate_limiter_t rate_limiter;
    mp_cache_http_metrics_t metrics;
} mp_cache_http_server_t;

int mp_cache_http_server_start(
    mp_cache_http_server_t *server,
    const mp_cache_config_t *config,
    mp_cache_log_t *log,
    mp_cache_store_t *store,
    mp_cache_security_t *security,
    mp_cache_storage_t *storage,
    time_t started_at_utc);

int mp_cache_http_server_serve(mp_cache_http_server_t *server, volatile sig_atomic_t *stop_requested);
void mp_cache_http_server_stop(mp_cache_http_server_t *server);

int mp_cache_http_client_health(const char *socket_path, FILE *stream);
int mp_cache_http_client_request(
    const char *socket_path,
    const char *method,
    const char *request_path,
    const char *auth_token,
    const char *content_type,
    const char *request_body,
    FILE *stream);
int mp_cache_http_server_test_request(
    mp_cache_http_server_t *server,
    const char *raw_request,
    int *out_response_status_code,
    char **out_response_body);

#endif
