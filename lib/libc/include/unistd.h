#pragma once

#include <stddef.h>
#include <sys/types.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

extern char **environ;
extern char *optarg;
extern int optind, opterr, optopt;

int getopt(int argc, char *const argv[], const char *options);

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

ssize_t read(int fd, void *buffer, size_t length);
ssize_t write(int fd, const void *buffer, size_t length);
int close(int fd);
int isatty(int fd);
int access(const char *path, int mode);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int getgroups(int size, gid_t list[]);
int setuid(uid_t uid);
int setgid(gid_t gid);
int gethostname(char *name, size_t length);
int sethostname(const char *name, size_t length);
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
int execvp(const char *file, char *const argv[]);
int execvpe(const char *file, char *const argv[], char *const envp[]);
int execl(const char *path, const char *argument, ...);
int execlp(const char *file, const char *argument, ...);
int execle(const char *path, const char *argument, ...);
int execve(const char *path, char *const argv[], char *const envp[]);
int unlink(const char *path);
int symlink(const char *target, const char *path);
int link(const char *old_path, const char *new_path);
ssize_t readlink(const char *restrict path, char *restrict buffer, size_t size);
int chdir(const char *path);
int fchdir(int fd);
char *getcwd(char *buffer, size_t size);
int rmdir(const char *path);
int pause(void);
unsigned int sleep(unsigned int seconds);
int usleep(useconds_t microseconds);
_Noreturn void _exit(int status);
