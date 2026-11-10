
#include "idt.h"
#include "kprint.h"
#include "pic.h"
#include "keyboard.h"
#include "process.h"

extern void syscall_entry(void);

static idt_entry_t idt[256];
static idtr_t idtr;

typedef struct {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} interrupt_frame_t;

__attribute__((interrupt))
static void keyboard_handler(interrupt_frame_t *frame) {
    (void)frame;

    keyboard_handle_irq();
}

__attribute__((interrupt))
static void divide_handler(interrupt_frame_t *frame) {
    if ((frame->cs & 3) == 3) {
        kprint("PROC: division error at rip 0x%lx, killing it\n", frame->rip);
        process_exit(-8);
    }

    panic("division error at rip 0x%lx", frame->rip);
}

__attribute__((interrupt))
static void invalid_opcode_handler(interrupt_frame_t *frame) {
    if ((frame->cs & 3) == 3) {
        kprint("PROC: invalid opcode at rip 0x%lx, killing it\n", frame->rip);
        process_exit(-4);
    }

    panic("invalid opcode at rip 0x%lx", frame->rip);
}

__attribute__((interrupt))
static void breakpoint_handler(interrupt_frame_t *frame) {
    (void)frame;
    kprint("Breakpoint interrupt!\n");
}

static volatile uint64_t ticks = 0;

extern void timer_entry(void);

__asm__(
    ".global timer_entry\n"
    "timer_entry:\n"
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
    "    mov %rsp, %rdi\n"
    "    call timer_interrupt\n"
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
    "    iretq\n"
);

__attribute__((used))
uint64_t timer_interrupt(uint64_t rsp) {
    ticks++;
    pic_send_eoi(0);
    return scheduler_tick(rsp);
}


__attribute__((interrupt))
static void page_fault_handler(
    interrupt_frame_t *frame,
    uint64_t error_code
) {
    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    if ((frame->cs & 3) == 3) {
        kprint("PROC: page fault at 0x%lx error 0x%lx, killing it\n", cr2, error_code);
        process_exit(-11);
    }

    panic("page fault at 0x%lx error 0x%lx rip 0x%lx", cr2, error_code, frame->rip);
}

__attribute__((interrupt))
static void gp_fault_handler(
    interrupt_frame_t *frame,
    uint64_t error_code
) {
    if ((frame->cs & 3) == 3) {
        kprint("PROC: general protection fault at rip 0x%lx, killing it\n", frame->rip);
        process_exit(-13);
    }

    panic("general protection fault error 0x%lx rip 0x%lx", error_code, frame->rip);
}

__attribute__((interrupt))
static void double_fault_handler(
    interrupt_frame_t *frame,
    uint64_t error_code
) {
    (void)error_code;

    panic("double fault rip 0x%lx rsp 0x%lx", frame->rip, frame->rsp);
}

static void idt_set_gate(uint8_t vector, void *handler) {
    uint64_t addr = (uint64_t)handler;

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
    idt_set_gate(0, (void *)divide_handler);
    idt_set_gate(3, (void *)breakpoint_handler);
    idt_set_gate(6, (void *)invalid_opcode_handler);
    idt_set_gate(8, (void *)double_fault_handler);
    idt[8].ist = 1;
    idt_set_gate(13, (void *)gp_fault_handler);
    idt_set_gate(14, (void *)page_fault_handler);

    idt_set_gate(32, (void *)timer_entry);
    idt_set_gate(33, (void *)keyboard_handler);

    idt_set_gate(0x80, (void *)syscall_entry);
    idt[0x80].type_attr = 0xEE;

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)&idt;

    __asm__ volatile ("lidt %0" : : "m"(idtr) : "memory");

    kprint("IDT initialized!\n");
}


uint64_t timer_get_ticks(void) {
    return ticks;
}