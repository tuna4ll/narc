#include <pablo/log.h>
#include <stdio.h>

static const char *level_name(enum pablo_log_level level) {
    if (level == PABLO_LOG_WARN) return "warn";
    if (level == PABLO_LOG_ERROR) return "error";
    return "info";
}

void pablo_log(enum pablo_log_level level, const char *message) {
    fprintf(stderr, "[pablo] %s: %s\n", level_name(level), message);
}

void pablo_log_service(enum pablo_log_level level, const char *service,
                       const char *message) {
    fprintf(stderr, "[pablo] %s: %s: %s\n", level_name(level), service, message);
}

void pablo_log_status(enum pablo_log_level level, const char *service,
                      const char *message, int status) {
    fprintf(stderr, "[pablo] %s: %s: %s (%d)\n", level_name(level), service,
            message, status);
}
