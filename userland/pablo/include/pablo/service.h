#pragma once

#include <pablo/limits.h>
#include <sys/types.h>

enum pablo_policy {
    PABLO_ONCE,
    PABLO_RESPAWN,
};

enum pablo_service_state {
    PABLO_STOPPED,
    PABLO_RUNNING,
    PABLO_BACKOFF,
    PABLO_COMPLETE,
    PABLO_FAILED,
};

struct pablo_service {
    char name[PABLO_NAME_MAX];
    char path[PABLO_PATH_MAX];
    char after[PABLO_NAME_MAX];
    char arguments[PABLO_ARG_MAX][PABLO_ARG_LENGTH];
    size_t argument_count;
    enum pablo_policy policy;
    enum pablo_service_state state;
    pid_t pid;
    unsigned int restart_count;
    unsigned long ready_tick;
    int exit_status;
};

struct pablo_config {
    struct pablo_service services[PABLO_SERVICE_MAX];
    size_t service_count;
    unsigned int restart_limit;
    unsigned int restart_delay;
};

struct pablo_service *pablo_service_find(struct pablo_config *config,
                                         const char *name);
int pablo_service_validate(struct pablo_config *config);
