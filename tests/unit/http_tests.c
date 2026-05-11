#define _POSIX_C_SOURCE 200809L

#include "internal/cache/cache.h"
#include "internal/config/config.h"
#include "internal/httpserver/http_server.h"
#include "internal/observability/log.h"
#include "internal/platform/fs.h"
#include "internal/security/security.h"
#include "internal/storage/storage.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    mp_cache_config_t config;
    mp_cache_log_t log;
    mp_cache_store_t store;
    mp_cache_security_t security;
    mp_cache_storage_t storage;
    mp_cache_http_server_t server;
} http_fixture_t;

static void assert_format_text(char *destination, size_t destination_capacity, const char *format, ...) {
    va_list arguments;
    int written = 0;

    assert(destination != NULL);
    assert(destination_capacity > 0u);
    assert(format != NULL);

    va_start(arguments, format);
    written = vsnprintf(destination, destination_capacity, format, arguments);
    va_end(arguments);

    assert(written >= 0);
    assert((size_t)written < destination_capacity);
}

static void configure_temp_paths(mp_cache_config_t *config, const char *suffix) {
    assert(config != NULL);
    assert_format_text(config->data_directory, sizeof(config->data_directory), ".tmp/http-test-%s-data", suffix);
    assert_format_text(config->export_directory, sizeof(config->export_directory), ".tmp/http-test-%s-exports", suffix);
    assert_format_text(config->checkpoint_path, sizeof(config->checkpoint_path), "%s/state.checkpoint", config->data_directory);
    assert_format_text(config->journal_path, sizeof(config->journal_path), "%s/state.journal", config->data_directory);
    assert_format_text(config->log_directory, sizeof(config->log_directory), ".tmp/http-test-%s-logs", suffix);
}

static void fixture_init(http_fixture_t *fixture, const char *suffix, uint32_t rate_limit_requests) {
    assert(fixture != NULL);
    assert(setenv("MP_TEST_BOOTSTRAP_ADMIN_TOKEN", "bootstrap-admin-token", 1) == 0);
    assert(setenv("MP_TEST_STORAGE_KEY", "storage-secret", 1) == 0);

    memset(fixture, 0, sizeof(*fixture));
    fixture->server.server_fd = -1;
    mp_cache_config_init_defaults(&fixture->config);
    configure_temp_paths(&fixture->config, suffix);
    fixture->config.rate_limit_requests = rate_limit_requests;
    fixture->config.rate_limit_window_seconds = 3600u;
    (void)snprintf(
        fixture->config.bootstrap_admin_token_secret_ref,
        sizeof(fixture->config.bootstrap_admin_token_secret_ref),
        "env:MP_TEST_BOOTSTRAP_ADMIN_TOKEN");
    (void)snprintf(
        fixture->config.storage_key_secret_ref,
        sizeof(fixture->config.storage_key_secret_ref),
        "env:MP_TEST_STORAGE_KEY");

    assert(mp_cache_fs_ensure_directory(fixture->config.data_directory) == 0);
    assert(mp_cache_fs_ensure_directory(fixture->config.export_directory) == 0);
    assert(mp_cache_fs_ensure_directory(fixture->config.log_directory) == 0);
    assert(mp_cache_store_init(&fixture->store, &fixture->config) == 0);
    assert(mp_cache_security_init(&fixture->security, &fixture->config) == 0);
    assert(mp_cache_storage_init(&fixture->storage, &fixture->config, &fixture->log) == 0);

    fixture->server.config = &fixture->config;
    fixture->server.log = &fixture->log;
    fixture->server.store = &fixture->store;
    fixture->server.security = &fixture->security;
    fixture->server.storage = &fixture->storage;
    fixture->server.started_at_utc = 1000;
    fixture->server.last_sweep_at_utc = 1000;
}

static void fixture_destroy(http_fixture_t *fixture) {
    if (fixture == NULL) {
        return;
    }

    free(fixture->server.rate_limiter.entries);
    fixture->server.rate_limiter.entries = NULL;
    mp_cache_storage_destroy(&fixture->storage);
    mp_cache_security_destroy(&fixture->security);
    mp_cache_store_destroy(&fixture->store);
    mp_cache_log_shutdown(&fixture->log, fixture->config.shutdown_timeout_millis);
}

static char *allocf(const char *format, ...) {
    va_list arguments;
    va_list copy;
    char *buffer = NULL;
    int length = 0;

    va_start(arguments, format);
    va_copy(copy, arguments);
    length = vsnprintf(NULL, 0, format, arguments);
    va_end(arguments);
    assert(length >= 0);

    buffer = malloc((size_t)length + 1u);
    assert(buffer != NULL);
    (void)vsnprintf(buffer, (size_t)length + 1u, format, copy);
    va_end(copy);
    return buffer;
}

