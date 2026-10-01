#include <narc/narc.h>
#include <unistd.h>

_Noreturn void _exit(int status) {
    narc_exit(status);
}
