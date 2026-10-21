#include <narcutils/quote.h>
#include <narcutils/system.h>
#include <sys/utsname.h>

#define PROGRAM_NAME "uname"

enum {
    PRINT_KERNEL_NAME = 1,
    PRINT_NODENAME = 2,
    PRINT_KERNEL_RELEASE = 4,
    PRINT_KERNEL_VERSION = 8,
    PRINT_MACHINE = 16,
    PRINT_PROCESSOR = 32,
    PRINT_HARDWARE_PLATFORM = 64,
    PRINT_OPERATING_SYSTEM = 128,
};

static const struct option long_options[] = {
    { "all", no_argument, 0, 'a' },
    { "kernel-name", no_argument, 0, 's' },
    { "sysname", no_argument, 0, 's' },
    { "nodename", no_argument, 0, 'n' },
    { "kernel-release", no_argument, 0, 'r' },
    { "release", no_argument, 0, 'r' },
    { "kernel-version", no_argument, 0, 'v' },
    { "machine", no_argument, 0, 'm' },
    { "processor", no_argument, 0, 'p' },
    { "hardware-platform", no_argument, 0, 'i' },
    { "operating-system", no_argument, 0, 'o' },
    GETOPT_HELP_OPTION_DECL,
    GETOPT_VERSION_OPTION_DECL,
    { 0, 0, 0, 0 },
};

static bool arch_mode;

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        emit_try_help();
        exit(status);
    }
    printf("Usage: %s [OPTION]...\n", program_name);
    if (arch_mode) {
        fputs("Print machine architecture.\n\n", stdout);
    } else {
        fputs("Print certain system information.  With no OPTION, same as -s.\n\n"
              "  -a, --all                print all information, in the following order,\n"
              "                             except omit -p and -i if unknown:\n"
              "  -s, --kernel-name        print the kernel name\n"
              "  -n, --nodename           print the network node hostname\n"
              "  -r, --kernel-release     print the kernel release\n"
              "  -v, --kernel-version     print the kernel version\n"
              "  -m, --machine            print the machine hardware name\n"
              "  -p, --processor          print the processor type (non-portable)\n"
              "  -i, --hardware-platform  print the hardware platform (non-portable)\n"
              "  -o, --operating-system   print the operating system\n",
              stdout);
    }
    fputs(HELP_OPTION_DESCRIPTION, stdout);
    fputs(VERSION_OPTION_DESCRIPTION, stdout);
    exit(status);
}

static void print_element(const char *element) {
    static bool printed;
    if (printed) putchar(' ');
    printed = true;
    fputs(element, stdout);
}

int main(int argc, char **argv) {
    unsigned toprint = 0;
    int c;
    initialize_main(&argc, &argv);
    const char *invoked = strrchr(argv[0], '/');
    invoked = invoked ? invoked + 1 : argv[0];
    arch_mode = STREQ(invoked, "arch");
    if (arch_mode) {
        while ((c = getopt_long(argc, argv, "", long_options + ARRAY_SIZE(long_options) - 3, 0)) != -1) {
            switch (c) {
            case_GETOPT_HELP_CHAR;
            case_GETOPT_VERSION_CHAR("arch");
            default: usage(EXIT_FAILURE);
            }
        }
        toprint = PRINT_MACHINE;
    } else {
        while ((c = getopt_long(argc, argv, "asnrvmpio", long_options, 0)) != -1) {
            switch (c) {
            case 'a': toprint = UINT_MAX; break;
            case 's': toprint |= PRINT_KERNEL_NAME; break;
            case 'n': toprint |= PRINT_NODENAME; break;
            case 'r': toprint |= PRINT_KERNEL_RELEASE; break;
            case 'v': toprint |= PRINT_KERNEL_VERSION; break;
            case 'm': toprint |= PRINT_MACHINE; break;
            case 'p': toprint |= PRINT_PROCESSOR; break;
            case 'i': toprint |= PRINT_HARDWARE_PLATFORM; break;
            case 'o': toprint |= PRINT_OPERATING_SYSTEM; break;
            case_GETOPT_HELP_CHAR;
            case_GETOPT_VERSION_CHAR(PROGRAM_NAME);
            default: usage(EXIT_FAILURE);
            }
        }
    }
    if (argc != optind) {
        error(0, 0, "extra operand %s", quote(argv[optind]));
        usage(EXIT_FAILURE);
    }
    if (!toprint) toprint = PRINT_KERNEL_NAME;

    struct utsname name;
    if (uname(&name) == -1) die(EXIT_FAILURE, errno, "cannot get system name");
    if (toprint & PRINT_KERNEL_NAME) print_element(name.sysname);
    if (toprint & PRINT_NODENAME) print_element(name.nodename);
    if (toprint & PRINT_KERNEL_RELEASE) print_element(name.release);
    if (toprint & PRINT_KERNEL_VERSION) print_element(name.version);
    if (toprint & PRINT_MACHINE) print_element(name.machine);
    if ((toprint & PRINT_PROCESSOR) && toprint != UINT_MAX) print_element("unknown");
    if ((toprint & PRINT_HARDWARE_PLATFORM) && toprint != UINT_MAX) print_element("unknown");
    if (toprint & PRINT_OPERATING_SYSTEM) print_element(STREQ(name.sysname, "Linux") ? "GNU/Linux" : name.sysname);
    putchar('\n');
    return EXIT_SUCCESS;
}
