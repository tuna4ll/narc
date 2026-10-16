#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *database;
static struct passwd entry;
static char line[512];

static char *field(char **cursor) {
    char *start = *cursor;
    char *end = strchr(start, ':');
    if (end) {
        *end = 0;
        *cursor = end + 1;
    } else {
        *cursor = start + strlen(start);
    }
    return start;
}

void setpwent(void) {
    if (database) rewind(database);
}

void endpwent(void) {
    if (database) fclose(database);
    database = 0;
}

struct passwd *getpwent(void) {
    if (!database && !(database = fopen("/etc/passwd", "re"))) return 0;
    while (fgets(line, sizeof(line), database)) {
        line[strcspn(line, "\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        char *cursor = line;
        entry.pw_name = field(&cursor);
        entry.pw_passwd = field(&cursor);
        entry.pw_uid = (uid_t)strtoul(field(&cursor), 0, 10);
        entry.pw_gid = (gid_t)strtoul(field(&cursor), 0, 10);
        entry.pw_gecos = field(&cursor);
        entry.pw_dir = field(&cursor);
        entry.pw_shell = field(&cursor);
        return &entry;
    }
    return 0;
}

struct passwd *getpwnam(const char *name) {
    setpwent();
    struct passwd *found;
    while ((found = getpwent()) && strcmp(found->pw_name, name)) {}
    endpwent();
    return found;
}

struct passwd *getpwuid(uid_t uid) {
    setpwent();
    struct passwd *found;
    while ((found = getpwent()) && found->pw_uid != uid) {}
    endpwent();
    return found;
}
