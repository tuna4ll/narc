#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMBER_LIMIT 64

static FILE *database;
static struct group entry;
static char line[1024];
static char *members[MEMBER_LIMIT + 1];

static int parse(void) {
    line[strcspn(line, "\n")] = 0;
    if (!line[0] || line[0] == '#') return 0;
    char *fields[4] = { 0 };
    char *cursor = line;
    for (int i = 0; i < 4; i++) {
        fields[i] = cursor;
        cursor = strchr(cursor, ':');
        if (!cursor) break;
        *cursor++ = 0;
    }
    if (!fields[2]) return 0;
    entry.gr_name = fields[0];
    entry.gr_passwd = fields[1];
    entry.gr_gid = (gid_t)strtoul(fields[2], 0, 10);
    size_t count = 0;
    char *state;
    for (char *member = fields[3] ? strtok_r(fields[3], ",", &state) : 0;
         member && count < MEMBER_LIMIT; member = strtok_r(0, ",", &state))
        members[count++] = member;
    members[count] = 0;
    entry.gr_mem = members;
    return 1;
}

void setgrent(void) {
    if (database) rewind(database);
}

void endgrent(void) {
    if (database) fclose(database);
    database = 0;
}

struct group *getgrent(void) {
    if (!database && !(database = fopen("/etc/group", "re"))) return 0;
    while (fgets(line, sizeof(line), database))
        if (parse()) return &entry;
    return 0;
}

struct group *getgrnam(const char *name) {
    setgrent();
    struct group *found;
    while ((found = getgrent()) && strcmp(found->gr_name, name)) {}
    endgrent();
    return found;
}

struct group *getgrgid(gid_t gid) {
    setgrent();
    struct group *found;
    while ((found = getgrent()) && found->gr_gid != gid) {}
    endgrent();
    return found;
}

int getgrouplist(const char *user, gid_t group, gid_t *groups, int *count) {
    int capacity = *count, total = 0;
    if (total < capacity) groups[total] = group;
    total++;
    setgrent();
    struct group *found;
    while ((found = getgrent())) {
        if (found->gr_gid == group) continue;
        for (char **member = found->gr_mem; *member; member++) {
            if (strcmp(*member, user)) continue;
            if (total < capacity) groups[total] = found->gr_gid;
            total++;
            break;
        }
    }
    endgrent();
    *count = total;
    return total > capacity ? -1 : total;
}
