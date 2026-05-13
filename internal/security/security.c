#include "internal/security/security.h"

#include "internal/crypto/crypto.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MP_CACHE_BOOTSTRAP_ADMIN_ID "bootstrap-admin"
#define MP_CACHE_MAX_CLIENTS 1024u

/* Trim leading and trailing ASCII whitespace in place. */
static char *mp_cache_trim(char *value) {
    char *end = NULL;

    if (value == NULL) {
        return NULL;
    }

    while (*value != '\0' && isspace((unsigned char)*value) != 0) {
        value++;
    }

    end = value + strlen(value);
    while (end > value && isspace((unsigned char)*(end - 1)) != 0) {
        end--;
    }
    *end = '\0';
    return value;
}

/* Enforce the stable client-id character and length policy used across APIs and storage. */
static bool mp_cache_client_id_is_valid(const char *client_id) {
    size_t index = 0u;

    if (client_id == NULL || *client_id == '\0' || strlen(client_id) >= MP_CACHE_CLIENT_ID_CAP) {
        return false;
    }

    for (index = 0u; client_id[index] != '\0'; index++) {
        char character = client_id[index];
        if (!(isalnum((unsigned char)character) != 0 || character == '-' || character == '_' || character == '.')) {
            return false;
        }
    }

    return true;
}

/* Find a mutable client record by ID and optionally report its array index. */
static mp_cache_client_record_t *mp_cache_security_find_record(
    const mp_cache_security_t *security,
    const char *client_id,
    size_t *out_index) {
    size_t index = 0u;

    if (security == NULL || client_id == NULL) {
        return NULL;
    }

    for (index = 0u; index < security->count; index++) {
        if (strcmp(security->records[index].client_id, client_id) == 0) {
            if (out_index != NULL) {
                *out_index = index;
            }
            return &security->records[index];
        }
    }

    return NULL;
}

/* Grow the registered-client array up to the repository-wide hard cap. */
static mp_cache_security_status_t mp_cache_security_ensure_capacity(mp_cache_security_t *security) {
    mp_cache_client_record_t *next_records = NULL;
    size_t next_capacity = 0u;

    if (security == NULL) {
        return MP_CACHE_SECURITY_STATUS_INVALID_ARGUMENT;
    }
    if (security->count < security->capacity) {
        return MP_CACHE_SECURITY_STATUS_OK;
    }
    if (security->capacity >= MP_CACHE_MAX_CLIENTS) {
        return MP_CACHE_SECURITY_STATUS_LIMIT_EXCEEDED;
    }

    next_capacity = security->capacity == 0u ? 8u : security->capacity * 2u;
    if (next_capacity > MP_CACHE_MAX_CLIENTS) {
        next_capacity = MP_CACHE_MAX_CLIENTS;
    }

    next_records = realloc(security->records, next_capacity * sizeof(*security->records));
    if (next_records == NULL) {
        return MP_CACHE_SECURITY_STATUS_NO_MEMORY;
    }

    security->records = next_records;
    security->capacity = next_capacity;
    return MP_CACHE_SECURITY_STATUS_OK;
}

/* Hash a bearer token into the fixed-width comparison form stored by the module. */
static void mp_cache_hash_token(const char *auth_token, uint8_t out_hash[MP_CACHE_TOKEN_HASH_SIZE]) {
    mp_cache_sha256((const uint8_t *)auth_token, auth_token == NULL ? 0u : strlen(auth_token), out_hash);
}

/* Generate a fresh random client token and hex-encode it for API callers. */
static mp_cache_security_status_t mp_cache_security_issue_token(char *out_client_token, size_t out_client_token_capacity) {
    uint8_t random_bytes[32];

    if (out_client_token == NULL || out_client_token_capacity < MP_CACHE_TOKEN_TEXT_CAP) {
        return MP_CACHE_SECURITY_STATUS_INVALID_ARGUMENT;
    }

    if (mp_cache_random_bytes(random_bytes, sizeof(random_bytes)) != 0) {
        return MP_CACHE_SECURITY_STATUS_IO_ERROR;
    }
    if (mp_cache_hex_encode(random_bytes, sizeof(random_bytes), out_client_token, out_client_token_capacity) != 0) {
        return MP_CACHE_SECURITY_STATUS_IO_ERROR;
    }

    return MP_CACHE_SECURITY_STATUS_OK;
}

