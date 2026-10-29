
#include "idt.h"
#include "terminal.h"

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
static void divide_handler(interrupt_frame_t *frame) {
    (void)frame;
    vga_kprint("Division Error!\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

__attribute__((interrupt))
static void breakpoint_handler(interrupt_frame_t *frame) {
    (void)frame;
    vga_kprint("Breakpoint interrupt!\n");
}

__attribute__((interrupt))
static void page_fault_handler(
    interrupt_frame_t *frame,
    uint64_t error_code
) {
    (void)frame;
    (void)error_code;

    vga_kprint("Page Fault!\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
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
    idt_set_gate(14, (void *)page_fault_handler);

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)&idt;

    __asm__ volatile ("lidt %0" : : "m"(idtr) : "memory");

    vga_kprint("IDT initialized!\n");
}
