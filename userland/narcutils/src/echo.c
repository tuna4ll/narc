#include <ctype.h>
#include <narcutils/system.h>

#define PROGRAM_NAME "echo"

void usage(int status) {
    printf("Usage: %s [SHORT-OPTION]... [STRING]...\n"
           "  or:  %s LONG-OPTION\n",
           program_name, program_name);
    fputs("Echo the STRING(s) to standard output.\n\n"
          "  -n             do not output the trailing newline\n"
          "  -e             enable interpretation of backslash escapes\n"
          "  -E             disable interpretation of backslash escapes (default)\n",
          stdout);
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    fputs("\nIf -e is in effect, the following sequences are recognized:\n\n"
          "  \\\\      backslash\n"
          "  \\a      alert (BEL)\n"
          "  \\b      backspace\n"
          "  \\c      produce no further output\n"
          "  \\e      escape\n"
          "  \\f      form feed\n"
          "  \\n      new line\n"
          "  \\r      carriage return\n"
          "  \\t      horizontal tab\n"
          "  \\v      vertical tab\n"
          "  \\0NNN   byte with octal value NNN (1 to 3 digits)\n"
          "  \\xHH    byte with hexadecimal value HH (1 to 2 digits)\n",
          stdout);
    printf("\nYour shell may have its own version of %s, which usually supersedes\n"
           "the version described here.  Please refer to your shell's documentation\n"
           "for details about the options it supports.\n",
           PROGRAM_NAME);
    fputs("\nConsider using the printf(1) command instead,\n"
          "as it avoids problems when outputting option-like strings.\n",
          stdout);
    exit(status);
}

static int hex_value(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

static bool print_escaped(const char *s) {
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c != '\\' || !*s) {
            putchar(c);
            continue;
        }
        c = (unsigned char)*s++;
        switch (c) {
        case 'a': c = '\a'; break;
        case 'b': c = '\b'; break;
        case 'c': return false;
        case 'e': c = 0x1b; break;
        case 'f': c = '\f'; break;
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case 'v': c = '\v'; break;
        case 'x': {
            if (!isxdigit((unsigned char)*s)) {
                putchar('\\');
                break;
            }
            c = (unsigned char)hex_value((unsigned char)*s++);
            if (isxdigit((unsigned char)*s)) c = (unsigned char)(c * 16 + hex_value((unsigned char)*s++));
            break;
        }
        case '0': {
            c = 0;
            for (int i = 0; i < 3 && *s >= '0' && *s <= '7'; i++) c = (unsigned char)(c * 8 + (*s++ - '0'));
            break;
        }
        case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
            c -= '0';
            for (int i = 0; i < 2 && *s >= '0' && *s <= '7'; i++) c = (unsigned char)(c * 8 + (*s++ - '0'));
            break;
        }
        case '\\': break;
        default:
            putchar('\\');
            break;
        }
        putchar(c);
    }
    return true;
}

int main(int argc, char **argv) {
    bool newline = true;
    bool escapes = false;
    bool posixly = getenv("POSIXLY_CORRECT") != 0;
    bool allow_options = !posixly || (argc > 1 && STREQ(argv[1], "-n"));

    initialize_main(&argc, &argv);
    if (allow_options && argc == 2) {
        if (STREQ(argv[1], "--help")) usage(EXIT_SUCCESS);
        if (STREQ(argv[1], "--version")) {
            version_etc(PROGRAM_NAME);
            return EXIT_SUCCESS;
        }
    }

    argv++;
    argc--;
    if (allow_options) {
        while (argc > 0 && argv[0][0] == '-') {
            const char *s = argv[0] + 1;
            if (!*s) break;
            for (const char *t = s; *t; t++)
                if (*t != 'e' && *t != 'E' && *t != 'n') goto done;
            for (; *s; s++) {
                if (*s == 'e') escapes = true;
                else if (*s == 'E') escapes = false;
                else newline = false;
            }
            argc--;
            argv++;
        }
    }
done:
    if (posixly) escapes = true;
    for (int i = 0; i < argc; i++) {
        if (escapes) {
            if (!print_escaped(argv[i])) return EXIT_SUCCESS;
        } else {
            fputs(argv[i], stdout);
        }
        if (i + 1 < argc) putchar(' ');
    }
    if (newline) putchar('\n');
    return EXIT_SUCCESS;
}
