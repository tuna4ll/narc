#include <caposh/builtins.h>
#include <caposh/error.h>
#include <caposh/memory.h>
#include <caposh/strbuf.h>
#include <caposh/vars.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *canonical(const char *base, const char *path) {
    struct strbuf out;
    strbuf_init(&out, stack);
    if (path[0] != '/') strbuf_puts(&out, base);
    const char *cursor = path;
    while (*cursor) {
        while (*cursor == '/') cursor++;
        const char *start = cursor;
        while (*cursor && *cursor != '/') cursor++;
        size_t length = (size_t)(cursor - start);
        if (!length || (length == 1 && start[0] == '.')) continue;
        if (length == 2 && start[0] == '.' && start[1] == '.') {
            while (out.length && out.data[out.length - 1] != '/') out.length--;
            if (out.length) out.length--;
            strbuf_truncate(&out, out.length);
            continue;
        }
        strbuf_putc(&out, '/');
        strbuf_append(&out, start, length);
    }
    if (!out.length) strbuf_putc(&out, '/');
    return strbuf_string(&out);
}

static int is_directory(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static char *search_cdpath(const char *dir, int *printed) {
    *printed = 0;
    if (dir[0] == '/' || !strcmp(dir, ".") || !strcmp(dir, "..") || !strncmp(dir, "./", 2) ||
        !strncmp(dir, "../", 3))
        return 0;
    const char *cdpath = var_get("CDPATH");
    if (!cdpath) return 0;
    for (const char *cursor = cdpath;;) {
        const char *end = strchr(cursor, ':');
        if (!end) end = cursor + strlen(cursor);
        struct strbuf candidate;
        strbuf_init(&candidate, stack);
        if (end == cursor) strbuf_putc(&candidate, '.');
        else strbuf_append(&candidate, cursor, (size_t)(end - cursor));
        strbuf_putc(&candidate, '/');
        strbuf_puts(&candidate, dir);
        char *path = strbuf_string(&candidate);
        if (is_directory(path)) {
            *printed = end != cursor;
            return path;
        }
        if (!*end) return 0;
        cursor = end + 1;
    }
}

static int change(const char *target, int physical, int print) {
    const char *pwd = var_get("PWD");
    char *current = pwd && pwd[0] == '/' ? ststrdup(pwd) : 0;
    if (!current) {
        char *cwd = getcwd(0, 0);
        current = ststrdup(cwd ? cwd : "/");
        free(cwd);
    }
    char *logical = physical ? 0 : canonical(current, target);
    if (logical && chdir(logical) == 0) {
        var_set("OLDPWD", current, 0);
        var_set("PWD", logical, VAR_EXPORT);
    } else {
        if (chdir(target) != 0) {
            builtin_error("cd", "%s: %s", target, strerror(errno));
            return 1;
        }
        char *cwd = getcwd(0, 0);
        var_set("OLDPWD", current, 0);
        var_set("PWD", cwd ? cwd : target, VAR_EXPORT);
        free(cwd);
    }
    if (print) printf("%s\n", var_get("PWD"));
    return 0;
}

int builtin_cd(int argc, char **argv) {
    int physical = 0, option;
    while ((option = getopt(argc, argv, "LP")) != -1) {
        if (option == 'L') physical = 0;
        else if (option == 'P') physical = 1;
        else return builtin_usage("cd");
    }
    if (argc - optind > 1) {
        builtin_error("cd", "too many arguments");
        return 1;
    }
    const char *dir = optind < argc ? argv[optind] : 0;
    int print = 0;
    if (!dir) {
        dir = var_get("HOME");
        if (!dir) {
            builtin_error("cd", "HOME not set");
            return 1;
        }
        if (!*dir) return 0;
    } else if (!strcmp(dir, "-")) {
        dir = var_get("OLDPWD");
        if (!dir) {
            builtin_error("cd", "OLDPWD not set");
            return 1;
        }
        print = 1;
    } else if (!*dir) {
        return 0;
    }
    int from_cdpath;
    char *found = search_cdpath(dir, &from_cdpath);
    return change(found ? found : dir, physical, print || from_cdpath);
}

int builtin_pwd(int argc, char **argv) {
    int physical = 0, option;
    while ((option = getopt(argc, argv, "LP")) != -1) {
        if (option == 'L') physical = 0;
        else if (option == 'P') physical = 1;
        else return builtin_usage("pwd");
    }
    const char *pwd = var_get("PWD");
    if (!physical && pwd && pwd[0] == '/') {
        struct stat logical, actual;
        if (stat(pwd, &logical) == 0 && stat(".", &actual) == 0 &&
            logical.st_ino == actual.st_ino) {
            printf("%s\n", pwd);
            return 0;
        }
    }
    char *cwd = getcwd(0, 0);
    if (!cwd) {
        builtin_error("pwd", "%s", strerror(errno));
        return 1;
    }
    printf("%s\n", cwd);
    free(cwd);
    return 0;
}
