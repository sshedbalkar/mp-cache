#include "internal/httpserver/http_server.h"

#include <stdio.h>
#include <string.h>

static void mp_cachectl_print_usage(FILE *stream) {
    (void)fprintf(
        stream,
        "usage: mp-cachectl [--socket <path>] health\n"
        "default socket path: /tmp/mp-cache/run/mp-cache.sock\n");
}

int main(int argc, char **argv) {
    const char *socket_path = "/tmp/mp-cache/run/mp-cache.sock";
    const char *command = NULL;
    int index = 0;

    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--socket") == 0) {
            if (index + 1 >= argc) {
                mp_cachectl_print_usage(stderr);
                return 1;
            }
            socket_path = argv[++index];
            continue;
        }

        command = argv[index];
    }

    if (command == NULL || strcmp(command, "health") != 0) {
        mp_cachectl_print_usage(stderr);
        return 1;
    }

    if (mp_cache_http_client_health(socket_path, stdout) != 0) {
        (void)fprintf(stderr, "error: failed to query health over %s\n", socket_path);
        return 1;
    }

    return 0;
}
