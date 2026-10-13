#include <errno.h>
#include <termios.h>
#include <unistd.h>

int isatty(int fd) {
    struct termios termios;
    if (tcgetattr(fd, &termios) == 0) return 1;
    if (errno != EBADF) errno = ENOTTY;
    return 0;
}
