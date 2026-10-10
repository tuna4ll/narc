#pragma once
#include <kernel/file.h>

void tty_init(void);
void tty_input(char c);
struct open_file *tty_open(void);
