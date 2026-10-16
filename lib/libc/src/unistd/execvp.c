#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

int execvpe(const char *file, char *const argv[], char *const envp[]) {
    if (!*file) {
        errno = ENOENT;
        return -1;
    }
    if (strchr(file, '/')) return execve(file, argv, envp);
    const char *path = getenv("PATH");
    if (!path) path = "/bin:/sbin:/usr/bin";
    size_t length = strlen(file);
    int saw_access = 0;
    for (const char *cursor = path;; cursor++) {
        const char *end = strchr(cursor, ':');
        if (!end) end = cursor + strlen(cursor);
        size_t prefix = (size_t)(end - cursor);
        char candidate[PATH_MAX];
        if (prefix + length + 2 <= sizeof(candidate)) {
            memcpy(candidate, prefix ? cursor : ".", prefix ? prefix : 1);
            size_t at = prefix ? prefix : 1;
            candidate[at++] = '/';
            memcpy(candidate + at, file, length + 1);
            execve(candidate, argv, envp);
            if (errno == EACCES) saw_access = 1;
            else if (errno != ENOENT && errno != ENOTDIR) return -1;
        }
        if (!*end) break;
        cursor = end;
    }
    if (saw_access) errno = EACCES;
    return -1;
}

int execvp(const char *file, char *const argv[]) {
    return execvpe(file, argv, environ);
}

static char **collect(const char *first, va_list arguments, char ***envp) {
    va_list copy;
    va_copy(copy, arguments);
    size_t count = 1;
    while (va_arg(copy, char *)) count++;
    va_end(copy);
    char **argv = malloc((count + 1) * sizeof(char *));
    if (!argv) return 0;
    argv[0] = (char *)first;
    for (size_t i = 1; i <= count; i++) argv[i] = va_arg(arguments, char *);
    if (envp) *envp = va_arg(arguments, char **);
    return argv;
}

int execl(const char *path, const char *argument, ...) {
    va_list arguments;
    va_start(arguments, argument);
    char **argv = collect(argument, arguments, 0);
    va_end(arguments);
    if (!argv) return -1;
    execv(path, argv);
    free(argv);
    return -1;
}

int execlp(const char *file, const char *argument, ...) {
    va_list arguments;
    va_start(arguments, argument);
    char **argv = collect(argument, arguments, 0);
    va_end(arguments);
    if (!argv) return -1;
    execvp(file, argv);
    free(argv);
    return -1;
}

int execle(const char *path, const char *argument, ...) {
    va_list arguments;
    va_start(arguments, argument);
    char **envp;
    char **argv = collect(argument, arguments, &envp);
    va_end(arguments);
    if (!argv) return -1;
    execve(path, argv, envp);
    free(argv);
    return -1;
}
