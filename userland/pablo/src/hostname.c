#include <errno.h>
#include <pablo/hostname.h>
#include <pablo/io.h>
#include <pablo/log.h>
#include <pablo/text.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int pablo_set_hostname(const char *path) {
    char *buffer;
    size_t length;
    if (pablo_read_file(path, &buffer, &length) != 0) return errno == ENOENT ? 0 : -1;
    char *name = pablo_trim(buffer);
    name[strcspn(name, "\n")] = 0;
    int status = *name ? sethostname(name, strlen(name)) : 0;
    if (status != 0) pablo_log(PABLO_LOG_WARN, "cannot set hostname");
    free(buffer);
    return status;
}
