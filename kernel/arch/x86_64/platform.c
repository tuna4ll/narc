#include <kernel/arch.h>
#include <kernel/cpu.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/timer.h>

#define IA32_FS_BASE 0xc0000100u

extern uint64_t syscall_kernel_rsp;

void arch_init(uint64_t kernel_stack) {
    cpu_init();
    gdt_init(kernel_stack);
    idt_init();
    syscall_init(kernel_stack);
    timer_init();
}

void arch_halt(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

void arch_set_tls(uint64_t value) {
    __asm__ volatile ("wrmsr" : : "c"(IA32_FS_BASE), "a"((uint32_t)value),
                      "d"((uint32_t)(value >> 32)));
}

void arch_task_frame_init(struct task_frame *frame) {
    memset(frame, 0, sizeof(*frame));
    frame->cs = 0x23;
    frame->ss = 0x1b;
    frame->rflags = 0x202;
}

void arch_task_set_entry(struct task_frame *frame, uint64_t pc, uint64_t sp) {
    frame->rip = pc;
    frame->rsp = sp;
}

void arch_task_state_init(void *state) {
    __asm__ volatile ("fninit; fxsave64 %0" : "=m"(*(uint8_t (*)[ARCH_STATE_SIZE])state));
}

void arch_task_state_save(void *state) {
    __asm__ volatile ("fxsave64 %0" : "=m"(*(uint8_t (*)[ARCH_STATE_SIZE])state));
}

void arch_task_state_restore(const void *state) {
    __asm__ volatile ("fxrstor64 %0" : : "m"(*(const uint8_t (*)[ARCH_STATE_SIZE])state));
}

uint64_t arch_syscall_number(const struct task_frame *frame) {
    return frame->rax;
}

uint64_t arch_syscall_arg(const struct task_frame *frame, unsigned index) {
    const uint64_t args[] = { frame->rdi, frame->rsi, frame->rdx,
                              frame->r10, frame->r8, frame->r9 };
    return index < 6 ? args[index] : 0;
}

void arch_syscall_return(struct task_frame *frame, uint64_t value) {
    frame->rax = value;
}

void arch_syscall_return2(struct task_frame *frame, uint64_t value, uint64_t status) {
    frame->rax = value;
    frame->rdx = status;
}

int arch_frame_from_user(const struct task_frame *frame) {
    return (frame->cs & 3) == 3;
}

uint64_t arch_context_init(uint64_t stack_top, void (*entry)(void)) {
    uint64_t *sp = (uint64_t *)(uintptr_t)((stack_top & ~0xfULL) - 64);
    memset(sp, 0, 64);
    sp[6] = (uint64_t)(uintptr_t)entry;
    return (uint64_t)(uintptr_t)sp;
}

void arch_set_kernel_stack(uint64_t top) {
    gdt_set_kernel_stack(top);
    syscall_kernel_rsp = top;
}
