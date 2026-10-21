#include <fcntl.h>
#include <narcutils/io.h>
#include <narcutils/system.h>

static size_t transfer(int fd, void *buffer, size_t count, bool writing) {
    for (;;) {
        ssize_t result = writing ? write(fd, buffer, count) : read(fd, buffer, count);
        if (result >= 0) return (size_t)result;
        if (errno == EINTR) continue;
        if (errno == EINVAL && count > INT_MAX) {
            count = INT_MAX & ~8191;
            continue;
        }
        return SAFE_READ_ERROR;
    }
}

size_t safe_read(int fd, void *buffer, size_t count) {
    return transfer(fd, buffer, count, false);
}

size_t safe_write(int fd, const void *buffer, size_t count) {
    return transfer(fd, (void *)buffer, count, true);
}

static size_t full_transfer(int fd, void *buffer, size_t count, bool writing) {
    size_t total = 0;
    char *cursor = buffer;
    while (count > 0) {
        size_t n = transfer(fd, cursor, count, writing);
        if (n == SAFE_READ_ERROR) break;
        if (n == 0) {
            errno = writing ? ENOSPC : 0;
            break;
        }
        total += n;
        cursor += n;
        count -= n;
    }
    return total;
}

size_t full_read(int fd, void *buffer, size_t count) {
    return full_transfer(fd, buffer, count, false);
}

size_t full_write(int fd, const void *buffer, size_t count) {
    return full_transfer(fd, (void *)buffer, count, true);
}

int xopen_input(const char *name, bool *is_stdin) {
    *is_stdin = STREQ(name, "-");
    if (*is_stdin) return STDIN_FILENO;
    return open(name, O_RDONLY);
}

int close_input(int fd, bool is_stdin) {
    return is_stdin ? 0 : close(fd);
}

void xset_binary_mode(int fd) {
    (void)fd;
}
