#include "internal/cache/cache.h"
#include "internal/config/config.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Verify the basic cache write, read, and delete flow. */
static void test_set_get_delete_round_trip(void) {
    mp_cache_config_t config;
    mp_cache_store_t store;
    uint8_t *value = NULL;
    size_t value_length = 0u;

    mp_cache_config_init_defaults(&config);
    assert(mp_cache_store_init(&store, &config) == 0);

    assert(mp_cache_store_set(&store, "alpha", 5u, "bravo", 5u, 60u, 1000) == MP_CACHE_STORE_STATUS_OK);
    assert(mp_cache_store_get_copy(&store, "alpha", 5u, 1001, &value, &value_length, NULL) == MP_CACHE_STORE_STATUS_OK);
    assert(value_length == 5u);
    assert(memcmp(value, "bravo", 5u) == 0);
    free(value);

    assert(mp_cache_store_delete(&store, "alpha", 5u) == MP_CACHE_STORE_STATUS_OK);
    assert(mp_cache_store_get_copy(&store, "alpha", 5u, 1001, &value, &value_length, NULL) == MP_CACHE_STORE_STATUS_NOT_FOUND);

    mp_cache_store_destroy(&store);
}

/* Verify expired entries are purged opportunistically on read. */
static void test_expired_entry_is_removed_on_read(void) {
    mp_cache_config_t config;
    mp_cache_store_t store;
    uint8_t *value = NULL;
    size_t value_length = 0u;

    mp_cache_config_init_defaults(&config);
    assert(mp_cache_store_init(&store, &config) == 0);

    assert(mp_cache_store_set(&store, "temp", 4u, "value", 5u, 1u, 2000) == MP_CACHE_STORE_STATUS_OK);
    assert(mp_cache_store_get_copy(&store, "temp", 4u, 2002, &value, &value_length, NULL) == MP_CACHE_STORE_STATUS_EXPIRED);
    assert(value == NULL);

    mp_cache_store_destroy(&store);
}

/* Verify the store rejects writes that would exceed the configured memory ceiling. */
static void test_memory_limit_is_enforced(void) {
    mp_cache_config_t config;
    mp_cache_store_t store;
    char large_value[256];

    memset(large_value, 'x', sizeof(large_value));
    mp_cache_config_init_defaults(&config);
    config.memory_limit_bytes = 96u;
    config.bucket_count = 8u;
    assert(mp_cache_store_init(&store, &config) == 0);

    assert(
        mp_cache_store_set(
            &store,
            "oversized",
            9u,
            large_value,
            sizeof(large_value),
            10u,
            3000) == MP_CACHE_STORE_STATUS_LIMIT_EXCEEDED);

    mp_cache_store_destroy(&store);
}

/* Run the cache unit-test group. */
int main(void) {
    test_set_get_delete_round_trip();
    test_expired_entry_is_removed_on_read();
    test_memory_limit_is_enforced();
    return 0;
}
