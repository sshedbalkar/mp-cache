#include "internal/cache/cache.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct mp_cache_entry {
    struct mp_cache_entry *next;
    size_t key_length;
    size_t value_length;
    int64_t expires_at_utc_seconds;
    char *key;
    uint8_t *value;
};

static uint64_t mp_cache_hash_bytes(const uint8_t *bytes, size_t length) {
    uint64_t hash = 1469598103934665603ull;
    size_t index = 0u;

    for (index = 0u; index < length; index++) {
        hash ^= (uint64_t)bytes[index];
        hash *= 1099511628211ull;
    }

    return hash;
}

static size_t mp_cache_entry_cost(const mp_cache_entry_t *entry) {
    if (entry == NULL) {
        return 0u;
    }
    return sizeof(*entry) + entry->key_length + 1u + entry->value_length;
}

static mp_cache_entry_t **mp_cache_find_slot(
    mp_cache_store_t *store,
    const void *key,
    size_t key_length) {
    uint64_t hash = 0u;
    size_t bucket_index = 0u;
    mp_cache_entry_t **slot = NULL;

    hash = mp_cache_hash_bytes((const uint8_t *)key, key_length);
    bucket_index = (size_t)(hash % store->bucket_count);
    slot = &store->buckets[bucket_index];

    while (*slot != NULL) {
        if ((*slot)->key_length == key_length && memcmp((*slot)->key, key, key_length) == 0) {
            break;
        }
        slot = &(*slot)->next;
    }

    return slot;
}

static void mp_cache_free_entry(mp_cache_entry_t *entry) {
    if (entry == NULL) {
        return;
    }
    free(entry->key);
    free(entry->value);
    free(entry);
}

int mp_cache_store_init(mp_cache_store_t *store, const mp_cache_config_t *config) {
    if (store == NULL || config == NULL || config->bucket_count == 0u) {
        return -1;
    }

    memset(store, 0, sizeof(*store));
    store->buckets = calloc(config->bucket_count, sizeof(*store->buckets));
    if (store->buckets == NULL) {
        return -1;
    }

    store->bucket_count = config->bucket_count;
    store->memory_limit_bytes = config->memory_limit_bytes;
    store->max_key_bytes = config->max_key_bytes;
    store->max_value_bytes = config->max_value_bytes;
    return 0;
}

void mp_cache_store_destroy(mp_cache_store_t *store) {
    size_t bucket_index = 0u;

    if (store == NULL || store->buckets == NULL) {
        return;
    }

    for (bucket_index = 0u; bucket_index < store->bucket_count; bucket_index++) {
        mp_cache_entry_t *entry = store->buckets[bucket_index];
        while (entry != NULL) {
            mp_cache_entry_t *next = entry->next;
            mp_cache_free_entry(entry);
            entry = next;
        }
    }

    free(store->buckets);
    memset(store, 0, sizeof(*store));
}

mp_cache_store_status_t mp_cache_store_set(
    mp_cache_store_t *store,
    const void *key,
    size_t key_length,
    const void *value,
    size_t value_length,
    uint32_t ttl_seconds,
    int64_t now_utc_seconds) {
    mp_cache_entry_t **slot = NULL;
    mp_cache_entry_t *entry = NULL;
    size_t previous_cost = 0u;
    size_t next_cost = 0u;
    char *key_copy = NULL;
    uint8_t *value_copy = NULL;

    if (store == NULL || key == NULL || value == NULL || ttl_seconds == 0u) {
        return MP_CACHE_STORE_STATUS_INVALID_ARGUMENT;
    }
    if (key_length == 0u || key_length > store->max_key_bytes || value_length > store->max_value_bytes) {
        return MP_CACHE_STORE_STATUS_LIMIT_EXCEEDED;
    }

    slot = mp_cache_find_slot(store, key, key_length);
    entry = *slot;
    previous_cost = mp_cache_entry_cost(entry);
    next_cost = sizeof(mp_cache_entry_t) + key_length + 1u + value_length;

    if (store->bytes_used - previous_cost + next_cost > store->memory_limit_bytes) {
        return MP_CACHE_STORE_STATUS_LIMIT_EXCEEDED;
    }

    key_copy = malloc(key_length + 1u);
    value_copy = malloc(value_length == 0u ? 1u : value_length);
    if (key_copy == NULL || value_copy == NULL) {
        free(key_copy);
        free(value_copy);
        return MP_CACHE_STORE_STATUS_NO_MEMORY;
    }

    memcpy(key_copy, key, key_length);
    key_copy[key_length] = '\0';
    if (value_length > 0u) {
        memcpy(value_copy, value, value_length);
    }

    if (entry == NULL) {
        entry = calloc(1u, sizeof(*entry));
        if (entry == NULL) {
            free(key_copy);
            free(value_copy);
            return MP_CACHE_STORE_STATUS_NO_MEMORY;
        }
        *slot = entry;
        store->entry_count++;
    } else {
        free(entry->key);
        free(entry->value);
    }

    entry->key = key_copy;
    entry->value = value_copy;
    entry->key_length = key_length;
    entry->value_length = value_length;
    entry->expires_at_utc_seconds = now_utc_seconds + (int64_t)ttl_seconds;

    store->bytes_used = store->bytes_used - previous_cost + next_cost;
    return MP_CACHE_STORE_STATUS_OK;
}

