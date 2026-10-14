#pragma once

#include <signal.h>

typedef struct __jmp_buf {
    unsigned long registers[26];
    int save_mask;
    sigset_t mask;
} jmp_buf[1], sigjmp_buf[1];

int setjmp(jmp_buf env) __attribute__((returns_twice));
_Noreturn void longjmp(jmp_buf env, int value);
int _setjmp(jmp_buf env) __attribute__((returns_twice));
_Noreturn void _longjmp(jmp_buf env, int value);
int sigsetjmp(sigjmp_buf env, int save_mask) __attribute__((returns_twice));
_Noreturn void siglongjmp(sigjmp_buf env, int value);
