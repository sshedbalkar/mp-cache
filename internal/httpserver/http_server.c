#include "internal/httpserver/http_server.h"

#include "internal/crypto/crypto.h"
#include "internal/platform/fs.h"

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MP_CACHE_HTTP_REQUEST_CAPACITY 65536u
#define MP_CACHE_HTTP_HEADER_CAPACITY 2048u
#define MP_CACHE_HTTP_PATH_CAPACITY 512u
#define MP_CACHE_HTTP_BODY_CAPACITY 32768u

typedef struct {
    char method[16];
    char request_target[MP_CACHE_HTTP_PATH_CAPACITY];
    char request_path[MP_CACHE_HTTP_PATH_CAPACITY];
    char query_string[MP_CACHE_HTTP_PATH_CAPACITY];
    char authorization_header[256];
    size_t content_length;
    char *request_body;
    size_t request_body_length;
} mp_cache_http_request_t;

typedef struct {
    int status_code;
    const char *status_text;
    char *response_body;
    const char *allow_header_value;
    const char *extra_headers;
} mp_cache_http_response_t;

static int mp_cache_http_send_all(int fd, const char *buffer, size_t length) {
    size_t offset = 0u;

    while (offset < length) {
        ssize_t written = send(fd, buffer + offset, length - offset, 0);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (written == 0) {
            return -1;
        }
        offset += (size_t)written;
    }

    return 0;
}

static int mp_cache_http_write_response(
    int client_fd,
    int status_code,
    const char *status_text,
    const char *response_body,
    const char *allow_header_value,
    const char *extra_headers) {
    char header_buffer[MP_CACHE_HTTP_HEADER_CAPACITY];
    size_t response_body_length = response_body == NULL ? 0u : strlen(response_body);
    int header_length = 0;

    header_length = snprintf(
        header_buffer,
        sizeof(header_buffer),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "%s%s%s"
        "%s"
        "\r\n",
        status_code,
        status_text,
        response_body_length,
        allow_header_value == NULL ? "" : "Allow: ",
        allow_header_value == NULL ? "" : allow_header_value,
        allow_header_value == NULL ? "" : "\r\n",
        extra_headers == NULL ? "" : extra_headers);

    if (header_length < 0 || (size_t)header_length >= sizeof(header_buffer)) {
        return -1;
    }
    if (mp_cache_http_send_all(client_fd, header_buffer, (size_t)header_length) != 0) {
        return -1;
    }
    if (response_body_length > 0u && mp_cache_http_send_all(client_fd, response_body, response_body_length) != 0) {
        return -1;
    }

    return 0;
}

static void mp_cache_http_response_destroy(mp_cache_http_response_t *response) {
    if (response == NULL) {
        return;
    }

    free(response->response_body);
    memset(response, 0, sizeof(*response));
}

static char *mp_cache_http_strdup_printf(const char *format, ...) {
    va_list arguments;
    va_list copy;
    char *buffer = NULL;
    int length = 0;

    va_start(arguments, format);
    va_copy(copy, arguments);
    length = vsnprintf(NULL, 0, format, arguments);
    va_end(arguments);
    if (length < 0) {
        va_end(copy);
        return NULL;
    }

    buffer = malloc((size_t)length + 1u);
    if (buffer == NULL) {
        va_end(copy);
        return NULL;
    }
    (void)vsnprintf(buffer, (size_t)length + 1u, format, copy);
    va_end(copy);
    return buffer;
}

static char *mp_cache_http_escape_json(const char *json_text, size_t text_length) {
    size_t capacity = text_length * 6u + 1u;
    char *escaped = NULL;
    size_t input_index = 0u;
    size_t output_index = 0u;

    escaped = malloc(capacity);
    if (escaped == NULL) {
        return NULL;
    }

    for (input_index = 0u; input_index < text_length; input_index++) {
        unsigned char character = (unsigned char)json_text[input_index];
        switch (character) {
            case '\\':
            case '"':
                escaped[output_index++] = '\\';
                escaped[output_index++] = (char)character;
                break;
            case '\n':
                escaped[output_index++] = '\\';
                escaped[output_index++] = 'n';
                break;
            case '\r':
                escaped[output_index++] = '\\';
                escaped[output_index++] = 'r';
                break;
            case '\t':
                escaped[output_index++] = '\\';
                escaped[output_index++] = 't';
                break;
            default:
                if (character < 0x20u) {
                    (void)snprintf(escaped + output_index, capacity - output_index, "\\u%04x", character);
                    output_index += 6u;
                } else {
                    escaped[output_index++] = (char)character;
                }
                break;
        }
    }

    escaped[output_index] = '\0';
    return escaped;
}

static void mp_cache_http_make_error(
    mp_cache_http_response_t *response,
    int status_code,
    const char *status_text,
    const char *error_code,
    const char *message,
    const char *allow_header,
    const char *extra_headers) {
    if (response == NULL) {
        return;
    }

    response->status_code = status_code;
    response->status_text = status_text;
    response->allow_header_value = allow_header;
    response->extra_headers = extra_headers;
    response->response_body = mp_cache_http_strdup_printf(
        "{\"error\":\"%s\",\"message\":\"%s\"}",
        error_code == NULL ? "internal_error" : error_code,
        message == NULL ? "request failed" : message);
}

static bool mp_cache_http_path_is_health(const char *request_path) {
    return request_path != NULL &&
           (strcmp(request_path, "/health") == 0 || strcmp(request_path, "/v1/health") == 0);
}

static int mp_cache_http_hex_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    return -1;
}

static int mp_cache_http_percent_decode(const char *encoded, char *decoded, size_t decoded_capacity) {
    size_t in_index = 0u;
    size_t out_index = 0u;

    if (encoded == NULL || decoded == NULL || decoded_capacity == 0u) {
        errno = EINVAL;
        return -1;
    }

    while (encoded[in_index] != '\0') {
        if (out_index + 1u >= decoded_capacity) {
            errno = ENOSPC;
            return -1;
        }
        if (encoded[in_index] == '%') {
            int high = 0;
            int low = 0;

            if (encoded[in_index + 1u] == '\0' || encoded[in_index + 2u] == '\0') {
                errno = EINVAL;
                return -1;
            }
            high = mp_cache_http_hex_value(encoded[in_index + 1u]);
            low = mp_cache_http_hex_value(encoded[in_index + 2u]);
            if (high < 0 || low < 0) {
                errno = EINVAL;
                return -1;
            }

            decoded[out_index++] = (char)((high << 4u) | low);
            in_index += 3u;
            continue;
        }

        decoded[out_index++] = encoded[in_index++];
    }

    decoded[out_index] = '\0';
    return 0;
}

static char *mp_cache_http_find_json_field(const char *json_body, const char *field_name) {
    char pattern[128];

    if (json_body == NULL || field_name == NULL) {
        return NULL;
    }

    (void)snprintf(pattern, sizeof(pattern), "\"%s\"", field_name);
    return strstr((char *)json_body, pattern);
}

