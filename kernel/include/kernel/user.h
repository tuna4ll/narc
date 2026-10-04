#pragma once
#include <kernel/arch.h>
#include <stddef.h>
#include <stdint.h>

void user_start(void);
int user_exec(struct task_frame *frame, const char *path, uint64_t argv, uint64_t envp);
