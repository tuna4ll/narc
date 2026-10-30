#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

#include "keyboard.h"
#include "io.h"
#include "cpu.h"
#include "terminal.h"
#include "pit.h"
#include "idt.h"
#include "pic.h"
#include "pmm.h"

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(4);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

void kmain(void) {
    outb(0xE9, 'K');

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)
        || framebuffer_request.response == NULL
        || framebuffer_request.response->framebuffer_count < 1) {
        outb(0xE9, 'E');
        khalt();
    }

    terminal_init(framebuffer_request.response->framebuffers[0]);
    kprint("Hello World\n");
    if (memmap_request.response == NULL) {
        kprint("Memory map unavailable!\n");
        khalt();
    }

    if (hhdm_request.response == NULL) {
        kprint("HHDM unavailable!\n");
        khalt();
    }

    if (!pmm_init(
        memmap_request.response,
        hhdm_request.response->offset
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

    uint64_t hhdm = hhdm_request.response->offset;

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
