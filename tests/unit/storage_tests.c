#define _POSIX_C_SOURCE 200809L

#include "internal/cache/cache.h"
#include "internal/config/config.h"
#include "internal/observability/log.h"
#include "internal/platform/fs.h"
#include "internal/security/security.h"
#include "internal/storage/storage.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    assert_format_text(config->data_directory, sizeof(config->data_directory), ".tmp/storage-test-%s-data", suffix);
    assert_format_text(config->export_directory, sizeof(config->export_directory), ".tmp/storage-test-%s-exports", suffix);
    assert_format_text(config->checkpoint_path, sizeof(config->checkpoint_path), "%s/state.checkpoint", config->data_directory);
    assert_format_text(config->journal_path, sizeof(config->journal_path), "%s/state.journal", config->data_directory);
    assert_format_text(config->log_directory, sizeof(config->log_directory), ".tmp/storage-test-%s-logs", suffix);
}

static void test_journal_checkpoint_and_export_import_round_trip(void) {
    mp_cache_config_t config;
    mp_cache_log_t log;
    mp_cache_store_t store;
    mp_cache_store_t restored_store;
    mp_cache_store_t tampered_store;
    mp_cache_security_t security;
    mp_cache_security_t restored_security;
    mp_cache_security_t tampered_security;
    mp_cache_storage_t storage;
    mp_cache_export_result_t export_result;
    mp_cache_principal_t principal;
    char client_token[MP_CACHE_TOKEN_TEXT_CAP];
    uint8_t *value = NULL;
    size_t value_length = 0u;
    char suffix[32];
    int64_t now_utc_seconds = 1000;

    (void)snprintf(suffix, sizeof(suffix), "%ld", (long)getpid());
    assert(setenv("MP_TEST_BOOTSTRAP_ADMIN_TOKEN", "bootstrap-admin-token", 1) == 0);
    assert(setenv("MP_TEST_STORAGE_KEY", "storage-secret", 1) == 0);

    mp_cache_config_init_defaults(&config);
    configure_temp_paths(&config, suffix);
    (void)snprintf(
        config.bootstrap_admin_token_secret_ref,
        sizeof(config.bootstrap_admin_token_secret_ref),
        "env:MP_TEST_BOOTSTRAP_ADMIN_TOKEN");
    (void)snprintf(
        config.storage_key_secret_ref,
        sizeof(config.storage_key_secret_ref),
        "env:MP_TEST_STORAGE_KEY");

    assert(mp_cache_fs_ensure_directory(config.data_directory) == 0);
    assert(mp_cache_fs_ensure_directory(config.export_directory) == 0);
    assert(mp_cache_fs_ensure_directory(config.log_directory) == 0);
    memset(&log, 0, sizeof(log));
    assert(mp_cache_store_init(&store, &config) == 0);
    assert(mp_cache_store_init(&restored_store, &config) == 0);
    assert(mp_cache_store_init(&tampered_store, &config) == 0);
    assert(mp_cache_security_init(&security, &config) == 0);
    assert(mp_cache_security_init(&restored_security, &config) == 0);
    assert(mp_cache_security_init(&tampered_security, &config) == 0);
    assert(mp_cache_storage_init(&storage, &config, &log) == 0);

    assert(
        mp_cache_store_set(&store, "alpha", 5u, "bravo", 5u, 60u, now_utc_seconds) == MP_CACHE_STORE_STATUS_OK);
    assert(mp_cache_storage_append_set(&storage, "alpha", 5u, (const uint8_t *)"bravo", 5u, now_utc_seconds + 60) == 0);

    assert(
        mp_cache_security_register_client(
            &security,
            "client-one",
            MP_CACHE_ROLE_CLIENT,
            client_token,
            sizeof(client_token)) == MP_CACHE_SECURITY_STATUS_OK);
    assert(mp_cache_storage_append_client(&storage, mp_cache_security_find_client(&security, "client-one")) == 0);

    assert(mp_cache_storage_load_state(&storage, &restored_store, &restored_security, now_utc_seconds) == 0);
    assert(
        mp_cache_store_get_copy(
            &restored_store,
            "alpha",
            5u,
            now_utc_seconds + 1,
            &value,
            &value_length,
            NULL) == MP_CACHE_STORE_STATUS_OK);
    assert(value_length == 5u);
    assert(memcmp(value, "bravo", 5u) == 0);
    free(value);
    assert(
        mp_cache_security_authenticate(&restored_security, client_token, &principal) == MP_CACHE_SECURITY_STATUS_OK);

    assert(mp_cache_storage_export_state(&storage, &store, &security, now_utc_seconds, &export_result) == 0);
    mp_cache_store_clear(&restored_store);
    mp_cache_security_clear_clients(&restored_security);
    assert(
        mp_cache_storage_import_state(
            &storage,
            export_result.export_path,
            &restored_store,
            &restored_security,
            now_utc_seconds) == 0);
    assert(
        mp_cache_store_get_copy(
            &restored_store,
            "alpha",
            5u,
            now_utc_seconds + 1,
            &value,
            &value_length,
            NULL) == MP_CACHE_STORE_STATUS_OK);
    assert(value_length == 5u);
    assert(memcmp(value, "bravo", 5u) == 0);
    free(value);
    assert(
        mp_cache_security_authenticate(&restored_security, client_token, &principal) == MP_CACHE_SECURITY_STATUS_OK);

    {
        FILE *export_file = fopen(export_result.export_path, "r+b");
        assert(export_file != NULL);
        assert(fseek(export_file, 32L, SEEK_SET) == 0);
        assert(fputc('X', export_file) != EOF);
        assert(fclose(export_file) == 0);
    }
    assert(
        mp_cache_storage_import_state(
            &storage,
            export_result.export_path,
            &tampered_store,
            &tampered_security,
            now_utc_seconds) != 0);

    mp_cache_storage_destroy(&storage);
    mp_cache_security_destroy(&tampered_security);
    mp_cache_security_destroy(&restored_security);
    mp_cache_security_destroy(&security);
    mp_cache_store_destroy(&tampered_store);
    mp_cache_store_destroy(&restored_store);
    mp_cache_store_destroy(&store);
    mp_cache_log_shutdown(&log, config.shutdown_timeout_millis);
}

