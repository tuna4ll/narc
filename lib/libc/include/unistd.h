#pragma once

#include <stddef.h>
#include <sys/types.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

extern char **environ;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

ssize_t read(int fd, void *buffer, size_t length);
ssize_t write(int fd, const void *buffer, size_t length);
int close(int fd);
off_t lseek(int fd, off_t offset, int origin);
pid_t getpid(void);
pid_t fork(void);
int execv(const char *path, char *const argv[]);
int execve(const char *path, char *const argv[], char *const envp[]);
int unlink(const char *path);
int rmdir(const char *path);
_Noreturn void _exit(int status);
