#include <narcutils/system.h>
#include <stdarg.h>

const char *program_name = "narcutils";

void set_program_name(const char *argv0) {
    if (argv0 && *argv0) program_name = argv0;
}

static void report(int errnum, const char *format, va_list arguments) {
    fflush(stdout);
    fprintf(stderr, "%s: ", program_name);
    vfprintf(stderr, format, arguments);
    if (errnum) fprintf(stderr, ": %s", strerror(errnum));
    fputc('\n', stderr);
    fflush(stderr);
}

void error(int status, int errnum, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    report(errnum, format, arguments);
    va_end(arguments);
    if (status) exit(status);
}

void die(int status, int errnum, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    report(errnum, format, arguments);
    va_end(arguments);
    exit(status ? status : EXIT_FAILURE);
}

void close_stdout(void) {
    int failed = ferror(stdout);
    int saved = errno;
    if (fclose(stdout) != 0 && !(errno == EBADF && !failed)) {
        failed = 1;
        saved = errno;
    }
    if (failed) {
        fprintf(stderr, "%s: write error", program_name);
        if (saved && saved != EBADF) fprintf(stderr, ": %s", strerror(saved));
        fputc('\n', stderr);
        _exit(EXIT_FAILURE);
    }
    if (fclose(stderr) != 0) _exit(EXIT_FAILURE);
}

void initialize_main(int *argc, char ***argv) {
    (void)argc;
    set_program_name((*argv)[0]);
    atexit(close_stdout);
}
