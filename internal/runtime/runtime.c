#include "internal/runtime/runtime.h"

#include "internal/cache/cache.h"
#include "internal/config/config.h"
#include "internal/httpserver/http_server.h"
#include "internal/observability/log.h"
#include "internal/platform/fs.h"
#include "internal/security/security.h"
#include "internal/storage/storage.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Shared stop flag flipped by the process signal handlers. */
static volatile sig_atomic_t mp_cache_stop_requested = 0;

/* Convert SIGINT and SIGTERM into a cooperative server-stop request. */
static void mp_cache_runtime_signal_handler(int signal_number) {
    (void)signal_number;
    mp_cache_stop_requested = 1;
}

/* Register the runtime signal handler for the supported shutdown signals. */
static int mp_cache_runtime_register_signals(void) {
    if (signal(SIGINT, mp_cache_runtime_signal_handler) == SIG_ERR) {
        return -1;
    }
    if (signal(SIGTERM, mp_cache_runtime_signal_handler) == SIG_ERR) {
        return -1;
    }
    return 0;
}

/* Compose config, logging, cache, security, storage, and HTTP serving into one process lifecycle. */
int mp_cache_runtime_run(const char *config_path, int print_config_only) {
    mp_cache_config_t config;
    mp_cache_config_status_t config_status;
    mp_cache_store_t store;
    mp_cache_security_t security;
    mp_cache_storage_t storage;
    mp_cache_log_t log;
    mp_cache_http_server_t server;
    time_t started_at_utc = time(NULL);
    bool log_started = false;
    bool store_started = false;
    bool security_started = false;
    bool storage_started = false;
    bool server_started = false;
    int exit_code = 1;

    if (config_path == NULL || *config_path == '\0') {
        (void)fprintf(stderr, "error: config path is required\n");
        return 1;
    }

    mp_cache_config_init_defaults(&config);
    config_status = mp_cache_config_load_file(config_path, &config);
    if (config_status == MP_CACHE_CONFIG_STATUS_NOT_FOUND) {
        (void)fprintf(
            stderr,
            "info: config file %s not found; using defaults and writing a bootstrap template\n",
            config_path);
        if (mp_cache_config_write_template(config_path, &config) != MP_CACHE_CONFIG_STATUS_OK) {
            (void)fprintf(stderr, "warning: failed to write bootstrap template to %s\n", config_path);
        }
    } else if (config_status != MP_CACHE_CONFIG_STATUS_OK) {
        (void)fprintf(
            stderr,
            "error: config load failed for %s with status %s\n",
            config_path,
            mp_cache_config_status_name(config_status));
        return 1;
    }

    if (print_config_only != 0) {
        mp_cache_config_print(stdout, &config, config_path);
        return 0;
    }

    if (mp_cache_fs_ensure_directory(config.log_directory) != 0 ||
        mp_cache_fs_ensure_directory(config.data_directory) != 0 ||
        mp_cache_fs_ensure_directory(config.export_directory) != 0 ||
        mp_cache_fs_ensure_parent_directory(config.socket_path) != 0 ||
        mp_cache_fs_ensure_parent_directory(config.pid_file_path) != 0) {
        (void)fprintf(stderr, "error: failed to prepare runtime directories\n");
        return 1;
    }

    if (mp_cache_log_init(&log, &config) != 0) {
        (void)fprintf(stderr, "error: failed to initialize mp_logger\n");
        return 1;
    }
    log_started = true;

    if (mp_cache_store_init(&store, &config) != 0) {
        mp_cache_log_writef(&log, MP_LOG_LEVEL_ERROR, "startup", "failed to initialize cache store");
        goto cleanup;
    }
    store_started = true;

    if (mp_cache_security_init(&security, &config) != 0) {
        mp_cache_log_writef(&log, MP_LOG_LEVEL_ERROR, "startup", "failed to resolve bootstrap admin token");
        goto cleanup;
    }
    security_started = true;

    if (mp_cache_storage_init(&storage, &config, &log) != 0) {
        mp_cache_log_writef(&log, MP_LOG_LEVEL_ERROR, "startup", "failed to initialize storage");
        goto cleanup;
    }
    storage_started = true;

    if (mp_cache_storage_load_state(&storage, &store, &security, started_at_utc) != 0) {
        mp_cache_log_writef(&log, MP_LOG_LEVEL_ERROR, "startup", "failed to load checkpoint or journal state");
        goto cleanup;
    }

    if (mp_cache_runtime_register_signals() != 0) {
        mp_cache_log_writef(&log, MP_LOG_LEVEL_ERROR, "startup", "failed to register signal handlers");
        goto cleanup;
    }

    if (mp_cache_http_server_start(&server, &config, &log, &store, &security, &storage, started_at_utc) != 0) {
        mp_cache_log_writef(
            &log,
            MP_LOG_LEVEL_ERROR,
            "startup",
            "failed to bind unix socket at %s: %s",
            config.socket_path,
            strerror(errno));
        goto cleanup;
    }
    server_started = true;

    if (mp_cache_fs_write_pid_file(config.pid_file_path, getpid()) != 0) {
        mp_cache_log_writef(&log, MP_LOG_LEVEL_ERROR, "startup", "failed to write pid file %s", config.pid_file_path);
        goto cleanup;
    }

    mp_cache_log_writef(
        &log,
        MP_LOG_LEVEL_INFO,
        "startup",
        "service started on unix socket %s with default TTL %u seconds",
        config.socket_path,
        config.default_ttl_seconds);

    if (mp_cache_http_server_serve(&server, &mp_cache_stop_requested) != 0) {
        mp_cache_log_writef(&log, MP_LOG_LEVEL_ERROR, "runtime", "server loop exited with an error");
        goto cleanup;
    }

    mp_cache_log_writef(&log, MP_LOG_LEVEL_INFO, "shutdown", "shutdown requested");
    exit_code = 0;

cleanup:
    if (server_started) {
        mp_cache_http_server_stop(&server);
    }
    (void)mp_cache_fs_remove_path_if_exists(config.pid_file_path);
    if (storage_started && store_started && security_started) {
        if (mp_cache_storage_checkpoint(&storage, &store, &security, time(NULL)) != 0) {
            mp_cache_log_writef(&log, MP_LOG_LEVEL_WARNING, "shutdown", "failed to write final checkpoint");
        }
    }
    if (store_started) {
        mp_cache_store_destroy(&store);
    }
    if (storage_started) {
        mp_cache_storage_destroy(&storage);
    }
    if (security_started) {
        mp_cache_security_destroy(&security);
    }
    if (log_started) {
        mp_cache_log_shutdown(&log, config.shutdown_timeout_millis);
    }

    return exit_code;
}
