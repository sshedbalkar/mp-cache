#include "internal/config/config.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Create a directory when the test needs a repository-local temp path. */
static void ensure_directory_exists(const char *path) {
    assert(path != NULL);
    assert(mkdir(path, 0777) == 0 || errno == EEXIST);
}

/* Verify the compiled defaults include the required TTL-related settings. */
static void test_defaults_include_required_ttl(void) {
    mp_cache_config_t config;

    mp_cache_config_init_defaults(&config);
    assert(strcmp(config.socket_path, ".tmp/run/mp-cache.sock") == 0);
    assert(strcmp(config.pid_file_path, ".tmp/run/mp-cache.pid") == 0);
    assert(config.default_ttl_seconds == 172800u);
    assert(config.min_ttl_seconds == 1u);
    assert(config.max_ttl_seconds >= config.default_ttl_seconds);
    assert(strcmp(config.bootstrap_admin_token_secret_ref, "env:MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN") == 0);
    assert(strcmp(config.storage_key_secret_ref, "env:MP_SECRET_LOCAL_STORAGE_KEY") == 0);
    assert(strcmp(config.checkpoint_path, ".tmp/data/state.checkpoint") == 0);
    assert(strcmp(config.journal_path, ".tmp/data/state.journal") == 0);
}

/* Verify file-backed config parsing overrides the default TTL and related settings. */
static void test_load_file_overrides_default_ttl(void) {
    char path[256];
    FILE *file = NULL;
    mp_cache_config_t config;

    ensure_directory_exists(".tmp");
    ensure_directory_exists(".tmp/config-tests");

    (void)snprintf(path, sizeof(path), ".tmp/config-tests/mp-cache-config-test-%ld.ini", (long)getpid());
    file = fopen(path, "w");
    assert(file != NULL);
    (void)fprintf(
        file,
        "[service]\n"
        "environment_name = qa\n"
        "service_name = mp-cache\n"
        "[server]\n"
        "socket_path = .tmp/config-tests/runtime/mp-cache.sock\n"
        "pid_file_path = .tmp/config-tests/runtime/mp-cache.pid\n"
        "shutdown_timeout_millis = 9000\n"
        "[cache]\n"
        "memory_limit_bytes = 1024\n"
        "default_ttl_seconds = 42\n"
        "min_ttl_seconds = 1\n"
        "max_ttl_seconds = 1000\n"
        "max_key_bytes = 64\n"
        "max_value_bytes = 256\n"
        "bucket_count = 16\n"
        "sweep_interval_seconds = 3\n"
        "[storage]\n"
        "data_directory = .tmp/config-tests/data\n"
        "export_directory = .tmp/config-tests/exports\n"
        "checkpoint_path = .tmp/config-tests/data/checkpoint.bin\n"
        "journal_path = .tmp/config-tests/data/journal.bin\n"
        "max_export_files = 4\n"
        "[observability]\n"
        "log_directory = .tmp/config-tests/logs\n"
        "max_log_lines = 42\n"
        "[security]\n"
        "bootstrap_admin_token_secret_ref = env:MP_TEST_ADMIN_TOKEN\n"
        "storage_key_secret_ref = file:/var/lib/mp-cache/storage-key\n"
        "rate_limit_requests = 15\n"
        "rate_limit_window_seconds = 30\n");
    assert(fclose(file) == 0);

    mp_cache_config_init_defaults(&config);
    assert(mp_cache_config_load_file(path, &config) == MP_CACHE_CONFIG_STATUS_OK);
    assert(strcmp(config.environment_name, "qa") == 0);
    assert(config.default_ttl_seconds == 42u);
    assert(config.max_export_files == 4u);
    assert(strcmp(config.checkpoint_path, ".tmp/config-tests/data/checkpoint.bin") == 0);
    assert(strcmp(config.journal_path, ".tmp/config-tests/data/journal.bin") == 0);
    assert(config.max_log_lines == 42u);
    assert(config.rate_limit_requests == 15u);
    assert(config.rate_limit_window_seconds == 30u);

    assert(unlink(path) == 0);
}

/* Verify generated config templates document the default TTL setting. */
static void test_template_write_mentions_default_ttl(void) {
    char path[256];
    char contents[8192];
    FILE *file = NULL;
    size_t read_length = 0u;
    mp_cache_config_t config;

    ensure_directory_exists(".tmp");
    ensure_directory_exists(".tmp/config-tests");

    (void)snprintf(path, sizeof(path), ".tmp/config-tests/mp-cache-template-test-%ld.ini", (long)getpid());

    mp_cache_config_init_defaults(&config);
    assert(mp_cache_config_write_template(path, &config) == MP_CACHE_CONFIG_STATUS_OK);

    file = fopen(path, "r");
    assert(file != NULL);
    read_length = fread(contents, 1u, sizeof(contents) - 1u, file);
    contents[read_length] = '\0';
    assert(fclose(file) == 0);

    assert(strstr(contents, "default_ttl_seconds = 172800") != NULL);
    assert(strstr(contents, "socket_path = .tmp/run/mp-cache.sock") != NULL);
    assert(strstr(contents, "pid_file_path = .tmp/run/mp-cache.pid") != NULL);
    assert(unlink(path) == 0);
}

/* Run the config unit-test group. */
int main(void) {
    test_defaults_include_required_ttl();
    test_load_file_overrides_default_ttl();
    test_template_write_mentions_default_ttl();
    return 0;
}