mp_cache_store_status_t mp_cache_store_get_copy(
    mp_cache_store_t *store,
    const void *key,
    size_t key_length,
    int64_t now_utc_seconds,
    uint8_t **out_value,
    size_t *out_value_length,
    int64_t *out_expires_at_utc_seconds) {
    mp_cache_entry_t **slot = NULL;
    mp_cache_entry_t *entry = NULL;
    uint8_t *copy = NULL;

    if (store == NULL || key == NULL || out_value == NULL || out_value_length == NULL) {
        return MP_CACHE_STORE_STATUS_INVALID_ARGUMENT;
    }

    slot = mp_cache_find_slot(store, key, key_length);
    entry = *slot;
    if (entry == NULL) {
        return MP_CACHE_STORE_STATUS_NOT_FOUND;
    }

    if (entry->expires_at_utc_seconds <= now_utc_seconds) {
        store->bytes_used -= mp_cache_entry_cost(entry);
        store->entry_count--;
        *slot = entry->next;
        mp_cache_free_entry(entry);
        return MP_CACHE_STORE_STATUS_EXPIRED;
    }

    copy = malloc(entry->value_length == 0u ? 1u : entry->value_length);
    if (copy == NULL) {
        return MP_CACHE_STORE_STATUS_NO_MEMORY;
    }

    if (entry->value_length > 0u) {
        memcpy(copy, entry->value, entry->value_length);
    }

    *out_value = copy;
    *out_value_length = entry->value_length;
    if (out_expires_at_utc_seconds != NULL) {
        *out_expires_at_utc_seconds = entry->expires_at_utc_seconds;
    }
    return MP_CACHE_STORE_STATUS_OK;
}

mp_cache_store_status_t mp_cache_store_delete(
    mp_cache_store_t *store,
    const void *key,
    size_t key_length) {
    mp_cache_entry_t **slot = NULL;
    mp_cache_entry_t *entry = NULL;

    if (store == NULL || key == NULL) {
        return MP_CACHE_STORE_STATUS_INVALID_ARGUMENT;
    }

    slot = mp_cache_find_slot(store, key, key_length);
    entry = *slot;
    if (entry == NULL) {
        return MP_CACHE_STORE_STATUS_NOT_FOUND;
    }

    *slot = entry->next;
    store->bytes_used -= mp_cache_entry_cost(entry);
    store->entry_count--;
    mp_cache_free_entry(entry);
    return MP_CACHE_STORE_STATUS_OK;
}

size_t mp_cache_store_purge_expired(mp_cache_store_t *store, int64_t now_utc_seconds) {
    size_t removed_count = 0u;
    size_t bucket_index = 0u;

    if (store == NULL) {
        return 0u;
    }

    for (bucket_index = 0u; bucket_index < store->bucket_count; bucket_index++) {
        mp_cache_entry_t **slot = &store->buckets[bucket_index];
        while (*slot != NULL) {
            mp_cache_entry_t *entry = *slot;
            if (entry->expires_at_utc_seconds > now_utc_seconds) {
                slot = &entry->next;
                continue;
            }

            *slot = entry->next;
            store->bytes_used -= mp_cache_entry_cost(entry);
            store->entry_count--;
            removed_count++;
            mp_cache_free_entry(entry);
        }
    }

    return removed_count;
}

void mp_cache_store_get_stats(const mp_cache_store_t *store, mp_cache_store_stats_t *out_stats) {
    if (store == NULL || out_stats == NULL) {
        return;
    }

    out_stats->entry_count = store->entry_count;
    out_stats->bytes_used = store->bytes_used;
    out_stats->memory_limit_bytes = store->memory_limit_bytes;
}

const char *mp_cache_store_status_name(mp_cache_store_status_t status) {
    switch (status) {
        case MP_CACHE_STORE_STATUS_OK:
            return "OK";
        case MP_CACHE_STORE_STATUS_NOT_FOUND:
            return "NOT_FOUND";
        case MP_CACHE_STORE_STATUS_EXPIRED:
            return "EXPIRED";
        case MP_CACHE_STORE_STATUS_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case MP_CACHE_STORE_STATUS_LIMIT_EXCEEDED:
            return "LIMIT_EXCEEDED";
        case MP_CACHE_STORE_STATUS_NO_MEMORY:
            return "NO_MEMORY";
        default:
            return "UNKNOWN";
    }
}
