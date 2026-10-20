#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int getlogin_r(char *buffer, size_t size) {
    const char *name = getenv("LOGNAME");
    if (!name || !*name) {
        struct passwd *entry = getpwuid(getuid());
        if (!entry) return ENXIO;
        name = entry->pw_name;
    }
    if (strlen(name) >= size) return ERANGE;
    strcpy(buffer, name);
    return 0;
}

char *getlogin(void) {
    static char name[256];
    int status = getlogin_r(name, sizeof(name));
    if (status) {
        errno = status;
        return 0;
    }
    return name;
}
