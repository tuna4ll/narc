.section .text

.macro PUSH_REGS
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %rax
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
.endm

.macro POP_REGS
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rax
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
.endm

.macro STUB vector
interrupt_stub_\vector:
.if !(\vector == 8 || (\vector >= 10 && \vector <= 14) || \vector == 17 || \vector == 21 || \vector == 29 || \vector == 30)
    pushq $0
.endif
    pushq $\vector
    jmp interrupt_common
.endm

.altmacro
.macro STUB_AT vector
    STUB %vector
.endm
.macro STUB_ADDRESS vector
    .quad interrupt_stub_\vector
.endm

.set vector, 0
.rept 48
    STUB_AT vector
    .set vector, vector + 1
.endr

interrupt_common:
    PUSH_REGS
    cld
    movq %rsp, %rdi
    call x86_interrupt

.global interrupt_return
interrupt_return:
    POP_REGS
    addq $16, %rsp
    iretq

.global arch_enter_user
.type arch_enter_user, @function
arch_enter_user:
    cli
    movq %rdi, %rsp
    jmp interrupt_return
.size arch_enter_user, .-arch_enter_user

.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    movq %rsp, syscall_user_rsp(%rip)
    movq syscall_kernel_rsp(%rip), %rsp
    pushq $0x1b
    pushq syscall_user_rsp(%rip)
    pushq %r11
    pushq $0x23
    pushq %rcx
    pushq $0
    pushq $0x100
    PUSH_REGS
    movq %rsp, %rdi
    call syscall_dispatch
    movq %rsp, %rdi
    call x86_syscall_sysret
    testl %eax, %eax
    jz interrupt_return
    POP_REGS
    addq $16, %rsp
    movq 0(%rsp), %rcx
    movq 16(%rsp), %r11
    movq 24(%rsp), %rsp
    sysretq
.size syscall_entry, .-syscall_entry

.section .rodata
.balign 8
.global interrupt_stubs
interrupt_stubs:
.set vector, 0
.rept 48
    STUB_ADDRESS %vector
    .set vector, vector + 1
.endr
