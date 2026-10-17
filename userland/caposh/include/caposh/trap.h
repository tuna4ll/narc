#pragma once

#include <signal.h>

#define TRAP_EXIT 0

extern volatile sig_atomic_t interrupted;

void signals_capture(void);
void signals_init(void);
void signals_reset_child(int background);
int trap_set(int signal, const char *action);
const char *trap_action(int signal);
int trap_pending(void);
void trap_run_pending(void);
void trap_run_exit(void);
void traps_reset_subshell(void);
int signal_number(const char *name);
const char *signal_name(int signal);
void check_interrupt(void);
