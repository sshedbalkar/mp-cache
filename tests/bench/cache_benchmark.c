#define _POSIX_C_SOURCE 200809L

#include "internal/cache/cache.h"
#include "internal/config/config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double seconds_since(const struct timespec *start, const struct timespec *end) {
    return (double)(end->tv_sec - start->tv_sec) + ((double)(end->tv_nsec - start->tv_nsec) / 1000000000.0);
}

int main(void) {
    mp_cache_config_t config;
    mp_cache_store_t store;
    struct timespec start_time;
    struct timespec end_time;
    char key[32];
    const char *value = "benchmark-value";
    const uint32_t operation_count = 25000u;
    uint32_t index = 0u;
    double elapsed_seconds = 0.0;
    double operations_per_second = 0.0;

    mp_cache_config_init_defaults(&config);
    config.bucket_count = 8192u;
    config.memory_limit_bytes = 16u * 1024u * 1024u;
    if (mp_cache_store_init(&store, &config) != 0) {
        (void)fprintf(stderr, "failed to initialize store\n");
        return 1;
    }

    (void)clock_gettime(CLOCK_MONOTONIC, &start_time);
    for (index = 0u; index < operation_count; index++) {
        uint8_t *copy = NULL;
        size_t copy_length = 0u;

        (void)snprintf(key, sizeof(key), "bench-%u", index);
        if (mp_cache_store_set(&store, key, strlen(key), value, strlen(value), 300u, 1000) != MP_CACHE_STORE_STATUS_OK ||
            mp_cache_store_get_copy(&store, key, strlen(key), 1001, &copy, &copy_length, NULL) != MP_CACHE_STORE_STATUS_OK ||
            mp_cache_store_delete(&store, key, strlen(key)) != MP_CACHE_STORE_STATUS_OK) {
            mp_cache_store_destroy(&store);
            (void)fprintf(stderr, "benchmark operation failed at index %u\n", index);
            return 1;
        }
        free(copy);
    }
    (void)clock_gettime(CLOCK_MONOTONIC, &end_time);

    elapsed_seconds = seconds_since(&start_time, &end_time);
    operations_per_second = elapsed_seconds <= 0.0 ? 0.0 : ((double)operation_count * 3.0) / elapsed_seconds;

    (void)printf(
        "cache_benchmark operations=%u elapsed_seconds=%.6f ops_per_second=%.2f\n",
        operation_count * 3u,
        elapsed_seconds,
        operations_per_second);

    mp_cache_store_destroy(&store);
    return 0;
}
