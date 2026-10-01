#pragma once

#include <narc/abi.h>

#ifdef __cplusplus
extern "C" {
#define NARC_NORETURN [[noreturn]]
#else
#define NARC_NORETURN _Noreturn
#endif

narc_result_t narc_abi_query(void);
narc_result_t narc_getpid(void);
narc_result_t narc_yield(void);
narc_result_t narc_fork(void);
narc_result_t narc_wait(int pid, int *status, uint32_t options);
narc_result_t narc_exec(const char *path, char *const argv[], char *const envp[]);
NARC_NORETURN void narc_exit(int status);

narc_result_t narc_open(const char *path, size_t path_length, uint32_t flags);
narc_result_t narc_close(int fd);
narc_result_t narc_read(int fd, void *buffer, size_t length);
narc_result_t narc_write(int fd, const void *buffer, size_t length);
narc_result_t narc_seek(int fd, int64_t offset, uint32_t origin);
narc_result_t narc_file_info(int fd, narc_file_info_t *info);
narc_result_t narc_read_dir(int fd, narc_dir_entry_t *entry);
narc_result_t narc_mkdir(const char *path, size_t path_length, uint32_t mode);
narc_result_t narc_unlink(const char *path, size_t path_length);
narc_result_t narc_rmdir(const char *path, size_t path_length);
narc_result_t narc_rename(const char *old_path, size_t old_length,
                          const char *new_path, size_t new_length);

narc_result_t narc_map(size_t length, uint32_t flags);
narc_result_t narc_unmap(void *address, size_t length);

#ifdef __cplusplus
}
#endif

#undef NARC_NORETURN
