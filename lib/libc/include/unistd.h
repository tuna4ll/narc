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
int isatty(int fd);
int pipe(int fds[2]);
int pipe2(int fds[2], int flags);
int dup(int fd);
int dup2(int fd, int target);
off_t lseek(int fd, off_t offset, int origin);
pid_t getpid(void);
pid_t getppid(void);
int setpgid(pid_t pid, pid_t pgid);
pid_t getpgid(pid_t pid);
pid_t getpgrp(void);
pid_t setsid(void);
pid_t getsid(pid_t pid);
pid_t fork(void);
int execv(const char *path, char *const argv[]);
int execve(const char *path, char *const argv[], char *const envp[]);
int unlink(const char *path);
int chdir(const char *path);
int fchdir(int fd);
char *getcwd(char *buffer, size_t size);
int rmdir(const char *path);
int pause(void);
unsigned int sleep(unsigned int seconds);
int usleep(useconds_t microseconds);
_Noreturn void _exit(int status);
