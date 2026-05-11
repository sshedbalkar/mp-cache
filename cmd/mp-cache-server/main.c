#include "internal/runtime/runtime.h"

#include <stdio.h>
#include <string.h>

static void mp_cache_print_usage(FILE *stream) {
    (void)fprintf(
        stream,
        "usage: mp-cache-server [--config <path>] [--print-config]\n"
        "default config path: configs/bootstrap.ini\n");
}

int main(int argc, char **argv) {
    const char *config_path = "configs/bootstrap.ini";
    int print_config_only = 0;
    int index = 0;

    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--config") == 0) {
            if (index + 1 >= argc) {
                mp_cache_print_usage(stderr);
                return 1;
            }
            config_path = argv[++index];
            continue;
        }
        if (strcmp(argv[index], "--print-config") == 0) {
            print_config_only = 1;
            continue;
        }
        if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
            mp_cache_print_usage(stdout);
            return 0;
        }

        mp_cache_print_usage(stderr);
        return 1;
    }

    return mp_cache_runtime_run(config_path, print_config_only);
}
