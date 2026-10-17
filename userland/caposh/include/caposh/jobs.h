#pragma once

#include <sys/types.h>
#include <termios.h>

enum process_state { PROCESS_RUNNING, PROCESS_STOPPED, PROCESS_DONE };

struct process {
    pid_t pid;
    int status;
    enum process_state state;
};

struct job {
    struct job *next;
    int id;
    pid_t pgid;
    struct process *processes;
    int count, capacity;
    char *command;
    int notified;
    int changed;
    struct termios modes;
    int has_modes;
};

void jobs_init(void);
int jobs_monitor(void);
void jobs_disable(void);
int jobs_terminal(void);
struct job *job_new(const char *command);
pid_t job_fork(struct job *job, int foreground);
int job_wait(struct job *job);
void job_started_background(struct job *job);
int job_continue(struct job *job, int foreground);
void job_remove(struct job *job);
int job_status(const struct job *job);
int job_is_stopped(const struct job *job);
int job_is_done(const struct job *job);
struct job *job_find(const char *spec, const char *context);
struct job *job_by_pid(pid_t pid);
struct job *jobs_list(void);
void jobs_update(int block);
void jobs_notify(void);
void jobs_print(struct job *job, int format);
int jobs_stopped(void);
void jobs_forget(void);
void jobs_hangup(void);
int job_current_mark(const struct job *job);
int wait_status_code(int status);
