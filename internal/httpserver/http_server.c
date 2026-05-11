#include "internal/httpserver/http_server.h"

#include "internal/platform/fs.h"

#include <stddef.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MP_CACHE_HTTP_REQUEST_CAPACITY 8192u
#define MP_CACHE_HTTP_RESPONSE_CAPACITY 4096u

static size_t mp_cache_bounded_string_length(const char *value, size_t limit) {
    size_t length = 0u;

    while (length < limit && value[length] != '\0') {
        length++;
    }

    return length;
}

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
    const char *body,
    const char *allow_header_value) {
    char response_buffer[MP_CACHE_HTTP_RESPONSE_CAPACITY];
    size_t body_length = body == NULL ? 0u : strlen(body);
    int response_length = 0;

    response_length = snprintf(
        response_buffer,
        sizeof(response_buffer),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "%s%s%s"
        "\r\n"
        "%s",
        status_code,
        status_text,
        body_length,
        allow_header_value == NULL ? "" : "Allow: ",
        allow_header_value == NULL ? "" : allow_header_value,
        allow_header_value == NULL ? "" : "\r\n",
        body == NULL ? "" : body);

    if (response_length < 0 || (size_t)response_length >= sizeof(response_buffer)) {
        return -1;
    }

    return mp_cache_http_send_all(client_fd, response_buffer, (size_t)response_length);
}

static bool mp_cache_http_path_is_health(const char *path) {
    return path != NULL && (strcmp(path, "/health") == 0 || strcmp(path, "/v1/health") == 0);
}

static int mp_cache_http_handle_client(int client_fd, mp_cache_http_server_t *server) {
    char request_buffer[MP_CACHE_HTTP_REQUEST_CAPACITY];
    char method[16];
    char path[256];
    char version[16];
    ssize_t bytes_read = 0;

    bytes_read = recv(client_fd, request_buffer, sizeof(request_buffer) - 1u, 0);
    if (bytes_read < 0) {
        return -1;
    }
    if (bytes_read == 0) {
        return 0;
    }

    request_buffer[bytes_read] = '\0';
    if (sscanf(request_buffer, "%15s %255s %15s", method, path, version) != 3) {
        return mp_cache_http_write_response(
            client_fd,
            400,
            "Bad Request",
            "{\"error\":\"invalid_argument\",\"message\":\"malformed request line\"}",
            NULL);
    }

    if (mp_cache_http_path_is_health(path)) {
        if (strcmp(method, "GET") != 0) {
            return mp_cache_http_write_response(
                client_fd,
                405,
                "Method Not Allowed",
                "{\"error\":\"invalid_argument\",\"message\":\"method not allowed\"}",
                "GET");
        }

        {
            mp_cache_store_stats_t stats;
            char body[2048];
            char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
            time_t now_utc = time(NULL);
            long uptime_seconds = 0l;
            size_t socket_path_length = 0u;

            mp_cache_store_get_stats(server->store, &stats);
            uptime_seconds = (long)(now_utc - server->started_at_utc);
            socket_path_length =
                mp_cache_bounded_string_length(server->config->socket_path, sizeof(socket_path) - 1u);
            memcpy(socket_path, server->config->socket_path, socket_path_length);
            socket_path[socket_path_length] = '\0';
            (void)snprintf(
                body,
                sizeof(body),
                "{\"status\":\"ok\",\"service\":\"%s\",\"environment\":\"%s\",\"socket_path\":\"%s\","
                "\"uptime_seconds\":%ld,"
                "\"default_ttl_seconds\":%u,\"memory_limit_bytes\":%llu,"
                "\"entry_count\":%zu,\"bytes_used\":%zu}",
                server->config->service_name,
                server->config->environment_name,
                socket_path,
                uptime_seconds,
                server->config->default_ttl_seconds,
                (unsigned long long)stats.memory_limit_bytes,
                stats.entry_count,
                stats.bytes_used);

            return mp_cache_http_write_response(client_fd, 200, "OK", body, NULL);
        }
    }

    if (strcmp(method, "GET") != 0 && strcmp(path, "/") == 0) {
        return mp_cache_http_write_response(
            client_fd,
            405,
            "Method Not Allowed",
            "{\"error\":\"invalid_argument\",\"message\":\"method not allowed\"}",
            "GET");
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
        return mp_cache_http_write_response(
            client_fd,
            200,
            "OK",
            "{\"service\":\"mp-cache\",\"message\":\"use /health or /v1/health\"}",
            NULL);
    }

    return mp_cache_http_write_response(
        client_fd,
        404,
        "Not Found",
        "{\"error\":\"not_found\",\"message\":\"route not found\"}",
        NULL);
}

int mp_cache_http_server_start(
    mp_cache_http_server_t *server,
    const mp_cache_config_t *config,
    mp_cache_log_t *log,
    mp_cache_store_t *store,
    time_t started_at_utc) {
    struct sockaddr_un address;
    socklen_t address_length = 0;

    if (server == NULL || config == NULL || log == NULL || store == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(server, 0, sizeof(*server));
    server->server_fd = -1;
    server->config = config;
    server->log = log;
    server->store = store;
    server->started_at_utc = started_at_utc;

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

    if (server->config != NULL && server->config->socket_path[0] != '\0') {
        (void)mp_cache_fs_remove_path_if_exists(server->config->socket_path);
    }
}

int mp_cache_http_client_health(const char *socket_path, FILE *stream) {
    struct sockaddr_un address;
    int client_fd = -1;
    socklen_t address_length = 0;
    char buffer[MP_CACHE_HTTP_REQUEST_CAPACITY];
    const char *request = "GET /v1/health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    ssize_t bytes_read = 0;
    char *body = NULL;

    if (socket_path == NULL || stream == NULL) {
        errno = EINVAL;
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

    if (connect(client_fd, (const struct sockaddr *)&address, address_length) != 0) {
        (void)close(client_fd);
        return -1;
    }
    if (mp_cache_http_send_all(client_fd, request, strlen(request)) != 0) {
        (void)close(client_fd);
        return -1;
    }

    bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1u, 0);
    if (bytes_read < 0) {
        (void)close(client_fd);
        return -1;
    }
    buffer[bytes_read] = '\0';
    body = strstr(buffer, "\r\n\r\n");
    if (body != NULL) {
        body += 4;
        (void)fprintf(stream, "%s\n", body);
    } else {
        (void)fprintf(stream, "%s\n", buffer);
    }

    (void)close(client_fd);
    return 0;
}
