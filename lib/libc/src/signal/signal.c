#include <signal.h>

sighandler_t signal(int number, sighandler_t handler) {
    struct sigaction action = { 0 }, old_action;
    action.sa_handler = handler;
    action.sa_flags = SA_RESTART;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, number);
    if (sigaction(number, &action, &old_action) != 0) return SIG_ERR;
    return old_action.sa_handler;
}
