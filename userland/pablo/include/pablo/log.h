#pragma once

enum pablo_log_level {
    PABLO_LOG_INFO,
    PABLO_LOG_WARN,
    PABLO_LOG_ERROR,
};

void pablo_log(enum pablo_log_level level, const char *message);
void pablo_log_service(enum pablo_log_level level, const char *service,
                       const char *message);
void pablo_log_status(enum pablo_log_level level, const char *service,
                      const char *message, int status);
