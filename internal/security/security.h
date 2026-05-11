#ifndef MP_CACHE_INTERNAL_SECURITY_H
#define MP_CACHE_INTERNAL_SECURITY_H

#include "internal/config/config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_CACHE_CLIENT_ID_CAP 64u
#define MP_CACHE_TOKEN_TEXT_CAP 65u
#define MP_CACHE_TOKEN_HASH_SIZE 32u

typedef enum {
    MP_CACHE_ROLE_NONE = 0,
    MP_CACHE_ROLE_CLIENT = 1,
    MP_CACHE_ROLE_OPERATOR = 2,
    MP_CACHE_ROLE_ADMIN = 3
} mp_cache_role_t;

typedef enum {
    MP_CACHE_SECURITY_STATUS_OK = 0,
    MP_CACHE_SECURITY_STATUS_NOT_FOUND = 1,
    MP_CACHE_SECURITY_STATUS_CONFLICT = 2,
    MP_CACHE_SECURITY_STATUS_INVALID_ARGUMENT = 3,
    MP_CACHE_SECURITY_STATUS_SECRET_ERROR = 4,
    MP_CACHE_SECURITY_STATUS_NO_MEMORY = 5,
    MP_CACHE_SECURITY_STATUS_IO_ERROR = 6,
    MP_CACHE_SECURITY_STATUS_UNAUTHORIZED = 7,
    MP_CACHE_SECURITY_STATUS_LIMIT_EXCEEDED = 8
} mp_cache_security_status_t;

typedef struct {
    char client_id[MP_CACHE_CLIENT_ID_CAP];
    mp_cache_role_t role;
    uint8_t token_hash[MP_CACHE_TOKEN_HASH_SIZE];
} mp_cache_client_record_t;

typedef struct {
    mp_cache_client_record_t *records;
    size_t count;
    size_t capacity;
    uint8_t bootstrap_admin_hash[MP_CACHE_TOKEN_HASH_SIZE];
    bool bootstrap_admin_loaded;
} mp_cache_security_t;

typedef struct {
    mp_cache_role_t role;
    const char *client_id;
} mp_cache_principal_t;

typedef int (*mp_cache_security_client_visit_fn)(const mp_cache_client_record_t *record, void *context);

int mp_cache_secret_resolve(const char *secret_ref, char *out_secret, size_t out_capacity);
void mp_cache_secret_derive_key(const char *secret, uint8_t out_key[MP_CACHE_TOKEN_HASH_SIZE]);

int mp_cache_security_init(mp_cache_security_t *security, const mp_cache_config_t *config);
void mp_cache_security_destroy(mp_cache_security_t *security);

mp_cache_security_status_t mp_cache_security_authenticate(
    const mp_cache_security_t *security,
    const char *auth_token,
    mp_cache_principal_t *out_principal);
mp_cache_security_status_t mp_cache_security_register_client(
    mp_cache_security_t *security,
    const char *client_id,
    mp_cache_role_t role,
    char *out_client_token,
    size_t out_client_token_capacity);
mp_cache_security_status_t mp_cache_security_rotate_client_token(
    mp_cache_security_t *security,
    const char *client_id,
    char *out_client_token,
    size_t out_client_token_capacity);
mp_cache_security_status_t mp_cache_security_import_client_hash(
    mp_cache_security_t *security,
    const char *client_id,
    mp_cache_role_t role,
    const uint8_t token_hash[MP_CACHE_TOKEN_HASH_SIZE]);

void mp_cache_security_clear_clients(mp_cache_security_t *security);
size_t mp_cache_security_client_count(const mp_cache_security_t *security);
int mp_cache_security_for_each_client(
    const mp_cache_security_t *security,
    mp_cache_security_client_visit_fn visit,
    void *context);
const mp_cache_client_record_t *mp_cache_security_find_client(const mp_cache_security_t *security, const char *client_id);

bool mp_cache_role_allows(mp_cache_role_t actual_role, mp_cache_role_t required_role);
const char *mp_cache_role_name(mp_cache_role_t role);
mp_cache_role_t mp_cache_role_from_string(const char *role_text);
const char *mp_cache_security_status_name(mp_cache_security_status_t status);

#endif
