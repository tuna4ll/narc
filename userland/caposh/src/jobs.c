#include <caposh/error.h>
#include <caposh/jobs.h>
#include <caposh/memory.h>
#include <caposh/options.h>
#include <caposh/shell.h>
#include <caposh/trap.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static struct job *jobs;
static struct job *current, *previous;
static int monitor;
static int terminal = -1;
static pid_t shell_group;
static struct termios shell_modes;
static int shell_modes_valid;

int wait_status_code(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    if (WIFSTOPPED(status)) return 128 + WSTOPSIG(status);
    return 0;
}

int jobs_monitor(void) {
    return monitor;
}

int jobs_terminal(void) {
    return terminal;
}

void jobs_disable(void) {
    monitor = 0;
    option_set(OPTION_MONITOR, 0);
}

void jobs_init(void) {
    if (!shell.interactive) return;
    int fd = isatty(0) ? 0 : isatty(2) ? 2 : -1;
    if (fd < 0) return;
    terminal = fcntl(fd, F_DUPFD_CLOEXEC, 10);
    if (terminal < 0) return;
    for (;;) {
        pid_t foreground = tcgetpgrp(terminal);
        if (foreground < 0) {
            fprintf(stderr, "%s: cannot set terminal process group (%s)\n", shell.name,
                    strerror(errno));
            fprintf(stderr, "%s: no job control in this shell\n", shell.name);
            close(terminal);
            terminal = -1;
            return;
        }
        if (foreground == getpgrp()) break;
        kill(-getpgrp(), SIGTTIN);
    }
    shell_group = getpid();
    if (getpgrp() != shell_group && setpgid(0, shell_group) != 0) shell_group = getpgrp();
    tcsetpgrp(terminal, shell_group);
    shell_modes_valid = tcgetattr(terminal, &shell_modes) == 0;
    monitor = 1;
    option_set(OPTION_MONITOR, 1);
}

static int next_id(void) {
    for (int id = 1;; id++) {
        int used = 0;
        for (struct job *job = jobs; job && !used; job = job->next) used = job->id == id;
        if (!used) return id;
    }
}

struct job *job_new(const char *command) {
    struct job *job = xcalloc(1, sizeof(*job));
    job->command = xstrdup(command ? command : "");
    job->id = next_id();
    struct job **link = &jobs;
    while (*link) link = &(*link)->next;
    *link = job;
    return job;
}

static void add_process(struct job *job, pid_t pid) {
    if (job->count == job->capacity) {
        job->capacity = job->capacity ? job->capacity * 2 : 4;
        job->processes = xrealloc(job->processes, (size_t)job->capacity * sizeof(*job->processes));
    }
    job->processes[job->count++] = (struct process) { pid, 0, PROCESS_RUNNING };
}

pid_t job_fork(struct job *job, int foreground) {
    pid_t pid = fork();
    if (pid < 0) {
        sh_error("fork: %s", strerror(errno));
    }
    if (pid == 0) {
        shell.subshell = 1;
        if (monitor) {
            pid_t group = job->pgid ? job->pgid : getpid();
            setpgid(0, group);
            if (foreground) tcsetpgrp(terminal, group);
        }
        signals_reset_child(!foreground && !monitor);
        if (!foreground && !monitor) {
            int null = open("/dev/null", O_RDONLY);
            if (null >= 0) {
                dup2(null, 0);
                close(null);
            }
        }
        jobs_forget();
        shell.interactive = 0;
        traps_reset_subshell();
        return 0;
    }
    if (monitor) {
        if (!job->pgid) job->pgid = pid;
        setpgid(pid, job->pgid);
    }
    add_process(job, pid);
    return pid;
}

int job_is_done(const struct job *job) {
    for (int i = 0; i < job->count; i++)
        if (job->processes[i].state != PROCESS_DONE) return 0;
    return 1;
}

int job_is_stopped(const struct job *job) {
    int stopped = 0;
    for (int i = 0; i < job->count; i++) {
        if (job->processes[i].state == PROCESS_RUNNING) return 0;
        stopped |= job->processes[i].state == PROCESS_STOPPED;
    }
    return stopped;
}

int job_status(const struct job *job) {
    if (!job->count) return 0;
    if (option_enabled(OPTION_PIPEFAIL)) {
        for (int i = job->count - 1; i >= 0; i--) {
            int code = wait_status_code(job->processes[i].status);
            if (code) return code;
        }
        return 0;
    }
    for (int i = job->count - 1; i >= 0; i--)
        if (job->processes[i].state == PROCESS_STOPPED)
            return wait_status_code(job->processes[i].status);
    return wait_status_code(job->processes[job->count - 1].status);
}

