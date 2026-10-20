#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static uint64_t entropy(void) {
    static uint64_t state;
    uint64_t seed = 0;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        if (read(fd, &seed, sizeof(seed)) != (ssize_t)sizeof(seed)) seed = 0;
        close(fd);
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    state ^= seed ^ ((uint64_t)now.tv_nsec << 20) ^ (uint64_t)now.tv_sec ^ (uint64_t)getpid() << 40;
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state;
}

static char *template_suffix(char *pattern) {
    size_t length = strlen(pattern);
    if (length < 6 || memcmp(pattern + length - 6, "XXXXXX", 6)) {
        errno = EINVAL;
        return 0;
    }
    return pattern + length - 6;
}

static void fill(char *suffix) {
    uint64_t value = entropy();
    for (int i = 0; i < 6; i++) {
        suffix[i] = letters[value % (sizeof(letters) - 1)];
        value /= sizeof(letters) - 1;
    }
}

int mkostemp(char *pattern, int flags) {
    char *suffix = template_suffix(pattern);
    if (!suffix) return -1;
    for (int attempt = 0; attempt < 100; attempt++) {
        fill(suffix);
        int fd = open(pattern, O_RDWR | O_CREAT | O_EXCL | (flags & ~O_ACCMODE), 0600);
        if (fd >= 0 || errno != EEXIST) return fd;
    }
    memcpy(suffix, "XXXXXX", 6);
    errno = EEXIST;
    return -1;
}

int mkstemp(char *pattern) {
    return mkostemp(pattern, 0);
}

char *mkdtemp(char *pattern) {
    char *suffix = template_suffix(pattern);
    if (!suffix) return 0;
    for (int attempt = 0; attempt < 100; attempt++) {
        fill(suffix);
        if (mkdir(pattern, 0700) == 0) return pattern;
        if (errno != EEXIST) return 0;
    }
    memcpy(suffix, "XXXXXX", 6);
    errno = EEXIST;
    return 0;
}
