#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "keyboard.h"
#include "io.h"
#include "cpu.h"
#include "terminal.h"
#include "pit.h"
#include "idt.h"
#include "pic.h"
#include "pmm.h"
#include "boot.h"

void kmain(void) {
    outb(0xE9, 'K');

    struct boot_info boot;

    if (!boot_init(&boot)) {
        outb(0xE9, 'E');
        khalt();
    }

    terminal_init(&boot.framebuffer);
    kprint("Hello World\n");

    if (!pmm_init(
        boot.memmap,
        boot.memmap_count,
        boot.hhdm_offset
    )) {
        kprint("PMM initialization failed!\n");
        khalt();
    }

    kprint("PMM memory map initialized!\n");

    uint64_t page = pmm_alloc_page();

    if (page == 0) {
        kprint("PMM: allocation failed!\n");
        khalt();
    }

    kprint("PMM: physical page allocated!\n");

    uint64_t hhdm = boot.hhdm_offset;

    volatile uint8_t *memory =
        (volatile uint8_t *)(uintptr_t)(page + hhdm);

    memory[0] = 42;

    if (memory[0] == 42) {
        kprint("PMM: read/write successful!\n");
    } else {
        kprint("PMM: read/write failed!\n");
        khalt();
    }

    pmm_free_page(page);

    kprint("PMM: physical page freed!\n");

    kprint("Narc\n");

    kprint("      %%%%%@%##     \n");
    kprint(" *+   %%%@%#@%#%    \n");
    kprint(" +-=  @%++++*%%@    \n");
    kprint(" #*#@ @*#%=*+*@@    \n");
    kprint("   #*###****=-+%   \n") ;
    kprint("  %++==:=**+-:-.-+\n")  ;
    kprint("   -::-:-=:.-----:*\n") ;
    kprint("  %%*=----..-**+.=*\n") ;
    kprint(" @###*:-..:-=*##-+%\n") ;
    kprint("  ###*+==*:-**#*#%\n")  ;
    kprint("   %##@    %####\n\n\n")    ;

    idt_init();
    pic_remap();
    pit_init(100);

    pic_clear_mask(0);
    pic_clear_mask(1);

    kprint("Testing INT3...\n");

    __asm__ volatile ("int3");

    kprint("Interrupt returned!\n");
    kprint("Enabling interrupts...\n");

    __asm__ volatile ("sti");

    uint8_t previous = 0;

    for (;;) {
        uint8_t current = keyboard_get_last_scancode();

        if (current != previous) {
            previous = current;

            if (current == 0x31) {
                kprint("N pressed (shout out to narcito)\n");
            }

            if (current == 0xB1) {
                kprint("N released\n");
            }
        }

        __asm__ volatile ("hlt");
    }
}
