#include "internal/config/config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_defaults_include_required_ttl(void) {
    mp_cache_config_t config;

    mp_cache_config_init_defaults(&config);
    assert(config.default_ttl_seconds == 172800u);
    assert(config.min_ttl_seconds == 1u);
    assert(config.max_ttl_seconds >= config.default_ttl_seconds);
}

static void test_load_file_overrides_default_ttl(void) {
    char path[256];
    FILE *file = NULL;
    mp_cache_config_t config;

    (void)snprintf(path, sizeof(path), "/tmp/mp-cache-config-test-%ld.ini", (long)getpid());
    file = fopen(path, "w");
    assert(file != NULL);
    (void)fprintf(
        file,
        "[service]\n"
        "environment_name = qa\n"
        "service_name = mp-cache\n"
        "[server]\n"
        "socket_path = /tmp/mp-cache.sock\n"
        "pid_file_path = /tmp/mp-cache.pid\n"
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
        "data_directory = /tmp/mp-cache-data\n"
        "export_directory = /tmp/mp-cache-exports\n"
        "max_export_files = 4\n"
        "[observability]\n"
        "log_directory = /tmp/mp-cache-logs\n"
        "[security]\n"
        "bootstrap_admin_token_secret_ref = env:test/admin\n"
        "storage_key_secret_ref = file:/tmp/storage-key\n");
    assert(fclose(file) == 0);

    mp_cache_config_init_defaults(&config);
    assert(mp_cache_config_load_file(path, &config) == MP_CACHE_CONFIG_STATUS_OK);
    assert(strcmp(config.environment_name, "qa") == 0);
    assert(config.default_ttl_seconds == 42u);
    assert(config.max_export_files == 4u);

    assert(unlink(path) == 0);
}

static void test_template_write_mentions_default_ttl(void) {
    char path[256];
    char contents[8192];
    FILE *file = NULL;
    size_t read_length = 0u;
    mp_cache_config_t config;

    (void)snprintf(path, sizeof(path), "/tmp/mp-cache-template-test-%ld.ini", (long)getpid());

    mp_cache_config_init_defaults(&config);
    assert(mp_cache_config_write_template(path, &config) == MP_CACHE_CONFIG_STATUS_OK);

    file = fopen(path, "r");
    assert(file != NULL);
    read_length = fread(contents, 1u, sizeof(contents) - 1u, file);
    contents[read_length] = '\0';
    assert(fclose(file) == 0);

    assert(strstr(contents, "default_ttl_seconds = 172800") != NULL);
    assert(unlink(path) == 0);
}

int main(void) {
    test_defaults_include_required_ttl();
    test_load_file_overrides_default_ttl();
    test_template_write_mentions_default_ttl();
    return 0;
}
