#include "internal/platform/fs.h"

#include "internal/config/config.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int mp_cache_fs_mkdir_single(const char *path) {
    struct stat status;

    if (path == NULL || *path == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (stat(path, &status) == 0) {
        if (S_ISDIR(status.st_mode) != 0) {
            return 0;
        }
        errno = ENOTDIR;
        return -1;
    }

    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }

    return -1;
}

int mp_cache_fs_ensure_directory(const char *path) {
    char scratch[MP_CACHE_PATH_CAP];
    size_t index = 0u;
    size_t length = 0u;

    if (path == NULL || *path == '\0') {
        errno = EINVAL;
        return -1;
    }

    length = strlen(path);
    if (length >= sizeof(scratch)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    (void)snprintf(scratch, sizeof(scratch), "%s", path);

    for (index = 1u; scratch[index] != '\0'; index++) {
        if (scratch[index] == '/') {
            scratch[index] = '\0';
            if (scratch[0] != '\0' && mp_cache_fs_mkdir_single(scratch) != 0) {
                return -1;
            }
            scratch[index] = '/';
        }
    }

    return mp_cache_fs_mkdir_single(scratch);
}

int mp_cache_fs_ensure_parent_directory(const char *path) {
    char scratch[MP_CACHE_PATH_CAP];
    char *last_separator = NULL;

    if (path == NULL || *path == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (strlen(path) >= sizeof(scratch)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    (void)snprintf(scratch, sizeof(scratch), "%s", path);
    last_separator = strrchr(scratch, '/');
    if (last_separator == NULL) {
        return 0;
    }

    if (last_separator == scratch) {
        return 0;
    }

    *last_separator = '\0';
    return mp_cache_fs_ensure_directory(scratch);
}

int mp_cache_fs_remove_path_if_exists(const char *path) {
    if (path == NULL || *path == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (unlink(path) == 0 || errno == ENOENT) {
        return 0;
    }

    return -1;
}

int mp_cache_fs_write_pid_file(const char *path, pid_t pid) {
    FILE *file = NULL;

    if (path == NULL || *path == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (mp_cache_fs_ensure_parent_directory(path) != 0) {
        return -1;
    }

    file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }

    if (fprintf(file, "%ld\n", (long)pid) < 0) {
        (void)fclose(file);
        return -1;
    }

    if (fclose(file) != 0) {
        return -1;
    }

    return 0;
}
