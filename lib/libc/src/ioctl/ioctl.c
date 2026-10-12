#include "../internal/narc.h"
#include <stdarg.h>
#include <sys/ioctl.h>

int ioctl(int fd, unsigned long request, ...) {
    va_list arguments;
    va_start(arguments, request);
    void *argument = va_arg(arguments, void *);
    va_end(arguments);
    return (int)__libc_result(narc_ioctl(fd, (uint32_t)request, (uint64_t)(uintptr_t)argument));
}