static char *make_request(const char *method, const char *path, const char *token, const char *body) {
    return allocf(
        "%s %s HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "%s%s%s"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        method,
        path,
        token == NULL ? "" : "Authorization: Bearer ",
        token == NULL ? "" : token,
        token == NULL ? "" : "\r\n",
        body == NULL ? 0u : strlen(body),
        body == NULL ? "" : body);
}

static void extract_json_string(const char *body, const char *field_name, char *out_text, size_t out_capacity) {
    char pattern[64];
    const char *start = NULL;
    const char *end = NULL;
    size_t length = 0u;

    (void)snprintf(pattern, sizeof(pattern), "\"%s\":\"", field_name);
    start = strstr(body, pattern);
    assert(start != NULL);
    start += strlen(pattern);
    end = strchr(start, '"');
    assert(end != NULL);
    length = (size_t)(end - start);
    assert(length + 1u <= out_capacity);
    memcpy(out_text, start, length);
    out_text[length] = '\0';
}

static void test_http_routes_cover_phase_two_three_and_four(void) {
    http_fixture_t fixture;
    char suffix[32];
    char *request = NULL;
    char *body = NULL;
    char *import_body = NULL;
    char client_token[MP_CACHE_TOKEN_TEXT_CAP];
    char export_path[MP_CACHE_PATH_CAP];
    int status_code = 0;
    FILE *log_file = NULL;

    (void)snprintf(suffix, sizeof(suffix), "%ld", (long)getpid());
    fixture_init(&fixture, suffix, 50u);

    request = make_request("GET", "/v1/health", NULL, "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    assert(strstr(body, "\"status\":\"ok\"") != NULL);
    free(request);
    free(body);

    request = make_request(
        "POST",
        "/v1/clients",
        "bootstrap-admin-token",
        "{\"client_id\":\"client-one\",\"role\":\"client\"}");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    extract_json_string(body, "token", client_token, sizeof(client_token));
    free(request);
    free(body);

    request = make_request(
        "PUT",
        "/v1/cache/alpha",
        client_token,
        "{\"value_base64\":\"aGVsbG8=\",\"ttl_seconds\":60}");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    free(request);
    free(body);

    request = make_request("GET", "/v1/cache/alpha", client_token, "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    assert(strstr(body, "\"value_base64\":\"aGVsbG8=\"") != NULL);
    free(request);
    free(body);

    request = make_request("GET", "/v1/stats", client_token, "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 403);
    free(request);
    free(body);

    request = make_request("GET", "/v1/stats", "bootstrap-admin-token", "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    assert(strstr(body, "\"cache_sets\":1") != NULL);
    free(request);
    free(body);

    {
        char final_log_path[MP_CACHE_PATH_CAP];
        sleep(1);
        assert_format_text(final_log_path, sizeof(final_log_path), "%s/latest.log", fixture.config.log_directory);
        log_file = fopen(final_log_path, "w");
    }
    assert(log_file != NULL);
    assert(fprintf(log_file, "line-1\nline-2\n") > 0);
    assert(fclose(log_file) == 0);

    request = make_request("GET", "/v1/logs?tail=1", "bootstrap-admin-token", "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    assert(strstr(body, "line-2") != NULL);
    free(request);
    free(body);

    request = make_request("POST", "/v1/export", "bootstrap-admin-token", "{}");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    extract_json_string(body, "path", export_path, sizeof(export_path));
    free(request);
    free(body);

    request = make_request("POST", "/v1/purge/all", "bootstrap-admin-token", "{}");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    free(request);
    free(body);

    request = make_request("GET", "/v1/cache/alpha", client_token, "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 404);
    free(request);
    free(body);

    import_body = allocf("{\"path\":\"%s\"}", export_path);
    request = make_request("POST", "/v1/import", "bootstrap-admin-token", import_body);
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    free(import_body);
    free(request);
    free(body);

    request = make_request("GET", "/v1/cache/alpha", client_token, "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    assert(strstr(body, "\"value_base64\":\"aGVsbG8=\"") != NULL);
    free(request);
    free(body);

    fixture_destroy(&fixture);
}

static void test_rate_limiting_is_enforced(void) {
    http_fixture_t fixture;
    char suffix[32];
    char *request = NULL;
    char *body = NULL;
    int status_code = 0;

    (void)snprintf(suffix, sizeof(suffix), "%ld-rl", (long)getpid());
    fixture_init(&fixture, suffix, 2u);

    request = make_request("GET", "/v1/stats", "bootstrap-admin-token", "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    free(body);
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    free(body);
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 429);
    free(body);
    free(request);

    fixture_destroy(&fixture);
}

int main(void) {
    test_http_routes_cover_phase_two_three_and_four();
    test_rate_limiting_is_enforced();
    return 0;
}