static void test_export_rejects_paths_that_do_not_fit(void) {
    mp_cache_config_t config;
    mp_cache_log_t log;
    mp_cache_store_t store;
    mp_cache_security_t security;
    mp_cache_storage_t storage;
    mp_cache_export_result_t export_result;
    size_t index = 0u;

    assert(setenv("MP_TEST_BOOTSTRAP_ADMIN_TOKEN", "bootstrap-admin-token", 1) == 0);
    assert(setenv("MP_TEST_STORAGE_KEY", "storage-secret", 1) == 0);

    mp_cache_config_init_defaults(&config);
    assert_format_text(config.data_directory, sizeof(config.data_directory), ".tmp/storage-test-long-data");
    assert_format_text(config.checkpoint_path, sizeof(config.checkpoint_path), "%s/state.checkpoint", config.data_directory);
    assert_format_text(config.journal_path, sizeof(config.journal_path), "%s/state.journal", config.data_directory);
    assert_format_text(config.log_directory, sizeof(config.log_directory), ".tmp/storage-test-long-logs");
    assert_format_text(
        config.bootstrap_admin_token_secret_ref,
        sizeof(config.bootstrap_admin_token_secret_ref),
        "env:MP_TEST_BOOTSTRAP_ADMIN_TOKEN");
    assert_format_text(config.storage_key_secret_ref, sizeof(config.storage_key_secret_ref), "env:MP_TEST_STORAGE_KEY");
    for (index = 0u; index + 1u < sizeof(config.export_directory); index++) {
        config.export_directory[index] = 'x';
    }
    config.export_directory[sizeof(config.export_directory) - 1u] = '\0';

    assert(mp_cache_fs_ensure_directory(config.data_directory) == 0);
    assert(mp_cache_fs_ensure_directory(config.log_directory) == 0);
    memset(&log, 0, sizeof(log));
    assert(mp_cache_store_init(&store, &config) == 0);
    assert(mp_cache_security_init(&security, &config) == 0);
    assert(mp_cache_storage_init(&storage, &config, &log) == 0);

    errno = 0;
    assert(mp_cache_storage_export_state(&storage, &store, &security, 1000, &export_result) != 0);
    assert(errno == ENAMETOOLONG);

    mp_cache_storage_destroy(&storage);
    mp_cache_security_destroy(&security);
    mp_cache_store_destroy(&store);
}

int main(void) {
    test_journal_checkpoint_and_export_import_round_trip();
    test_export_rejects_paths_that_do_not_fit();
    return 0;
}
