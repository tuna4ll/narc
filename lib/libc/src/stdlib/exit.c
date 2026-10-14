#include <stdlib.h>
#include <unistd.h>

#define ATEXIT_MAX 32

static void (*handlers[ATEXIT_MAX])(void);
static int handler_count;

__attribute__((weak)) void __stdio_exit(void) {}

int atexit(void (*function)(void)) {
    if (handler_count == ATEXIT_MAX) return -1;
    handlers[handler_count++] = function;
    return 0;
}

_Noreturn void exit(int status) {
    while (handler_count) handlers[--handler_count]();
    __stdio_exit();
    _exit(status);
}

_Noreturn void _Exit(int status) {
    _exit(status);
}
