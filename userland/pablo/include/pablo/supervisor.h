#pragma once

#include <pablo/service.h>

struct pablo_supervisor {
    struct pablo_config *config;
    unsigned long tick;
    int quiescent_reported;
};

void pablo_supervisor_init(struct pablo_supervisor *supervisor,
                           struct pablo_config *config);
void pablo_supervisor_run(struct pablo_supervisor *supervisor) __attribute__((noreturn));
