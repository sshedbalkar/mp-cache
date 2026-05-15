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

/* Owns the per-test server, cache, storage, security, and log fixture state. */
typedef struct {
    mp_cache_config_t config;
    mp_cache_log_t log;
    mp_cache_store_t store;
    mp_cache_security_t security;
    mp_cache_storage_t storage;
    mp_cache_http_server_t server;
} http_fixture_t;

/* Format expected path and JSON snippets for the HTTP test fixture. */
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

/* Redirect runtime paths into per-test temporary files and directories. */
static void configure_temp_paths(mp_cache_config_t *config, const char *suffix) {
    assert(config != NULL);
    assert_format_text(config->data_directory, sizeof(config->data_directory), ".tmp/http-test-%s-data", suffix);
    assert_format_text(config->export_directory, sizeof(config->export_directory), ".tmp/http-test-%s-exports", suffix);
    assert_format_text(config->checkpoint_path, sizeof(config->checkpoint_path), "%s/state.checkpoint", config->data_directory);
    assert_format_text(config->journal_path, sizeof(config->journal_path), "%s/state.journal", config->data_directory);
    assert_format_text(config->log_directory, sizeof(config->log_directory), ".tmp/http-test-%s-logs", suffix);
}

/* Build and start an in-process HTTP fixture with isolated storage and logging paths. */
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

/* Tear down the in-process HTTP fixture and remove its transient runtime state. */
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

/* Allocate one formatted request or assertion helper string on the heap. */
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

/* Construct one raw HTTP/1.1 request string for the in-process server harness. */
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

/* Extract one JSON string field from a handler response body for assertions. */
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

/* Return whether an HTTP error code belongs to the project-owned API vocabulary. */
static bool is_project_error_code(const char *error_code) {
    return error_code != NULL &&
           (strcmp(error_code, "conflict") == 0 ||
            strcmp(error_code, "forbidden") == 0 ||
            strcmp(error_code, "internal_error") == 0 ||
            strcmp(error_code, "invalid_argument") == 0 ||
            strcmp(error_code, "limit_exceeded") == 0 ||
            strcmp(error_code, "not_found") == 0 ||
            strcmp(error_code, "unauthorized") == 0);
}

/* Verify error responses carry machine-stable codes and human-only descriptions. */
static void assert_error_response_code(const char *body, const char *expected_error_code) {
    char error_code[64];
    char error_description[256];
    char legacy_error[64];
    char legacy_message[256];

    assert(body != NULL);
    extract_json_string(body, "error_code", error_code, sizeof(error_code));
    extract_json_string(body, "error_description", error_description, sizeof(error_description));
    extract_json_string(body, "error", legacy_error, sizeof(legacy_error));
    extract_json_string(body, "message", legacy_message, sizeof(legacy_message));

    assert(is_project_error_code(error_code));
    assert(error_description[0] != '\0');
    assert(strcmp(legacy_error, error_code) == 0);
    assert(strcmp(legacy_message, error_description) == 0);
    if (expected_error_code != NULL) {
        assert(strcmp(error_code, expected_error_code) == 0);
    }
}

