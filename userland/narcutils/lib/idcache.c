#include <grp.h>
#include <narcutils/idcache.h>
#include <narcutils/system.h>
#include <pwd.h>

struct entry {
    struct entry *next;
    unsigned long id;
    char *name;
};

static struct entry *users, *groups;

static const char *remember(struct entry **list, unsigned long id, const char *name) {
    struct entry *entry = xmalloc(sizeof(*entry));
    entry->id = id;
    entry->name = name ? xstrdup(name) : 0;
    entry->next = *list;
    *list = entry;
    return entry->name;
}

const char *getuser(uid_t uid) {
    for (struct entry *e = users; e; e = e->next)
        if (e->id == uid) return e->name;
    struct passwd *pw = getpwuid(uid);
    return remember(&users, uid, pw ? pw->pw_name : 0);
}

const char *getgroup(gid_t gid) {
    for (struct entry *e = groups; e; e = e->next)
        if (e->id == gid) return e->name;
    struct group *gr = getgrgid(gid);
    return remember(&groups, gid, gr ? gr->gr_name : 0);
}

uid_t *getuidbyname(const char *user) {
    static uid_t uid;
    struct passwd *pw = getpwnam(user);
    if (!pw) return 0;
    uid = pw->pw_uid;
    return &uid;
}

gid_t *getgidbyname(const char *group) {
    static gid_t gid;
    struct group *gr = getgrnam(group);
    if (!gr) return 0;
    gid = gr->gr_gid;
    return &gid;
}

int xgetgroups(const char *username, gid_t gid, gid_t **result) {
    if (!username) {
        int count = getgroups(0, 0);
        if (count < 0) return -1;
        gid_t *list = xnmalloc((size_t)count + 1, sizeof(*list));
        count = getgroups(count, list + 1);
        if (count < 0) {
            free(list);
            return -1;
        }
        list[0] = gid;
        int total = 1;
        for (int i = 1; i <= count; i++) {
            bool duplicate = false;
            for (int j = 0; j < total; j++)
                if (list[j] == list[i]) duplicate = true;
            if (!duplicate) list[total++] = list[i];
        }
        *result = list;
        return total;
    }
    int count = 16;
    gid_t *list = xnmalloc((size_t)count, sizeof(*list));
    while (getgrouplist(username, gid, list, &count) < 0)
        list = xnrealloc(list, (size_t)count, sizeof(*list));
    *result = list;
    return count;
}