static int mp_cache_http_extract_json_string(
    const char *json_body,
    const char *field_name,
    char *out_string_value,
    size_t out_capacity) {
    char *field = NULL;
    char *value = NULL;
    size_t out_index = 0u;

    if (json_body == NULL || field_name == NULL || out_string_value == NULL || out_capacity == 0u) {
        errno = EINVAL;
        return -1;
    }

    field = mp_cache_http_find_json_field(json_body, field_name);
    if (field == NULL) {
        errno = ENOENT;
        return -1;
    }
    value = strchr(field, ':');
    if (value == NULL) {
        errno = EINVAL;
        return -1;
    }
    value++;
    while (*value != '\0' && isspace((unsigned char)*value) != 0) {
        value++;
    }
    if (*value != '"') {
        errno = EINVAL;
        return -1;
    }
    value++;

    while (*value != '\0' && *value != '"') {
        char next_character = *value++;

        if (next_character == '\\') {
            if (*value == '\0') {
                errno = EINVAL;
                return -1;
            }
            switch (*value) {
                case '"':
                case '\\':
                case '/':
                    next_character = *value;
                    break;
                case 'n':
                    next_character = '\n';
                    break;
                case 'r':
                    next_character = '\r';
                    break;
                case 't':
                    next_character = '\t';
                    break;
                default:
                    errno = EINVAL;
                    return -1;
            }
            value++;
        }

        if (out_index + 1u >= out_capacity) {
            errno = ENOSPC;
            return -1;
        }
        out_string_value[out_index++] = next_character;
    }

    if (*value != '"') {
        errno = EINVAL;
        return -1;
    }
    out_string_value[out_index] = '\0';
    return 0;
}

static int mp_cache_http_extract_json_u32(
    const char *json_body,
    const char *field_name,
    uint32_t *out_value,
    bool *out_found) {
    char *field = NULL;
    char *value = NULL;
    unsigned long parsed = 0ul;
    char *end = NULL;

    if (out_found != NULL) {
        *out_found = false;
    }
    if (json_body == NULL || field_name == NULL || out_value == NULL) {
        errno = EINVAL;
        return -1;
    }

    field = mp_cache_http_find_json_field(json_body, field_name);
    if (field == NULL) {
        errno = ENOENT;
        return -1;
    }
    value = strchr(field, ':');
    if (value == NULL) {
        errno = EINVAL;
        return -1;
    }
    value++;
    while (*value != '\0' && isspace((unsigned char)*value) != 0) {
        value++;
    }
    if (!isdigit((unsigned char)*value)) {
        errno = EINVAL;
        return -1;
    }

    parsed = strtoul(value, &end, 10);
    if (end == value || parsed > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }

    *out_value = (uint32_t)parsed;
    if (out_found != NULL) {
        *out_found = true;
    }
    return 0;
}

static void mp_cache_http_split_target(
    const char *request_target,
    char *request_path,
    size_t request_path_capacity,
    char *query_string,
    size_t query_string_capacity) {
    const char *separator = NULL;

    if (request_target == NULL || request_path == NULL || query_string == NULL) {
        return;
    }

    separator = strchr(request_target, '?');
    if (separator == NULL) {
        (void)snprintf(request_path, request_path_capacity, "%s", request_target);
        query_string[0] = '\0';
        return;
    }

    (void)snprintf(request_path, request_path_capacity, "%.*s", (int)(separator - request_target), request_target);
    (void)snprintf(query_string, query_string_capacity, "%s", separator + 1);
}

static int mp_cache_http_extract_query_u32(const char *query_string, const char *field_name, uint32_t *out_value) {
    const char *position = query_string;
    size_t field_length = 0u;

    if (query_string == NULL || field_name == NULL || out_value == NULL) {
        errno = EINVAL;
        return -1;
    }

    field_length = strlen(field_name);
    while (position != NULL && *position != '\0') {
        if (strncmp(position, field_name, field_length) == 0 && position[field_length] == '=') {
            unsigned long parsed = strtoul(position + field_length + 1u, NULL, 10);
            if (parsed > UINT32_MAX) {
                errno = EINVAL;
                return -1;
            }
            *out_value = (uint32_t)parsed;
            return 0;
        }

        position = strchr(position, '&');
        if (position != NULL) {
            position++;
        }
    }

    errno = ENOENT;
    return -1;
}

static int mp_cache_http_parse_headers(
    char *headers_text,
    mp_cache_http_request_t *request) {
    char *line = NULL;
    line = strtok(headers_text, "\r\n");
    if (line == NULL || sscanf(line, "%15s %511s", request->method, request->request_target) != 2) {
        errno = EINVAL;
        return -1;
    }

    while ((line = strtok(NULL, "\r\n")) != NULL) {
        if (strncasecmp(line, "Content-Length:", 15u) == 0) {
            request->content_length = (size_t)strtoul(line + 15u, NULL, 10);
            continue;
        }
        if (strncasecmp(line, "Authorization:", 14u) == 0) {
            char *value = line + 14u;
            while (*value != '\0' && isspace((unsigned char)*value) != 0) {
                value++;
            }
            (void)snprintf(request->authorization_header, sizeof(request->authorization_header), "%s", value);
        }
    }

    mp_cache_http_split_target(request->request_target, request->request_path, sizeof(request->request_path), request->query_string, sizeof(request->query_string));
    return 0;
}