/* Verify the main authenticated route set across the implemented feature phases. */
static void test_http_routes_cover_phase_two_three_and_four(void) {
    http_fixture_t fixture;
    char suffix[32];
    char *request = NULL;
    char *body = NULL;
    char *import_body = NULL;
    char client_token[MP_CACHE_TOKEN_TEXT_CAP];
    char rotated_client_token[MP_CACHE_TOKEN_TEXT_CAP];
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

    request = make_request(
        "PUT",
        "/v1/cache/beta",
        client_token,
        "{\"value_base64\":\"d29ybGQ=\",\"ttl_seconds\":60}");
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
    assert_error_response_code(body, "forbidden");
    free(request);
    free(body);

    request = make_request("GET", "/v1/stats", "bootstrap-admin-token", "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    assert(strstr(body, "\"cache_sets\":2") != NULL);
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

    request = make_request(
        "POST",
        "/v1/purge/keys",
        "bootstrap-admin-token",
        "{\"keys\":[\"alpha\",\"missing-key\"]}");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    assert(strstr(body, "\"requested_keys\":2") != NULL);
    assert(strstr(body, "\"purged_keys\":1") != NULL);
    assert(strstr(body, "\"missing_keys\":1") != NULL);
    free(request);
    free(body);

    request = make_request("GET", "/v1/cache/alpha", client_token, "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 404);
    assert_error_response_code(body, "not_found");
    free(request);
    free(body);

    request = make_request("GET", "/v1/cache/beta", client_token, "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    assert(strstr(body, "\"value_base64\":\"d29ybGQ=\"") != NULL);
    free(request);
    free(body);

    request = make_request("POST", "/v1/clients/client-one/invalidate-token", "bootstrap-admin-token", "{}");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    assert(strstr(body, "\"token_active\":false") != NULL);
    free(request);
    free(body);

    request = make_request("GET", "/v1/cache/beta", client_token, "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 401);
    assert_error_response_code(body, "unauthorized");
    free(request);
    free(body);

    request = make_request("POST", "/v1/clients/client-one/rotate-token", "bootstrap-admin-token", "{}");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    extract_json_string(body, "token", rotated_client_token, sizeof(rotated_client_token));
    free(request);
    free(body);

    request = make_request("GET", "/v1/cache/beta", rotated_client_token, "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    assert(strstr(body, "\"value_base64\":\"d29ybGQ=\"") != NULL);
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

    request = make_request("GET", "/v1/cache/beta", rotated_client_token, "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 404);
    assert_error_response_code(body, "not_found");
    free(request);
    free(body);

    import_body = allocf("{\"path\":\"%s\"}", export_path);
    request = make_request("POST", "/v1/import", "bootstrap-admin-token", import_body);
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    free(import_body);
    free(request);
    free(body);

    request = make_request("GET", "/v1/cache/beta", rotated_client_token, "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    assert(strstr(body, "\"value_base64\":\"d29ybGQ=\"") != NULL);
    free(request);
    free(body);

    request = make_request("GET", "/v1/stats", "bootstrap-admin-token", "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    assert(strstr(body, "\"cache_sets\":2") != NULL);
    assert(strstr(body, "\"cache_deletes\":1") != NULL);
    assert(strstr(body, "\"token_rotations\":1") != NULL);
    assert(strstr(body, "\"token_invalidations\":1") != NULL);
    free(request);
    free(body);

    fixture_destroy(&fixture);
}

/* Verify representative application errors always include a stable project error code. */
static void test_error_responses_include_project_error_codes(void) {
    http_fixture_t fixture;
    char suffix[32];
    char *request = NULL;
    char *body = NULL;
    char client_token[MP_CACHE_TOKEN_TEXT_CAP];
    int status_code = 0;

    (void)snprintf(suffix, sizeof(suffix), "%ld-errors", (long)getpid());
    fixture_init(&fixture, suffix, 50u);

    request = make_request("GET", "/v1/stats", NULL, "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 401);
    assert_error_response_code(body, "unauthorized");
    free(request);
    free(body);

    request = make_request("POST", "/v1/health", NULL, "{}");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 405);
    assert_error_response_code(body, "invalid_argument");
    free(request);
    free(body);

    request = make_request("GET", "/v1/missing", NULL, "");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 404);
    assert_error_response_code(body, "not_found");
    free(request);
    free(body);

    request = make_request(
        "POST",
        "/v1/clients",
        "bootstrap-admin-token",
        "{\"client_id\":\"client-errors\",\"role\":\"client\"}");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 200);
    extract_json_string(body, "token", client_token, sizeof(client_token));
    free(request);
    free(body);

    request = make_request(
        "POST",
        "/v1/clients",
        "bootstrap-admin-token",
        "{\"client_id\":\"client-errors\",\"role\":\"client\"}");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 409);
    assert_error_response_code(body, "conflict");
    free(request);
    free(body);

    request = make_request("PUT", "/v1/cache/bad-value", client_token, "{\"value_base64\":\"not-base64\"}");
    assert(mp_cache_http_server_test_request(&fixture.server, request, &status_code, &body) == 0);
    assert(status_code == 400);
    assert_error_response_code(body, "invalid_argument");
    free(request);
    free(body);

    fixture_destroy(&fixture);
}

/* Verify per-principal rate limiting rejects requests after the configured budget is exhausted. */
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
    assert_error_response_code(body, "limit_exceeded");
    free(body);
    free(request);

    fixture_destroy(&fixture);
}

/* Run the HTTP unit-test group. */
int main(void) {
    test_http_routes_cover_phase_two_three_and_four();
    test_error_responses_include_project_error_codes();
    test_rate_limiting_is_enforced();
    return 0;
}
