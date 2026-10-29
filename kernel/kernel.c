/*
 * kmain'e geldiğimizde: 64-bit long mode, ring 0, interrupt kapalı ve IDT yok,
 * sadece ilk 2 MiB map'li, stack 0x90000, ekran 80x25 text mode, libc yok.
 */

#include "io.h"
#include "cpu.h"
#include "terminal.h"
#include "pit.h"
#include "idt.h"

/* stage2 kernel.bin'in ilk byte'ına atlıyor, linker.ld .text.entry'yi en başa
 * koyuyor. bu section'da kmain'den başka bir şey olmasın */
__attribute__((section(".text.entry")))
void kmain(void) {
    outb(0xE9, 'K');    /* debugcon'a "kernel'e geldik" */

    vga_kprint("NarcOs\n");
    pit_init(100);
    vga_kprint("PIT 100HZ");
    idt_init();

    vga_kprint("Testing INT3...\n");

    __asm__ volatile ("int3");

    vga_kprint("Interrupt returned!\n");

    khalt();
}