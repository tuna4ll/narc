#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct group entry;
static char line[512];
static char *members[32];

static struct group *search(const char *name, gid_t gid) {
    FILE *database = fopen("/etc/group", "re");
    if (!database) return 0;
    struct group *found = 0;
    while (!found && fgets(line, sizeof(line), database)) {
        line[strcspn(line, "\n")] = 0;
        char *fields[4] = { 0 };
        char *cursor = line;
        for (int i = 0; i < 4; i++) {
            fields[i] = cursor;
            cursor = strchr(cursor, ':');
            if (!cursor) break;
            *cursor++ = 0;
        }
        if (!fields[2]) continue;
        gid_t id = (gid_t)strtoul(fields[2], 0, 10);
        if (name ? strcmp(fields[0], name) : id != gid) continue;
        entry.gr_name = fields[0];
        entry.gr_passwd = fields[1];
        entry.gr_gid = id;
        size_t count = 0;
        char *state;
        for (char *member = fields[3] ? strtok_r(fields[3], ",", &state) : 0;
             member && count < 31; member = strtok_r(0, ",", &state))
            members[count++] = member;
        members[count] = 0;
        entry.gr_mem = members;
        found = &entry;
    }
    fclose(database);
    return found;
}

struct group *getgrnam(const char *name) {
    return search(name, 0);
}

struct group *getgrgid(gid_t gid) {
    return search(0, gid);
}
