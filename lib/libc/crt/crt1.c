#include <stdlib.h>

extern int main(int argc, char **argv, char **envp);
char **environ;

_Noreturn void __libc_start(int argc, char **argv, char **envp) {
    environ = envp;
    exit(main(argc, argv, envp));
}
