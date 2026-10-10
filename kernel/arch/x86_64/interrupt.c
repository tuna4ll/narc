#include <kernel/arch.h>
#include <kernel/console.h>
#include <kernel/idt.h>
#include <kernel/sched.h>
#include <kernel/serial.h>
#include <stdint.h>

#define USER_END 0x0000800000000000ULL

static interrupt_handler_t handlers[IDT_VECTORS];

static uint64_t read_cr2(void) {
    uint64_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

void idt_set_handler(unsigned vector, interrupt_handler_t handler) {
    if (vector < IDT_VECTORS) handlers[vector] = handler;
}

__attribute__((noreturn))
static void exception_die(struct task_frame *frame) {
    serial_puts("[fault] vector=");
    serial_puthex(frame->vector);
    serial_puts(" error=");
    serial_puthex(frame->error);
    serial_puts(" rip=");
    serial_puthex(frame->rip);
    serial_puts(" cs=");
    serial_puthex(frame->cs);
    if (frame->vector == 14) {
        serial_puts(" cr2=");
        serial_puthex(read_cr2());
    }
    serial_puts("\n");

    console_puts("[fault] userspace exception; see serial\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

void x86_interrupt(struct task_frame *frame) {
    if (frame->vector < 32) exception_die(frame);
    interrupt_handler_t handler = handlers[frame->vector];
    if (handler) handler(frame);
    if (arch_frame_from_user(frame)) sched_user_return();
}

int x86_syscall_sysret(const struct task_frame *frame) {
    return frame->rcx == frame->rip && frame->r11 == frame->rflags &&
           frame->rip < USER_END;
}
