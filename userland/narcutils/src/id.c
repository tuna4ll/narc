#include <grp.h>
#include <narcutils/group_list.h>
#include <narcutils/idcache.h>
#include <narcutils/quote.h>
#include <narcutils/system.h>
#include <pwd.h>

#define PROGRAM_NAME "id"

static const struct option long_options[] = {
    { "context", no_argument, 0, 'Z' },
    { "group", no_argument, 0, 'g' },
    { "groups", no_argument, 0, 'G' },
    { "name", no_argument, 0, 'n' },
    { "real", no_argument, 0, 'r' },
    { "user", no_argument, 0, 'u' },
    { "zero", no_argument, 0, 'z' },
    GETOPT_HELP_OPTION_DECL,
    GETOPT_VERSION_OPTION_DECL,
    { 0, 0, 0, 0 },
};

static bool use_name, use_real, opt_zero;
static bool ok = true;
static uid_t ruid, euid;
static gid_t rgid, egid;

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        emit_try_help();
        exit(status);
    }
    printf("Usage: %s [OPTION]... [USER]...\n", program_name);
    fputs("Print user and group information for each specified USER,\n"
          "or (when USER omitted) for the current process.\n\n"
          "  -a             ignore, for compatibility with other versions\n"
          "  -Z, --context  print only the security context of the process\n"
          "  -g, --group    print only the effective group ID\n"
          "  -G, --groups   print all group IDs\n"
          "  -n, --name     print a name instead of a number, for -u,-g,-G\n"
          "  -r, --real     print the real ID instead of the effective ID, with -u,-g,-G\n"
          "  -u, --user     print only the effective user ID\n"
          "  -z, --zero     delimit entries with NUL characters, not whitespace;\n"
          "                   not permitted in default format\n",
          stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    fputs("\nWithout any OPTION, print some useful set of identified information.\n", stdout);
    exit(status);
}

static void print_user(uid_t uid) {
    const char *name = 0;
    if (use_name) {
        name = getuser(uid);
        if (!name) {
            error(0, 0, "cannot find name for user ID %lu", (unsigned long)uid);
            ok = false;
        }
    }
    if (name) fputs(name, stdout);
    else printf("%lu", (unsigned long)uid);
}

static void print_full_info(const char *username) {
    printf("uid=%lu", (unsigned long)ruid);
    const char *name = getuser(ruid);
    if (name) printf("(%s)", name);
    printf(" gid=%lu", (unsigned long)rgid);
    name = getgroup(rgid);
    if (name) printf("(%s)", name);
    if (euid != ruid) {
        printf(" euid=%lu", (unsigned long)euid);
        name = getuser(euid);
        if (name) printf("(%s)", name);
    }
    if (egid != rgid) {
        printf(" egid=%lu", (unsigned long)egid);
        name = getgroup(egid);
        if (name) printf("(%s)", name);
    }
    gid_t *groups;
    struct passwd *pw = username ? getpwuid(ruid) : 0;
    int count = xgetgroups(username, pw ? pw->pw_gid : egid, &groups);
    if (count < 0) {
        error(0, errno, username ? "failed to get groups for user %s"
                                 : "failed to get groups for the current process",
              username ? quote(username) : "");
        ok = false;
        return;
    }
    if (count > 0) fputs(" groups=", stdout);
    for (int i = 0; i < count; i++) {
        if (i > 0) putchar(',');
        printf("%lu", (unsigned long)groups[i]);
        name = getgroup(groups[i]);
        if (name) printf("(%s)", name);
    }
    free(groups);
}

static struct passwd *lookup_user(const char *spec) {
    struct passwd *pw = getpwnam(spec);
    if (pw) return pw;
    char *end;
    errno = 0;
    unsigned long value = strtoul(spec, &end, 10);
    if (*spec && !*end && !errno) pw = getpwuid((uid_t)value);
    return pw;
}

int main(int argc, char **argv) {
    bool just_user = false, just_group = false, just_group_list = false, just_context = false;
    int c;
    initialize_main(&argc, &argv);
    while ((c = getopt_long(argc, argv, "agnruzGZ", long_options, 0)) != -1) {
        switch (c) {
        case 'a': break;
        case 'Z': just_context = true; break;
        case 'u': just_user = true; break;
        case 'g': just_group = true; break;
        case 'G': just_group_list = true; break;
        case 'n': use_name = true; break;
        case 'r': use_real = true; break;
        case 'z': opt_zero = true; break;
        case_GETOPT_HELP_CHAR;
        case_GETOPT_VERSION_CHAR(PROGRAM_NAME);
        default: usage(EXIT_FAILURE);
        }
    }
    size_t users = (size_t)(argc - optind);
    if (just_context) die(EXIT_FAILURE, 0, "--context (-Z) works only on an SELinux-enabled kernel");
    if (just_user + just_group + just_group_list + just_context > 1)
        die(EXIT_FAILURE, 0, "cannot print \"only\" of more than one choice");
    bool default_format = !(just_user || just_group || just_group_list || just_context);
    if (default_format && (use_real || use_name))
        die(EXIT_FAILURE, 0, "printing only names or real IDs requires -u, -g, or -G");
    if (default_format && opt_zero)
        die(EXIT_FAILURE, 0, "option --zero not permitted in default format");

    if (!users) {
        euid = geteuid();
        ruid = getuid();
        egid = getegid();
        rgid = getgid();
    }
    size_t index = 0;
    do {
        const char *username = 0;
        if (users) {
            const char *spec = argv[optind + index];
            struct passwd *pw = *spec ? lookup_user(spec) : 0;
            if (!pw) {
                error(0, 0, "%s: no such user", quote(spec));
                ok = false;
                continue;
            }
            username = xstrdup(pw->pw_name);
            ruid = euid = pw->pw_uid;
            rgid = egid = pw->pw_gid;
        }
        if (just_user) {
            print_user(use_real ? ruid : euid);
        } else if (just_group) {
            if (!print_group(use_real ? rgid : egid, use_name)) ok = false;
        } else if (just_group_list) {
            if (!print_group_list(username, ruid, rgid, egid, use_name, opt_zero ? '\0' : ' '))
                ok = false;
        } else {
            print_full_info(username);
        }
        if (opt_zero && just_group_list && users > 1) {
            putchar('\0');
            putchar('\0');
        } else {
            putchar(opt_zero ? '\0' : '\n');
        }
    } while (++index < users);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
