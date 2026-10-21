#include <narcutils/io.h>
#include <narcutils/system.h>

#define PROGRAM_NAME "yes"

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        emit_try_help();
        exit(status);
    }
    printf("Usage: %s [STRING]...\n"
           "  or:  %s OPTION\n",
           program_name, program_name);
    fputs("Repeatedly output a line with all specified STRING(s), or 'y'.\n\n", stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    exit(status);
}

int main(int argc, char **argv) {
    initialize_main(&argc, &argv);
    parse_gnu_standard_options_only(argc, argv, PROGRAM_NAME);
    char **operands = argv + optind;
    char *default_operand[] = { "y", 0 };
    if (optind == argc) operands = default_operand;

    size_t line_length = 0;
    for (char **op = operands; *op; op++) line_length += strlen(*op) + 1;
    size_t copies = line_length >= IO_BUFSIZE / 2 ? 1 : IO_BUFSIZE / 2 / line_length;
    size_t size = line_length * copies;
    char *buffer = xmalloc(size);
    char *cursor = buffer;
    for (char **op = operands; *op; op++) {
        size_t length = strlen(*op);
        memcpy(cursor, *op, length);
        cursor += length;
        *cursor++ = op[1] ? ' ' : '\n';
    }
    for (size_t i = 1; i < copies; i++) memcpy(buffer + i * line_length, buffer, line_length);
    while (full_write(STDOUT_FILENO, buffer, size) == size) {}
    error(0, errno, "standard output");
    return EXIT_FAILURE;
}
