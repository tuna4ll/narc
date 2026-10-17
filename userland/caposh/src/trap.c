#include <caposh/error.h>
#include <caposh/exec.h>
#include <caposh/jobs.h>
#include <caposh/memory.h>
#include <caposh/shell.h>
#include <caposh/trap.h>
#include <ctype.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

volatile sig_atomic_t interrupted;

static char *actions[NSIG];
static volatile sig_atomic_t pending[NSIG];
static volatile sig_atomic_t any_pending;
static int ignored_at_entry[NSIG];
static int exit_trap_ran;

static const char *const names[NSIG] = {
    [0] = "EXIT", [SIGHUP] = "HUP", [SIGINT] = "INT", [SIGQUIT] = "QUIT", [SIGILL] = "ILL",
    [SIGTRAP] = "TRAP", [SIGABRT] = "ABRT", [SIGBUS] = "BUS", [SIGFPE] = "FPE",
    [SIGKILL] = "KILL", [SIGUSR1] = "USR1", [SIGSEGV] = "SEGV", [SIGUSR2] = "USR2",
    [SIGPIPE] = "PIPE", [SIGALRM] = "ALRM", [SIGTERM] = "TERM", [SIGSTKFLT] = "STKFLT",
    [SIGCHLD] = "CHLD", [SIGCONT] = "CONT", [SIGSTOP] = "STOP", [SIGTSTP] = "TSTP",
    [SIGTTIN] = "TTIN", [SIGTTOU] = "TTOU", [SIGURG] = "URG", [SIGXCPU] = "XCPU",
    [SIGXFSZ] = "XFSZ", [SIGVTALRM] = "VTALRM", [SIGPROF] = "PROF", [SIGWINCH] = "WINCH",
    [SIGIO] = "IO", [SIGPWR] = "PWR", [SIGSYS] = "SYS",
};

const char *signal_name(int signal) {
    return signal >= 0 && signal < NSIG && names[signal] ? names[signal] : 0;
}

int signal_number(const char *name) {
    if (isdigit((unsigned char)*name)) {
        char *end;
        long value = strtol(name, &end, 10);
        return *end || value < 0 || value >= NSIG ? -1 : (int)value;
    }
    if (!strncasecmp(name, "SIG", 3)) name += 3;
    for (int signal = 0; signal < NSIG; signal++)
        if (names[signal] && !strcasecmp(names[signal], name)) return signal;
    return -1;
}

static void catch(int signal) {
    pending[signal] = 1;
    any_pending = 1;
    if (signal == SIGINT) interrupted = 1;
}

static void install(int signal, void (*handler)(int)) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    sigaction(signal, &action, 0);
}

static void default_disposition(int signal) {
    if (ignored_at_entry[signal] && !shell.interactive) {
        install(signal, SIG_IGN);
        return;
    }
    if (shell.interactive && !shell.subshell) {
        if (signal == SIGINT) {
            install(signal, catch);
            return;
        }
        if (signal == SIGQUIT || signal == SIGTERM) {
            install(signal, SIG_IGN);
            return;
        }
        if (jobs_monitor() && (signal == SIGTSTP || signal == SIGTTIN || signal == SIGTTOU)) {
            install(signal, SIG_IGN);
            return;
        }
    }
    install(signal, SIG_DFL);
}

void signals_capture(void) {
    for (int signal = 1; signal < NSIG; signal++) {
        if (signal == SIGKILL || signal == SIGSTOP) continue;
        struct sigaction old;
        if (sigaction(signal, 0, &old) == 0 && old.sa_handler == SIG_IGN)
            ignored_at_entry[signal] = 1;
    }
}

void signals_init(void) {
    int handled[] = { SIGINT, SIGQUIT, SIGTERM, SIGTSTP, SIGTTIN, SIGTTOU };
    for (size_t i = 0; i < sizeof(handled) / sizeof(handled[0]); i++)
        default_disposition(handled[i]);
}

void signals_reset_child(int background) {
    int handled[] = { SIGINT, SIGQUIT, SIGTERM, SIGTSTP, SIGTTIN, SIGTTOU, SIGCHLD };
    for (size_t i = 0; i < sizeof(handled) / sizeof(handled[0]); i++) {
        int signal = handled[i];
        if (actions[signal] && !*actions[signal]) continue;
        install(signal, ignored_at_entry[signal] ? SIG_IGN : SIG_DFL);
    }
    if (background) {
        install(SIGINT, SIG_IGN);
        install(SIGQUIT, SIG_IGN);
    }
}

int trap_set(int signal, const char *action) {
    if (signal < 0 || signal >= NSIG || signal == SIGKILL || signal == SIGSTOP) return -1;
    if (signal && ignored_at_entry[signal] && !shell.interactive) return 0;
    free(actions[signal]);
    actions[signal] = action ? xstrdup(action) : 0;
    if (!signal) return 0;
    if (!action) default_disposition(signal);
    else if (!*action) install(signal, SIG_IGN);
    else install(signal, catch);
    return 0;
}

const char *trap_action(int signal) {
    return signal >= 0 && signal < NSIG ? actions[signal] : 0;
}

int trap_pending(void) {
    return any_pending;
}

static void run_trap(int signal) {
    if (signal == SIGINT) interrupted = 0;
    int saved = shell.status;
    shell.evaluating_trap++;
    struct handler frame;
    int kind = TRY(&frame);
    if (!kind) evaluate_string(actions[signal], 0, 0);
    END_TRY(&frame);
    shell.evaluating_trap--;
    if (kind == EXCEPTION_EXIT) raise_exception(kind);
    shell.status = saved;
}

void trap_run_pending(void) {
    if (shell.evaluating_trap) return;
    while (any_pending) {
        any_pending = 0;
        for (int signal = 1; signal < NSIG; signal++) {
            if (!pending[signal]) continue;
            pending[signal] = 0;
            if (actions[signal] && *actions[signal]) run_trap(signal);
        }
    }
}

void trap_run_exit(void) {
    if (exit_trap_ran || !actions[TRAP_EXIT]) return;
    exit_trap_ran = 1;
    char *action = actions[TRAP_EXIT];
    actions[TRAP_EXIT] = 0;
    int saved = shell.status;
    struct handler frame;
    int kind = TRY(&frame);
    if (!kind) evaluate_string(action, 0, 0);
    END_TRY(&frame);
    if (kind != EXCEPTION_EXIT) shell.status = saved;
    free(action);
}

void traps_reset_subshell(void) {
    for (int signal = 0; signal < NSIG; signal++) {
        if (!actions[signal] || !*actions[signal]) continue;
        free(actions[signal]);
        actions[signal] = 0;
        if (signal) install(signal, SIG_DFL);
    }
    exit_trap_ran = 0;
}

void check_interrupt(void) {
    if (!interrupted) return;
    if (actions[SIGINT] && *actions[SIGINT]) {
        trap_run_pending();
        return;
    }
    interrupted = 0;
    pending[SIGINT] = 0;
    if (shell.interactive) {
        exception_status = 130;
        raise_exception(EXCEPTION_INTERRUPT);
    }
}
