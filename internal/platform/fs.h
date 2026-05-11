#ifndef MP_CACHE_INTERNAL_PLATFORM_FS_H
#define MP_CACHE_INTERNAL_PLATFORM_FS_H

#include <sys/types.h>

int mp_cache_fs_ensure_directory(const char *filesystem_path);
int mp_cache_fs_ensure_parent_directory(const char *filesystem_path);
int mp_cache_fs_remove_path_if_exists(const char *filesystem_path);
int mp_cache_fs_write_pid_file(const char *pid_file_path, pid_t pid);

#endif
