#include <caposh/builtins.h>
#include <caposh/error.h>
#include <caposh/jobs.h>
#include <caposh/shell.h>
#include <caposh/trap.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int require_monitor(const char *name) {
    if (jobs_monitor()) return 1;
    builtin_error(name, "no job control");
    return 0;
}

int builtin_jobs(int argc, char **argv) {
    int option, format = 0, running = 0, stopped = 0;
    while ((option = getopt(argc, argv, "lnprs")) != -1) {
        if (option == 'l') format = 1;
        else if (option == 'p') format = 2;
        else if (option == 'r') running = 1;
        else if (option == 's') stopped = 1;
        else if (option == 'n') continue;
        else return builtin_usage("jobs");
    }
    jobs_update(0);
    int status = 0;
    if (optind < argc) {
        for (int i = optind; i < argc; i++) {
            struct job *job = job_find(argv[i], "jobs");
            if (job) jobs_print(job, format);
            else status = 1;
        }
        return status;
    }
    for (struct job *job = jobs_list(); job; job = job->next) {
        if (running && (job_is_stopped(job) || job_is_done(job))) continue;
        if (stopped && !job_is_stopped(job)) continue;
        jobs_print(job, format);
        job->notified = 1;
        job->changed = 0;
    }
    struct job *job = jobs_list();
    while (job) {
        struct job *next = job->next;
        if (job_is_done(job)) job_remove(job);
        job = next;
    }
    return 0;
}

int builtin_fg(int argc, char **argv) {
    if (!require_monitor("fg")) return 1;
    struct job *job = job_find(argc > 1 ? argv[1] : 0, "fg");
    if (!job) return 1;
    if (job_is_done(job)) {
        builtin_error("fg", "job has terminated");
        return 1;
    }
    return job_continue(job, 1);
}

int builtin_bg(int argc, char **argv) {
    if (!require_monitor("bg")) return 1;
    int status = 0;
    for (int i = 1; i < argc || i == 1; i++) {
        struct job *job = job_find(i < argc ? argv[i] : 0, "bg");
        if (!job) {
            status = 1;
            if (i >= argc) break;
            continue;
        }
        if (!job_is_stopped(job)) {
            builtin_error("bg", "job %d already in background", job->id);
        } else {
            job_continue(job, 0);
        }
        if (i >= argc) break;
    }
    return status;
}

int builtin_disown(int argc, char **argv) {
    int all = argc > 1 && !strcmp(argv[1], "-a");
    if (all) {
        struct job *job = jobs_list();
        while (job) {
            struct job *next = job->next;
            job_remove(job);
            job = next;
        }
        return 0;
    }
    int status = 0;
    for (int i = 1; i < argc || i == 1; i++) {
        struct job *job = job_find(i < argc ? argv[i] : 0, "disown");
        if (job) job_remove(job);
        else status = 1;
        if (i >= argc) break;
    }
    return status;
}

static int wait_job(struct job *job) {
    while (!job_is_done(job)) {
        if (trap_pending() || interrupted) return 128 + SIGINT;
        int status;
        pid_t pid = waitpid(-1, &status, WUNTRACED);
        if (pid < 0) {
            if (errno == EINTR) {
                if (trap_pending()) return 128 + SIGINT;
                continue;
            }
            break;
        }
        jobs_update(0);
        struct job *owner = job_by_pid(pid);
        if (owner) {
            for (int i = 0; i < owner->count; i++) {
                if (owner->processes[i].pid != pid) continue;
                owner->processes[i].state = WIFSTOPPED(status) ? PROCESS_STOPPED : PROCESS_DONE;
                owner->processes[i].status = status;
            }
            owner->changed = 1;
        }
        if (job_is_stopped(job)) break;
    }
    int status = job_status(job);
    if (job_is_done(job)) job_remove(job);
    return status;
}