static int mp_cache_http_parse_request_buffer(char *buffer, size_t total_bytes, mp_cache_http_request_t *request) {
    char *header_end = NULL;
    size_t header_length = 0u;
    char *headers_copy = NULL;

    if (buffer == NULL || request == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(request, 0, sizeof(*request));
    header_end = strstr(buffer, "\r\n\r\n");
    if (header_end == NULL) {
        errno = EINVAL;
        return -1;
    }

    header_length = (size_t)(header_end - buffer) + 4u;
    headers_copy = malloc(header_length + 1u);
    if (headers_copy == NULL) {
        return -1;
    }
    memcpy(headers_copy, buffer, header_length);
    headers_copy[header_length] = '\0';
    if (mp_cache_http_parse_headers(headers_copy, request) != 0) {
        free(headers_copy);
        return -1;
    }
    free(headers_copy);

    if (total_bytes < header_length + request->content_length) {
        errno = EINVAL;
        return -1;
    }

    request->request_body = buffer + header_length;
    request->request_body_length = request->content_length;
    request->request_body[request->request_body_length] = '\0';
    return 0;
}

static int mp_cache_http_read_request(int client_fd, char *buffer, size_t buffer_capacity, mp_cache_http_request_t *request) {
    size_t total_bytes = 0u;
    char *header_end = NULL;
    size_t header_length = 0u;
    char *headers_copy = NULL;

    if (buffer == NULL || request == NULL || buffer_capacity == 0u) {
        errno = EINVAL;
        return -1;
    }

    while (total_bytes + 1u < buffer_capacity) {
        ssize_t bytes_read = recv(client_fd, buffer + total_bytes, buffer_capacity - total_bytes - 1u, 0);
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (bytes_read == 0) {
            break;
        }

        total_bytes += (size_t)bytes_read;
        buffer[total_bytes] = '\0';
        header_end = strstr(buffer, "\r\n\r\n");
        if (header_end != NULL) {
            header_length = (size_t)(header_end - buffer) + 4u;
            break;
        }
    }

    if (header_end == NULL) {
        errno = EINVAL;
        return -1;
    }
    header_length = (size_t)(header_end - buffer) + 4u;

    memset(request, 0, sizeof(*request));
    headers_copy = malloc(header_length + 1u);
    if (headers_copy == NULL) {
        return -1;
    }
    memcpy(headers_copy, buffer, header_length);
    headers_copy[header_length] = '\0';
    if (mp_cache_http_parse_headers(headers_copy, request) != 0) {
        free(headers_copy);
        return -1;
    }
    free(headers_copy);

    if (request->content_length > buffer_capacity - header_length - 1u) {
        errno = EOVERFLOW;
        return -1;
    }

    while (total_bytes < header_length + request->content_length) {
        ssize_t bytes_read = recv(client_fd, buffer + total_bytes, buffer_capacity - total_bytes - 1u, 0);
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (bytes_read == 0) {
            break;
        }
        total_bytes += (size_t)bytes_read;
        buffer[total_bytes] = '\0';
    }

    if (total_bytes < header_length + request->content_length) {
        errno = EINVAL;
        return -1;
    }

    buffer[total_bytes] = '\0';
    return mp_cache_http_parse_request_buffer(buffer, total_bytes, request);
}

static const char *mp_cache_http_extract_bearer_token(const char *authorization_header) {
    if (authorization_header == NULL) {
        return NULL;
    }
    if (strncasecmp(authorization_header, "Bearer ", 7u) != 0) {
        return NULL;
    }
    return authorization_header + 7u;
}

static void mp_cache_http_maybe_sweep(mp_cache_http_server_t *server, int64_t now_utc_seconds) {
    if (server == NULL) {
        return;
    }

    if (server->last_sweep_at_utc == 0 || now_utc_seconds - server->last_sweep_at_utc >= server->config->sweep_interval_seconds) {
        (void)mp_cache_store_purge_expired(server->store, now_utc_seconds);
        server->last_sweep_at_utc = now_utc_seconds;
    }
}

static bool mp_cache_http_rate_limit_allow(
    mp_cache_http_server_t *server,
    const char *subject,
    int64_t now_utc_seconds) {
    size_t index = 0u;
    mp_cache_rate_limit_entry_t *entry = NULL;
    mp_cache_rate_limit_entry_t *next_entries = NULL;

    if (server == NULL || subject == NULL) {
        return false;
    }

    for (index = 0u; index < server->rate_limiter.count; index++) {
        if (strcmp(server->rate_limiter.entries[index].subject, subject) == 0) {
            entry = &server->rate_limiter.entries[index];
            break;
        }
    }

    if (entry == NULL) {
        if (server->rate_limiter.count == server->rate_limiter.capacity) {
            size_t next_capacity = server->rate_limiter.capacity == 0u ? 8u : server->rate_limiter.capacity * 2u;
            next_entries = realloc(server->rate_limiter.entries, next_capacity * sizeof(*server->rate_limiter.entries));
            if (next_entries == NULL) {
                return false;
            }
            server->rate_limiter.entries = next_entries;
            server->rate_limiter.capacity = next_capacity;
        }

        entry = &server->rate_limiter.entries[server->rate_limiter.count++];
        memset(entry, 0, sizeof(*entry));
        (void)snprintf(entry->subject, sizeof(entry->subject), "%s", subject);
        entry->window_started_at = now_utc_seconds;
    }

    if (now_utc_seconds - entry->window_started_at >= server->config->rate_limit_window_seconds) {
        entry->window_started_at = now_utc_seconds;
        entry->request_count = 0u;
    }
    if (entry->request_count >= server->config->rate_limit_requests) {
        return false;
    }

    entry->request_count++;
    return true;
}

static int mp_cache_http_authenticate(
    mp_cache_http_server_t *server,
    const mp_cache_http_request_t *request,
    mp_cache_role_t required_role,
    int64_t now_utc_seconds,
    mp_cache_principal_t *out_principal,
    mp_cache_http_response_t *out_response) {
    const char *bearer_token = NULL;
    mp_cache_security_status_t auth_status;

    if (server == NULL || request == NULL || out_principal == NULL || out_response == NULL) {
        errno = EINVAL;
        return -1;
    }

    bearer_token = mp_cache_http_extract_bearer_token(request->authorization_header);
    if (bearer_token == NULL || *bearer_token == '\0') {
        server->metrics.unauthorized_requests++;
        mp_cache_http_make_error(
            out_response,
            401,
            "Unauthorized",
            "unauthorized",
            "missing bearer token",
            NULL,
            "WWW-Authenticate: Bearer realm=\"mp-cache\"\r\n");
        return 1;
    }

    auth_status = mp_cache_security_authenticate(server->security, bearer_token, out_principal);
    if (auth_status != MP_CACHE_SECURITY_STATUS_OK) {
        server->metrics.unauthorized_requests++;
        mp_cache_http_make_error(
            out_response,
            401,
            "Unauthorized",
            "unauthorized",
            "invalid bearer token",
            NULL,
            "WWW-Authenticate: Bearer realm=\"mp-cache\"\r\n");
        return 1;
    }

    if (!mp_cache_role_allows(out_principal->role, required_role)) {
        server->metrics.forbidden_requests++;
        mp_cache_http_make_error(out_response, 403, "Forbidden", "forbidden", "insufficient role", NULL, NULL);
        return 1;
    }

    if (!mp_cache_http_rate_limit_allow(server, out_principal->client_id, now_utc_seconds)) {
        server->metrics.rate_limited_requests++;
        mp_cache_http_make_error(
            out_response,
            429,
            "Too Many Requests",
            "limit_exceeded",
            "rate limit exceeded",
            NULL,
            "Retry-After: 60\r\n");
        return 1;
    }

    return 0;
}

static int64_t mp_cache_http_now(void) {
    return (int64_t)time(NULL);
}

static char *mp_cache_http_build_health_body(mp_cache_http_server_t *server, int64_t now_utc_seconds) {
    mp_cache_store_stats_t stats;

    mp_cache_store_get_stats(server->store, &stats);
    return mp_cache_http_strdup_printf(
        "{\"status\":\"ok\",\"service\":\"%s\",\"environment\":\"%s\",\"socket_path\":\"%s\","
        "\"uptime_seconds\":%lld,\"default_ttl_seconds\":%u,\"memory_limit_bytes\":%llu,"
        "\"entry_count\":%zu,\"bytes_used\":%zu}",
        server->config->service_name,
        server->config->environment_name,
        server->config->socket_path,
        (long long)(now_utc_seconds - server->started_at_utc),
        server->config->default_ttl_seconds,
        (unsigned long long)stats.memory_limit_bytes,
        stats.entry_count,
        stats.bytes_used);
}

static char *mp_cache_http_build_stats_body(mp_cache_http_server_t *server, int64_t now_utc_seconds) {
    mp_cache_store_stats_t stats;

    mp_cache_store_get_stats(server->store, &stats);
    return mp_cache_http_strdup_printf(
        "{\"uptime_seconds\":%lld,\"entry_count\":%zu,\"bytes_used\":%zu,\"memory_limit_bytes\":%llu,"
        "\"total_requests\":%llu,\"unauthorized_requests\":%llu,\"forbidden_requests\":%llu,"
        "\"rate_limited_requests\":%llu,\"cache_hits\":%llu,\"cache_misses\":%llu,"
        "\"cache_sets\":%llu,\"cache_deletes\":%llu,\"client_registrations\":%llu,"
        "\"token_rotations\":%llu,\"exports\":%llu,\"imports\":%llu,\"log_reads\":%llu}",
        (long long)(now_utc_seconds - server->started_at_utc),
        stats.entry_count,
        stats.bytes_used,
        (unsigned long long)stats.memory_limit_bytes,
        (unsigned long long)server->metrics.total_requests,
        (unsigned long long)server->metrics.unauthorized_requests,
        (unsigned long long)server->metrics.forbidden_requests,
        (unsigned long long)server->metrics.rate_limited_requests,
        (unsigned long long)server->metrics.cache_hits,
        (unsigned long long)server->metrics.cache_misses,
        (unsigned long long)server->metrics.cache_sets,
        (unsigned long long)server->metrics.cache_deletes,
        (unsigned long long)server->metrics.client_registrations,
        (unsigned long long)server->metrics.token_rotations,
        (unsigned long long)server->metrics.exports,
        (unsigned long long)server->metrics.imports,
        (unsigned long long)server->metrics.log_reads);
}

static char *mp_cache_http_build_memory_body(mp_cache_http_server_t *server) {
    mp_cache_store_stats_t stats;

    mp_cache_store_get_stats(server->store, &stats);
    return mp_cache_http_strdup_printf(
        "{\"entry_count\":%zu,\"bytes_used\":%zu,\"memory_limit_bytes\":%llu}",
        stats.entry_count,
        stats.bytes_used,
        (unsigned long long)stats.memory_limit_bytes);
}

static int mp_cache_http_resilient_checkpoint(mp_cache_http_server_t *server, int64_t now_utc_seconds) {
    return mp_cache_storage_checkpoint(server->storage, server->store, server->security, now_utc_seconds);
}

static int mp_cache_http_handle_cache_get(
    mp_cache_http_server_t *server,
    const char *key,
    int64_t now_utc_seconds,
    mp_cache_http_response_t *response) {
    uint8_t *value = NULL;
    size_t value_length = 0u;
    int64_t expires_at_utc_seconds = 0;
    char *value_base64 = NULL;
    size_t value_base64_length = 0u;
    mp_cache_store_status_t status;

    status = mp_cache_store_get_copy(
        server->store,
        key,
        strlen(key),
        now_utc_seconds,
        &value,
        &value_length,
        &expires_at_utc_seconds);
    if (status == MP_CACHE_STORE_STATUS_NOT_FOUND || status == MP_CACHE_STORE_STATUS_EXPIRED) {
        server->metrics.cache_misses++;
        mp_cache_http_make_error(response, 404, "Not Found", "not_found", "cache key not found", NULL, NULL);
        free(value);
        return 0;
    }
    if (status != MP_CACHE_STORE_STATUS_OK) {
        mp_cache_http_make_error(response, 500, "Internal Server Error", "internal_error", "cache read failed", NULL, NULL);
        free(value);
        return 0;
    }

    value_base64 = malloc(((value_length + 2u) / 3u) * 4u + 1u);
    if (value_base64 == NULL || mp_cache_base64_encode(value, value_length, value_base64, ((value_length + 2u) / 3u) * 4u + 1u, &value_base64_length) !=
                                  0) {
        free(value);
        free(value_base64);
        mp_cache_http_make_error(response, 500, "Internal Server Error", "internal_error", "value encoding failed", NULL, NULL);
        return 0;
    }

    server->metrics.cache_hits++;
    response->status_code = 200;
    response->status_text = "OK";
    response->response_body = mp_cache_http_strdup_printf(
        "{\"key\":\"%s\",\"value_base64\":\"%s\",\"value_bytes\":%zu,\"expires_at_utc_seconds\":%lld}",
        key,
        value_base64,
        value_length,
        (long long)expires_at_utc_seconds);

    free(value);
    free(value_base64);
    return 0;
}

static int mp_cache_http_handle_cache_put(
    mp_cache_http_server_t *server,
    const char *key,
    const mp_cache_http_request_t *request,
    int64_t now_utc_seconds,
    mp_cache_http_response_t *response) {
    char value_base64[MP_CACHE_HTTP_BODY_CAPACITY];
    uint8_t *value_bytes = NULL;
    size_t value_length = 0u;
    uint32_t ttl_seconds = server->config->default_ttl_seconds;
    bool ttl_found = false;
    size_t decoded_capacity = 0u;
    mp_cache_store_status_t status;

    if (request->request_body == NULL || request->request_body_length == 0u ||
        mp_cache_http_extract_json_string(request->request_body, "value_base64", value_base64, sizeof(value_base64)) != 0) {
        mp_cache_http_make_error(response, 400, "Bad Request", "invalid_argument", "value_base64 is required", NULL, NULL);
        return 0;
    }
    if (mp_cache_http_extract_json_u32(request->request_body, "ttl_seconds", &ttl_seconds, &ttl_found) != 0 && errno != ENOENT) {
        mp_cache_http_make_error(response, 400, "Bad Request", "invalid_argument", "ttl_seconds must be an integer", NULL, NULL);
        return 0;
    }
    if (ttl_seconds < server->config->min_ttl_seconds || ttl_seconds > server->config->max_ttl_seconds) {
        mp_cache_http_make_error(response, 400, "Bad Request", "invalid_argument", "ttl_seconds is outside the configured bounds", NULL, NULL);
        return 0;
    }

    decoded_capacity = (strlen(value_base64) / 4u) * 3u + 3u;
    value_bytes = malloc(decoded_capacity);
    if (value_bytes == NULL ||
        mp_cache_base64_decode(value_base64, value_bytes, decoded_capacity, &value_length) != 0) {
        free(value_bytes);
        mp_cache_http_make_error(response, 400, "Bad Request", "invalid_argument", "value_base64 is invalid", NULL, NULL);
        return 0;
    }

    status = mp_cache_store_set(server->store, key, strlen(key), value_bytes, value_length, ttl_seconds, now_utc_seconds);
    if (status == MP_CACHE_STORE_STATUS_LIMIT_EXCEEDED) {
        free(value_bytes);
        mp_cache_http_make_error(response, 413, "Payload Too Large", "limit_exceeded", "cache size limit exceeded", NULL, NULL);
        return 0;
    }
    if (status != MP_CACHE_STORE_STATUS_OK) {
        free(value_bytes);
        mp_cache_http_make_error(response, 500, "Internal Server Error", "internal_error", "cache write failed", NULL, NULL);
        return 0;
    }

    if (mp_cache_storage_append_set(server->storage, key, strlen(key), value_bytes, value_length, now_utc_seconds + ttl_seconds) != 0 &&
        mp_cache_http_resilient_checkpoint(server, now_utc_seconds) != 0) {
        free(value_bytes);
        mp_cache_http_make_error(
            response,
            500,
            "Internal Server Error",
            "internal_error",
            "cache was updated but persistence failed",
            NULL,
            NULL);
        return 0;
    }

    server->metrics.cache_sets++;
    response->status_code = 200;
    response->status_text = "OK";
    response->response_body = mp_cache_http_strdup_printf(
        "{\"key\":\"%s\",\"ttl_seconds\":%u,\"expires_at_utc_seconds\":%lld}",
        key,
        ttl_seconds,
        (long long)(now_utc_seconds + ttl_seconds));

    free(value_bytes);
    return 0;
}

static int mp_cache_http_handle_cache_delete(
    mp_cache_http_server_t *server,
    const char *key,
    int64_t now_utc_seconds,
    mp_cache_http_response_t *response) {
    mp_cache_store_status_t status = mp_cache_store_delete(server->store, key, strlen(key));

    if (status == MP_CACHE_STORE_STATUS_NOT_FOUND) {
        mp_cache_http_make_error(response, 404, "Not Found", "not_found", "cache key not found", NULL, NULL);
        return 0;
    }
    if (status != MP_CACHE_STORE_STATUS_OK) {
        mp_cache_http_make_error(response, 500, "Internal Server Error", "internal_error", "cache delete failed", NULL, NULL);
        return 0;
    }

    if (mp_cache_storage_append_delete(server->storage, key, strlen(key)) != 0 &&
        mp_cache_http_resilient_checkpoint(server, now_utc_seconds) != 0) {
        mp_cache_http_make_error(
            response,
            500,
            "Internal Server Error",
            "internal_error",
            "cache was deleted but persistence failed",
            NULL,
            NULL);
        return 0;
    }

    server->metrics.cache_deletes++;
    response->status_code = 200;
    response->status_text = "OK";
    response->response_body = mp_cache_http_strdup_printf("{\"key\":\"%s\",\"deleted\":true}", key);
    return 0;
}

static int mp_cache_http_handle_logs(
    mp_cache_http_server_t *server,
    const mp_cache_http_request_t *request,
    mp_cache_http_response_t *response) {
    uint32_t tail_lines = 50u;
    char log_file_name[MP_CACHE_PATH_CAP];
    char *log_text = NULL;
    char *escaped = NULL;

    if (mp_cache_http_extract_query_u32(request->query_string, "tail", &tail_lines) != 0 && errno != ENOENT) {
        mp_cache_http_make_error(response, 400, "Bad Request", "invalid_argument", "tail must be a positive integer", NULL, NULL);
        return 0;
    }

    if (mp_cache_log_read_latest_tail(
            server->config->log_directory,
            server->config->max_log_lines,
            tail_lines,
            log_file_name,
            sizeof(log_file_name),
            &log_text) != 0) {
        if (errno == ENOENT) {
            response->status_code = 200;
            response->status_text = "OK";
            response->response_body = mp_cache_http_strdup_printf("{\"file\":null,\"tail_lines\":0,\"text\":\"\"}");
            return 0;
        }
        mp_cache_http_make_error(response, 500, "Internal Server Error", "internal_error", "failed to read logs", NULL, NULL);
        return 0;
    }

    escaped = mp_cache_http_escape_json(log_text, strlen(log_text));
    if (escaped == NULL) {
        free(log_text);
        mp_cache_http_make_error(response, 500, "Internal Server Error", "internal_error", "failed to encode logs", NULL, NULL);
        return 0;
    }

    server->metrics.log_reads++;
    response->status_code = 200;
    response->status_text = "OK";
    response->response_body = mp_cache_http_strdup_printf(
        "{\"file\":\"%s\",\"tail_lines\":%u,\"text\":\"%s\"}",
        log_file_name,
        tail_lines,
        escaped);

    free(log_text);
    free(escaped);
    return 0;
}

static int mp_cache_http_handle_register_client(
    mp_cache_http_server_t *server,
    const mp_cache_http_request_t *request,
    int64_t now_utc_seconds,
    mp_cache_http_response_t *response) {
    char client_id[MP_CACHE_CLIENT_ID_CAP];
    char role_text[16];
    char issued_client_token[MP_CACHE_TOKEN_TEXT_CAP];
    const mp_cache_client_record_t *record = NULL;
    mp_cache_role_t role = MP_CACHE_ROLE_NONE;
    mp_cache_security_status_t status;

    if (mp_cache_http_extract_json_string(request->request_body, "client_id", client_id, sizeof(client_id)) != 0 ||
        mp_cache_http_extract_json_string(request->request_body, "role", role_text, sizeof(role_text)) != 0) {
        mp_cache_http_make_error(response, 400, "Bad Request", "invalid_argument", "client_id and role are required", NULL, NULL);
        return 0;
    }

    role = mp_cache_role_from_string(role_text);
    if (role == MP_CACHE_ROLE_NONE) {
        mp_cache_http_make_error(response, 400, "Bad Request", "invalid_argument", "role must be client, operator, or admin", NULL, NULL);
        return 0;
    }

    status = mp_cache_security_register_client(
        server->security,
        client_id,
        role,
        issued_client_token,
        sizeof(issued_client_token));
    if (status == MP_CACHE_SECURITY_STATUS_CONFLICT) {
        mp_cache_http_make_error(response, 409, "Conflict", "conflict", "client_id already exists", NULL, NULL);
        return 0;
    }
    if (status != MP_CACHE_SECURITY_STATUS_OK) {
        mp_cache_http_make_error(response, 400, "Bad Request", "invalid_argument", "failed to register client", NULL, NULL);
        return 0;
    }

    record = mp_cache_security_find_client(server->security, client_id);
    if (record == NULL ||
        (mp_cache_storage_append_client(server->storage, record) != 0 && mp_cache_http_resilient_checkpoint(server, now_utc_seconds) != 0)) {
        mp_cache_http_make_error(
            response,
            500,
            "Internal Server Error",
            "internal_error",
            "client was created but persistence failed",
            NULL,
            NULL);
        return 0;
    }

    server->metrics.client_registrations++;
    response->status_code = 200;
    response->status_text = "OK";
    response->response_body = mp_cache_http_strdup_printf(
        "{\"client_id\":\"%s\",\"role\":\"%s\",\"token\":\"%s\"}",
        client_id,
        mp_cache_role_name(role),
        issued_client_token);
    return 0;
}

static int mp_cache_http_handle_rotate_client(
    mp_cache_http_server_t *server,
    const char *client_id,
    int64_t now_utc_seconds,
    mp_cache_http_response_t *response) {
    char rotated_client_token[MP_CACHE_TOKEN_TEXT_CAP];
    const mp_cache_client_record_t *record = NULL;
    mp_cache_security_status_t status;

    status = mp_cache_security_rotate_client_token(
        server->security,
        client_id,
        rotated_client_token,
        sizeof(rotated_client_token));
    if (status == MP_CACHE_SECURITY_STATUS_NOT_FOUND) {
        mp_cache_http_make_error(response, 404, "Not Found", "not_found", "client_id not found", NULL, NULL);
        return 0;
    }
    if (status != MP_CACHE_SECURITY_STATUS_OK) {
        mp_cache_http_make_error(response, 400, "Bad Request", "invalid_argument", "failed to rotate token", NULL, NULL);
        return 0;
    }

    record = mp_cache_security_find_client(server->security, client_id);
    if (record == NULL ||
        (mp_cache_storage_append_client(server->storage, record) != 0 && mp_cache_http_resilient_checkpoint(server, now_utc_seconds) != 0)) {
        mp_cache_http_make_error(
            response,
            500,
            "Internal Server Error",
            "internal_error",
            "token was rotated but persistence failed",
            NULL,
            NULL);
        return 0;
    }

    server->metrics.token_rotations++;
    response->status_code = 200;
    response->status_text = "OK";
    response->response_body = mp_cache_http_strdup_printf(
        "{\"client_id\":\"%s\",\"token\":\"%s\"}",
        client_id,
        rotated_client_token);
    return 0;
}

static int mp_cache_http_handle_export(
    mp_cache_http_server_t *server,
    int64_t now_utc_seconds,
    mp_cache_http_response_t *response) {
    mp_cache_export_result_t export_result;

    if (mp_cache_storage_export_state(server->storage, server->store, server->security, now_utc_seconds, &export_result) != 0) {
        mp_cache_http_make_error(response, 500, "Internal Server Error", "internal_error", "state export failed", NULL, NULL);
        return 0;
    }

    server->metrics.exports++;
    response->status_code = 200;
    response->status_text = "OK";
    response->response_body = mp_cache_http_strdup_printf(
        "{\"path\":\"%s\",\"digest_sha256\":\"%s\",\"entry_count\":%zu,\"client_count\":%zu}",
        export_result.export_path,
        export_result.digest_hex,
        export_result.entry_count,
        export_result.client_count);
    return 0;
}

static int mp_cache_http_handle_import(
    mp_cache_http_server_t *server,
    const mp_cache_http_request_t *request,
    int64_t now_utc_seconds,
    mp_cache_http_response_t *response) {
    char import_path[MP_CACHE_PATH_CAP];

    if (mp_cache_http_extract_json_string(request->request_body, "path", import_path, sizeof(import_path)) != 0) {
        mp_cache_http_make_error(response, 400, "Bad Request", "invalid_argument", "path is required", NULL, NULL);
        return 0;
    }

    if (mp_cache_storage_import_state(server->storage, import_path, server->store, server->security, now_utc_seconds) != 0) {
        mp_cache_http_make_error(response, 400, "Bad Request", "invalid_argument", "state import failed integrity or bounds checks", NULL, NULL);
        return 0;
    }

    server->metrics.imports++;
    response->status_code = 200;
    response->status_text = "OK";
    response->response_body = mp_cache_http_strdup_printf("{\"path\":\"%s\",\"imported\":true}", import_path);
    return 0;
}

static int mp_cache_http_handle_purge_all(
    mp_cache_http_server_t *server,
    int64_t now_utc_seconds,
    mp_cache_http_response_t *response) {
    mp_cache_store_clear(server->store);
    if (mp_cache_storage_append_purge_all(server->storage) != 0 &&
        mp_cache_http_resilient_checkpoint(server, now_utc_seconds) != 0) {
        mp_cache_http_make_error(
            response,
            500,
            "Internal Server Error",
            "internal_error",
            "cache was purged but persistence failed",
            NULL,
            NULL);
        return 0;
    }

    response->status_code = 200;
    response->status_text = "OK";
    response->response_body = mp_cache_http_strdup_printf("{\"purged\":true}");
    return 0;
}

static int mp_cache_http_handle_root(
    const mp_cache_http_request_t *request,
    mp_cache_http_response_t *response) {
    if (strcmp(request->method, "GET") != 0) {
        mp_cache_http_make_error(response, 405, "Method Not Allowed", "invalid_argument", "method not allowed", "GET", NULL);
        return 0;
    }

    response->status_code = 200;
    response->status_text = "OK";
    response->response_body = mp_cache_http_strdup_printf(
        "{\"service\":\"mp-cache\",\"message\":\"use /health or /v1/* endpoints\"}");
    return 0;
}

static int mp_cache_http_handle_uptime(
    mp_cache_http_server_t *server,
    int64_t now_utc_seconds,
    mp_cache_http_response_t *response) {
    response->status_code = 200;
    response->status_text = "OK";
    response->response_body = mp_cache_http_strdup_printf(
        "{\"uptime_seconds\":%lld,\"started_at_utc_seconds\":%lld}",
        (long long)(now_utc_seconds - server->started_at_utc),
        (long long)server->started_at_utc);
    return 0;
}

static int mp_cache_http_route_request(
    mp_cache_http_server_t *server,
    const mp_cache_http_request_t *request,
    mp_cache_http_response_t *response) {
    int64_t now_utc_seconds = mp_cache_http_now();
    mp_cache_principal_t principal;
    char key[MP_CACHE_NAME_CAP * 4u];
    const char *cache_prefix = "/v1/cache/";
    const char *rotate_suffix = "/rotate-token";

    memset(&principal, 0, sizeof(principal));
    memset(response, 0, sizeof(*response));
    server->metrics.total_requests++;
    mp_cache_http_maybe_sweep(server, now_utc_seconds);

    if (mp_cache_http_path_is_health(request->request_path)) {
        if (strcmp(request->method, "GET") != 0) {
            mp_cache_http_make_error(response, 405, "Method Not Allowed", "invalid_argument", "method not allowed", "GET", NULL);
            return 0;
        }
        response->status_code = 200;
        response->status_text = "OK";
        response->response_body = mp_cache_http_build_health_body(server, now_utc_seconds);
        return 0;
    }

    if (strcmp(request->request_path, "/") == 0) {
        return mp_cache_http_handle_root(request, response);
    }

    if (strncmp(request->request_path, cache_prefix, strlen(cache_prefix)) == 0) {
        if (mp_cache_http_percent_decode(request->request_path + strlen(cache_prefix), key, sizeof(key)) != 0 || key[0] == '\0') {
            mp_cache_http_make_error(response, 400, "Bad Request", "invalid_argument", "cache key is invalid", NULL, NULL);
            return 0;
        }
        if (mp_cache_http_authenticate(server, request, MP_CACHE_ROLE_CLIENT, now_utc_seconds, &principal, response) != 0) {
            return 0;
        }
        if (strcmp(request->method, "GET") == 0) {
            return mp_cache_http_handle_cache_get(server, key, now_utc_seconds, response);
        }
        if (strcmp(request->method, "PUT") == 0) {
            return mp_cache_http_handle_cache_put(server, key, request, now_utc_seconds, response);
        }
        if (strcmp(request->method, "DELETE") == 0) {
            return mp_cache_http_handle_cache_delete(server, key, now_utc_seconds, response);
        }

        mp_cache_http_make_error(
            response,
            405,
            "Method Not Allowed",
            "invalid_argument",
            "method not allowed",
            "GET, PUT, DELETE",
            NULL);
        return 0;
    }

    if (strcmp(request->request_path, "/v1/stats") == 0) {
        if (strcmp(request->method, "GET") != 0) {
            mp_cache_http_make_error(response, 405, "Method Not Allowed", "invalid_argument", "method not allowed", "GET", NULL);
            return 0;
        }
        if (mp_cache_http_authenticate(server, request, MP_CACHE_ROLE_OPERATOR, now_utc_seconds, &principal, response) != 0) {
            return 0;
        }
        response->status_code = 200;
        response->status_text = "OK";
        response->response_body = mp_cache_http_build_stats_body(server, now_utc_seconds);
        return 0;
    }

    if (strcmp(request->request_path, "/v1/metrics/memory") == 0) {
        if (strcmp(request->method, "GET") != 0) {
            mp_cache_http_make_error(response, 405, "Method Not Allowed", "invalid_argument", "method not allowed", "GET", NULL);
            return 0;
        }
        if (mp_cache_http_authenticate(server, request, MP_CACHE_ROLE_OPERATOR, now_utc_seconds, &principal, response) != 0) {
            return 0;
        }
        response->status_code = 200;
        response->status_text = "OK";
        response->response_body = mp_cache_http_build_memory_body(server);
        return 0;
    }

    if (strcmp(request->request_path, "/v1/uptime") == 0) {
        if (strcmp(request->method, "GET") != 0) {
            mp_cache_http_make_error(response, 405, "Method Not Allowed", "invalid_argument", "method not allowed", "GET", NULL);
            return 0;
        }
        if (mp_cache_http_authenticate(server, request, MP_CACHE_ROLE_OPERATOR, now_utc_seconds, &principal, response) != 0) {
            return 0;
        }
        return mp_cache_http_handle_uptime(server, now_utc_seconds, response);
    }

    if (strcmp(request->request_path, "/v1/logs") == 0) {
        if (strcmp(request->method, "GET") != 0) {
            mp_cache_http_make_error(response, 405, "Method Not Allowed", "invalid_argument", "method not allowed", "GET", NULL);
            return 0;
        }
        if (mp_cache_http_authenticate(server, request, MP_CACHE_ROLE_OPERATOR, now_utc_seconds, &principal, response) != 0) {
            return 0;
        }
        return mp_cache_http_handle_logs(server, request, response);
    }

    if (strcmp(request->request_path, "/v1/clients") == 0) {
        if (strcmp(request->method, "POST") != 0) {
            mp_cache_http_make_error(response, 405, "Method Not Allowed", "invalid_argument", "method not allowed", "POST", NULL);
            return 0;
        }
        if (mp_cache_http_authenticate(server, request, MP_CACHE_ROLE_ADMIN, now_utc_seconds, &principal, response) != 0) {
            return 0;
        }
        return mp_cache_http_handle_register_client(server, request, now_utc_seconds, response);
    }

    if (strncmp(request->request_path, "/v1/clients/", 12u) == 0 && strlen(request->request_path) > 12u) {
        const char *suffix = strstr(request->request_path + 12u, rotate_suffix);
        size_t id_length = 0u;
        char client_id[MP_CACHE_CLIENT_ID_CAP];

        if (suffix != NULL && strcmp(suffix, rotate_suffix) == 0) {
            if (strcmp(request->method, "POST") != 0) {
                mp_cache_http_make_error(response, 405, "Method Not Allowed", "invalid_argument", "method not allowed", "POST", NULL);
                return 0;
            }
            if (mp_cache_http_authenticate(server, request, MP_CACHE_ROLE_ADMIN, now_utc_seconds, &principal, response) != 0) {
                return 0;
            }

            id_length = (size_t)(suffix - (request->request_path + 12u));
            if (id_length == 0u || id_length >= sizeof(client_id)) {
                mp_cache_http_make_error(response, 400, "Bad Request", "invalid_argument", "client_id is invalid", NULL, NULL);
                return 0;
            }
            memcpy(client_id, request->request_path + 12u, id_length);
            client_id[id_length] = '\0';
            return mp_cache_http_handle_rotate_client(server, client_id, now_utc_seconds, response);
        }
    }

    if (strcmp(request->request_path, "/v1/export") == 0) {
        if (strcmp(request->method, "POST") != 0) {
            mp_cache_http_make_error(response, 405, "Method Not Allowed", "invalid_argument", "method not allowed", "POST", NULL);
            return 0;
        }
        if (mp_cache_http_authenticate(server, request, MP_CACHE_ROLE_ADMIN, now_utc_seconds, &principal, response) != 0) {
            return 0;
        }
        return mp_cache_http_handle_export(server, now_utc_seconds, response);
    }

    if (strcmp(request->request_path, "/v1/import") == 0) {
        if (strcmp(request->method, "POST") != 0) {
            mp_cache_http_make_error(response, 405, "Method Not Allowed", "invalid_argument", "method not allowed", "POST", NULL);
            return 0;
        }
        if (mp_cache_http_authenticate(server, request, MP_CACHE_ROLE_ADMIN, now_utc_seconds, &principal, response) != 0) {
            return 0;
        }
        return mp_cache_http_handle_import(server, request, now_utc_seconds, response);
    }

    if (strcmp(request->request_path, "/v1/purge/all") == 0) {
        if (strcmp(request->method, "POST") != 0) {
            mp_cache_http_make_error(response, 405, "Method Not Allowed", "invalid_argument", "method not allowed", "POST", NULL);
            return 0;
        }
        if (mp_cache_http_authenticate(server, request, MP_CACHE_ROLE_ADMIN, now_utc_seconds, &principal, response) != 0) {
            return 0;
        }
        return mp_cache_http_handle_purge_all(server, now_utc_seconds, response);
    }

    mp_cache_http_make_error(response, 404, "Not Found", "not_found", "route not found", NULL, NULL);
    return 0;
}

static int mp_cache_http_handle_client(int client_fd, mp_cache_http_server_t *server) {
    char request_buffer[MP_CACHE_HTTP_REQUEST_CAPACITY];
    mp_cache_http_request_t request;
    mp_cache_http_response_t response;

    if (mp_cache_http_read_request(client_fd, request_buffer, sizeof(request_buffer), &request) != 0) {
        mp_cache_http_make_error(&response, 400, "Bad Request", "invalid_argument", "malformed request", NULL, NULL);
        (void)mp_cache_http_write_response(
            client_fd,
            response.status_code,
            response.status_text,
            response.response_body,
            response.allow_header_value,
            response.extra_headers);
        mp_cache_http_response_destroy(&response);
        return 0;
    }

    if (mp_cache_http_route_request(server, &request, &response) != 0) {
        mp_cache_http_make_error(&response, 500, "Internal Server Error", "internal_error", "request handling failed", NULL, NULL);
    }

    (void)mp_cache_http_write_response(
        client_fd,
        response.status_code,
        response.status_text,
        response.response_body,
        response.allow_header_value,
        response.extra_headers);
    mp_cache_http_response_destroy(&response);
    return 0;
}

int mp_cache_http_server_start(
    mp_cache_http_server_t *server,
    const mp_cache_config_t *config,
    mp_cache_log_t *log,
    mp_cache_store_t *store,
    mp_cache_security_t *security,
    mp_cache_storage_t *storage,
    time_t started_at_utc) {
    struct sockaddr_un address;
    socklen_t address_length = 0;

    if (server == NULL || config == NULL || log == NULL || store == NULL || security == NULL || storage == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(server, 0, sizeof(*server));
    server->server_fd = -1;
    server->config = config;
    server->log = log;
    server->store = store;
    server->security = security;
    server->storage = storage;
    server->started_at_utc = started_at_utc;
    server->last_sweep_at_utc = started_at_utc;

    if (mp_cache_fs_ensure_parent_directory(config->socket_path) != 0) {
        return -1;
    }
    if (mp_cache_fs_remove_path_if_exists(config->socket_path) != 0) {
        return -1;
    }

    server->server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server->server_fd < 0) {
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (strlen(config->socket_path) >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(address.sun_path, config->socket_path, strlen(config->socket_path) + 1u);
    address_length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(address.sun_path) + 1u);

    if (bind(server->server_fd, (const struct sockaddr *)&address, address_length) != 0) {
        return -1;
    }
    if (listen(server->server_fd, 64) != 0) {
        return -1;
    }

    return 0;
}

int mp_cache_http_server_serve(mp_cache_http_server_t *server, volatile sig_atomic_t *stop_requested) {
    struct pollfd poll_descriptor;

    if (server == NULL || server->server_fd < 0 || stop_requested == NULL) {
        errno = EINVAL;
        return -1;
    }

    poll_descriptor.fd = server->server_fd;
    poll_descriptor.events = POLLIN;
    poll_descriptor.revents = 0;

    while (*stop_requested == 0) {
        int poll_status = poll(&poll_descriptor, 1u, 500);
        if (poll_status < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (poll_status == 0) {
            continue;
        }
        if ((poll_descriptor.revents & POLLIN) != 0) {
            int client_fd = accept(server->server_fd, NULL, NULL);
            if (client_fd < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return -1;
            }

            (void)mp_cache_http_handle_client(client_fd, server);
            (void)close(client_fd);
        }
    }

    return 0;
}

void mp_cache_http_server_stop(mp_cache_http_server_t *server) {
    if (server == NULL) {
        return;
    }

    if (server->server_fd >= 0) {
        (void)close(server->server_fd);
        server->server_fd = -1;
    }
    free(server->rate_limiter.entries);
    server->rate_limiter.entries = NULL;
    server->rate_limiter.count = 0u;
    server->rate_limiter.capacity = 0u;

    if (server->config != NULL && server->config->socket_path[0] != '\0') {
        (void)mp_cache_fs_remove_path_if_exists(server->config->socket_path);
    }
}

int mp_cache_http_client_request(
    const char *socket_path,
    const char *method,
    const char *request_path,
    const char *auth_token,
    const char *content_type,
    const char *request_body,
    FILE *stream) {
    struct sockaddr_un address;
    int client_fd = -1;
    socklen_t address_length = 0;
    char request_buffer[MP_CACHE_HTTP_REQUEST_CAPACITY];
    char response_buffer[MP_CACHE_HTTP_REQUEST_CAPACITY];
    size_t request_body_length = request_body == NULL ? 0u : strlen(request_body);
    int request_length = 0;
    ssize_t bytes_read = 0;
    char *response_body = NULL;

    if (socket_path == NULL || method == NULL || request_path == NULL || stream == NULL) {
        errno = EINVAL;
        return -1;
    }

    request_length = snprintf(
        request_buffer,
        sizeof(request_buffer),
        "%s %s HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "%s%s%s"
        "%s%s%s"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        method,
        request_path,
        auth_token == NULL ? "" : "Authorization: Bearer ",
        auth_token == NULL ? "" : auth_token,
        auth_token == NULL ? "" : "\r\n",
        content_type == NULL ? "" : "Content-Type: ",
        content_type == NULL ? "" : content_type,
        content_type == NULL ? "" : "\r\n",
        request_body_length,
        request_body == NULL ? "" : request_body);
    if (request_length < 0 || (size_t)request_length >= sizeof(request_buffer)) {
        errno = EOVERFLOW;
        return -1;
    }

    client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd < 0) {
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        (void)close(client_fd);
        return -1;
    }
    memcpy(address.sun_path, socket_path, strlen(socket_path) + 1u);
    address_length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(address.sun_path) + 1u);

    if (connect(client_fd, (const struct sockaddr *)&address, address_length) != 0 ||
        mp_cache_http_send_all(client_fd, request_buffer, (size_t)request_length) != 0) {
        (void)close(client_fd);
        return -1;
    }

    bytes_read = recv(client_fd, response_buffer, sizeof(response_buffer) - 1u, 0);
    if (bytes_read < 0) {
        (void)close(client_fd);
        return -1;
    }
    response_buffer[bytes_read] = '\0';
    response_body = strstr(response_buffer, "\r\n\r\n");
    if (response_body != NULL) {
        response_body += 4;
        (void)fprintf(stream, "%s\n", response_body);
    } else {
        (void)fprintf(stream, "%s\n", response_buffer);
    }

    (void)close(client_fd);
    return 0;
}

int mp_cache_http_client_health(const char *socket_path, FILE *stream) {
    return mp_cache_http_client_request(socket_path, "GET", "/v1/health", NULL, "application/json", "", stream);
}

int mp_cache_http_server_test_request(
    mp_cache_http_server_t *server,
    const char *raw_request,
    int *out_response_status_code,
    char **out_response_body) {
    char *buffer = NULL;
    size_t request_length = 0u;
    mp_cache_http_request_t request;
    mp_cache_http_response_t response;

    if (server == NULL || raw_request == NULL || out_response_status_code == NULL || out_response_body == NULL) {
        errno = EINVAL;
        return -1;
    }

    request_length = strlen(raw_request);
    buffer = malloc(request_length + 1u);
    if (buffer == NULL) {
        return -1;
    }
    memcpy(buffer, raw_request, request_length + 1u);

    if (mp_cache_http_parse_request_buffer(buffer, request_length, &request) != 0 ||
        mp_cache_http_route_request(server, &request, &response) != 0) {
        free(buffer);
        return -1;
    }

    *out_response_status_code = response.status_code;
    *out_response_body = response.response_body;
    free(buffer);
    return 0;
}