struct job *job_by_pid(pid_t pid) {
    for (struct job *job = jobs; job; job = job->next)
        for (int i = 0; i < job->count; i++)
            if (job->processes[i].pid == pid) return job;
    return 0;
}

static void record(pid_t pid, int status) {
    struct job *job = job_by_pid(pid);
    if (!job) return;
    for (int i = 0; i < job->count; i++) {
        struct process *process = &job->processes[i];
        if (process->pid != pid) continue;
        if (WIFSTOPPED(status)) {
            process->state = PROCESS_STOPPED;
            process->status = status;
        } else if (WIFCONTINUED(status)) {
            process->state = PROCESS_RUNNING;
        } else {
            process->state = PROCESS_DONE;
            process->status = status;
        }
    }
    job->changed = 1;
    job->notified = 0;
}

void jobs_update(int block) {
    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status, (block ? 0 : WNOHANG) | WUNTRACED | WCONTINUED);
        if (pid < 0 && errno == EINTR) continue;
        if (pid <= 0) return;
        record(pid, status);
        if (block) return;
    }
}

static void set_current(struct job *job) {
    if (current == job) return;
    previous = current;
    current = job;
}

void job_remove(struct job *job) {
    struct job **link = &jobs;
    while (*link && *link != job) link = &(*link)->next;
    if (*link) *link = job->next;
    if (current == job) {
        current = previous;
        previous = 0;
    }
    if (previous == job) previous = 0;
    if (!current) {
        for (struct job *candidate = jobs; candidate; candidate = candidate->next)
            if (candidate != previous) current = candidate;
    }
    free(job->processes);
    free(job->command);
    free(job);
}

int job_current_mark(const struct job *job) {
    return job == current ? '+' : job == previous ? '-' : ' ';
}

static const char *state_text(const struct job *job, char *buffer, size_t size) {
    if (!job_is_done(job)) return job_is_stopped(job) ? "Stopped" : "Running";
    int status = job->processes[job->count - 1].status;
    if (WIFSIGNALED(status)) {
        snprintf(buffer, size, "%s%s", strsignal(WTERMSIG(status)),
                 WCOREDUMP(status) ? " (core dumped)" : "");
        return buffer;
    }
    int code = WEXITSTATUS(status);
    if (!code) return "Done";
    snprintf(buffer, size, "Exit %d", code);
    return buffer;
}

void jobs_print(struct job *job, int format) {
    if (format == 2) {
        printf("%d\n", (int)(job->pgid ? job->pgid : job->processes[0].pid));
        return;
    }
    char buffer[64];
    const char *state = state_text(job, buffer, sizeof(buffer));
    if (format == 1) {
        printf("[%d]%c %d %-22s %s%s\n", job->id, job_current_mark(job),
               (int)job->processes[0].pid, state, job->command,
               job_is_done(job) || job_is_stopped(job) ? "" : " &");
        return;
    }
    printf("[%d]%c  %-22s  %s%s\n", job->id, job_current_mark(job), state, job->command,
           job_is_done(job) || job_is_stopped(job) ? "" : " &");
}

void job_started_background(struct job *job) {
    shell.last_background = job->count ? job->processes[job->count - 1].pid : 0;
    set_current(job);
    if (shell.interactive && monitor) fprintf(stderr, "[%d] %d\n", job->id, (int)shell.last_background);
}

static void report_signal(const struct job *job) {
    int status = job->processes[job->count - 1].status;
    if (!WIFSIGNALED(status)) return;
    int signal = WTERMSIG(status);
    if (signal == SIGINT) {
        if (shell.interactive) fputc('\n', stderr);
        return;
    }
    if (signal == SIGPIPE) return;
    fprintf(stderr, "%s%s\n", strsignal(signal), WCOREDUMP(status) ? " (core dumped)" : "");
}