int mp_cache_secret_resolve(const char *secret_ref, char *out_secret, size_t out_capacity) {
    FILE *file = NULL;
    const char *resolved_secret_value = NULL;

    if (secret_ref == NULL || out_secret == NULL || out_capacity == 0u) {
        errno = EINVAL;
        return -1;
    }

    if (strncmp(secret_ref, "env:", 4u) == 0) {
        resolved_secret_value = getenv(secret_ref + 4u);
        if (resolved_secret_value == NULL || *resolved_secret_value == '\0') {
            errno = ENOENT;
            return -1;
        }
        (void)snprintf(out_secret, out_capacity, "%s", resolved_secret_value);
        return 0;
    }

    if (strncmp(secret_ref, "file:", 5u) == 0) {
        const char *secret_file_path = secret_ref + 5u;
        if (*secret_file_path != '/') {
            errno = EINVAL;
            return -1;
        }

        file = fopen(secret_file_path, "r");
        if (file == NULL) {
            return -1;
        }
        if (fgets(out_secret, (int)out_capacity, file) == NULL) {
            (void)fclose(file);
            errno = EIO;
            return -1;
        }
        (void)fclose(file);
        (void)snprintf(out_secret, out_capacity, "%s", mp_cache_trim(out_secret));
        if (out_secret[0] == '\0') {
            errno = EINVAL;
            return -1;
        }
        return 0;
    }

    errno = EINVAL;
    return -1;
}

void mp_cache_secret_derive_key(const char *secret, uint8_t out_key[MP_CACHE_TOKEN_HASH_SIZE]) {
    mp_cache_sha256((const uint8_t *)secret, secret == NULL ? 0u : strlen(secret), out_key);
}

