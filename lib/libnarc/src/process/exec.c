#include "../internal/syscall.h"

narc_result_t narc_exec(const char *path, char *const argv[], char *const envp[]) {
    return __narc_call3(NARC_SYS_EXEC, (uint64_t)(uintptr_t)path,
                        (uint64_t)(uintptr_t)argv, (uint64_t)(uintptr_t)envp);
}
