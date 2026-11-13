#include <stdbool.h>
#include <stdint.h>

#include "idt.h"
#include "kprint.h"
#include "pic.h"
#include "keyboard.h"
#include "process.h"

#define VECTOR_BREAKPOINT 3
#define VECTOR_PAGE_FAULT 14
#define VECTOR_TIMER 32
#define VECTOR_KEYBOARD 33
#define VECTOR_SYSCALL 0x80

static idt_entry_t idt[256];
static idtr_t idtr;
static volatile uint64_t ticks = 0;

// stubs for 0 to 33 then the syscall one
extern const uint64_t interrupt_stubs[];

// every entry builds the same frame so the scheduler can switch from any of them
__asm__(
    ".pushsection .text\n"
    ".macro isr_stub n, has_error\n"
    "isr_stub_\\n:\n"
    "    .if \\has_error == 0\n"
    "    push $0\n"
    "    .endif\n"
    "    push $\\n\n"
    "    jmp interrupt_common\n"
    ".endm\n"
    ".irp n, 0,1,2,3,4,5,6,7,9,15,16,18,19,20,22,23,24,25,26,27,28,31,32,33,128\n"
    "    isr_stub \\n, 0\n"
    ".endr\n"
    ".irp n, 8,10,11,12,13,14,17,21,29,30\n"
    "    isr_stub \\n, 1\n"
    ".endr\n"
    "interrupt_common:\n"
    "    push %rax\n"
    "    push %rbx\n"
    "    push %rcx\n"
    "    push %rdx\n"
    "    push %rsi\n"
    "    push %rdi\n"
    "    push %rbp\n"
    "    push %r8\n"
    "    push %r9\n"
    "    push %r10\n"
    "    push %r11\n"
    "    push %r12\n"
    "    push %r13\n"
    "    push %r14\n"
    "    push %r15\n"
    "    cld\n"
    "    mov %rsp, %rdi\n"
    "    call interrupt_dispatch\n"
    "    mov %rax, %rsp\n"
    "    pop %r15\n"
    "    pop %r14\n"
    "    pop %r13\n"
    "    pop %r12\n"
    "    pop %r11\n"
    "    pop %r10\n"
    "    pop %r9\n"
    "    pop %r8\n"
    "    pop %rbp\n"
    "    pop %rdi\n"
    "    pop %rsi\n"
    "    pop %rdx\n"
    "    pop %rcx\n"
    "    pop %rbx\n"
    "    pop %rax\n"
    "    add $16, %rsp\n"
    "    iretq\n"
    ".popsection\n"
    ".pushsection .rodata\n"
    ".global interrupt_stubs\n"
    "interrupt_stubs:\n"
    ".irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,128\n"
    "    .quad isr_stub_\\n\n"
    ".endr\n"
    ".popsection\n"
);

static int user_exit_code(uint64_t vector) {
    switch (vector) {
    case 0:
        return -8;
    case 6:
        return -4;
    case 13:
        return -13;
    default:
        return -11;
    }
}

static uint64_t exception_handler(struct interrupt_frame *f) {
    if (f->vector == VECTOR_BREAKPOINT) {
        kprint("Breakpoint interrupt!\n");
        return (uint64_t)f;
    }

    // cr2 only means something for page faults
    uint64_t cr2 = 0;
    if (f->vector == VECTOR_PAGE_FAULT)
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    // nmi double fault and machine check are never the process fault
    bool fatal = f->vector == 2 || f->vector == 8 || f->vector == 18;

    if ((f->cs & 3) == 3 && !fatal) {
        kprint("PROC: exception %lu error 0x%lx rip 0x%lx cr2 0x%lx, killing it\n", f->vector, f->error, f->rip, cr2);
        process_exit(user_exit_code(f->vector));
        return schedule((uint64_t)f);
    }

    panic("exception %lu error 0x%lx rip 0x%lx rsp 0x%lx cr2 0x%lx", f->vector, f->error, f->rip, f->rsp, cr2);
}

__attribute__((used))
uint64_t interrupt_dispatch(struct interrupt_frame *f) {
    if (f->vector == VECTOR_TIMER) {
        ticks++;
        pic_send_eoi(0);
        return scheduler_tick((uint64_t)f);
    }

    if (f->vector == VECTOR_KEYBOARD) {
        keyboard_handle_irq();
        return schedule_if_needed((uint64_t)f);
    }

    if (f->vector == VECTOR_SYSCALL)
        return syscall_handler(f);

    return exception_handler(f);
}

static void idt_set_gate(uint8_t vector, uint64_t addr) {
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));

    idt[vector].offset_low = addr & 0xFFFF;
    idt[vector].selector = cs;
    idt[vector].ist = 0;
    idt[vector].type_attr = 0x8E;
    idt[vector].offset_mid = (addr >> 16) & 0xFFFF;
    idt[vector].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vector].reserved = 0;
}

void idt_init(void) {
    for (int i = 0; i <= VECTOR_KEYBOARD; i++)
        idt_set_gate(i, interrupt_stubs[i]);
    idt[8].ist = 1;

    idt_set_gate(VECTOR_SYSCALL, interrupt_stubs[VECTOR_KEYBOARD + 1]);
    idt[VECTOR_SYSCALL].type_attr = 0xEE;

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)&idt;

    __asm__ volatile ("lidt %0" : : "m"(idtr) : "memory");

    kprint("IDT initialized!\n");
}

uint64_t timer_get_ticks(void) {
    return ticks;
}
