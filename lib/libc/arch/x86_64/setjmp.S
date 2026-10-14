.section .text
.global setjmp
.global _setjmp
.type setjmp, @function
.type _setjmp, @function
setjmp:
_setjmp:
    movq %rbx, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq %r12, 16(%rdi)
    movq %r13, 24(%rdi)
    movq %r14, 32(%rdi)
    movq %r15, 40(%rdi)
    leaq 8(%rsp), %rdx
    movq %rdx, 48(%rdi)
    movq (%rsp), %rdx
    movq %rdx, 56(%rdi)
    xorl %eax, %eax
    ret
.size setjmp, .-setjmp

.global longjmp
.global _longjmp
.type longjmp, @function
.type _longjmp, @function
longjmp:
_longjmp:
    movl %esi, %eax
    testl %eax, %eax
    jnz 1f
    incl %eax
1:
    movq 0(%rdi), %rbx
    movq 8(%rdi), %rbp
    movq 16(%rdi), %r12
    movq 24(%rdi), %r13
    movq 32(%rdi), %r14
    movq 40(%rdi), %r15
    movq 48(%rdi), %rsp
    jmpq *56(%rdi)
.size longjmp, .-longjmp

.global sigsetjmp
.type sigsetjmp, @function
sigsetjmp:
    movl %esi, 208(%rdi)
    testl %esi, %esi
    jz setjmp
    pushq %rdi
    call __sigsetjmp_save
    popq %rdi
    jmp setjmp
.size sigsetjmp, .-sigsetjmp

.section .note.GNU-stack,"",@progbits