int job_wait(struct job *job) {
    if (monitor && job->pgid) tcsetpgrp(terminal, job->pgid);
    while (!job_is_done(job) && !job_is_stopped(job)) {
        int status;
        pid_t pid = waitpid(-1, &status, WUNTRACED);
        if (pid < 0) {
            if (errno == EINTR) continue;
            break;
        }
        record(pid, status);
    }
    if (monitor) {
        tcsetpgrp(terminal, shell_group);
        if (job_is_stopped(job)) job->has_modes = tcgetattr(terminal, &job->modes) == 0;
        if (shell_modes_valid) tcsetattr(terminal, TCSADRAIN, &shell_modes);
    }
    int status = job_status(job);
    if (job_is_stopped(job)) {
        set_current(job);
        job->notified = 1;
        job->changed = 0;
        fputc('\n', stderr);
        fflush(stdout);
        fprintf(stderr, "[%d]%c  %-22s  %s\n", job->id, job_current_mark(job), "Stopped",
                job->command);
        return status;
    }
    report_signal(job);
    job_remove(job);
    return status;
}

int job_continue(struct job *job, int foreground) {
    for (int i = 0; i < job->count; i++)
        if (job->processes[i].state == PROCESS_STOPPED) job->processes[i].state = PROCESS_RUNNING;
    job->notified = 0;
    if (foreground) {
        printf("%s\n", job->command);
        fflush(stdout);
        if (monitor) {
            tcsetpgrp(terminal, job->pgid);
            if (job->has_modes) tcsetattr(terminal, TCSADRAIN, &job->modes);
        }
    } else {
        set_current(job);
        printf("[%d]%c %s &\n", job->id, job_current_mark(job), job->command);
        fflush(stdout);
    }
    if (monitor && job->pgid) {
        kill(-job->pgid, SIGCONT);
    } else {
        for (int i = 0; i < job->count; i++) kill(job->processes[i].pid, SIGCONT);
    }
    if (foreground) return job_wait(job);
    return 0;
}

void jobs_notify(void) {
    jobs_update(0);
    struct job *job = jobs;
    while (job) {
        struct job *next = job->next;
        if (job_is_done(job)) {
            if (shell.interactive && monitor) jobs_print(job, 0);
            job_remove(job);
        } else if (job->changed && job_is_stopped(job) && !job->notified) {
            if (shell.interactive && monitor) jobs_print(job, 0);
            job->notified = 1;
            job->changed = 0;
        }
        job = next;
    }
}

struct job *jobs_list(void) {
    return jobs;
}

int jobs_stopped(void) {
    int count = 0;
    for (struct job *job = jobs; job; job = job->next) count += job_is_stopped(job);
    return count;
}

void jobs_forget(void) {
    struct job *job = jobs;
    while (job) {
        struct job *next = job->next;
        free(job->processes);
        free(job->command);
        free(job);
        job = next;
    }
    jobs = current = previous = 0;
    monitor = 0;
}

void jobs_hangup(void) {
    for (struct job *job = jobs; job; job = job->next) {
        if (!job_is_stopped(job)) continue;
        if (job->pgid) {
            kill(-job->pgid, SIGHUP);
            kill(-job->pgid, SIGCONT);
        }
    }
}

struct job *job_find(const char *spec, const char *context) {
    struct job *found = 0;
    if (!spec || !strcmp(spec, "%%") || !strcmp(spec, "%+") || !strcmp(spec, "%")) {
        found = current;
    } else if (!strcmp(spec, "%-")) {
        found = previous;
    } else if (spec[0] == '%' && spec[1] >= '0' && spec[1] <= '9') {
        int id = atoi(spec + 1);
        for (struct job *job = jobs; job && !found; job = job->next)
            if (job->id == id) found = job;
    } else if (spec[0] == '%' && spec[1] == '?') {
        for (struct job *job = jobs; job; job = job->next) {
            if (!strstr(job->command, spec + 2)) continue;
            if (found) {
                fprintf(stderr, "%s: %s: %s: ambiguous job spec\n", shell.name, context, spec);
                return 0;
            }
            found = job;
        }
    } else if (spec[0] == '%') {
        size_t length = strlen(spec + 1);
        for (struct job *job = jobs; job; job = job->next) {
            if (strncmp(job->command, spec + 1, length)) continue;
            if (found) {
                fprintf(stderr, "%s: %s: %s: ambiguous job spec\n", shell.name, context, spec);
                return 0;
            }
            found = job;
        }
    } else {
        pid_t pid = (pid_t)atoi(spec);
        found = pid > 0 ? job_by_pid(pid) : 0;
    }
    if (!found)
        fprintf(stderr, "%s: %s: %s: no such job\n", shell.name, context, spec ? spec : "current");
    return found;
}
