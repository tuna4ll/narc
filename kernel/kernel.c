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
#include "vmm.h"

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

    uint64_t limine_cr3 = vmm_read_cr3();
    uint64_t free_before = pmm_get_free_pages();

    if (!vmm_init(&boot)) {
        kprint("VMM: init failed!\n");
        khalt();
    }

    kprint("VMM: limine CR3 = ");
    kprint_hex(limine_cr3);
    kprint("\n");
    kprint("VMM: narc CR3   = ");
    kprint_hex(vmm_read_cr3());
    kprint("\n");
    kprint("VMM: page tables used ");
    kprint_dec(free_before - pmm_get_free_pages());
    kprint(" pages\n");
    kprint("VMM: still alive after CR3 switch :D\n");

    uint64_t test_virt = (uint64_t)&kmain;
    kprint("VMM: kmain virt = ");
    kprint_hex(test_virt);
    kprint(" phys = ");
    kprint_hex(vmm_virt_to_phys(test_virt));
    kprint("\n");

    uint64_t test_page = pmm_alloc_page();
    kprint("VMM: pmm page = ");
    kprint_hex(test_page);
    kprint(" hhdm walk = ");
    kprint_hex(vmm_virt_to_phys(test_page + boot.hhdm_offset));
    kprint("\n");
    pmm_free_page(test_page);

    uint64_t my_virt = 0xFFFF900000000000;
    uint64_t free_map = pmm_get_free_pages();
    uint64_t my_phys = pmm_alloc_page();

    if (!vmm_map_page(my_virt, my_phys, PTE_WRITE)) {
        kprint("VMM: map failed!\n");
        khalt();
    }

    kprint("VMM: mapped ");
    kprint_hex(my_virt);
    kprint(" -> ");
    kprint_hex(vmm_virt_to_phys(my_virt));
    kprint("\n");

    volatile uint32_t *p1 = (volatile uint32_t *)my_virt;
    volatile uint32_t *p2 = (volatile uint32_t *)(my_phys + boot.hhdm_offset);

    *p1 = 0x4E415243;

    kprint("VMM: write via my_virt, read via hhdm = ");
    kprint_hex(*p2);
    kprint("\n");

    if (*p2 == 0x4E415243)
        kprint("VMM: SAME PAGE!!! map works\n");

    uint64_t freed = vmm_unmap_page(my_virt);
    pmm_free_page(freed);

    kprint("VMM: unmapped, walk now = ");
    kprint_hex(vmm_virt_to_phys(my_virt));
    kprint("\n");

    kprint("VMM: free pages before map = ");
    kprint_dec(free_map);
    kprint(" after unmap = ");
    kprint_dec(pmm_get_free_pages());
    kprint("\n");

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
