.section .text
.global arch_enter_user
.type arch_enter_user, @function
arch_enter_user:
    cli
    mov $0x1b, %ax
    mov %ax, %ds
    mov %ax, %es
    movq %rdi, %rsp
    movq 0(%rsp), %r15
    movq 8(%rsp), %r14
    movq 16(%rsp), %r13
    movq 24(%rsp), %r12
    movq 32(%rsp), %rbp
    movq 40(%rsp), %rbx
    movq 48(%rsp), %rax
    movq 56(%rsp), %rdi
    movq 64(%rsp), %rsi
    movq 72(%rsp), %rdx
    movq 80(%rsp), %r10
    movq 88(%rsp), %r8
    movq 96(%rsp), %r9
    movq 104(%rsp), %rcx
    movq 120(%rsp), %r11
    movq 128(%rsp), %rsp
    sysretq
.size arch_enter_user, .-arch_enter_user
