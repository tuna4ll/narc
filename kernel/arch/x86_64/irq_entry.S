.section .text

.macro IRQ_ENTRY name, handler
.global \name
.type \name, @function
\name:
    pushq %r9
    pushq %r8
    pushq %r10
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rax
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    cld
    movq %rsp, %rdi
    call \handler
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    popq %rax
    popq %rdi
    popq %rsi
    popq %rdx
    popq %r10
    popq %r8
    popq %r9
    iretq
.size \name, .-\name
.endm

IRQ_ENTRY timer_interrupt, timer_dispatch
IRQ_ENTRY serial_interrupt_entry, serial_dispatch
