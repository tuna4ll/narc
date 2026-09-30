#include <errno.h>
#include <pablo/log.h>
#include <pablo/supervisor.h>
#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>

static char *const child_environment[] = {
    "PATH=/bin:/sbin",
    "HOME=/",
    0,
};

static int dependency_ready(struct pablo_supervisor *supervisor,
                            struct pablo_service *service) {
    if (!service->after[0]) return 1;
    struct pablo_service *dependency = pablo_service_find(supervisor->config,
                                                          service->after);
    if (dependency->state == PABLO_FAILED) {
        service->state = PABLO_FAILED;
        pablo_log_service(PABLO_LOG_ERROR, service->name, "dependency failed");
        return 0;
    }
    return dependency->state == PABLO_RUNNING || dependency->state == PABLO_COMPLETE;
}

static void spawn_service(struct pablo_service *service) {
    char *arguments[PABLO_ARG_MAX + 2];
    arguments[0] = service->path;
    for (size_t i = 0; i < service->argument_count; i++)
        arguments[i + 1] = service->arguments[i];
    arguments[service->argument_count + 1] = 0;

    pid_t pid = fork();
    if (pid < 0) {
        service->state = PABLO_BACKOFF;
        pablo_log_service(PABLO_LOG_ERROR, service->name, "fork failed");
        return;
    }
    if (!pid) {
        execve(service->path, arguments, child_environment);
        _exit(127);
    }
    service->pid = pid;
    service->state = PABLO_RUNNING;
    pablo_log_service(PABLO_LOG_INFO, service->name, "started");
}

static struct pablo_service *service_by_pid(struct pablo_config *config, pid_t pid) {
    for (size_t i = 0; i < config->service_count; i++)
        if (config->services[i].state == PABLO_RUNNING &&
            config->services[i].pid == pid)
            return &config->services[i];
    return 0;
}

static void record_exit(struct pablo_supervisor *supervisor, pid_t pid, int status) {
    struct pablo_service *service = service_by_pid(supervisor->config, pid);
    if (!service) {
        pablo_log_status(PABLO_LOG_WARN, "orphan", "reaped", pid);
        return;
    }
    service->pid = 0;
    service->exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
    if (service->policy == PABLO_ONCE) {
        service->state = service->exit_status ? PABLO_FAILED : PABLO_COMPLETE;
        pablo_log_status(service->exit_status ? PABLO_LOG_ERROR : PABLO_LOG_INFO,
                         service->name, "exited", service->exit_status);
        return;
    }
    if (service->restart_count >= supervisor->config->restart_limit) {
        service->state = PABLO_FAILED;
        pablo_log_service(PABLO_LOG_ERROR, service->name, "restart limit reached");
        return;
    }
    service->restart_count++;
    service->ready_tick = supervisor->tick + supervisor->config->restart_delay;
    service->state = PABLO_BACKOFF;
    pablo_log_status(PABLO_LOG_WARN, service->name, "stopped", service->exit_status);
}

static void reap_children(struct pablo_supervisor *supervisor) {
    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            record_exit(supervisor, pid, status);
            continue;
        }
        if (pid < 0 && errno != ECHILD)
            pablo_log(PABLO_LOG_ERROR, "waitpid failed");
        return;
    }
}

static int terminal(struct pablo_config *config) {
    for (size_t i = 0; i < config->service_count; i++)
        if (config->services[i].state == PABLO_RUNNING ||
            config->services[i].state == PABLO_STOPPED ||
            config->services[i].state == PABLO_BACKOFF)
            return 0;
    return 1;
}

static void start_ready(struct pablo_supervisor *supervisor) {
    for (size_t i = 0; i < supervisor->config->service_count; i++) {
        struct pablo_service *service = &supervisor->config->services[i];
        if (service->state != PABLO_STOPPED && service->state != PABLO_BACKOFF)
            continue;
        if (service->state == PABLO_BACKOFF && service->ready_tick > supervisor->tick)
            continue;
        if (!dependency_ready(supervisor, service)) continue;
        spawn_service(service);
        if (service->state == PABLO_BACKOFF)
            service->ready_tick = supervisor->tick + supervisor->config->restart_delay;
    }
}

void pablo_supervisor_init(struct pablo_supervisor *supervisor,
                           struct pablo_config *config) {
    supervisor->config = config;
    supervisor->tick = 0;
    supervisor->quiescent_reported = 0;
}

void pablo_supervisor_run(struct pablo_supervisor *supervisor) {
    for (;;) {
        reap_children(supervisor);
        start_ready(supervisor);
        if (!supervisor->quiescent_reported && terminal(supervisor->config)) {
            pablo_log(PABLO_LOG_INFO, "service graph settled");
            supervisor->quiescent_reported = 1;
        }
        supervisor->tick++;
        sched_yield();
    }
}
