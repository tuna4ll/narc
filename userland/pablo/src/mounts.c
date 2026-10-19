#include <errno.h>
#include <pablo/io.h>
#include <pablo/log.h>
#include <pablo/mounts.h>
#include <pablo/text.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>

struct entry {
    char *source, *target, *type, *options;
};

static int split_fields(char *line, struct entry *entry) {
    char *fields[4] = { 0 };
    char *state;
    int count = 0;
    for (char *field = strtok_r(line, " \t", &state); field && count < 4;
         field = strtok_r(0, " \t", &state))
        fields[count++] = field;
    if (count < 3) return -1;
    entry->source = fields[0];
    entry->target = fields[1];
    entry->type = fields[2];
    entry->options = fields[3] ? fields[3] : "defaults";
    return 0;
}

static int has_option(const char *options, const char *name) {
    size_t length = strlen(name);
    for (const char *cursor = options; *cursor;) {
        const char *end = strchr(cursor, ',');
        size_t size = end ? (size_t)(end - cursor) : strlen(cursor);
        if (size == length && !strncmp(cursor, name, length)) return 1;
        if (!end) break;
        cursor = end + 1;
    }
    return 0;
}

static void mount_entry(const struct entry *entry) {
    char message[256];
    mkdir(entry->target, 0755);
    unsigned long flags = has_option(entry->options, "ro") ? MS_RDONLY : 0;
    const char *source = strcmp(entry->source, "none") ? entry->source : 0;
    if (mount(source, entry->target, entry->type, flags, 0) == 0) {
        snprintf(message, sizeof(message), "mounted %s on %s", entry->source, entry->target);
        pablo_log(PABLO_LOG_INFO, message);
        return;
    }
    snprintf(message, sizeof(message), "cannot mount %s on %s: %s", entry->source,
             entry->target, strerror(errno));
    pablo_log(has_option(entry->options, "nofail") ? PABLO_LOG_WARN : PABLO_LOG_ERROR, message);
}

int pablo_mount_table(const char *path) {
    char *buffer;
    size_t length;
    if (pablo_read_file(path, &buffer, &length) != 0) return errno == ENOENT ? 0 : -1;
    char *state;
    for (char *line = strtok_r(buffer, "\n", &state); line; line = strtok_r(0, "\n", &state)) {
        line = pablo_trim(line);
        if (!*line || *line == '#') continue;
        struct entry entry;
        if (split_fields(line, &entry) != 0) {
            pablo_log(PABLO_LOG_WARN, "ignoring malformed mount table entry");
            continue;
        }
        mount_entry(&entry);
    }
    free(buffer);
    return 0;
}
