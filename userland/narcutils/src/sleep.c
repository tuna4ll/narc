#include <math.h>
#include <narcutils/quote.h>
#include <narcutils/system.h>
#include <time.h>

#define PROGRAM_NAME "sleep"

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        emit_try_help();
        exit(status);
    }
    printf("Usage: %s NUMBER[SUFFIX]...\n"
           "  or:  %s OPTION\n",
           program_name, program_name);
    fputs("Pause for NUMBER seconds, where NUMBER is an integer or floating-point.\n"
          "SUFFIX may be 's','m','h', or 'd', for seconds, minutes, hours, days.\n"
          "With multiple arguments, pause for the sum of their values.\n\n",
          stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    exit(status);
}

static bool apply_suffix(double *value, char suffix) {
    int multiplier;
    switch (suffix) {
    case 0: case 's': multiplier = 1; break;
    case 'm': multiplier = 60; break;
    case 'h': multiplier = 60 * 60; break;
    case 'd': multiplier = 60 * 60 * 24; break;
    default: return false;
    }
    *value *= multiplier;
    return true;
}

static int sleep_for(double seconds) {
    while (seconds > 0) {
        double chunk = seconds > 1e9 ? 1e9 : seconds;
        struct timespec request;
        request.tv_sec = (time_t)chunk;
        request.tv_nsec = (long)((chunk - (double)request.tv_sec) * 1e9);
        if (request.tv_nsec >= 1000000000L) request.tv_nsec = 999999999L;
        if (request.tv_sec == 0 && request.tv_nsec == 0 && chunk > 0) request.tv_nsec = 1;
        struct timespec remaining;
        while (nanosleep(&request, &remaining) != 0) {
            if (errno != EINTR) return -1;
            request = remaining;
        }
        seconds -= chunk;
    }
    return 0;
}

int main(int argc, char **argv) {
    initialize_main(&argc, &argv);
    parse_gnu_standard_options_only(argc, argv, PROGRAM_NAME);
    if (argc == 1) {
        error(0, 0, "missing operand");
        usage(EXIT_FAILURE);
    }
    double seconds = 0.0;
    bool ok = true;
    for (int i = optind; i < argc; i++) {
        char *end;
        errno = 0;
        double value = strtod(argv[i], &end);
        if (end == argv[i] || isnan(value) || value < 0 || (*end && end[1]) ||
            !apply_suffix(&value, *end)) {
            error(0, 0, "invalid time interval %s", quote(argv[i]));
            ok = false;
            continue;
        }
        seconds += value;
    }
    if (!ok) usage(EXIT_FAILURE);
    if (sleep_for(seconds) != 0) die(EXIT_FAILURE, errno, "cannot read realtime clock");
    return EXIT_SUCCESS;
}
