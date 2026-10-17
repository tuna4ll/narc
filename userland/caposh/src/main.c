#include <caposh/error.h>
#include <caposh/exec.h>
#include <caposh/history.h>
#include <caposh/input.h>
#include <caposh/jobs.h>
#include <caposh/lineedit.h>
#include <caposh/options.h>
#include <caposh/redirect.h>
#include <caposh/run.h>
#include <caposh/shell.h>
#include <caposh/strbuf.h>
#include <caposh/trap.h>
#include <caposh/vars.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct shell shell;

struct invocation {
    const char *command;
    const char *script;
    int force_interactive;
    int read_stdin;
    int norc;
    int noprofile;
};

static void usage(FILE *stream) {
    fprintf(stream,
            "Usage: caposh [option ...] [script [argument ...]]\n"
            "       caposh [option ...] -c command [name [argument ...]]\n"
            "\n"
            "Options:\n"
            "  -c command      read commands from the command string\n"
            "  -i              force an interactive shell\n"
            "  -l, --login     act as a login shell\n"
            "  -s              read commands from standard input\n"
            "  -o option       enable a named option (see `set -o')\n"
            "  -abCefhmnuvx    set single-letter options, `+' clears them\n"
            "  --norc          do not read ~/.caposhrc\n"
            "  --noprofile     do not read login profiles\n"
            "  --version       print version information\n"
            "  --help          print this help\n");
}

static char *history_path(void) {
    const char *file = var_get("HISTFILE");
    if (file) return *file ? xstrdup(file) : 0;
    const char *home = var_get("HOME");
    if (!home) return 0;
    struct strbuf path;
    strbuf_init(&path, 0);
    strbuf_printf(&path, "%s/.caposh_history", strcmp(home, "/") ? home : "");
    return strbuf_string(&path);
}

_Noreturn void shell_exit(int status) {
    shell.status = status;
    trap_run_exit();
    if (shell.interactive && !shell.subshell) {
        char *path = history_path();
        if (path) history_save(path);
        free(path);
        jobs_hangup();
    }
    lineedit_restore_terminal();
    fflush(stdout);
    fflush(stderr);
    exit(shell.status & 0xff);
}

static int parse_options(int argc, char **argv, struct invocation *invocation) {
    int index = 1;
    for (; index < argc; index++) {
        char *argument = argv[index];
        if (!strcmp(argument, "--")) {
            index++;
            break;
        }
        if (!strcmp(argument, "--login")) {
            shell.login = 1;
            continue;
        }
        if (!strcmp(argument, "--norc")) {
            invocation->norc = 1;
            continue;
        }
        if (!strcmp(argument, "--noprofile")) {
            invocation->noprofile = 1;
            continue;
        }
        if (!strcmp(argument, "--version")) {
            printf("caposh %s (narc)\n", CAPOSH_VERSION);
            exit(0);
        }
        if (!strcmp(argument, "--help")) {
            usage(stdout);
            exit(0);
        }
        if ((argument[0] != '-' && argument[0] != '+') || !argument[1]) break;
        int enable = argument[0] == '-';
        for (char *flag = argument + 1; *flag; flag++) {
            switch (*flag) {
            case 'c':
                if (!enable) goto invalid;
                invocation->command = "";
                break;
            case 'i':
                invocation->force_interactive = enable;
                break;
            case 'l':
                shell.login = enable;
                break;
            case 's':
                invocation->read_stdin = enable;
                break;
            case 'o': {
                if (index + 1 >= argc) {
                    option_print(!enable);
                    break;
                }
                int option = option_by_name(argv[++index]);
                if (option < 0) {
                    fprintf(stderr, "caposh: %s: invalid option name\n", argv[index]);
                    exit(2);
                }
                option_set(option, enable);
                break;
            }
            default: {
                int option = option_by_letter(*flag);
                if (option < 0) goto invalid;
                option_set(option, enable);
                break;
            }
            }
            continue;
        invalid:
            fprintf(stderr, "caposh: %c%c: invalid option\n", enable ? '-' : '+', *flag);
            usage(stderr);
            exit(2);
        }
    }
    if (invocation->command) {
        if (index >= argc) {
            fprintf(stderr, "caposh: -c: option requires an argument\n");
            exit(2);
        }
        invocation->command = argv[index++];
        if (index < argc) shell.name = argv[index++];
    } else if (!invocation->read_stdin && index < argc) {
        invocation->script = argv[index];
        shell.name = argv[index++];
    }
    return index;
}

static void source_if_present(const char *path) {
    if (access(path, R_OK) == 0) source_file(path, 0, 0, 1);
}

static void source_home(const char *name) {
    const char *home = var_get("HOME");
    if (!home) return;
    struct strbuf path;
    strbuf_init(&path, stack);
    strbuf_printf(&path, "%s/%s", strcmp(home, "/") ? home : "", name);
    source_if_present(strbuf_string(&path));
}

static void startup_files(const struct invocation *invocation) {
    struct handler frame;
    int kind = TRY(&frame);
    if (!kind) {
        if (shell.login && !invocation->noprofile) {
            source_if_present("/etc/profile");
            source_home(".caposh_profile");
            source_home(".profile");
        }
        if (shell.interactive && !invocation->norc) {
            source_if_present("/etc/caposhrc");
            source_home(".caposhrc");
            const char *env = var_get("ENV");
            if (env && *env) source_if_present(env);
        }
    }
    END_TRY(&frame);
    if (kind == EXCEPTION_EXIT) shell_exit(shell.status);
}

int main(int argc, char **argv, char **envp) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    shell.started = now.tv_sec;
    shell.pid = getpid();
    shell.name = argv[0] ? argv[0] : CAPOSH_NAME;
    if (shell.name[0] == '-') shell.login = 1;
    struct invocation invocation = { 0 };
    int index = parse_options(argc, argv, &invocation);
    shell.arguments = argv + index;
    shell.argument_count = argc - index;
    shell.interactive = invocation.force_interactive ||
                        (!invocation.command && !invocation.script && isatty(0) && isatty(2));
    shell.session_interactive = shell.interactive;
    const char *slash = strrchr(shell.name, '/');
    if (shell.interactive && slash) shell.name = slash + 1;
    vars_init(envp);
    history_init();
    signals_capture();
    signals_init();
    if (shell.interactive) {
        option_set(OPTION_MONITOR, 1);
        jobs_init();
        signals_init();
        char *path = history_path();
        if (path) history_load(path);
        free(path);
    }
    startup_files(&invocation);
    struct handler frame;
    int kind = TRY(&frame);
    if (!kind) {
        if (invocation.command) {
            input_push_string(invocation.command, 0);
            run_loop(1);
        } else if (invocation.script) {
            int fd = open(invocation.script, O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                fprintf(stderr, "%s: %s: %s\n", CAPOSH_NAME, invocation.script, strerror(errno));
                shell_exit(127);
            }
            fd = redirect_move_high(fd);
            shell.script = invocation.script;
            input_push_file(fd, invocation.script);
            run_loop(1);
        } else if (shell.interactive && isatty(0)) {
            input_push_interactive();
            run_loop(1);
        } else {
            input_push_file(0, 0);
            run_loop(1);
        }
    }
    END_TRY(&frame);
    if (kind && kind != EXCEPTION_EXIT) shell.status = exception_status;
    shell_exit(shell.status);
}