int mp_cache_security_init(mp_cache_security_t *security, const mp_cache_config_t *config) {
    char bootstrap_admin_token[MP_CACHE_SECRET_REF_CAP];

    if (security == NULL || config == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(security, 0, sizeof(*security));
    if (mp_cache_secret_resolve(
            config->bootstrap_admin_token_secret_ref,
            bootstrap_admin_token,
            sizeof(bootstrap_admin_token)) != 0) {
        return -1;
    }

    mp_cache_hash_token(bootstrap_admin_token, security->bootstrap_admin_hash);
    security->bootstrap_admin_loaded = true;
    return 0;
}

void mp_cache_security_destroy(mp_cache_security_t *security) {
    if (security == NULL) {
        return;
    }

    free(security->records);
    memset(security, 0, sizeof(*security));
}

mp_cache_security_status_t mp_cache_security_authenticate(
    const mp_cache_security_t *security,
    const char *auth_token,
    mp_cache_principal_t *out_principal) {
    uint8_t token_hash[MP_CACHE_TOKEN_HASH_SIZE];
    size_t index = 0u;

    if (security == NULL || auth_token == NULL || *auth_token == '\0' || out_principal == NULL) {
        return MP_CACHE_SECURITY_STATUS_INVALID_ARGUMENT;
    }

    mp_cache_hash_token(auth_token, token_hash);
    if (security->bootstrap_admin_loaded &&
        mp_cache_constant_time_equals(token_hash, security->bootstrap_admin_hash, sizeof(token_hash))) {
        out_principal->role = MP_CACHE_ROLE_ADMIN;
        out_principal->client_id = MP_CACHE_BOOTSTRAP_ADMIN_ID;
        return MP_CACHE_SECURITY_STATUS_OK;
    }

    for (index = 0u; index < security->count; index++) {
        if (mp_cache_constant_time_equals(
                token_hash,
                security->records[index].token_hash,
                sizeof(security->records[index].token_hash))) {
            out_principal->role = security->records[index].role;
            out_principal->client_id = security->records[index].client_id;
            return MP_CACHE_SECURITY_STATUS_OK;
        }
    }

    return MP_CACHE_SECURITY_STATUS_UNAUTHORIZED;
}

mp_cache_security_status_t mp_cache_security_register_client(
    mp_cache_security_t *security,
    const char *client_id,
    mp_cache_role_t role,
    char *out_client_token,
    size_t out_client_token_capacity) {
    mp_cache_security_status_t status;
    mp_cache_client_record_t *record = NULL;

    if (security == NULL || out_client_token == NULL || role == MP_CACHE_ROLE_NONE || !mp_cache_client_id_is_valid(client_id)) {
        return MP_CACHE_SECURITY_STATUS_INVALID_ARGUMENT;
    }
    if (mp_cache_security_find_record(security, client_id, NULL) != NULL) {
        return MP_CACHE_SECURITY_STATUS_CONFLICT;
    }

    status = mp_cache_security_ensure_capacity(security);
    if (status != MP_CACHE_SECURITY_STATUS_OK) {
        return status;
    }
    status = mp_cache_security_issue_token(out_client_token, out_client_token_capacity);
    if (status != MP_CACHE_SECURITY_STATUS_OK) {
        return status;
    }

    record = &security->records[security->count++];
    memset(record, 0, sizeof(*record));
    (void)snprintf(record->client_id, sizeof(record->client_id), "%s", client_id);
    record->role = role;
    mp_cache_hash_token(out_client_token, record->token_hash);
    return MP_CACHE_SECURITY_STATUS_OK;
}

mp_cache_security_status_t mp_cache_security_rotate_client_token(
    mp_cache_security_t *security,
    const char *client_id,
    char *out_client_token,
    size_t out_client_token_capacity) {
    mp_cache_client_record_t *record = NULL;
    mp_cache_security_status_t status;

    if (security == NULL || out_client_token == NULL || !mp_cache_client_id_is_valid(client_id)) {
        return MP_CACHE_SECURITY_STATUS_INVALID_ARGUMENT;
    }

    record = mp_cache_security_find_record(security, client_id, NULL);
    if (record == NULL) {
        return MP_CACHE_SECURITY_STATUS_NOT_FOUND;
    }

    status = mp_cache_security_issue_token(out_client_token, out_client_token_capacity);
    if (status != MP_CACHE_SECURITY_STATUS_OK) {
        return status;
    }

    mp_cache_hash_token(out_client_token, record->token_hash);
    return MP_CACHE_SECURITY_STATUS_OK;
}

mp_cache_security_status_t mp_cache_security_import_client_hash(
    mp_cache_security_t *security,
    const char *client_id,
    mp_cache_role_t role,
    const uint8_t token_hash[MP_CACHE_TOKEN_HASH_SIZE]) {
    mp_cache_security_status_t status;
    mp_cache_client_record_t *record = NULL;

    if (security == NULL || token_hash == NULL || role == MP_CACHE_ROLE_NONE || !mp_cache_client_id_is_valid(client_id)) {
        return MP_CACHE_SECURITY_STATUS_INVALID_ARGUMENT;
    }

    record = mp_cache_security_find_record(security, client_id, NULL);
    if (record == NULL) {
        status = mp_cache_security_ensure_capacity(security);
        if (status != MP_CACHE_SECURITY_STATUS_OK) {
            return status;
        }
        record = &security->records[security->count++];
        memset(record, 0, sizeof(*record));
        (void)snprintf(record->client_id, sizeof(record->client_id), "%s", client_id);
    }

    record->role = role;
    memcpy(record->token_hash, token_hash, MP_CACHE_TOKEN_HASH_SIZE);
    return MP_CACHE_SECURITY_STATUS_OK;
}

void mp_cache_security_clear_clients(mp_cache_security_t *security) {
    if (security == NULL) {
        return;
    }

    security->count = 0u;
}

size_t mp_cache_security_client_count(const mp_cache_security_t *security) {
    return security == NULL ? 0u : security->count;
}

int mp_cache_security_for_each_client(
    const mp_cache_security_t *security,
    mp_cache_security_client_visit_fn visit,
    void *context) {
    size_t index = 0u;

    if (security == NULL || visit == NULL) {
        return -1;
    }

    for (index = 0u; index < security->count; index++) {
        if (visit(&security->records[index], context) != 0) {
            return -1;
        }
    }

    return 0;
}

const mp_cache_client_record_t *mp_cache_security_find_client(const mp_cache_security_t *security, const char *client_id) {
    return mp_cache_security_find_record(security, client_id, NULL);
}

bool mp_cache_role_allows(mp_cache_role_t actual_role, mp_cache_role_t required_role) {
    return actual_role >= required_role;
}

const char *mp_cache_role_name(mp_cache_role_t role) {
    switch (role) {
        case MP_CACHE_ROLE_CLIENT:
            return "client";
        case MP_CACHE_ROLE_OPERATOR:
            return "operator";
        case MP_CACHE_ROLE_ADMIN:
            return "admin";
        default:
            return "none";
    }
}

mp_cache_role_t mp_cache_role_from_string(const char *role_text) {
    if (role_text == NULL) {
        return MP_CACHE_ROLE_NONE;
    }
    if (strcmp(role_text, "client") == 0) {
        return MP_CACHE_ROLE_CLIENT;
    }
    if (strcmp(role_text, "operator") == 0) {
        return MP_CACHE_ROLE_OPERATOR;
    }
    if (strcmp(role_text, "admin") == 0) {
        return MP_CACHE_ROLE_ADMIN;
    }
    return MP_CACHE_ROLE_NONE;
}

const char *mp_cache_security_status_name(mp_cache_security_status_t status) {
    switch (status) {
        case MP_CACHE_SECURITY_STATUS_OK:
            return "OK";
        case MP_CACHE_SECURITY_STATUS_NOT_FOUND:
            return "NOT_FOUND";
        case MP_CACHE_SECURITY_STATUS_CONFLICT:
            return "CONFLICT";
        case MP_CACHE_SECURITY_STATUS_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case MP_CACHE_SECURITY_STATUS_SECRET_ERROR:
            return "SECRET_ERROR";
        case MP_CACHE_SECURITY_STATUS_NO_MEMORY:
            return "NO_MEMORY";
        case MP_CACHE_SECURITY_STATUS_IO_ERROR:
            return "IO_ERROR";
        case MP_CACHE_SECURITY_STATUS_UNAUTHORIZED:
            return "UNAUTHORIZED";
        case MP_CACHE_SECURITY_STATUS_LIMIT_EXCEEDED:
            return "LIMIT_EXCEEDED";
        default:
            return "UNKNOWN";
    }
}
