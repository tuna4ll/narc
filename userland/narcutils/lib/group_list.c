#include <narcutils/group_list.h>
#include <narcutils/idcache.h>
#include <narcutils/quote.h>
#include <narcutils/system.h>
#include <pwd.h>

bool print_group(gid_t gid, bool use_name) {
    const char *name = 0;
    bool ok = true;
    if (use_name) {
        name = getgroup(gid);
        if (!name) {
            error(0, 0, "cannot find name for group ID %lu", (unsigned long)gid);
            ok = false;
        }
    }
    if (name) fputs(name, stdout);
    else printf("%lu", (unsigned long)gid);
    return ok;
}

bool print_group_list(const char *username, uid_t ruid, gid_t rgid, gid_t egid, bool use_names,
                      char delimiter) {
    bool ok = true;
    struct passwd *pw = 0;
    gid_t primary = egid;
    if (username) {
        pw = getpwuid(ruid);
        if (!pw) ok = false;
        else primary = pw->pw_gid;
    }
    if (!print_group(rgid, use_names)) ok = false;
    if (egid != rgid) {
        putchar(delimiter);
        if (!print_group(egid, use_names)) ok = false;
    }
    gid_t *groups;
    int count = xgetgroups(username, primary, &groups);
    if (count < 0) {
        if (username) error(0, errno, "failed to get groups for user %s", quote(username));
        else error(0, errno, "failed to get groups for the current process");
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (groups[i] == rgid || groups[i] == egid) continue;
        putchar(delimiter);
        if (!print_group(groups[i], use_names)) ok = false;
    }
    free(groups);
    return ok;
}
