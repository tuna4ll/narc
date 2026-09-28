#pragma once
#include <kernel/arch.h>
#include <stddef.h>

void user_start(void);
int user_exec(struct task_frame *frame, const char *path, const char *const argv[], size_t argc,
              const char *const envp[], size_t envc);