int builtin_wait(int argc, char **argv) {
    int any = argc > 1 && !strcmp(argv[1], "-n");
    int index = any ? 2 : 1;
    if (index >= argc && !any) {
        struct job *job;
        while ((job = jobs_list())) {
            int status = wait_job(job);
            if (status > 128 && trap_pending()) return status;
            if (jobs_list() == job) job_remove(job);
        }
        return 0;
    }
    if (any) {
        for (;;) {
            struct job *job = jobs_list();
            if (!job) return 127;
            for (; job; job = job->next) {
                if (job_is_done(job)) {
                    int status = job_status(job);
                    job_remove(job);
                    return status;
                }
            }
            jobs_update(1);
            if (trap_pending()) return 128 + SIGINT;
        }
    }
    int status = 0;
    for (int i = index; i < argc; i++) {
        struct job *job;
        if (argv[i][0] == '%') {
            job = job_find(argv[i], "wait");
        } else {
            char *end;
            long pid = strtol(argv[i], &end, 10);
            if (*end || pid <= 0) {
                builtin_error("wait", "`%s': not a pid or valid job spec", argv[i]);
                status = 2;
                continue;
            }
            job = job_by_pid((pid_t)pid);
            if (!job) {
                int raw;
                if (waitpid((pid_t)pid, &raw, 0) == pid) {
                    status = wait_status_code(raw);
                    continue;
                }
                builtin_error("wait", "pid %ld is not a child of this shell", pid);
                status = 127;
                continue;
            }
        }
        status = job ? wait_job(job) : 127;
    }
    return status;
}

static void list_signals(void) {
    for (int signal = 1; signal < NSIG; signal++) {
        printf("%2d) SIG%-8s", signal, signal_name(signal));
        if (signal % 5 == 0 || signal == NSIG - 1) putchar('\n');
    }
}

int builtin_kill(int argc, char **argv) {
    int signal = SIGTERM, index = 1;
    if (argc < 2) return builtin_usage("kill");
    if (!strcmp(argv[1], "-l") || !strcmp(argv[1], "-L")) {
        if (argc == 2) {
            list_signals();
            return 0;
        }
        int status = 0;
        for (int i = 2; i < argc; i++) {
            if (isdigit((unsigned char)argv[i][0])) {
                int number = atoi(argv[i]);
                if (number > 128) number -= 128;
                const char *name = signal_name(number);
                if (name && number) printf("%s\n", name);
                else status = 1;
            } else {
                int number = signal_number(argv[i]);
                if (number > 0) printf("%d\n", number);
                else status = 1;
            }
        }
        return status;
    }
    if (!strcmp(argv[1], "-s") || !strcmp(argv[1], "-n")) {
        if (argc < 3) return builtin_usage("kill");
        signal = signal_number(argv[2]);
        index = 3;
    } else if (argv[1][0] == '-' && argv[1][1] && strcmp(argv[1], "--")) {
        signal = signal_number(argv[1] + 1);
        index = 2;
    } else if (!strcmp(argv[1], "--")) {
        index = 2;
    }
    if (signal < 0) {
        builtin_error("kill", "%s: invalid signal specification", argv[index - 1]);
        return 1;
    }
    if (index >= argc) return builtin_usage("kill");
    int status = 0;
    for (int i = index; i < argc; i++) {
        pid_t target;
        if (argv[i][0] == '%') {
            struct job *job = job_find(argv[i], "kill");
            if (!job) {
                status = 1;
                continue;
            }
            target = job->pgid ? -job->pgid : job->processes[0].pid;
            if (signal == SIGKILL || signal == SIGTERM || signal == SIGHUP) {
                if (job_is_stopped(job)) kill(target, SIGCONT);
            }
        } else {
            char *end;
            long value = strtol(argv[i], &end, 10);
            if (*end || !*argv[i]) {
                builtin_error("kill", "%s: arguments must be process or job IDs", argv[i]);
                status = 1;
                continue;
            }
            target = (pid_t)value;
        }
        if (kill(target, signal) != 0) {
            builtin_error("kill", "(%s) - %s", argv[i], strerror(errno));
            status = 1;
        }
    }
    return status;
}
