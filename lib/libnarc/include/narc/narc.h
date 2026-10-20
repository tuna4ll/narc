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
narc_result_t narc_getppid(void);
narc_result_t narc_setpgid(int pid, int pgid);
narc_result_t narc_getpgid(int pid);
narc_result_t narc_setsid(void);
narc_result_t narc_getsid(int pid);
narc_result_t narc_umask(uint32_t mask);
narc_result_t narc_chdir(const char *path, size_t path_length);
narc_result_t narc_fchdir(int fd);
narc_result_t narc_getcwd(char *buffer, size_t size);

narc_result_t narc_open(const char *path, size_t path_length, uint32_t flags, uint32_t mode);
narc_result_t narc_close(int fd);
narc_result_t narc_read(int fd, void *buffer, size_t length);
narc_result_t narc_write(int fd, const void *buffer, size_t length);
narc_result_t narc_seek(int fd, int64_t offset, uint32_t origin);
narc_result_t narc_file_info(int fd, narc_file_info_t *info);
narc_result_t narc_stat(const char *path, size_t path_length, narc_file_info_t *info,
                        uint32_t flags);
narc_result_t narc_set_attr(const char *path, size_t path_length, const narc_file_attr_t *attr,
                            uint32_t flags);
narc_result_t narc_file_set_attr(int fd, const narc_file_attr_t *attr);
narc_result_t narc_sync(int fd);
narc_result_t narc_fs_info(const char *path, size_t path_length, narc_fs_info_t *info);
narc_result_t narc_file_fs_info(int fd, narc_fs_info_t *info);
narc_result_t narc_read_dir(int fd, narc_dir_entry_t *entry);
narc_result_t narc_mkdir(const char *path, size_t path_length, uint32_t mode);
narc_result_t narc_unlink(const char *path, size_t path_length);
narc_result_t narc_rmdir(const char *path, size_t path_length);
narc_result_t narc_rename(const char *old_path, size_t old_length,
                          const char *new_path, size_t new_length);
narc_result_t narc_symlink(const char *target, const char *path);
narc_result_t narc_link(const char *old_path, const char *new_path);
narc_result_t narc_readlink(const char *path, char *buffer, size_t size);
narc_result_t narc_pipe(int32_t fds[2], uint32_t flags);
narc_result_t narc_mount(const char *source, const char *target, const char *type,
                         uint32_t flags);
narc_result_t narc_umount(const char *target);
narc_result_t narc_fcntl(int fd, uint32_t command, uint64_t argument);
narc_result_t narc_ioctl(int fd, uint32_t request, uint64_t argument);
narc_result_t narc_dup(int fd, int minimum);
narc_result_t narc_dup2(int fd, int target);

narc_result_t narc_map(size_t length, uint32_t flags);
narc_result_t narc_unmap(void *address, size_t length);

narc_result_t narc_kill(int pid, int signal);
narc_result_t narc_sigaction(int signal, const narc_sigaction_t *action,
                             narc_sigaction_t *old_action);
narc_result_t narc_sigmask(uint32_t how, uint64_t set);
narc_result_t narc_sigpending(void);
narc_result_t narc_sigsuspend(uint64_t mask);
void narc_signal_return(void);

narc_result_t narc_uname(narc_utsname_t *info);
narc_result_t narc_sethostname(const char *name, size_t length);

narc_result_t narc_clock_get(uint32_t clock);
narc_result_t narc_sleep(uint64_t nanoseconds);

#ifdef __cplusplus
}
#endif

#undef NARC_NORETURN
