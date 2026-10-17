#include <caposh/builtins.h>
#include <caposh/error.h>
#include <caposh/input.h>
#include <caposh/parser.h>
#include <caposh/strbuf.h>
#include <caposh/trap.h>
#include <caposh/vars.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

struct options {
    int raw, silent, fd, count, poll;
    char delimiter;
    const char *prompt;
};

static int zero_timeout(const char *text) {
    for (; *text; text++)
        if (*text != '0' && *text != '.') return 0;
    return 1;
}

static int parse(int argc, char **argv, struct options *options) {
    int option;
    while ((option = getopt(argc, argv, "ersd:n:N:p:t:u:")) != -1) {
        switch (option) {
        case 'e': break;
        case 'r': options->raw = 1; break;
        case 's': options->silent = 1; break;
        case 'd': options->delimiter = optarg[0]; break;
        case 'n': case 'N': options->count = atoi(optarg); break;
        case 'p': options->prompt = optarg; break;
        case 't': options->poll = zero_timeout(optarg); break;
        case 'u':
            options->fd = atoi(optarg);
            if (fcntl(options->fd, F_GETFD) >= 0) break;
            builtin_error("read", "%s: invalid file descriptor", optarg);
            return 1;
        default: return builtin_usage("read");
        }
    }
    return 0;
}

static int ifs_space(char c, const char *ifs) {
    return (c == ' ' || c == '\t' || c == '\n') && strchr(ifs, c);
}

static void assign_fields(char **names, int count, const char *line, const char *protect) {
    const char *ifs = var_get("IFS");
    if (!ifs) ifs = " \t\n";
    size_t length = strlen(line), i = 0;
    while (i < length && !protect[i] && ifs_space(line[i], ifs)) i++;
    for (int n = 0; n < count; n++) {
        struct strbuf field;
        strbuf_init(&field, stack);
        if (n == count - 1) {
            size_t end = length;
            while (end > i && !protect[end - 1] && ifs_space(line[end - 1], ifs)) end--;
            strbuf_append(&field, line + i, end > i ? end - i : 0);
            var_set(names[n], strbuf_string(&field), 0);
            return;
        }
        while (i < length && (protect[i] || !strchr(ifs, line[i]))) strbuf_putc(&field, line[i++]);
        if (i < length) {
            int hard = !ifs_space(line[i], ifs);
            i++;
            while (i < length && !protect[i] && ifs_space(line[i], ifs)) i++;
            if (!hard && i < length && !protect[i] && strchr(ifs, line[i])) {
                i++;
                while (i < length && !protect[i] && ifs_space(line[i], ifs)) i++;
            }
        }
        var_set(names[n], strbuf_string(&field), 0);
    }
}

int builtin_read(int argc, char **argv) {
    struct options options = { 0, 0, 0, 0, 0, '\n', 0 };
    int status = parse(argc, argv, &options);
    if (status) return status;
    char *reply[] = { "REPLY", 0 };
    char **names = optind < argc ? argv + optind : reply;
    int name_count = optind < argc ? argc - optind : 1;
    for (int i = 0; i < name_count; i++) {
        if (!is_name(names[i])) {
            builtin_error("read", "`%s': not a valid identifier", names[i]);
            return 1;
        }
    }
    int fd = options.fd;
    if (options.poll) {
        int available = 0;
        return ioctl(fd, FIONREAD, &available) == 0 && available > 0 ? 0 : 1;
    }
    struct termios saved;
    int restore = 0;
    if ((options.silent || options.count) && tcgetattr(fd, &saved) == 0) {
        struct termios modes = saved;
        if (options.silent) modes.c_lflag &= ~(tcflag_t)ECHO;
        if (options.count) {
            modes.c_lflag &= ~(tcflag_t)ICANON;
            modes.c_cc[VMIN] = 1;
            modes.c_cc[VTIME] = 0;
        }
        tcsetattr(fd, TCSADRAIN, &modes);
        restore = 1;
    }
    if (options.prompt && isatty(fd)) {
        fputs(options.prompt, stderr);
        fflush(stderr);
    }
    input_sync();
    struct strbuf line, protect;
    strbuf_init(&line, stack);
    strbuf_init(&protect, stack);
    int eof = 0, got = 0;
    for (;;) {
        char c;
        ssize_t result = read(fd, &c, 1);
        if (result < 0 && errno == EINTR) {
            if (trap_pending() || interrupted) {
                eof = 1;
                break;
            }
            continue;
        }
        if (result <= 0) {
            eof = 1;
            break;
        }
        if (c == options.delimiter) break;
        if (c == '\\' && !options.raw) {
            char next;
            if (read(fd, &next, 1) != 1) {
                eof = 1;
                break;
            }
            if (next == '\n') continue;
            strbuf_putc(&line, next);
            strbuf_putc(&protect, 1);
        } else {
            strbuf_putc(&line, c);
            strbuf_putc(&protect, 0);
        }
        if (options.count && ++got >= options.count) break;
    }
    if (restore) {
        tcsetattr(fd, TCSADRAIN, &saved);
        if (options.silent) fputc('\n', stderr);
    }
    char *text = strbuf_string(&line);
    char *flags = strbuf_string(&protect);
    if (names == reply) var_set("REPLY", text, 0);
    else assign_fields(names, name_count, text, flags);
    return eof ? 1 : 0;
}
