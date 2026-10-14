#include <setjmp.h>
#include <signal.h>

void __sigsetjmp_save(sigjmp_buf env) {
    sigprocmask(SIG_BLOCK, 0, &env->mask);
}

_Noreturn void siglongjmp(sigjmp_buf env, int value) {
    if (env->save_mask) sigprocmask(SIG_SETMASK, &env->mask, 0);
    longjmp(env, value);
}
