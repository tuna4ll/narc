#include <pablo/config.h>
#include <pablo/log.h>
#include <pablo/supervisor.h>
#include <stdio.h>
#include <unistd.h>

#define PABLO_VERSION "0.1.0"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 1 || !argv || getpid() != 1) {
        pablo_log(PABLO_LOG_ERROR, "must run as pid 1");
        return 1;
    }

    fprintf(stderr, "[pablo] info: pablo %s starting\n", PABLO_VERSION);
    struct pablo_config config;
    if (pablo_config_load(&config, "/etc/pablo.conf", "/etc/pablo/services") != 0) {
        pablo_log(PABLO_LOG_ERROR, "configuration rejected");
        return 2;
    }
    fprintf(stderr, "[pablo] info: loaded %zu services\n", config.service_count);
    pablo_log(PABLO_LOG_INFO, "system ready");

    struct pablo_supervisor supervisor;
    pablo_supervisor_init(&supervisor, &config);
    pablo_supervisor_run(&supervisor);
}
